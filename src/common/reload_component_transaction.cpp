#include "common/reload_component_transaction.hpp"

namespace safevst3 {

ReloadComponentResult coordinate_reload_component(ReloadComponentTarget& target) noexcept
{
    ReloadComponentResult result{};
    const auto fail = [&](ReloadComponentStep step) {
        target.reload_request_recovery();
        result.failed_step = step;
        return result;
    };

    if (!target.reload_pause_dsp())
        return fail(ReloadComponentStep::PauseDsp);
    if (!target.reload_reconcile_pending())
        return fail(ReloadComponentStep::ReconcilePending);
    if (!target.reload_capture_state())
        return fail(ReloadComponentStep::CaptureState);
    if (!target.reload_close_editor())
        return fail(ReloadComponentStep::CloseEditor);
    if (!target.reload_recreate_plugin())
        return fail(ReloadComponentStep::RecreatePlugin);
    if (!target.reload_restore_state())
        return fail(ReloadComponentStep::RestoreState);
    if (!target.reload_reconcile_restored_state())
        return fail(ReloadComponentStep::ReconcileRestoredState);
    if (!target.reload_regenerate_runtime())
        return fail(ReloadComponentStep::RegenerateRuntime);
    if (!target.reload_republish_runtime())
        return fail(ReloadComponentStep::RepublishRuntime);
    if (!target.reload_resume_dsp())
        return fail(ReloadComponentStep::ResumeDsp);

    target.reload_commit();
    result.completed = true;
    result.failed_step = ReloadComponentStep::None;
    return result;
}

} // namespace safevst3
