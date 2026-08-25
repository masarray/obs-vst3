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

    const auto latency = plan_restart_component(kRestartLatencyChanged);
    require(latency.refresh_latency, "kLatencyChanged must refresh latency");
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

    constexpr std::uint32_t unknown = 1u << 30;
    constexpr std::uint32_t combined_flags =
        kRestartReloadComponent | kRestartIoChanged | kRestartParamValuesChanged |
        kRestartLatencyChanged | kRestartParamTitlesChanged | unknown;
    const auto combined = plan_restart_component(combined_flags);
    require(combined.reload_component && combined.reconfigure_io &&
                combined.refresh_parameter_values && combined.refresh_latency &&
                combined.refresh_parameter_metadata,
            "combined flags must retain every requested action");
    require(combined.unknown_flags == unknown,
            "combined flags must preserve the exact unknown-bit mask");

    const auto mixed = plan_restart_component(kRestartIoChanged | unknown);
    require(mixed.reconfigure_io, "known work must survive alongside unknown bits");
    require(mixed.unknown_flags == unknown, "unknown bits must be reported exactly");
    require(!mixed.empty(), "a plan with unknown bits is not a no-op");

    std::cout << "restartComponent lifecycle matrix ok\n";
    return 0;
}
