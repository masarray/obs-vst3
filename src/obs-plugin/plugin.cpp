#ifdef _WIN32

#include "common/recovery_policy.hpp"
#include "obs-plugin/parameter_controls.hpp"
#include "obs-plugin/state_store.hpp"
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
#include <limits>
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
constexpr const char* kClassId = "class_id"; // Backward compatibility with early scenes.
constexpr const char* kEnabled = "enabled";  // Legacy saved key; normal UI now follows OBS filter visibility.
constexpr const char* kBrowseAutoOpenDone = "browse_auto_open_done";
constexpr const char* kRescan = "rescan_vst3";
constexpr const char* kOpenEditor = "open_plugin_ui";
constexpr const char* kPluginStatus = "plugin_status";
constexpr double kInternalDeadlineFraction = 0.70;
constexpr std::size_t kBridgeHazardSlots = 8;

struct ScanEntry {
    std::string name;
    std::string path;
    std::string class_id;
};

struct BridgeHealthSnapshot {
    bool has_bridge = false;
    bool process_alive = false;
    std::uint64_t heartbeat_age_ms = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t deadline_misses = 0;
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
    // Protected by config_mutex. Only an actual first custom-path change from
    // the OBS Browse control may arm one automatic editor open. Scene restore,
    // opening Properties, installed-list selection, recovery and scan never do.
    std::string observed_custom_path;
    bool browse_auto_open_consumed = false;
    bool browse_auto_open_pending = false;
    // Protected by restart_mutex. These fields describe the helper that is
    // actually published, not the possibly newer identity selected in the UI.
    std::string bridge_path;
    std::string bridge_class_id;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::atomic<std::uint32_t> observed_state_generation{0};
    std::atomic<std::uint32_t> saved_state_generation{0};
    std::atomic<double> deadline_fraction{kInternalDeadlineFraction};
    std::atomic<bool> enabled{true};
    std::atomic<bool> shutting_down{false};
};

struct RetiredBridge {
    std::unique_ptr<WinObsBridge> bridge;
    std::shared_ptr<BridgeRtState> state;
};

std::mutex retired_bridges_mutex;
std::vector<RetiredBridge> retired_bridges;
std::jthread retired_bridge_reaper;

std::mutex retired_filters_mutex;
std::vector<std::unique_ptr<Filter>> retired_filters;

std::mutex scanner_mutex;
std::jthread startup_scanner_thread;

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

void publish_bridge_locked(Filter* filter,
                           std::unique_ptr<WinObsBridge> next,
                           const std::string& path = {},
                           const std::string& class_id = {})
{
    auto previous = std::move(filter->bridge_owner);
    filter->bridge_owner = std::move(next);
    if (filter->bridge_owner) {
        filter->bridge_path = path;
        filter->bridge_class_id = class_id;
    } else {
        filter->bridge_path.clear();
        filter->bridge_class_id.clear();
    }
    filter->rt_state->active.store(filter->bridge_owner.get(), std::memory_order_seq_cst);
    retire_bridge(std::move(previous), filter->rt_state);
}

class AudioBridgeRead {
public:
    explicit AudioBridgeRead(BridgeRtState* state) noexcept : state_(state)
    {
        if (!state_)
            return;
        for (auto& candidate : state_->hazards) {
            if (!candidate.claimed.test_and_set(std::memory_order_acquire)) {
                slot_ = &candidate;
                break;
            }
        }
        if (!slot_)
            return;
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

bool run_scanner(std::stop_token stop = {})
{
    std::unique_lock scan_lock(scanner_mutex);
    if (stop.stop_requested())
        return false;

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

    const ULONGLONG deadline = GetTickCount64() + 180000;
    DWORD wait = WAIT_TIMEOUT;
    while (!stop.stop_requested()) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        const DWORD slice = static_cast<DWORD>(std::min<ULONGLONG>(250, deadline - now));
        wait = WaitForSingleObject(pi.hProcess, slice);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED)
            break;
    }

    DWORD code = 1;
    if (stop.stop_requested()) {
        TerminateProcess(pi.hProcess, ERROR_CANCELLED);
        (void)WaitForSingleObject(pi.hProcess, 2000);
    } else if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        TerminateProcess(pi.hProcess, 0xDEAD);
        (void)WaitForSingleObject(pi.hProcess, 2000);
        if (wait != WAIT_FAILED)
            blog(LOG_WARNING, "[obs-safe-vst3] installed VST3 scan exceeded 180 seconds; current cache kept");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (stop.stop_requested())
        return false;
    if (wait != WAIT_OBJECT_0 || code != 0) {
        blog(LOG_WARNING, "[obs-safe-vst3] isolated VST3 scan did not complete successfully");
        return false;
    }
    return true;
}

void start_startup_scanner()
{
    if (startup_scanner_thread.joinable())
        return;

    startup_scanner_thread = std::jthread([](std::stop_token stop) {
        // Let OBS finish its critical startup work, then scan fully out of the
        // UI/audio threads. The scanner publishes bundle-name fallbacks before
        // probing vendor code, so the installed list becomes useful quickly.
        for (int i = 0; i < 5 && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stop.stop_requested())
            return;
        blog(LOG_INFO, "[obs-safe-vst3] background installed VST3 discovery started");
        if (run_scanner(stop))
            blog(LOG_INFO, "[obs-safe-vst3] background installed VST3 discovery completed");
    });
}

void stop_startup_scanner()
{
    startup_scanner_thread.request_stop();
    startup_scanner_thread = std::jthread{};
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

    obs_property_list_add_string(list, obs_module_text("UseBrowseVST3"), "");
    for (const auto& entry : entries) {
        std::string display = entry.name;
        const auto filename = filename_utf8(entry.path);
        if (!filename.empty())
            display += "  [" + filename + "]";
        const std::string value = entry.path + "\t" + entry.class_id;
        obs_property_list_add_string(list, display.c_str(), value.c_str());
    }
}

void hide_editor_before_scan(Filter* filter)
{
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return;
    std::lock_guard restart_lock(filter->restart_mutex);
    if (!filter->bridge_owner || !filter->bridge_owner->running())
        return;
    if (filter->bridge_owner->editor_status() == safevst3::EditorStatus::Open)
        (void)filter->bridge_owner->hide_editor();
}

bool rescan_button(obs_properties_t* props, obs_property_t*, void* data)
{
    auto* filter = static_cast<Filter*>(data);
    hide_editor_before_scan(filter);
    const bool ok = run_scanner();
    populate_plugin_list(obs_properties_get(props, kPluginPath));
    if (ok)
        blog(LOG_INFO, "[obs-safe-vst3] installed VST3 cache refreshed");
    return true;
}

bool split_selection(const std::string& selection, std::string& path, std::string& class_id)
{
    path.clear();
    class_id.clear();
    const auto tab = selection.find('\t');
    if (tab == std::string::npos) {
        path = selection;
        return false;
    }
    path = selection.substr(0, tab);
    class_id = selection.substr(tab + 1);
    return true;
}

std::pair<std::string, std::string> current_identity(Filter* filter)
{
    if (!filter)
        return {};
    std::lock_guard lock(filter->config_mutex);
    return {filter->path, filter->class_id};
}

bool custom_path_modified(void* data, obs_properties_t*, obs_property_t*, obs_data_t* settings)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter || !settings || filter->shutting_down.load(std::memory_order_acquire))
        return false;

    const std::string selected = obs_data_get_string(settings, kPluginPath);
    const std::string custom = obs_data_get_string(settings, kCustomPath);
    bool armed = false;
    bool clear_legacy_class = false;
    {
        std::lock_guard lock(filter->config_mutex);
        if (filter->shutting_down.load(std::memory_order_acquire))
            return false;

        const bool actual_custom_change = custom != filter->observed_custom_path;
        filter->observed_custom_path = custom;
        clear_legacy_class = actual_custom_change && !custom.empty();
        if (actual_custom_change && filter->path.empty() && selected.empty() && !custom.empty() &&
            !filter->browse_auto_open_consumed) {
            filter->browse_auto_open_consumed = true;
            filter->browse_auto_open_pending = true;
            armed = true;
        }
    }

    // A newly browsed bundle has no proven relationship to an early-preview
    // class_id left in the settings. Let the isolated host choose its first
    // audio-effect class instead of accidentally requesting a stale class.
    if (clear_legacy_class)
        obs_data_set_string(settings, kClassId, "");

    if (armed) {
        obs_data_set_bool(settings, kBrowseAutoOpenDone, true);
        blog(LOG_INFO, "[obs-safe-vst3] first custom Browse gesture armed one native-editor open");
    }
    return false;
}

bool capture_bridge_state(Filter* filter,
                          const std::string& path,
                          const std::string& class_id,
                          bool force)
{
    if (!filter || !filter->context || path.empty())
        return false;

    std::lock_guard restart_lock(filter->restart_mutex);
    if (!filter->bridge_owner || !filter->bridge_owner->running())
        return false;
    if (filter->bridge_path != path || filter->bridge_class_id != class_id)
        return false;

    const std::uint32_t dirty = filter->bridge_owner->state_dirty_generation();
    if (!force && dirty == filter->saved_state_generation.load(std::memory_order_acquire))
        return true;

    safevst3::PluginStateSnapshot snapshot{};
    std::string error;
    if (!filter->bridge_owner->capture_state(snapshot, error)) {
        filter->observed_state_generation.store(dirty, std::memory_order_release);
        blog(LOG_WARNING, "[obs-safe-vst3] exact VST3 state capture failed; last-known-good snapshot preserved: %s", error.c_str());
        return false;
    }

    if (!safevst3::obsstate::save(filter->context, path, class_id, snapshot, error)) {
        filter->observed_state_generation.store(dirty, std::memory_order_release);
        blog(LOG_WARNING, "[obs-safe-vst3] could not persist exact VST3 state; last-known-good snapshot preserved: %s", error.c_str());
        return false;
    }

    filter->observed_state_generation.store(dirty, std::memory_order_release);
    filter->saved_state_generation.store(dirty, std::memory_order_release);
    return true;
}

bool capture_current_bridge_state(Filter* filter, bool force)
{
    if (!filter)
        return false;
    const auto [path, class_id] = current_identity(filter);
    return capture_bridge_state(filter, path, class_id, force);
}

void apply_parameter_fallback(WinObsBridge& bridge,
                              obs_source_t* context,
                              const std::string& path,
                              const std::string& class_id)
{
    if (!context || path.empty())
        return;
    obs_data_t* settings = obs_source_get_settings(context);
    if (!settings)
        return;
    safevst3::obsparam::apply_parameter_settings(
        bridge, settings, safevst3::obsparam::parameter_scope(path, class_id));
    obs_data_release(settings);
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
    const auto [path, class_id] = current_identity(filter);

    if (!filter->enabled.load(std::memory_order_relaxed) || path.empty()) {
        publish_bridge_locked(filter, {});
        return;
    }

    const auto sample_rate = filter->sample_rate;
    const auto channels = filter->channels;
    if (channels == 0 || channels > safevst3::kMaxChannels) {
        blog(LOG_WARNING, "[obs-safe-vst3] stable runtime supports mono/stereo only; current OBS layout has %u channels", channels);
        publish_bridge_locked(filter, {});
        return;
    }

    const auto helper = helper_path();
    if (helper.empty() || !std::filesystem::exists(helper)) {
        blog(LOG_ERROR, "[obs-safe-vst3] helper executable not found next to plugin binary");
        publish_bridge_locked(filter, {});
        return;
    }

    std::string error;
    const auto start_candidate = [&]() -> std::unique_ptr<WinObsBridge> {
        auto candidate = std::make_unique<WinObsBridge>();
        error.clear();
        if (!candidate->start(helper, std::filesystem::u8path(path), class_id,
                              sample_rate, channels, error, cancel))
            return {};
        if (cancel.stop_requested() || filter->shutting_down.load(std::memory_order_acquire) ||
            !filter->enabled.load(std::memory_order_relaxed)) {
            candidate->abort();
            return {};
        }
        return candidate;
    };

    auto bridge = start_candidate();
    if (!bridge) {
        if (cancel.stop_requested() || filter->shutting_down.load(std::memory_order_acquire))
            return;
        blog(LOG_ERROR, "[obs-safe-vst3] failed to start isolated VST3 host: %s", error.c_str());
        publish_bridge_locked(filter, {});
        return;
    }

    bool restored_exact_state = false;
    bool restore_rejected = false;
    safevst3::PluginStateSnapshot snapshot{};
    std::string state_error;
    const auto load_result = safevst3::obsstate::load(
        filter->context, path, class_id, snapshot, state_error);
    if (load_result == safevst3::obsstate::LoadResult::Loaded) {
        if (bridge->restore_state(snapshot, state_error)) {
            restored_exact_state = true;
        } else {
            restore_rejected = true;
            safevst3::obsstate::discard(filter->context, path, class_id);
            blog(LOG_WARNING, "[obs-safe-vst3] saved exact VST3 state was rejected; restarting a clean helper: %s",
                 state_error.c_str());
        }
    } else if (load_result == safevst3::obsstate::LoadResult::Invalid) {
        safevst3::obsstate::discard(filter->context, path, class_id);
        blog(LOG_WARNING, "[obs-safe-vst3] saved VST3 state is invalid; using parameter fallback: %s",
             state_error.c_str());
    }

    if (restore_rejected) {
        bridge->abort();
        bridge = start_candidate();
        if (!bridge) {
            if (cancel.stop_requested() || filter->shutting_down.load(std::memory_order_acquire))
                return;
            blog(LOG_ERROR, "[obs-safe-vst3] clean helper restart after rejected state failed: %s", error.c_str());
            publish_bridge_locked(filter, {});
            return;
        }
    }

    if (!restored_exact_state)
        apply_parameter_fallback(*bridge, filter->context, path, class_id);

    const std::uint32_t state_generation = bridge->state_dirty_generation();
    filter->observed_state_generation.store(state_generation, std::memory_order_release);
    filter->saved_state_generation.store(state_generation, std::memory_order_release);

    blog(LOG_INFO, "[obs-safe-vst3] isolated VST3 helper ready: %s (%s, %u samples latency, %u/%u parameters exposed, state=%s)",
         path.c_str(), bridge->plugin_name().c_str(), bridge->latency_samples(),
         static_cast<unsigned>(bridge->parameters().size()),
         static_cast<unsigned>(bridge->parameter_total_count()),
         restored_exact_state ? "restored" : "parameter-fallback");
    publish_bridge_locked(filter, std::move(bridge), path, class_id);
}

bool bridge_running(Filter* filter)
{
    std::lock_guard restart_lock(filter->restart_mutex);
    return filter->bridge_owner && filter->bridge_owner->running();
}

BridgeHealthSnapshot read_bridge_health(Filter* filter)
{
    BridgeHealthSnapshot snapshot{};
    if (!filter)
        return snapshot;

    std::lock_guard restart_lock(filter->restart_mutex);
    if (!filter->bridge_owner)
        return snapshot;

    snapshot.has_bridge = true;
    snapshot.process_alive = filter->bridge_owner->running();
    snapshot.heartbeat_age_ms = filter->bridge_owner->heartbeat_age_ms();
    snapshot.deadline_misses = filter->bridge_owner->deadline_misses();
    return snapshot;
}

bool read_bridge_state_generation(Filter* filter, std::uint32_t& generation)
{
    if (!filter)
        return false;
    std::lock_guard restart_lock(filter->restart_mutex);
    if (!filter->bridge_owner || !filter->bridge_owner->running())
        return false;
    generation = filter->bridge_owner->state_dirty_generation();
    return true;
}

bool request_editor_open(Filter* filter, const char* reason)
{
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return false;

    std::lock_guard restart_lock(filter->restart_mutex);
    if (!filter->bridge_owner || !filter->bridge_owner->running()) {
        blog(LOG_WARNING, "[obs-safe-vst3] cannot open VST3 interface because helper is not running");
        return false;
    }

    if (!filter->bridge_owner->open_editor()) {
        blog(LOG_WARNING, "[obs-safe-vst3] failed to request native VST3 interface");
        return false;
    }
    blog(LOG_INFO, "[obs-safe-vst3] native VST3 interface requested: %s", reason ? reason : "explicit request");
    return true;
}

bool open_editor_button(obs_properties_t*, obs_property_t*, void* data)
{
    auto* filter = static_cast<Filter*>(data);
    (void)request_editor_open(filter, "Open Plug-in Interface button");
    return false;
}

void apply_current_parameter_settings(Filter* filter, obs_data_t* settings)
{
    if (!filter || !settings)
        return;
    const auto [path, class_id] = current_identity(filter);
    if (path.empty())
        return;
    const std::string scope = safevst3::obsparam::parameter_scope(path, class_id);

    std::lock_guard restart_lock(filter->restart_mutex);
    if (filter->bridge_owner && filter->bridge_owner->running() &&
        filter->bridge_path == path && filter->bridge_class_id == class_id) {
        safevst3::obsparam::apply_parameter_settings(*filter->bridge_owner, settings, scope);
    }
}

const char* filter_name(void*) { return obs_module_text("SafeVST3Filter"); }

void filter_defaults(obs_data_t* settings)
{
    obs_data_set_default_bool(settings, kEnabled, true);
    obs_data_set_default_bool(settings, kBrowseAutoOpenDone, false);
    obs_data_set_default_string(settings, kCustomPath, "");
}

void filter_update(void* data, obs_data_t* settings)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return;

    const std::string selected = obs_data_get_string(settings, kPluginPath);
    const std::string custom = obs_data_get_string(settings, kCustomPath);
    const std::string legacy_class = obs_data_get_string(settings, kClassId);

    std::string path;
    std::string class_id;
    const bool selection_has_class_field = split_selection(selected, path, class_id);
    const bool custom_browse_active = path.empty() && !custom.empty();
    if (custom_browse_active) {
        path = custom;
        class_id = legacy_class;
    } else if (!selection_has_class_field && class_id.empty()) {
        // Early scenes stored a path without a tab-delimited class field.
        // A modern scanner fallback deliberately stores "path\t" and must keep
        // that class id empty so Vst3Engine selects its first audio-effect.
        class_id = legacy_class;
    }

    const auto [old_path, old_class_id] = current_identity(filter);
    const bool identity_changed = old_path != path || old_class_id != class_id;

    if (identity_changed && !old_path.empty())
        (void)capture_bridge_state(filter, old_path, old_class_id, true);

    bool auto_open_after_first_browse = false;
    bool persist_consumed = false;
    {
        std::lock_guard lock(filter->config_mutex);
        if (filter->shutting_down.load(std::memory_order_acquire))
            return;

        // Any non-Browse transition that gives a previously empty filter an
        // identity consumes the one-shot without opening the editor. The real
        // Browse callback has already consumed + armed the one-shot before this
        // update arrives, so it is not caught here.
        if (!filter->browse_auto_open_consumed && identity_changed && !path.empty()) {
            filter->browse_auto_open_consumed = true;
            persist_consumed = true;
        }

        auto_open_after_first_browse = filter->browse_auto_open_pending &&
                                       identity_changed && custom_browse_active && !path.empty();
        filter->browse_auto_open_pending = false;
        filter->observed_custom_path = custom;
        filter->path = path;
        filter->class_id = class_id;
    }

    if (persist_consumed)
        obs_data_set_bool(settings, kBrowseAutoOpenDone, true);

    const bool new_enabled = true;
    const bool enabled_changed = filter->enabled.exchange(new_enabled, std::memory_order_relaxed) != new_enabled;
    filter->deadline_fraction.store(kInternalDeadlineFraction, std::memory_order_relaxed);

    const bool needs_restart = identity_changed || enabled_changed;
    if (needs_restart) {
        restart_bridge(filter);
    } else {
        apply_current_parameter_settings(filter, settings);
    }

    if (auto_open_after_first_browse)
        (void)request_editor_open(filter, "first custom Browse gesture");

    if (needs_restart && filter->context)
        obs_source_update_properties(filter->context);
}

void filter_save(void* data, obs_data_t* settings)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return;

    bool consumed = false;
    {
        std::lock_guard lock(filter->config_mutex);
        consumed = filter->browse_auto_open_consumed;
    }
    if (settings && consumed)
        obs_data_set_bool(settings, kBrowseAutoOpenDone, true);
    (void)capture_current_bridge_state(filter, true);
}

void* filter_create(obs_data_t* settings, obs_source_t* context)
{
    auto* filter = new Filter{};
    filter->context = context;

    const std::string saved_selection = obs_data_get_string(settings, kPluginPath);
    const std::string saved_custom = obs_data_get_string(settings, kCustomPath);
    filter->observed_custom_path = saved_custom;
    filter->browse_auto_open_consumed = obs_data_get_bool(settings, kBrowseAutoOpenDone) ||
                                        !saved_selection.empty() || !saved_custom.empty();

    obs_audio_info audio_info{};
    if (obs_get_audio_info(&audio_info)) {
        filter->sample_rate = audio_info.samples_per_sec;
        filter->channels = static_cast<std::uint32_t>(get_audio_channels(audio_info.speakers));
    }

    filter_update(filter, settings);
    filter->recovery_thread = std::jthread([filter](std::stop_token stop) {
        safevst3::RecoveryPolicy recovery_policy;
        std::uint64_t previous_deadline_misses = 0;
        unsigned stable_dirty_ticks = 0;
        unsigned deadline_pressure_ticks = 0;

        while (!stop.stop_requested()) {
            for (int i = 0; i < 10 && !stop.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop.stop_requested() || filter->shutting_down.load(std::memory_order_acquire))
                break;

            bool has_path = false;
            {
                std::lock_guard lock(filter->config_mutex);
                has_path = !filter->path.empty();
            }
            if (!filter->enabled.load(std::memory_order_relaxed) || !has_path) {
                recovery_policy.reset();
                previous_deadline_misses = 0;
                stable_dirty_ticks = 0;
                deadline_pressure_ticks = 0;
                continue;
            }

            const BridgeHealthSnapshot health = read_bridge_health(filter);
            const std::uint64_t miss_delta = health.deadline_misses >= previous_deadline_misses
                                                 ? health.deadline_misses - previous_deadline_misses
                                                 : 0;
            previous_deadline_misses = health.deadline_misses;
            const std::uint64_t now_ms = static_cast<std::uint64_t>(GetTickCount64());
            const safevst3::RecoveryDecision decision = recovery_policy.observe(
                now_ms,
                {health.has_bridge && health.process_alive, health.heartbeat_age_ms, miss_delta});

            if (decision.restart) {
                const char* reason = decision.health == safevst3::RecoveryHealth::Hung
                                         ? "helper heartbeat stalled"
                                         : "helper exited/unavailable";
                blog(LOG_WARNING,
                     "[obs-safe-vst3] %s; starting isolated recovery attempt %u",
                     reason,
                     recovery_policy.recovery_attempts() + 1u);
                recovery_policy.record_restart_attempt(now_ms);
                restart_bridge(filter);
                previous_deadline_misses = 0;
                stable_dirty_ticks = 0;
                deadline_pressure_ticks = 0;
                if (filter->context)
                    obs_source_update_properties(filter->context);
                continue;
            }

            if (decision.health == safevst3::RecoveryHealth::Backoff) {
                stable_dirty_ticks = 0;
                continue;
            }

            if (decision.health == safevst3::RecoveryHealth::DeadlinePressure) {
                if (++deadline_pressure_ticks == 1 || deadline_pressure_ticks % 10 == 0) {
                    blog(LOG_WARNING,
                         "[obs-safe-vst3] helper under realtime deadline pressure (%llu misses in last observation); keeping DSP alive",
                         static_cast<unsigned long long>(miss_delta));
                }
            } else {
                deadline_pressure_ticks = 0;
            }

            std::uint32_t dirty = 0;
            if (read_bridge_state_generation(filter, dirty)) {
                const std::uint32_t observed =
                    filter->observed_state_generation.load(std::memory_order_acquire);
                if (dirty != observed) {
                    filter->observed_state_generation.store(dirty, std::memory_order_release);
                    stable_dirty_ticks = 0;
                } else if (dirty != filter->saved_state_generation.load(std::memory_order_acquire)) {
                    if (++stable_dirty_ticks >= 2) {
                        (void)capture_current_bridge_state(filter, false);
                        stable_dirty_ticks = 0;
                    }
                } else {
                    stable_dirty_ticks = 0;
                }
            } else {
                stable_dirty_ticks = 0;
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

    (void)capture_current_bridge_state(filter, true);

    filter->shutting_down.store(true, std::memory_order_release);
    filter->shutdown_source.request_stop();
    filter->enabled.store(false, std::memory_order_release);
    filter->recovery_thread.request_stop();
    filter->recovery_thread = std::jthread{};

    {
        std::lock_guard restart_lock(filter->restart_mutex);
        publish_bridge_locked(filter, {});
    }

    retire_filter_until_module_unload(filter);
    reap_retired_bridges();
}

obs_properties_t* filter_properties(void* data)
{
    auto* filter = static_cast<Filter*>(data);
    obs_properties_t* props = obs_properties_create();

    auto* list = obs_properties_add_list(props, kPluginPath, obs_module_text("InstalledVST3"),
                                         OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    populate_plugin_list(list);
    obs_properties_add_button2(props, kRescan, obs_module_text("RescanVST3"), rescan_button, filter);
    auto* custom_path = obs_properties_add_path(
        props, kCustomPath, obs_module_text("CustomVST3Path"), OBS_PATH_DIRECTORY, nullptr, nullptr);
    obs_property_set_modified_callback2(custom_path, custom_path_modified, filter);

    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return props;

    const auto [path, class_id] = current_identity(filter);
    if (path.empty())
        return props;

    std::vector<safevst3::ParameterSnapshot> parameters;
    std::uint32_t total_parameter_count = 0;
    safevst3::EditorStatus editor_status = safevst3::EditorStatus::Unknown;
    bool running = false;
    std::string plugin_name;
    std::uint32_t latency_samples = 0;
    {
        std::lock_guard restart_lock(filter->restart_mutex);
        running = filter->bridge_owner && filter->bridge_owner->running() &&
                  filter->bridge_path == path && filter->bridge_class_id == class_id;
        if (running) {
            plugin_name = filter->bridge_owner->plugin_name();
            latency_samples = filter->bridge_owner->latency_samples();
            parameters = filter->bridge_owner->parameters();
            total_parameter_count = filter->bridge_owner->parameter_total_count();
            editor_status = filter->bridge_owner->editor_status();
        }
    }

    std::string status;
    if (running) {
        if (plugin_name.empty())
            plugin_name = filename_utf8(path);
        status = plugin_name + " — Ready — " + std::to_string(latency_samples) + " samples latency";
    } else {
        status = "Plug-in unavailable — dry audio remains active";
    }
    obs_properties_add_text(props, kPluginStatus, status.c_str(), OBS_TEXT_INFO);

    auto* open_editor = obs_properties_add_button2(
        props, kOpenEditor, obs_module_text("OpenPluginUI"), open_editor_button, filter);
    const bool can_try_native_editor = running &&
        editor_status != safevst3::EditorStatus::Unsupported &&
        editor_status != safevst3::EditorStatus::Error;
    const bool native_editor_confirmed = running &&
        (editor_status == safevst3::EditorStatus::Open ||
         editor_status == safevst3::EditorStatus::Closed);

    if (!can_try_native_editor)
        obs_property_set_enabled(open_editor, false);

    if (native_editor_confirmed || parameters.empty())
        return props;

    obs_data_t* settings = filter->context ? obs_source_get_settings(filter->context) : nullptr;
    safevst3::obsparam::add_generic_parameter_properties(
        props, parameters, total_parameter_count, settings,
        safevst3::obsparam::parameter_scope(path, class_id));
    if (settings)
        obs_data_release(settings);
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
    info.save = filter_save;
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
    try {
        start_startup_scanner();
    } catch (const std::exception& e) {
        blog(LOG_WARNING, "[obs-safe-vst3] background VST3 discovery could not start: %s", e.what());
    }
    blog(LOG_INFO, "[obs-safe-vst3] Phase S stable module loaded");
    return true;
}

void obs_module_unload(void)
{
    stop_startup_scanner();
    stop_retired_bridge_reaper();
    release_retired_filters();
}

const char* obs_module_description(void)
{
    return "Crash-isolated VST3 host with OBS-native workflow, vendor interface and fallback controls";
}

#endif