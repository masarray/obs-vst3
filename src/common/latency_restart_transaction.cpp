#include "common/latency_restart_transaction.hpp"

namespace safevst3 {

namespace {

LatencyRestartResult failed(LatencyRestartStep step) noexcept
{
    return {false, 0, step};
}

} // namespace

LatencyRestartResult
run_latency_restart_transaction(LatencyRestartTarget& target) noexcept
{
    if (!target.set_processing(false))
        return failed(LatencyRestartStep::StopProcessing);
    if (!target.set_active(false))
        return failed(LatencyRestartStep::Deactivate);
    if (!target.set_active(true))
        return failed(LatencyRestartStep::Activate);

    const std::uint32_t candidate_latency = target.get_latency_samples();
    if (!target.set_processing(true))
        return failed(LatencyRestartStep::StartProcessing);

    return {true, candidate_latency, LatencyRestartStep::None};
}

LatencyRestartCoordinatorResult
coordinate_latency_restart(LatencyRestartCoordinatorTarget& target) noexcept
{
    const auto reject = [&target](LatencyRestartCoordinatorStep step) noexcept {
        target.request_recovery();
        return LatencyRestartCoordinatorResult{false, step};
    };

    if (!target.pause_dsp())
        return reject(LatencyRestartCoordinatorStep::PauseDsp);

    std::uint32_t latency_samples = 0;
    if (!target.refresh_latency(latency_samples))
        return reject(LatencyRestartCoordinatorStep::RefreshLifecycle);

    target.publish_latency(latency_samples);
    if (!target.resume_dsp())
        return reject(LatencyRestartCoordinatorStep::ResumeDsp);

    return {true, LatencyRestartCoordinatorStep::None};
}

} // namespace safevst3
