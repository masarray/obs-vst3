#ifdef _WIN32

#include "obs-plugin/rack_filter.hpp"

#include "platform/windows/win_rack_bridge.hpp"

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace safevst3::obsrack {
namespace {

constexpr char kRackStatus[] = "rack_status";
constexpr char kRackOpen[] = "rack_open";

struct RackFilter {
    obs_source_t* context = nullptr;
    std::unique_ptr<WinRackBridge> bridge;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::atomic<bool> shutting_down{false};
    std::string startup_error;
};

std::mutex retired_filters_mutex;
std::vector<std::unique_ptr<RackFilter>> retired_filters;

std::filesystem::path module_binary_dir()
{
    const char* module_path = obs_get_module_binary_path(obs_current_module());
    if (!module_path)
        return {};
    return std::filesystem::u8path(module_path).parent_path();
}

std::filesystem::path rack_helper_path()
{
    if (const char* env = std::getenv("OBS_SAFE_VST3_RACK_HOST_PATH"); env && *env)
        return std::filesystem::u8path(env);
    const auto dir = module_binary_dir();
    return dir.empty() ? std::filesystem::path{} : dir / "obs-safe-vst3-rack-host.exe";
}

bool start_rack_bridge(RackFilter& filter)
{
    const auto helper = rack_helper_path();
    if (helper.empty() || !std::filesystem::exists(helper)) {
        filter.startup_error = "Rack helper executable is unavailable";
        return false;
    }

    auto bridge = std::make_unique<WinRackBridge>();
    std::string error;
    if (!bridge->start(helper, filter.sample_rate, filter.channels, error)) {
        filter.startup_error = error.empty() ? "Rack helper could not start" : error;
        return false;
    }

    filter.startup_error.clear();
    filter.bridge = std::move(bridge);
    return true;
}

const char* rack_name(void*)
{
    return obs_module_text("SafeVST3RackFilter");
}

void* rack_create(obs_data_t*, obs_source_t* context)
{
    auto filter = std::make_unique<RackFilter>();
    filter->context = context;

    obs_audio_info audio_info{};
    if (obs_get_audio_info(&audio_info)) {
        filter->sample_rate = audio_info.samples_per_sec;
        filter->channels = static_cast<std::uint32_t>(get_audio_channels(audio_info.speakers));
    }

    if (filter->sample_rate == 0 || filter->channels == 0 || filter->channels > rack::kMaxChannels) {
        filter->startup_error = "Unsupported OBS audio layout";
    } else {
        (void)start_rack_bridge(*filter);
    }

    return filter.release();
}

void rack_destroy(void* data)
{
    std::unique_ptr<RackFilter> filter(static_cast<RackFilter*>(data));
    if (!filter)
        return;

    filter->shutting_down.store(true, std::memory_order_release);
    if (filter->bridge)
        filter->bridge->stop();

    // OBS owns Properties objects. Keep callback data alive until module unload
    // so removing a Rack while Properties is closing cannot leave a dangling
    // callback pointer. No async code rebuilds the Properties tree.
    std::lock_guard lock(retired_filters_mutex);
    retired_filters.push_back(std::move(filter));
}

bool open_rack_button(obs_properties_t*, obs_property_t*, void* data)
{
    auto* filter = static_cast<RackFilter*>(data);
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return false;
    if (!filter->bridge || !filter->bridge->running())
        return false;
    return filter->bridge->open_editor();
}

std::string rack_status_text(const RackFilter* filter)
{
    if (!filter || filter->shutting_down.load(std::memory_order_acquire))
        return "Rack unavailable — dry audio remains active";

    if (!filter->bridge || !filter->bridge->running()) {
        std::string status = "Needs Attention — dry audio remains active";
        if (!filter->startup_error.empty())
            status += " — " + filter->startup_error;
        return status;
    }

    const RackBridgeStatus status = filter->bridge->status();
    return "Ready — " + std::to_string(status.effect_count) +
           (status.effect_count == 1 ? " effect — " : " effects — ") +
           std::to_string(status.total_latency_samples) + " samples latency";
}

obs_properties_t* rack_properties(void* data)
{
    auto* filter = static_cast<RackFilter*>(data);
    obs_properties_t* props = obs_properties_create();
    const std::string status = rack_status_text(filter);
    obs_properties_add_text(props, kRackStatus, status.c_str(), OBS_TEXT_INFO);
    obs_properties_add_button2(props, kRackOpen, obs_module_text("RackOpen"), open_rack_button, filter);
    return props;
}

obs_audio_data* rack_filter_audio(void* data, obs_audio_data* audio)
{
    auto* filter = static_cast<RackFilter*>(data);
    if (!filter || !audio || filter->shutting_down.load(std::memory_order_relaxed) ||
        !filter->bridge || !filter->bridge->running())
        return audio;

    if (audio->frames == 0 || audio->frames > rack::kMaxFrames ||
        filter->channels == 0 || filter->channels > rack::kMaxChannels)
        return audio;

    float* planes[rack::kMaxChannels]{};
    for (std::uint32_t ch = 0; ch < filter->channels; ++ch) {
        if (!audio->data[ch])
            return audio;
        planes[ch] = reinterpret_cast<float*>(audio->data[ch]);
    }

    (void)filter->bridge->process(planes, filter->channels, audio->frames, 0.70);
    return audio;
}

} // namespace

obs_source_info make_source_info()
{
    obs_source_info info{};
    info.id = "obs_safe_vst3_rack_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = rack_name;
    info.create = rack_create;
    info.destroy = rack_destroy;
    info.get_properties = rack_properties;
    info.filter_audio = rack_filter_audio;
    return info;
}

void module_unload() noexcept
{
    std::lock_guard lock(retired_filters_mutex);
    retired_filters.clear();
}

} // namespace safevst3::obsrack

#endif
