#pragma once

#include <cstdint>

namespace safevst3 {

// Mirror VST3 IProcessContextRequirements flags so the policy remains portable
// and can be tested without loading the Steinberg SDK. The Windows adapter
// statically verifies every value against the pinned SDK.
inline constexpr std::uint32_t kProcessNeedSystemTime = 1u << 0;
inline constexpr std::uint32_t kProcessNeedContinuousTimeSamples = 1u << 1;
inline constexpr std::uint32_t kProcessNeedProjectTimeMusic = 1u << 2;
inline constexpr std::uint32_t kProcessNeedBarPositionMusic = 1u << 3;
inline constexpr std::uint32_t kProcessNeedCycleMusic = 1u << 4;
inline constexpr std::uint32_t kProcessNeedSamplesToNextClock = 1u << 5;
inline constexpr std::uint32_t kProcessNeedTempo = 1u << 6;
inline constexpr std::uint32_t kProcessNeedTimeSignature = 1u << 7;
inline constexpr std::uint32_t kProcessNeedChord = 1u << 8;
inline constexpr std::uint32_t kProcessNeedFrameRate = 1u << 9;
inline constexpr std::uint32_t kProcessNeedTransportState = 1u << 10;

// VST3 ProcessContext::kContTimeValid. No other optional validity or transport
// flags are emitted by the S1 OBS host policy because OBS does not provide a
// truthful DAW musical/transport timeline for an audio filter.
inline constexpr std::uint32_t kProcessContextContinuousTimeValid = 1u << 17;

struct ProcessContextPolicy {
    std::uint32_t requested_requirements = 0;
    std::uint32_t unsupported_requirements = 0;
    bool provide_continuous_time = false;
};

struct ProcessContextFrame {
    double sample_rate = 0.0;
    std::int64_t project_time_samples = 0;
    std::int64_t continuous_time_samples = 0;
    std::uint32_t state = 0;
};

[[nodiscard]] ProcessContextPolicy plan_process_context(
    std::uint32_t requested_requirements) noexcept;

[[nodiscard]] ProcessContextFrame make_process_context_frame(
    double sample_rate,
    std::int64_t sample_position,
    const ProcessContextPolicy& policy) noexcept;

} // namespace safevst3
