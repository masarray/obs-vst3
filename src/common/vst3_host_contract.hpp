#pragma once

#include <cstddef>
#include <cstdint>

namespace safevst3 {

inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxFrames = 2048;
inline constexpr std::uint32_t kMaxParameters = 256;
inline constexpr std::size_t kMaxStateBytes = 16u * 1024u * 1024u;

enum ParameterFlags : std::uint32_t {
    ParameterCanAutomate = 1u << 0,
    ParameterReadOnly = 1u << 1,
    ParameterHidden = 1u << 2,
    ParameterList = 1u << 3,
    ParameterProgramChange = 1u << 4,
    ParameterBypass = 1u << 5,
};

} // namespace safevst3
