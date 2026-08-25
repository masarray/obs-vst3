#include "common/io_restart_transaction.hpp"

namespace safevst3 {

IoRestartResult run_io_restart_transaction(IoRestartTarget& target) noexcept
{
    IoRestartResult result{};
    const auto fail = [&](IoRestartStep step) {
        target.request_recovery();
        result.failed_step = step;
        return result;
    };

    if (!target.set_processing(false))
        return fail(IoRestartStep::StopProcessing);
    if (!target.set_active(false))
        return fail(IoRestartStep::Deactivate);

    IoLayout requested{};
    if (!target.inspect_requested_io(requested) || !requested.supported())
        return fail(IoRestartStep::InspectRequested);

    const bool confirmation_result = target.confirm_requested_io(requested);

    IoLayout confirmed{};
    if (!target.inspect_confirmed_io(confirmed) || !confirmed.supported())
        return fail(IoRestartStep::InspectConfirmed);
    if (!confirmation_result &&
        (confirmed.input_channels != requested.input_channels ||
         confirmed.output_channels != requested.output_channels))
        return fail(IoRestartStep::ConfirmArrangements);

    if (!target.rebuild_process_data(confirmed))
        return fail(IoRestartStep::RebuildProcessData);
    if (!target.set_active(true))
        return fail(IoRestartStep::Activate);

    const std::uint32_t latency = target.query_latency();
    if (!target.set_processing(true))
        return fail(IoRestartStep::StartProcessing);
    if (!target.commit_io(confirmed, latency))
        return fail(IoRestartStep::Commit);

    result.committed = true;
    result.layout = confirmed;
    result.latency_samples = latency;
    return result;
}

} // namespace safevst3
