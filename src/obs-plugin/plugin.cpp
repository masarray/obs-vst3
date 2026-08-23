#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

struct ScanEntry {
    std::string name;
    std::string path;
    std::string class_id;
};

struct Filter {
    obs_source_t* context = nullptr;
    std::shared_ptr<WinObsBridge> bridge;
    std::mutex config_mutex;
    std::mutex restart_mutex;
    std::jthread recovery_thread;
    std::string path;
    std::string class_id;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::atomic<double> deadline_fraction{0.70};
    std::atomic<bool> enabled{true};
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

    const DWORD wait = WaitForSingleObject(pi.hProcess, 120000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0xDEAD);
        blog(LOG_WARNING, "[obs-safe-vst3] VST3 scan timed out after 120 seconds; previous cache kept");
    }

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
        const auto filename = std::filesystem::u8path(entry.path).filename().string();
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
    if (!filter)
        return;
    std::lock_guard restart_lock(filter->restart_mutex);

    std::string path;
    std::string class_id;
    {
        std::lock_guard config_lock(filter->config_mutex);
        path = filter->path;
        class_id = filter->class_id;
    }

    if (!filter->enabled.load(std::memory_order_relaxed) || path.empty()) {
        std::atomic_store_explicit(&filter->bridge, std::shared_ptr<WinObsBridge>{}, std::memory_order_release);
        return;
    }

    const auto sample_rate = filter->sample_rate;
    const auto channels = filter->channels;
    if (channels == 0 || channels > safevst3::kMaxChannels) {
        blog(LOG_WARNING, "[obs-safe-vst3] public trial supports mono/stereo only; current OBS layout has %u channels", channels);
        std::atomic_store_explicit(&filter->bridge, std::shared_ptr<WinObsBridge>{}, std::memory_order_release);
        return;
    }

    auto bridge = std::make_shared<WinObsBridge>();
    std::string error;
    const auto helper = helper_path();
    if (helper.empty() || !std::filesystem::exists(helper)) {
        blog(LOG_ERROR, "[obs-safe-vst3] helper executable not found next to plugin binary");
        std::atomic_store_explicit(&filter->bridge, std::shared_ptr<WinObsBridge>{}, std::memory_order_release);
        return;
    }

    if (!bridge->start(helper, std::filesystem::u8path(path), class_id, sample_rate, channels, error)) {
        blog(LOG_ERROR, "[obs-safe-vst3] failed to start isolated VST3 host: %s", error.c_str());
        std::atomic_store_explicit(&filter->bridge, std::shared_ptr<WinObsBridge>{}, std::memory_order_release);
        return;
    }

    blog(LOG_INFO, "[obs-safe-vst3] isolated VST3 helper ready: %s", path.c_str());
    std::atomic_store_explicit(&filter->bridge, std::move(bridge), std::memory_order_release);
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
            if (stop.stop_requested())
                break;

            bool has_path = false;
            {
                std::lock_guard lock(filter->config_mutex);
                has_path = !filter->path.empty();
            }
            if (!filter->enabled.load(std::memory_order_relaxed) || !has_path)
                continue;

            auto bridge = std::atomic_load_explicit(&filter->bridge, std::memory_order_acquire);
            if (!bridge || !bridge->running()) {
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
    filter->recovery_thread.request_stop();
    filter->recovery_thread = std::jthread{};
    std::atomic_store_explicit(&filter->bridge, std::shared_ptr<WinObsBridge>{}, std::memory_order_release);
    delete filter;
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
    if (!filter || !audio || !filter->enabled.load(std::memory_order_relaxed))
        return audio;

    auto bridge = std::atomic_load_explicit(&filter->bridge, std::memory_order_acquire);
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
    obs_register_source(&source_info);
    blog(LOG_INFO, "[obs-safe-vst3] public-trial module loaded");
    return true;
}

const char* obs_module_description(void)
{
    return "Crash-isolated VST3 audio-effect host public trial for OBS Studio";
}

#endif
