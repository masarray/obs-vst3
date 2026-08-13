#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-safe-vst3", "en-US")

namespace {
using safevst3::WinObsBridge;

constexpr const char* kPluginPath = "vst3_path";
constexpr const char* kClassId = "class_id";
constexpr const char* kEnabled = "enabled";
constexpr const char* kDeadline = "deadline_fraction";

struct Filter {
    obs_source_t* context = nullptr;
    std::unique_ptr<WinObsBridge> bridge;
    std::string path;
    std::string class_id;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    double deadline_fraction = 0.70;
    bool enabled = true;
    bool warned_multichannel = false;
};

std::filesystem::path helper_path()
{
    if (const char* env = std::getenv("OBS_SAFE_VST3_HOST_PATH"); env && *env)
        return std::filesystem::u8path(env);

    const char* module_path = obs_get_module_binary_path(obs_current_module());
    if (!module_path)
        return {};
    return std::filesystem::u8path(module_path).parent_path() / "obs-safe-vst3-host.exe";
}

void restart_bridge(Filter* filter)
{
    if (!filter)
        return;
    filter->bridge.reset();
    if (!filter->enabled || filter->path.empty())
        return;

    if (filter->channels == 0 || filter->channels > safevst3::kMaxChannels) {
        blog(LOG_WARNING, "[obs-safe-vst3] P0 supports mono/stereo only; current OBS layout has %u channels", filter->channels);
        return;
    }

    auto bridge = std::make_unique<WinObsBridge>();
    std::string error;
    const auto helper = helper_path();
    if (helper.empty() || !std::filesystem::exists(helper)) {
        blog(LOG_ERROR, "[obs-safe-vst3] helper executable not found next to plugin binary");
        return;
    }

    if (!bridge->start(helper, std::filesystem::u8path(filter->path), filter->class_id,
                       filter->sample_rate, filter->channels, error)) {
        blog(LOG_ERROR, "[obs-safe-vst3] failed to start isolated VST3 host: %s", error.c_str());
        return;
    }

    blog(LOG_INFO, "[obs-safe-vst3] isolated VST3 helper ready: %s", filter->path.c_str());
    filter->bridge = std::move(bridge);
}

const char* filter_name(void*) { return obs_module_text("SafeVST3Filter"); }

void filter_defaults(obs_data_t* settings)
{
    obs_data_set_default_bool(settings, kEnabled, true);
    obs_data_set_default_double(settings, kDeadline, 0.70);
}

void filter_update(void* data, obs_data_t* settings)
{
    auto* filter = static_cast<Filter*>(data);
    filter->enabled = obs_data_get_bool(settings, kEnabled);
    filter->deadline_fraction = obs_data_get_double(settings, kDeadline);
    filter->path = obs_data_get_string(settings, kPluginPath);
    filter->class_id = obs_data_get_string(settings, kClassId);
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
    return filter;
}

void filter_destroy(void* data)
{
    auto* filter = static_cast<Filter*>(data);
    delete filter;
}

obs_properties_t* filter_properties(void*)
{
    obs_properties_t* props = obs_properties_create();
    obs_properties_add_bool(props, kEnabled, obs_module_text("Enabled"));
    obs_properties_add_path(props, kPluginPath, obs_module_text("VST3Path"), OBS_PATH_DIRECTORY, nullptr, nullptr);
    obs_properties_add_text(props, kClassId, obs_module_text("ClassID"), OBS_TEXT_DEFAULT);
    obs_properties_add_float_slider(props, kDeadline, obs_module_text("DeadlineFraction"), 0.10, 0.95, 0.05);
    return props;
}

obs_audio_data* filter_audio(void* data, obs_audio_data* audio)
{
    auto* filter = static_cast<Filter*>(data);
    if (!filter || !audio || !filter->enabled || !filter->bridge || !filter->bridge->running())
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

    // On timeout/error WinObsBridge intentionally leaves the original OBS buffer untouched.
    (void)filter->bridge->process(planes, filter->channels, audio->frames, filter->deadline_fraction);
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
    blog(LOG_INFO, "[obs-safe-vst3] P0 module loaded");
    return true;
}

const char* obs_module_description(void)
{
    return "Crash-isolated VST3 audio-effect host P0 for OBS Studio";
}

#endif
