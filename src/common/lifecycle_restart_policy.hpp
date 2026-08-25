#pragma once

#include <cstdint>

namespace safevst3 {

// VST3 RestartFlags are a stable wire-level bit mask. Keeping the planning
// seam independent of SDK types makes the lifecycle policy portable-testable;
// the Windows adapter asserts these values against the pinned SDK.
inline constexpr std::uint32_t kRestartReloadComponent = 1u << 0;
inline constexpr std::uint32_t kRestartIoChanged = 1u << 1;
inline constexpr std::uint32_t kRestartParamValuesChanged = 1u << 2;
inline constexpr std::uint32_t kRestartLatencyChanged = 1u << 3;
inline constexpr std::uint32_t kRestartParamTitlesChanged = 1u << 4;

struct RestartTransactionPlan {
    bool reload_component = false;
    bool reconfigure_io = false;
    bool refresh_parameter_values = false;
    bool refresh_latency = false;
    bool refresh_parameter_metadata = false;
    std::uint32_t unknown_flags = 0;

    [[nodiscard]] bool empty() const noexcept;
};

[[nodiscard]] RestartTransactionPlan plan_restart_component(std::uint32_t flags) noexcept;
[[nodiscard]] bool requires_standalone_latency_restart(
    const RestartTransactionPlan& plan) noexcept;
[[nodiscard]] bool should_run_incremental_restart_actions(
    const RestartTransactionPlan& plan) noexcept;
[[nodiscard]] bool can_absorb_restart_before_full_regeneration(
    const RestartTransactionPlan& plan) noexcept;
[[nodiscard]] bool reload_regeneration_reached_fixed_point(
    const RestartTransactionPlan& plan) noexcept;

} // namespace safevst3
