#pragma once

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace safevst3::rack::ui {

inline constexpr float kRackMasterMinDb = -60.0f;
inline constexpr float kRackMasterMaxDb = 12.0f;
inline constexpr std::int32_t kRackMasterDbScale = 100;

inline std::int32_t rack_master_encode_db(float db) noexcept
{
    const float bounded = std::clamp(db, kRackMasterMinDb, kRackMasterMaxDb);
    return static_cast<std::int32_t>(std::lround(bounded * static_cast<float>(kRackMasterDbScale)));
}

inline float rack_master_decode_db(std::int32_t encoded) noexcept
{
    return static_cast<float>(encoded) / static_cast<float>(kRackMasterDbScale);
}

inline float rack_master_db_to_linear(float db) noexcept
{
    if (db <= kRackMasterMinDb)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

struct RackMasterControlSnapshot {
    float input_db = 0.0f;
    float output_db = 0.0f;
};

class RackMasterControlBus {
public:
    void set_input_db(float db) noexcept
    {
        input_db_.store(rack_master_encode_db(db), std::memory_order_release);
    }

    void set_output_db(float db) noexcept
    {
        output_db_.store(rack_master_encode_db(db), std::memory_order_release);
    }

    RackMasterControlSnapshot snapshot() const noexcept
    {
        RackMasterControlSnapshot result{};
        result.input_db = rack_master_decode_db(input_db_.load(std::memory_order_acquire));
        result.output_db = rack_master_decode_db(output_db_.load(std::memory_order_acquire));
        return result;
    }

private:
    std::atomic<std::int32_t> input_db_{0};
    std::atomic<std::int32_t> output_db_{0};
};

inline RackMasterControlBus g_rack_master_controls{};

struct RackMasterGainSmoother {
    float input_linear = 1.0f;
    float output_linear = 1.0f;
};

inline void rack_apply_smoothed_gain(float* const* source,
                                     float* const* destination,
                                     std::uint32_t channels,
                                     std::uint32_t frames,
                                     float target_db,
                                     float& current_linear) noexcept
{
    if (!source || !destination || channels == 0 || frames == 0)
        return;

    const float target_linear = rack_master_db_to_linear(target_db);
    const float start_linear = current_linear;
    const float step = (target_linear - start_linear) / static_cast<float>(frames);

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const float gain = start_linear + step * static_cast<float>(frame + 1);
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const float* src = source[channel];
            float* dst = destination[channel];
            if (src && dst)
                dst[frame] = src[frame] * gain;
        }
    }

    current_linear = target_linear;
}

} // namespace safevst3::rack::ui

#endif // _WIN32
