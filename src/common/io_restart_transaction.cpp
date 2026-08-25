#include "common/io_restart_transaction.hpp"

namespace safevst3 {

bool is_supported_io_layout(const IoLayout& layout) noexcept
{
    const auto supported_channels = [](std::uint32_t channels) {
        return channels == 1u || channels == 2u;
    };
    return layout.main_input_bus >= 0 && layout.main_output_bus >= 0 &&
           supported_channels(layout.input_channels) &&
           supported_channels(layout.output_channels);
}

bool has_unambiguous_main_io(std::uint32_t main_inputs,
                             std::uint32_t main_outputs) noexcept
{
    return main_inputs == 1u && main_outputs == 1u;
}

IoRestartLifecycleResult run_io_restart_lifecycle(IoRestartLifecycleTarget& target) noexcept
{
    IoRestartLifecycleResult result{};
    const auto fail = [&](IoRestartLifecycleStep step) {
        result.failed_step = step;
        return result;
    };

    if (!target.io_stop_processing())
        return fail(IoRestartLifecycleStep::StopProcessing);
    if (!target.io_deactivate())
        return fail(IoRestartLifecycleStep::Deactivate);

    IoLayout requested{};
    if (!target.io_inspect_requested_layout(requested) || !is_supported_io_layout(requested))
        return fail(IoRestartLifecycleStep::InspectRequested);

    const auto arrangement = target.io_confirm_requested_layout(requested);
    if (arrangement == IoArrangementResult::FatalFailure)
        return fail(IoRestartLifecycleStep::ConfirmRequested);
    result.arrangement_was_advisory_rejected =
        arrangement == IoArrangementResult::AdvisoryRejected;

    IoLayout confirmed{};
    if (!target.io_inspect_confirmed_layout(confirmed) || !is_supported_io_layout(confirmed))
        return fail(IoRestartLifecycleStep::InspectConfirmed);
    if (!target.io_rebuild_processing(confirmed))
        return fail(IoRestartLifecycleStep::RebuildProcessing);
    if (!target.io_activate())
        return fail(IoRestartLifecycleStep::Activate);

    std::uint32_t latency_samples = 0;
    if (!target.io_query_latency(latency_samples))
        return fail(IoRestartLifecycleStep::QueryLatency);
    if (!target.io_start_processing())
        return fail(IoRestartLifecycleStep::StartProcessing);

    target.io_commit_layout(confirmed, latency_samples);
    result.committed = true;
    result.failed_step = IoRestartLifecycleStep::None;
    result.layout = confirmed;
    result.latency_samples = latency_samples;
    return result;
}

IoRestartCoordinatorResult coordinate_io_restart(IoRestartCoordinatorTarget& target) noexcept
{
    IoRestartCoordinatorResult result{};
    const auto fail = [&](IoRestartCoordinatorStep step) {
        target.request_recovery();
        result.failed_step = step;
        return result;
    };

    if (!target.pause_dsp())
        return fail(IoRestartCoordinatorStep::PauseDsp);
    if (!target.reconcile_pending())
        return fail(IoRestartCoordinatorStep::Reconcile);

    IoLayout layout{};
    std::uint32_t latency_samples = 0;
    if (!target.reconfigure_io(layout, latency_samples))
        return fail(IoRestartCoordinatorStep::Reconfigure);
    if (!target.publish_io(layout, latency_samples))
        return fail(IoRestartCoordinatorStep::Publish);
    if (!target.resume_dsp())
        return fail(IoRestartCoordinatorStep::ResumeDsp);

    result.completed = true;
    result.failed_step = IoRestartCoordinatorStep::None;
    result.layout = layout;
    result.latency_samples = latency_samples;
    return result;
}

} // namespace safevst3
