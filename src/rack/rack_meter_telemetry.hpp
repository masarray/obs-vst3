#pragma once

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace safevst3::rack::ui {

// Metering is intentionally a separate, lossy telemetry stream. The audio
// thread only writes bounded lock-free atomics; UI may skip intermediate blocks
// without affecting sound, topology generations, recovery or command semantics.
struct RackStereoPeaks {
    float left = 0.0f;
    float right = 0.0f;
};

struct RackMeterTelemetrySnapshot {
    std::uint64_t sequence = 0;
    float input_peak_linear = 0.0f;
    float output_peak_linear = 0.0f;
    float input_left_peak_linear = 0.0f;
    float input_right_peak_linear = 0.0f;
    float output_left_peak_linear = 0.0f;
    float output_right_peak_linear = 0.0f;
    bool valid = false;
};

inline constexpr float kRackMeterMaxLinear = 4.0f; // +12.04 dBFS headroom
inline constexpr std::uint32_t kRackMeterFixedScale = 1'000'000u;

inline std::uint32_t rack_meter_encode(float peak) noexcept
{
    if (!(peak > 0.0f))
        return 0;
    const float bounded = std::min(peak, kRackMeterMaxLinear);
    return static_cast<std::uint32_t>(bounded * static_cast<float>(kRackMeterFixedScale) + 0.5f);
}

inline float rack_meter_decode(std::uint32_t encoded) noexcept
{
    return static_cast<float>(encoded) / static_cast<float>(kRackMeterFixedScale);
}

class RackMeterTelemetryBus {
public:
    void publish(std::uint64_t sequence, float input_peak, float output_peak) noexcept
    {
        publish_stereo(sequence,
                       RackStereoPeaks{input_peak, input_peak},
                       RackStereoPeaks{output_peak, output_peak});
    }

    void publish_stereo(std::uint64_t sequence,
                        RackStereoPeaks input,
                        RackStereoPeaks output) noexcept
    {
        const float input_peak = std::max(input.left, input.right);
        const float output_peak = std::max(output.left, output.right);

        input_peak_.store(rack_meter_encode(input_peak), std::memory_order_relaxed);
        output_peak_.store(rack_meter_encode(output_peak), std::memory_order_relaxed);
        input_left_peak_.store(rack_meter_encode(input.left), std::memory_order_relaxed);
        input_right_peak_.store(rack_meter_encode(input.right), std::memory_order_relaxed);
        output_left_peak_.store(rack_meter_encode(output.left), std::memory_order_relaxed);
        output_right_peak_.store(rack_meter_encode(output.right), std::memory_order_relaxed);
        sequence_.store(sequence, std::memory_order_release);
    }

    void clear() noexcept
    {
        input_peak_.store(0, std::memory_order_relaxed);
        output_peak_.store(0, std::memory_order_relaxed);
        input_left_peak_.store(0, std::memory_order_relaxed);
        input_right_peak_.store(0, std::memory_order_relaxed);
        output_left_peak_.store(0, std::memory_order_relaxed);
        output_right_peak_.store(0, std::memory_order_relaxed);
        sequence_.store(0, std::memory_order_release);
    }

    RackMeterTelemetrySnapshot snapshot() const noexcept
    {
        RackMeterTelemetrySnapshot result{};
        result.sequence = sequence_.load(std::memory_order_acquire);
        result.input_peak_linear = rack_meter_decode(input_peak_.load(std::memory_order_relaxed));
        result.output_peak_linear = rack_meter_decode(output_peak_.load(std::memory_order_relaxed));
        result.input_left_peak_linear = rack_meter_decode(input_left_peak_.load(std::memory_order_relaxed));
        result.input_right_peak_linear = rack_meter_decode(input_right_peak_.load(std::memory_order_relaxed));
        result.output_left_peak_linear = rack_meter_decode(output_left_peak_.load(std::memory_order_relaxed));
        result.output_right_peak_linear = rack_meter_decode(output_right_peak_.load(std::memory_order_relaxed));
        result.valid = result.sequence != 0;
        return result;
    }

private:
    std::atomic<std::uint32_t> input_peak_{0};
    std::atomic<std::uint32_t> output_peak_{0};
    std::atomic<std::uint32_t> input_left_peak_{0};
    std::atomic<std::uint32_t> input_right_peak_{0};
    std::atomic<std::uint32_t> output_left_peak_{0};
    std::atomic<std::uint32_t> output_right_peak_{0};
    std::atomic<std::uint64_t> sequence_{0};
};

inline RackMeterTelemetryBus g_rack_meter_telemetry{};

inline float rack_meter_block_peak(float* const* audio,
                                   std::uint32_t channels,
                                   std::uint32_t frames) noexcept
{
    float peak = 0.0f;
    if (!audio)
        return peak;

    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const float* samples = audio[channel];
        if (!samples)
            continue;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            peak = std::max(peak, std::fabs(samples[frame]));
    }
    return peak;
}

inline RackStereoPeaks rack_meter_stereo_peaks(float* const* audio,
                                               std::uint32_t channels,
                                               std::uint32_t frames) noexcept
{
    RackStereoPeaks result{};
    if (!audio || channels == 0)
        return result;

    const float* left = audio[0];
    const float* right = channels > 1 ? audio[1] : left;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        if (left)
            result.left = std::max(result.left, std::fabs(left[frame]));
        if (right)
            result.right = std::max(result.right, std::fabs(right[frame]));
    }
    return result;
}

inline float rack_meter_linear_to_db(float linear) noexcept
{
    if (!(linear > 0.000001f))
        return -120.0f;
    return 20.0f * std::log10(linear);
}

} // namespace safevst3::rack::ui

#endif // _WIN32
