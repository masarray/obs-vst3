#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-safe-vst3", "en-US")

namespace {
using safevst3::WinObsBridge;

constexpr const char* kPluginPath = "vst3_path";
constexpr const char* kCustomPath = "custom_vst3_path";
constexpr const char* kClassId = "class_id"; // Backward compatibility with P0 scenes.
constexpr const char* kEnabled = "enabled";
constexpr const char* kDeadline = "deadline_fraction";
constexpr const char* kRescan = "rescan_vst3";
constexpr std::size_t kBridgeHazardSlots = 8;

struct ScanEntry {
    std::string name;
    std::string path;
    std::string class_id;
};

struct BridgeHazardSlot {
    std::atomic_flag claimed = ATOMIC_FLAG_INIT;
    std::atomic<WinObsBridge*> bridge{nullptr};
};

struct BridgeRtState {
    std::atomic<WinObsBridge*> active{nullptr};
    std::array<BridgeHazardSlot, kBridgeHazardSlots> hazards{};
};

static_assert(std::atomic<WinObsBridge*>::is_always_lock_free,
              "OBS realtime bridge publication requires lock-free pointer atomics");
static_assert(std::atomic<double>::is_always_lock_free,
              "OBS realtime deadline reads require lock-free double atomics");
static_assert(std::atomic<bool>::is_always_lock_free,
              "OBS realtime state reads require lock-free bool atomics");

struct Filter {
    obs_source_t* context = nullptr;
    std::unique_ptr<WinObsBridge> bridge_owner;
    std::shared_ptr<BridgeRtState> rt_state = std::make_shared<BridgeRtState>();
    std::mutex config_mutex;
    std::mutex restart_mutex;
    std::jthread recovery_thread;
    std::stop_source shutdown_source;
    std::string path;
    std::string class_id;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::atomic<double> deadline_fraction{0.70};
    std::atomic<bool> enabled{true};
    std::atomic<bool> shutting_down{false};
};

struct RetiredBridge {
    std::unique_ptr<WinObsBridge> bridge;
    std::shared_ptr<BridgeRtState> state;
};

// Bridge teardown can wait for or terminate the helper process. Ownership is
// therefore retained outside the OBS realtime callback. Audio uses a bounded
// lock-free hazard-pointer protocol: a bridge is dereferenced only after its
// hazard is published and the active pointer is revalidated. The non-realtime
// reaper destroys a retired bridge only when no hazard slot names it.
std::mutex retired_bridges_mutex;
std::vector<RetiredBridge> retired_bridges;
std::jthread retired_bridge_reaper;

// OBS owns the callback data pointer and destroy can race an already-entered
// audio callback before that callback executes its first instruction. There is
// no object-local counter that can close that entry window. Keep destroyed
// Filter tombstones alive until module unload, when OBS can no longer execute
// this module's callbacks. Their helper bridge is still detached/reaped early,
// so the quarantine retains only small callback-visible state.
std::mutex retired_filters_mutex;
std::vector<std::unique_ptr<Filter>> retired_filters;

void retire_bridge(std::unique_ptr<WinObsBridge> bridge, std::shared_ptr<BridgeRtState> state)
{
    if (!bridge || !state)
        return;
    std::lock_guard lock(retired_bridges_mutex);
    retired_bridges.push_back({std::move(bridge), std::move(state)});
}

bool bridge_is_hazardous(const RetiredBridge& retired) noexcept
{
    if (!retired.bridge || !retired.state)
        return false;
    WinObsBridge* candidate = retired.bridge.get();
    for (const auto& slot : retired.state->hazards) {
        if (slot.bridge.load(std::memory_order_seq_cst) == candidate)
            return true;
    }
    return false;
}

void reap_retired_bridges()
{
    std::vector<RetiredBridge> ready_to_destroy;
    {
        std::lock_guard lock(retired_bridges_mutex);
        auto it = retired_bridges.begin();
        while (it != retired_bridges.end()) {
            if (!bridge_is_hazardous(*it)) {
                ready_to_destroy.push_back(std::move(*it));
                it = retired_bridges.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Destruct helper bridges outside the quarantine mutex and off the audio thread.
    ready_to_destroy.clear();
}

void retire_filter_until_module_unload(Filter* filter)
{
    if (!filter)
        return;
    std::lock_guard lock(retired_filters_mutex);
    retired_filters.emplace_back(filter);
}

void release_retired_filters()
{
    std::vector<std::unique_ptr<Filter>> ready_to_destroy;
    {
        std::lock_guard lock(retired_filters_mutex);
        ready_to_destroy.swap(retired_filters);
    }
    ready_to_destroy.clear();
}

void start_retired_bridge_reaper()
{
    if (retired_bridge_reaper.joinable())
        return;

    retired_bridge_reaper = std::jthread([](std::stop_token stop) {
        while (!stop.stop_requested()) {
            reap_retired_bridges();
            for (int i = 0; i < 10 && !stop.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        reap_retired_bridges();
    });
}

void stop_retired_bridge_reaper()
{
    retired_bridge_reaper.request_stop();
    retired_bridge_reaper = std::jthread{};
    reap_retired_bridges();
}

// Caller holds restart_mutex. Ownership never enters the audio thread. The
// seq_cst publication participates in the hazard-pointer validation order.
void publish_bridge_locked(Filter* filter, std::unique_ptr<WinObsBridge> next)
{
    auto previous = std::move(filter->bridge_owner);
    filter->bridge_owner = std::move(next);
    filter->rt_state->active.store(filter->bridge_owner.get(), std::memory_order_seq_cst);
    retire_bridge(std::move(previous), filter->rt_state);
}

class AudioBridgeRead {
public:
    explicit AudioBridgeRead(BridgeRtState* state) noexcept : state_(state)
    {
        if (!state_)
            return;

        // Slot acquisition is bounded: if all slots are occupied, fail open to
        // dry audio rather than spin or block the realtime thread.
        for (auto& candidate : state_->hazards) {
            if (!candidate.claimed.test_and_set(std::memory_order_acquire)) {
                slot_ = &candidate;
                break;
            }
        }
        if (!slot_)
            return;

        // Hazard-pointer protocol: publish the candidate before revalidating
        // active. If publication raced replacement/reclamation, validation
        // fails and we retry without ever dereferencing the stale candidate.
        // Two bounded attempts are sufficient for the normal rare replacement
        // path; repeated churn simply fails open for this audio block.
        for (int attempt = 0; attempt < 2; ++attempt) {
            WinObsBridge* candidate = state_->active.load(std::memory_order_seq_cst);
            slot_->bridge.store(candidate, std::memory_order_seq_cst);
            if (candidate == state_->active.load(std::memory_order_seq_cst)) {
                bridge_ = candidate;
                return;
            }
        }
        slot_->bridge.store(nullptr, std::memory_order_seq_cst);
    }

    AudioBridgeRead(const AudioBridgeRead&) = delete;
    AudioBridgeRead& operator=(const AudioBridgeRead&) = delete;

    ~AudioBridgeRead()
    {
        if (!slot_)
            return;
        slot_->bridge.store(nullptr, std::memory_order_seq_cst);
        slot_->claimed.clear(std::memory_order_release);
    }

    WinObsBridge* get() const noexcept { return bridge_; }

private:
    BridgeRtState* state_ = nullptr;
    BridgeHazardSlot* slot_ = nullptr;
    WinObsBridge* bridge_ = nullptr;
};

std::filesystem::path module_binary_dir()
{
    const char* module_path = obs_get_module_binary_path(obs_current_module());
    if (!module_path)
        return {};
    return std::filesystem::u8path(module_path).parent_path();
}

std::filesystem::path helper_path()
{
    if (const char* env = std::getenv("OBS_SAFE_VST3_HOST_PATH"); env && *env)
        return std::filesystem::u8path(env);
    const auto dir = module_binary_dir();
    return dir.empty() ? std::filesystem::path{} : dir / "obs-safe-vst3-host.exe";
}

std::filesystem::path scanner_path()
{
    const auto dir = module_binary_dir();
    return dir.empty() ? std::filesystem::path{} : dir / "obs-safe-vst3-scanner.exe";
}

std::filesystem::path scan_cache_path()
{
    wchar_t buffer[32768]{};
    const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (!len || len >= std::size(buffer))
        return {};
    return std::filesystem::path(std::wstring(buffer, len)) / L"OBS Safe VST3 Host" / L"plugins.tsv";
}

std::wstring quote(const std::wstring& value)
{
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        if (c == L'\"')
            out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

bool run_scanner()
{
    const auto scanner = scanner_path();
    const auto cache = scan_cache_path();
    if (scanner.empty() || cache.empty() || !std::filesystem::exists(scanner)) {
        blog(LOG_ERROR, "[obs-safe-vst3] isolated scanner executable not found");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cache.parent_path(), ec);

    std::wstring command = quote(scanner.wstring()) + L" --scan-to " + quote(cache.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(scanner.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, scanner.parent_path().c_str(), &si, &pi)) {
        blog(LOG_ERROR, "[obs-safe-vst3] failed to start isolated VST3 scanner");
        return false;
    }

    // The scanner parent never loads third-party code. Every VST3 candidate is
    // probed by a child process with its own 15-second timeout, so let the parent
    // finish the finite discovered set instead of truncating large scans at an
    // arbitrary aggregate timeout.
    const DWORD wait = WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (wait != WAIT_OBJECT_0 || code != 0) {
        blog(LOG_WARNING, "[obs-safe-vst3] isolated VST3 scan did not complete successfully");
        return false;
    }
    return true;
}

std::vector<ScanEntry> load_scan_cache()
{
    std::vector<ScanEntry> entries;
    const auto cache = scan_cache_path();
    if (cache.empty())
        return entries;

    std::ifstream in(cache, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            continue;
        ScanEntry entry{line.substr(0, first), line.substr(first + 1, second - first - 1), line.substr(second + 1)};
        if (!entry.name.empty() && !entry.path.empty())
            entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const ScanEntry& a, const ScanEntry& b) {
        if (a.name != b.name)
            return a.name < b.name;
        return a.path < b.path;
    });
    return entries;
}

std::string filename_utf8(const std::string& path_utf8)
{
    const auto u8 = std::filesystem::u8path(path_utf8).filename().u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

void populate_plugin_list(obs_property_t* list)
{
    if (!list)
        return;
    obs_property_list_clear(list);
    const auto entries = load_scan_cache();
    if (entries.empty()) {
        obs_property_list_add_string(list, obs_module_text("NoPluginsScanned"), "");
        return;
    }

    for (const auto& entry : entries) {
        std::string display = entry.name;
        const auto filename = filename_utf8(entry.path);
        if (!filename.empty())
            display += "  [" + filename + "]";
        const std::string value = entry.path + "\t" + entry.class_id;
        obs_property_list_add_string(list, display.c_str(), value.c_str());
    }
}

bool rescan_button(obs_properties_t* props, obs_property_t*, void*)
{
    const bool ok = run_scanner();
    populate_plugin_list(obs_properties_get(props, kPluginPath));
    if (ok)
        blog(LOG_INFO, "[obs-safe-vst3] installed VST3 cache refreshed");
    return true;
}

void split_selection(const std::string& selection, std::string& path, std::string& class_id)
{
    const auto tab = selection.find('\t');
    if (tab == std::string::npos) {
        path = selection;
        return;
    }
    path = selection.substr(0, tab);
    class_id = selection.substr(tab + 1);
}

void restart_bridge(Filter* filter)
{
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return;
    const std::stop_token cancel = filter->shutdown_source.get_token();
    if (cancel.stop_requested())
        return;

    std::lock_guard restart_lock(filter->restart_mutex);
    if (cancel.stop_requested() || filter->shutting_down.load(std::memory_order_acquire))
        return;

    std::string path;
    std::string class_id;
    {
        std::lock_guard config_lock(filter->config_mutex);
        path = filter->path;
        class_id = filter->class_id;
    }

    if (!filter->enabled.load(std::memory_order_relaxed) || path.empty()) {
        publish_bridge_locked(filter, {});
        return;
    }

    const auto sample_rate = filter->sample_rate;
    const auto channels = filter->channels;
    if (channels == 0 || channels > safevst3::kMaxChannels) {
        blog(LOG_WARNING, "[obs-safe-vst3] public trial supports mono/stereo only; current OBS layout has %u channels", channels);
        publish_bridge_locked(filter, {});
        return;
    }

    auto bridge = std::make_unique<WinObsBridge>();
    std::string error;
    const auto helper = helper_path();
    if (helper.empty() || !std::filesystem::exists(helper)) {
        blog(LOG_ERROR, "[obs-safe-vst3] helper executable not found next to plugin binary");
        publish_bridge_locked(filter, {});
        return;
    }

    if (!bridge->start(helper, std::filesystem::u8path(path), class_id, sample_rate, channels, error, cancel)) {
        if (cancel.stop_requested() || filter->shutting_down.load(std::memory_order_acquire))
            return;
        blog(LOG_ERROR, "[obs-safe-vst3] failed to start isolated VST3 host: %s", error.c_str());
        publish_bridge_locked(filter, {});
        return;
    }

    if (cancel.stop_requested() || filter->shutting_down.load(std::memory_order_acquire) ||
        !filter->enabled.load(std::memory_order_relaxed)) {
        bridge->abort();
        return;
    }

    blog(LOG_INFO, "[obs-safe-vst3] isolated VST3 helper ready: %s", path.c_str());
    publish_bridge_locked(filter, std::move(bridge));
}

bool bridge_running(Filter* filter)
{
    std::lock_guard restart_lock(filter->restart_mutex);
    return filter->bridge_owner && filter->bridge_owner->running();
}

const char* filter_name(void*) { return obs_module_text("SafeVST3Filter"); }

void filter_defaults(obs_data_t* settings)
{
    obs_data_set_default_bool(settings, kEnabled, true);
    obs_data_set_default_double(settings, kDeadline, 0.70);
    obs_data_set_default_string(settings, kCustomPath, "");
}

void filter_update(void* data, obs_data_t* settings)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return;

    std::string selected = obs_data_get_string(settings, kPluginPath);
    std::string custom = obs_data_get_string(settings, kCustomPath);
    std::string legacy_class = obs_data_get_string(settings, kClassId);

    std::string path;
    std::string class_id;
    split_selection(selected, path, class_id);
    if (!custom.empty()) {
        path = custom;
        class_id = legacy_class;
    } else if (class_id.empty()) {
        class_id = legacy_class;
    }

    {
        std::lock_guard lock(filter->config_mutex);
        if (filter->shutting_down.load(std::memory_order_acquire))
            return;
        filter->path = std::move(path);
        filter->class_id = std::move(class_id);
    }
    filter->enabled.store(obs_data_get_bool(settings, kEnabled), std::memory_order_relaxed);
    filter->deadline_fraction.store(obs_data_get_double(settings, kDeadline), std::memory_order_relaxed);
    restart_bridge(filter);
}

void* filter_create(obs_data_t* settings, obs_source_t* context)
{
    auto* filter = new Filter{};
    filter->context = context;

    obs_audio_info audio_info{};
    if (obs_get_audio_info(&audio_info)) {
        filter->sample_rate = audio_info.samples_per_sec;
        filter->channels = static_cast<std::uint32_t>(get_audio_channels(audio_info.speakers));
    }

    filter_update(filter, settings);
    filter->recovery_thread = std::jthread([filter](std::stop_token stop) {
        while (!stop.stop_requested()) {
            for (int i = 0; i < 30 && !stop.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop.stop_requested() || filter->shutting_down.load(std::memory_order_acquire))
                break;

            bool has_path = false;
            {
                std::lock_guard lock(filter->config_mutex);
                has_path = !filter->path.empty();
            }
            if (!filter->enabled.load(std::memory_order_relaxed) || !has_path)
                continue;

            if (!bridge_running(filter)) {
                blog(LOG_WARNING, "[obs-safe-vst3] helper stopped; attempting isolated recovery");
                restart_bridge(filter);
            }
        }
    });
    return filter;
}

void filter_destroy(void* data)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter)
        return;

    filter->shutting_down.store(true, std::memory_order_release);
    filter->shutdown_source.request_stop();
    filter->enabled.store(false, std::memory_order_release);
    filter->recovery_thread.request_stop();
    filter->recovery_thread = std::jthread{};

    {
        std::lock_guard restart_lock(filter->restart_mutex);
        publish_bridge_locked(filter, {});
    }

    // Keep the complete callback-visible Filter object valid through any audio
    // callback that OBS had already entered but that has not yet executed its
    // first instruction. The helper bridge itself can still be reaped promptly.
    retire_filter_until_module_unload(filter);
    reap_retired_bridges();
}

obs_properties_t* filter_properties(void*)
{
    obs_properties_t* props = obs_properties_create();
    obs_properties_add_bool(props, kEnabled, obs_module_text("Enabled"));

    auto* list = obs_properties_add_list(props, kPluginPath, obs_module_text("InstalledVST3"),
                                         OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    populate_plugin_list(list);
    obs_properties_add_button(props, kRescan, obs_module_text("RescanVST3"), rescan_button);
    obs_properties_add_path(props, kCustomPath, obs_module_text("CustomVST3Path"), OBS_PATH_DIRECTORY, nullptr, nullptr);
    obs_properties_add_float_slider(props, kDeadline, obs_module_text("DeadlineFraction"), 0.10, 0.95, 0.05);
    return props;
}

obs_audio_data* filter_audio(void* data, obs_audio_data* audio)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter || !audio || filter->shutting_down.load(std::memory_order_relaxed) ||
        !filter->enabled.load(std::memory_order_relaxed))
        return audio;

    AudioBridgeRead read(filter->rt_state.get());
    WinObsBridge* bridge = read.get();
    if (!bridge || !bridge->running())
        return audio;
    if (audio->frames == 0 || audio->frames > safevst3::kMaxFrames)
        return audio;
    if (filter->channels == 0 || filter->channels > safevst3::kMaxChannels)
        return audio;

    float* planes[safevst3::kMaxChannels]{};
    for (std::uint32_t ch = 0; ch < filter->channels; ++ch) {
        if (!audio->data[ch])
            return audio;
        planes[ch] = reinterpret_cast<float*>(audio->data[ch]);
    }

    // Timeout/error intentionally keeps the original OBS buffer untouched (dry fail-open).
    const double deadline = filter->deadline_fraction.load(std::memory_order_relaxed);
    (void)bridge->process(planes, filter->channels, audio->frames, deadline);
    return audio;
}

obs_source_info make_source_info()
{
    obs_source_info info{};
    info.id = "obs_safe_vst3_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = filter_name;
    info.create = filter_create;
    info.destroy = filter_destroy;
    info.get_defaults = filter_defaults;
    info.get_properties = filter_properties;
    info.update = filter_update;
    info.filter_audio = filter_audio;
    return info;
}

obs_source_info source_info = make_source_info();
} // namespace

bool obs_module_load(void)
{
    try {
        start_retired_bridge_reaper();
    } catch (const std::exception& e) {
        blog(LOG_ERROR, "[obs-safe-vst3] failed to start non-realtime bridge reaper: %s", e.what());
        return false;
    }

    obs_register_source(&source_info);
    blog(LOG_INFO, "[obs-safe-vst3] public-trial module loaded");
    return true;
}

void obs_module_unload(void)
{
    // OBS no longer invokes this module's source callbacks once unload begins.
    // Stop the bridge reaper first, then release callback tombstones.
    stop_retired_bridge_reaper();
    release_retired_filters();
}

const char* obs_module_description(void)
{
    return "Crash-isolated VST3 audio-effect host public trial for OBS Studio";
}

#endif
