#ifdef _WIN32

#include "obs-plugin/rack_filter.hpp"

#include "platform/windows/win_rack_bridge.hpp"
#include "rack/rack_recovery_policy.hpp"

#include <obs.h>
#include <obs-module.h>
#include <media-io/audio-io.h>
#include <util/bmem.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace safevst3::obsrack {
namespace {

constexpr char kRackSummary[] = "rack_summary";
constexpr char kRackStatus[] = "rack_status";
constexpr char kRackOpen[] = "rack_open";
constexpr char kRackSessionId[] = "rack_session_id";
constexpr unsigned kDestroyDrainAttempts = 100;
constexpr unsigned kRestartDrainAttempts = 50;
constexpr unsigned kMaxRecoveryAttempts = 5;
constexpr auto kSupervisorPollInterval = std::chrono::milliseconds(100);

struct RackFilter {
    obs_source_t* context = nullptr;
    std::unique_ptr<WinRackBridge> bridge;
    std::mutex bridge_mutex;
    std::mutex recovery_mutex;
    std::mutex supervisor_wait_mutex;
    std::condition_variable supervisor_cv;
    std::jthread supervisor;
    rack::RackRecoveryPolicy recovery_policy;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::atomic<bool> shutting_down{false};
    std::atomic<bool> bridge_restarting{false};
    std::atomic<bool> recovery_quarantined{false};
    std::atomic<std::uint32_t> audio_readers{0};
    std::filesystem::path session_snapshot;
    std::string session_id;
    std::string startup_error;
};

class RackAudioReadGuard {
public:
    explicit RackAudioReadGuard(RackFilter* filter) noexcept : filter_(filter)
    {
        if (!filter_)
            return;
        filter_->audio_readers.fetch_add(1, std::memory_order_acq_rel);
        if (filter_->shutting_down.load(std::memory_order_acquire) ||
            filter_->bridge_restarting.load(std::memory_order_acquire)) {
            filter_->audio_readers.fetch_sub(1, std::memory_order_release);
            filter_ = nullptr;
        }
    }

    RackAudioReadGuard(const RackAudioReadGuard&) = delete;
    RackAudioReadGuard& operator=(const RackAudioReadGuard&) = delete;

    ~RackAudioReadGuard()
    {
        if (filter_)
            filter_->audio_readers.fetch_sub(1, std::memory_order_release);
    }

    bool active() const noexcept { return filter_ != nullptr; }

private:
    RackFilter* filter_ = nullptr;
};

std::mutex retired_filters_mutex;
std::vector<std::unique_ptr<RackFilter>> retired_filters;

bool valid_session_id(std::string_view value) noexcept
{
    if (value.size() != 32)
        return false;
    for (const char ch : value) {
        const bool digit = ch >= '0' && ch <= '9';
        const bool lower = ch >= 'a' && ch <= 'f';
        const bool upper = ch >= 'A' && ch <= 'F';
        if (!digit && !lower && !upper)
            return false;
    }
    return true;
}

std::string generate_session_id()
{
    std::array<std::uint8_t, 16> bytes{};
    try {
        std::random_device random;
        for (auto& byte : bytes)
            byte = static_cast<std::uint8_t>(random());
    } catch (...) {
        static std::atomic<std::uint64_t> counter{0};
        const std::uint64_t seed = static_cast<std::uint64_t>(GetTickCount64()) ^
            (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32u) ^
            counter.fetch_add(1, std::memory_order_relaxed);
        std::mt19937_64 fallback(seed);
        for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(std::uint64_t)) {
            const std::uint64_t value = fallback();
            for (std::size_t i = 0; i < sizeof(value); ++i)
                bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8u));
        }
    }

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.resize(bytes.size() * 2u);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        result[i * 2u] = hex[(bytes[i] >> 4u) & 0x0fu];
        result[i * 2u + 1u] = hex[bytes[i] & 0x0fu];
    }
    return result;
}

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

std::filesystem::path rack_session_path(std::string_view session_id)
{
    if (!valid_session_id(session_id))
        return {};
    char* config = obs_module_config_path("rack-sessions");
    if (!config)
        return {};
    std::filesystem::path result;
    try {
        result = std::filesystem::u8path(config) /
            (std::string(session_id) + ".rack-session");
    } catch (...) {
        result.clear();
    }
    bfree(config);
    return result;
}

bool start_rack_bridge(RackFilter& filter, std::stop_token cancel = {})
{
    const auto helper = rack_helper_path();
    if (helper.empty() || !std::filesystem::exists(helper)) {
        filter.startup_error = "Rack helper executable is unavailable";
        return false;
    }
    if (!valid_session_id(filter.session_id) || filter.session_snapshot.empty()) {
        filter.startup_error = "Rack Session Snapshot identity/path is unavailable";
        return false;
    }

    if (!filter.bridge)
        filter.bridge = std::make_unique<WinRackBridge>();

    std::string error;
    if (!filter.bridge->start(helper, filter.sample_rate, filter.channels, error,
                              cancel, filter.session_snapshot, filter.session_id)) {
        filter.startup_error = error.empty() ? "Rack helper could not start" : error;
        return false;
    }

    filter.startup_error.clear();
    return true;
}

bool restart_rack_bridge(RackFilter& filter, std::stop_token stop) noexcept
{
    if (filter.shutting_down.load(std::memory_order_acquire) || stop.stop_requested())
        return false;

    filter.bridge_restarting.store(true, std::memory_order_release);
    bool drained = false;
    for (unsigned attempt = 0; attempt < kRestartDrainAttempts; ++attempt) {
        if (filter.audio_readers.load(std::memory_order_acquire) == 0) {
            drained = true;
            break;
        }
        if (stop.stop_requested() || filter.shutting_down.load(std::memory_order_acquire))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool restarted = false;
    if (drained && !stop.stop_requested() &&
        !filter.shutting_down.load(std::memory_order_acquire)) {
        std::lock_guard lock(filter.bridge_mutex);
        if (!stop.stop_requested() &&
            !filter.shutting_down.load(std::memory_order_acquire))
            restarted = start_rack_bridge(filter, stop);
    }

    filter.bridge_restarting.store(false, std::memory_order_release);
    return restarted;
}

void rack_supervisor_loop(RackFilter* filter, std::stop_token stop) noexcept
{
    if (!filter)
        return;

    std::uint64_t last_progress = 0;
    std::uint64_t last_progress_ms = static_cast<std::uint64_t>(GetTickCount64());
    std::uint64_t last_deadline_misses = 0;

    while (!stop.stop_requested() &&
           !filter->shutting_down.load(std::memory_order_acquire)) {
        {
            std::unique_lock wait_lock(filter->supervisor_wait_mutex);
            filter->supervisor_cv.wait_for(wait_lock, kSupervisorPollInterval);
        }
        if (stop.stop_requested() ||
            filter->shutting_down.load(std::memory_order_acquire))
            break;

        if (filter->recovery_quarantined.load(std::memory_order_acquire))
            continue;

        RackBridgeHealthSnapshot health{};
        {
            std::lock_guard lock(filter->bridge_mutex);
            if (filter->bridge)
                health = filter->bridge->health_snapshot();
        }

        const std::uint64_t now = static_cast<std::uint64_t>(GetTickCount64());
        const bool progress_changed = health.dsp_progress_generation != last_progress;
        if (progress_changed) {
            last_progress = health.dsp_progress_generation;
            last_progress_ms = now;
        }

        const std::uint64_t miss_delta = health.deadline_misses >= last_deadline_misses
            ? health.deadline_misses - last_deadline_misses
            : health.deadline_misses;
        last_deadline_misses = health.deadline_misses;

        // A quiet/inactive OBS source may legitimately produce no Rack requests.
        // Only age the DSP heartbeat while fresh realtime deadline misses prove
        // that audio requests are currently arriving but the helper is not
        // making progress.
        std::uint64_t heartbeat_age_ms = 0;
        if (health.process_alive && health.ready && miss_delta != 0 && !progress_changed)
            heartbeat_age_ms = now >= last_progress_ms ? now - last_progress_ms : 0;

        RecoveryDecision decision{};
        {
            std::lock_guard policy_lock(filter->recovery_mutex);
            decision = filter->recovery_policy.observe(
                now,
                RecoveryObservation{
                    health.process_alive && health.ready,
                    heartbeat_age_ms,
                    miss_delta,
                });
            if (decision.restart) {
                if (filter->recovery_policy.recovery_attempts() >= kMaxRecoveryAttempts) {
                    filter->recovery_quarantined.store(true, std::memory_order_release);
                    continue;
                }
                filter->recovery_policy.record_restart_attempt(now);
            }
        }

        if (!decision.restart)
            continue;

        if (restart_rack_bridge(*filter, stop)) {
            RackBridgeHealthSnapshot restarted_health{};
            {
                std::lock_guard lock(filter->bridge_mutex);
                if (filter->bridge)
                    restarted_health = filter->bridge->health_snapshot();
            }
            last_progress = restarted_health.dsp_progress_generation;
            last_progress_ms = static_cast<std::uint64_t>(GetTickCount64());
            last_deadline_misses = restarted_health.deadline_misses;
        }
    }
}

const char* rack_name(void*)
{
    return obs_module_text("SafeVST3RackFilter");
}

void* rack_create(obs_data_t* settings, obs_source_t* context)
{
    auto filter = std::make_unique<RackFilter>();
    filter->context = context;

    std::string session_id;
    if (settings) {
        const char* stored = obs_data_get_string(settings, kRackSessionId);
        if (stored && valid_session_id(stored))
            session_id = stored;
    }
    if (session_id.empty()) {
        session_id = generate_session_id();
        if (settings)
            obs_data_set_string(settings, kRackSessionId, session_id.c_str());
    }
    filter->session_id = session_id;
    filter->session_snapshot = rack_session_path(session_id);

    obs_audio_info audio_info{};
    if (obs_get_audio_info(&audio_info)) {
        filter->sample_rate = audio_info.samples_per_sec;
        filter->channels = static_cast<std::uint32_t>(get_audio_channels(audio_info.speakers));
    }

    const bool audio_supported = filter->sample_rate != 0 && filter->channels != 0 &&
        filter->channels <= safevst3::kMaxChannels;
    if (!audio_supported) {
        filter->startup_error = "Unsupported OBS audio layout";
    } else if (filter->session_snapshot.empty()) {
        filter->startup_error = "Rack Session Snapshot path is unavailable";
    } else {
        (void)start_rack_bridge(*filter);
        RackFilter* raw = filter.get();
        filter->supervisor = std::jthread(
            [raw](std::stop_token stop) { rack_supervisor_loop(raw, stop); });
    }

    return filter.release();
}

void rack_destroy(void* data)
{
    std::unique_ptr<RackFilter> filter(static_cast<RackFilter*>(data));
    if (!filter)
        return;

    // Publish both gates before stopping the supervisor. New realtime callbacks
    // remain dry while control-side recovery is being cancelled.
    filter->shutting_down.store(true, std::memory_order_release);
    filter->bridge_restarting.store(true, std::memory_order_release);
    if (filter->supervisor.joinable()) {
        filter->supervisor.request_stop();
        filter->supervisor_cv.notify_all();
        filter->supervisor.join();
    }

    bool drained = false;
    for (unsigned attempt = 0; attempt < kDestroyDrainAttempts; ++attempt) {
        if (filter->audio_readers.load(std::memory_order_acquire) == 0) {
            drained = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (drained) {
        // Properties/control callbacks use this mutex and are non-realtime.
        // Once the realtime reader frontier is empty, serialize against any
        // callback already in progress before stopping/unmapping the helper.
        std::lock_guard lock(filter->bridge_mutex);
        if (filter->bridge)
            filter->bridge->stop();
    }

    // OBS owns Properties objects. Keep callback data alive until module unload
    // so removing a Rack while Properties is closing cannot leave a dangling
    // callback pointer. If a pathological realtime reader failed to drain in
    // the bounded window, retaining the live bridge is safer than racing stop;
    // module unload releases it after OBS has stopped module callbacks.
    std::lock_guard lock(retired_filters_mutex);
    retired_filters.push_back(std::move(filter));
}

bool open_rack_button(obs_properties_t*, obs_property_t*, void* data)
{
    auto* filter = static_cast<RackFilter*>(data);
    if (!filter)
        return false;

    {
        std::lock_guard lock(filter->bridge_mutex);
        if (filter->shutting_down.load(std::memory_order_acquire))
            return false;
        if (filter->bridge && filter->bridge->running())
            return filter->bridge->open_editor();
    }

    // A Rack that exhausted its automatic recovery budget stays dry instead of
    // restart-looping. The existing Open Rack button doubles as an intentional
    // user retry without adding another mutable OBS Properties surface.
    {
        std::lock_guard policy_lock(filter->recovery_mutex);
        filter->recovery_policy.reset();
        filter->recovery_quarantined.store(false, std::memory_order_release);
    }
    filter->supervisor_cv.notify_all();
    return false;
}

std::string rack_status_text(RackFilter* filter)
{
    if (!filter)
        return "Rack unavailable — dry audio remains active";

    if (filter->recovery_quarantined.load(std::memory_order_acquire))
        return "Needs Attention — recovery paused after repeated failures — dry audio remains active";
    if (filter->bridge_restarting.load(std::memory_order_acquire))
        return "Recovering — dry audio remains active";

    std::lock_guard lock(filter->bridge_mutex);
    if (filter->shutting_down.load(std::memory_order_acquire))
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
    obs_properties_add_text(
        props, kRackSummary, obs_module_text("RackWorkingSummary"), OBS_TEXT_INFO);
    const std::string status = rack_status_text(filter);
    obs_properties_add_text(props, kRackStatus, status.c_str(), OBS_TEXT_INFO);
    obs_properties_add_button2(props, kRackOpen, obs_module_text("RackOpen"), open_rack_button, filter);
    return props;
}

obs_audio_data* rack_filter_audio(void* data, obs_audio_data* audio)
{
    auto* filter = static_cast<RackFilter*>(data);
    if (!filter || !audio)
        return audio;

    RackAudioReadGuard read(filter);
    if (!read.active())
        return audio;

    WinRackBridge* bridge = filter->bridge.get();
    if (!bridge || !bridge->running())
        return audio;

    if (audio->frames == 0 || audio->frames > safevst3::kMaxFrames ||
        filter->channels == 0 || filter->channels > safevst3::kMaxChannels)
        return audio;

    float* planes[safevst3::kMaxChannels]{};
    for (std::uint32_t ch = 0; ch < filter->channels; ++ch) {
        if (!audio->data[ch])
            return audio;
        planes[ch] = reinterpret_cast<float*>(audio->data[ch]);
    }

    (void)bridge->process(planes, filter->channels, audio->frames, 0.70);
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
