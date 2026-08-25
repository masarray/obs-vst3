#pragma once

#include <cstddef>

namespace safevst3 {

inline void duplicate_mono_to_stereo(const float* mono,
                                     float* left,
                                     float* right,
                                     std::size_t frames) noexcept
{
    for (std::size_t i = 0; i < frames; ++i) {
        const float sample = mono[i];
        left[i] = sample;
        right[i] = sample;
    }
}

inline void average_stereo_to_mono(const float* left,
                                   const float* right,
                                   float* mono,
                                   std::size_t frames) noexcept
{
    for (std::size_t i = 0; i < frames; ++i)
        mono[i] = (left[i] + right[i]) * 0.5f;
}

} // namespace safevst3
