#include "common/parameter_refresh_transaction.hpp"

namespace safevst3 {

ParameterRefreshCoordinatorResult coordinate_parameter_refresh(
    ParameterRefreshCoordinatorTarget& target,
    bool values_changed,
    bool metadata_changed) noexcept
{
    if (!values_changed && !metadata_changed)
        return {true, ParameterRefreshCoordinatorStep::None};

    const auto reject = [&target](ParameterRefreshCoordinatorStep step) noexcept {
        target.request_recovery();
        return ParameterRefreshCoordinatorResult{false, step};
    };

    if (!target.pause_dsp())
        return reject(ParameterRefreshCoordinatorStep::PauseDsp);
    if (!target.reconcile_pending_edits())
        return reject(ParameterRefreshCoordinatorStep::ReconcilePendingEdits);

    const auto scope = metadata_changed ? ParameterRefreshScope::MetadataAndValues
                                        : ParameterRefreshScope::Values;
    if (!target.refresh_and_publish(scope))
        return reject(ParameterRefreshCoordinatorStep::RefreshAndPublish);
    if (!target.resume_dsp())
        return reject(ParameterRefreshCoordinatorStep::ResumeDsp);

    return {true, ParameterRefreshCoordinatorStep::None};
}

} // namespace safevst3
