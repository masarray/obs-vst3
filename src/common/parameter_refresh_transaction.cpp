#include "common/parameter_refresh_transaction.hpp"

namespace safevst3 {

ParameterRefreshCoordinatorResult coordinate_parameter_refresh(
    ParameterRefreshCoordinatorTarget& target,
    ParameterRefreshRequest request) noexcept
{
    if (request.empty())
        return {true, ParameterRefreshCoordinatorStep::None};

    const auto fail = [&](ParameterRefreshCoordinatorStep step) {
        target.request_recovery();
        return ParameterRefreshCoordinatorResult{false, step};
    };

    if (!target.pause_dsp())
        return fail(ParameterRefreshCoordinatorStep::PauseDsp);
    if (!target.reconcile_pending())
        return fail(ParameterRefreshCoordinatorStep::Reconcile);

    // kParamTitlesChanged invalidates the complete ParameterInfo surface and
    // current values are re-read as part of re-enumeration. When both VST3
    // flags arrive together, metadata refresh subsumes values refresh so the
    // helper executes only one pause/reconcile/publish/resume transaction.
    if (request.refresh_metadata) {
        if (!target.refresh_parameter_metadata())
            return fail(ParameterRefreshCoordinatorStep::RefreshMetadata);
    } else if (request.refresh_values) {
        if (!target.refresh_parameter_values())
            return fail(ParameterRefreshCoordinatorStep::RefreshValues);
    }

    if (!target.publish_parameter_catalog())
        return fail(ParameterRefreshCoordinatorStep::PublishCatalog);
    if (!target.resume_dsp())
        return fail(ParameterRefreshCoordinatorStep::ResumeDsp);

    return {true, ParameterRefreshCoordinatorStep::None};
}

} // namespace safevst3
