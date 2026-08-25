#include "common/lifecycle_restart_policy.hpp"

namespace safevst3 {

namespace {

constexpr std::uint32_t kSupportedRestartFlags =
    kRestartReloadComponent | kRestartIoChanged | kRestartParamValuesChanged |
    kRestartLatencyChanged | kRestartParamTitlesChanged;

} // namespace

bool RestartTransactionPlan::empty() const noexcept
{
    return !reload_component && !reconfigure_io && !refresh_parameter_values &&
           !refresh_latency && !refresh_parameter_metadata && unknown_flags == 0;
}

RestartTransactionPlan plan_restart_component(std::uint32_t flags) noexcept
{
    RestartTransactionPlan plan{};
    plan.reload_component = (flags & kRestartReloadComponent) != 0;
    plan.reconfigure_io = (flags & kRestartIoChanged) != 0;
    plan.refresh_parameter_values = (flags & kRestartParamValuesChanged) != 0;
    plan.refresh_latency = (flags & kRestartLatencyChanged) != 0;
    plan.refresh_parameter_metadata = (flags & kRestartParamTitlesChanged) != 0;
    plan.unknown_flags = flags & ~kSupportedRestartFlags;
    return plan;
}

} // namespace safevst3
