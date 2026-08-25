#include "common/lifecycle_restart_policy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "lifecycle restart policy failure: " << message << '\n';
    std::exit(1);
}

} // namespace

int main()
{
    using namespace safevst3;

    const auto none = plan_restart_component(0);
    require(none.empty(), "zero flags must be a no-op");
    require(none.unknown_flags == 0, "zero flags must not report unknown bits");
    require(should_run_incremental_restart_actions(none),
            "empty plan leaves incremental path available as a no-op");

    const auto latency = plan_restart_component(kRestartLatencyChanged);
    require(latency.refresh_latency, "kLatencyChanged must refresh latency");
    require(requires_standalone_latency_restart(latency),
            "latency-only request requires its own restart transaction");
    require(should_run_incremental_restart_actions(latency),
            "non-reload request must use incremental lifecycle actions");
    require(!latency.reload_component && !latency.reconfigure_io &&
                !latency.refresh_parameter_values && !latency.refresh_parameter_metadata,
            "kLatencyChanged must not imply unrelated work");

    const auto values = plan_restart_component(kRestartParamValuesChanged);
    require(values.refresh_parameter_values,
            "kParamValuesChanged must invalidate and refresh parameter values");

    const auto titles = plan_restart_component(kRestartParamTitlesChanged);
    require(titles.refresh_parameter_metadata,
            "kParamTitlesChanged must invalidate and refresh ParameterInfo metadata");

    const auto io = plan_restart_component(kRestartIoChanged);
    require(io.reconfigure_io, "kIoChanged must request an I/O transaction");
    require(!io.reload_component, "kIoChanged must remain distinct from reload");

    const auto reload = plan_restart_component(kRestartReloadComponent);
    require(reload.reload_component, "kReloadComponent must request explicit reload policy");
    require(!reload.reconfigure_io, "reload must remain distinct from kIoChanged");
    require(!should_run_incremental_restart_actions(reload),
            "full reload must suppress redundant incremental actions");

    const auto reload_latency = plan_restart_component(
        kRestartReloadComponent | kRestartLatencyChanged);
    require(reload_latency.reload_component && reload_latency.refresh_latency,
            "reload+latency must retain both flags for diagnostics");
    require(!requires_standalone_latency_restart(reload_latency),
            "full reload itself must satisfy a combined latency refresh");
    require(!should_run_incremental_restart_actions(reload_latency),
            "reload+latency must execute only the full recreation transaction");

    constexpr std::uint32_t unknown = 1u << 30;
    constexpr std::uint32_t combined_flags =
        kRestartReloadComponent | kRestartIoChanged | kRestartParamValuesChanged |
        kRestartLatencyChanged | kRestartParamTitlesChanged | unknown;
    const auto combined = plan_restart_component(combined_flags);
    require(combined.reload_component && combined.reconfigure_io &&
                combined.refresh_parameter_values && combined.refresh_latency &&
                combined.refresh_parameter_metadata,
            "combined flags must retain every requested action for diagnostics");
    require(combined.unknown_flags == unknown,
            "combined flags must preserve the exact unknown-bit mask");
    require(!requires_standalone_latency_restart(combined),
            "full reload/I/O transaction must satisfy a combined latency refresh");
    require(!should_run_incremental_restart_actions(combined),
            "full reload must coalesce known I/O/parameter/latency actions into one recreation");

    const auto mixed = plan_restart_component(kRestartIoChanged | unknown);
    require(mixed.reconfigure_io, "known work must survive alongside unknown bits");
    require(mixed.unknown_flags == unknown, "unknown bits must be reported exactly");
    require(!mixed.empty(), "a plan with unknown bits is not a no-op");
    require(should_run_incremental_restart_actions(mixed),
            "unknown bits alone must not suppress supported incremental work");

    const auto absorbable = plan_restart_component(
        kRestartIoChanged | kRestartParamValuesChanged |
        kRestartLatencyChanged | kRestartParamTitlesChanged);
    require(can_absorb_restart_before_full_regeneration(none),
            "no restart request is safe before broad regeneration");
    require(can_absorb_restart_before_full_regeneration(absorbable),
            "known incremental requests must be absorbable by one broad regeneration");
    require(!can_absorb_restart_before_full_regeneration(reload),
            "recursive reload must never be absorbed inside full reload");
    require(!can_absorb_restart_before_full_regeneration(mixed),
            "unknown restart bits must reject the reload frontier");

    require(reload_regeneration_reached_fixed_point(none),
            "empty final restart plan is the only successful reload fixed point");
    require(!reload_regeneration_reached_fixed_point(latency),
            "known restart emitted after final regeneration must force recovery");
    require(!reload_regeneration_reached_fixed_point(reload),
            "recursive reload after final regeneration must force recovery");
    require(!reload_regeneration_reached_fixed_point(mixed),
            "unknown restart after final regeneration must force recovery");

    std::cout << "restartComponent lifecycle matrix ok\n";
    return 0;
}
