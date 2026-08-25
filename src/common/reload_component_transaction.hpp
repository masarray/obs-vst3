#pragma once

namespace safevst3 {

enum class ReloadComponentStep {
    None,
    PauseDsp,
    ReconcilePending,
    CaptureState,
    CloseEditor,
    RecreatePlugin,
    RestoreState,
    ReconcileRestoredState,
    RegenerateRuntime,
    RepublishRuntime,
    ResumeDsp,
    Commit,
};

struct ReloadComponentResult {
    bool completed = false;
    ReloadComponentStep failed_step = ReloadComponentStep::None;
};

class ReloadComponentTarget {
public:
    virtual ~ReloadComponentTarget() = default;

    virtual bool reload_pause_dsp() noexcept = 0;
    virtual bool reload_reconcile_pending() noexcept = 0;
    virtual bool reload_capture_state() noexcept = 0;
    virtual bool reload_close_editor() noexcept = 0;
    virtual bool reload_recreate_plugin() noexcept = 0;
    virtual bool reload_restore_state() noexcept = 0;
    virtual bool reload_reconcile_restored_state() noexcept = 0;
    virtual bool reload_regenerate_runtime() noexcept = 0;
    virtual bool reload_republish_runtime() noexcept = 0;
    virtual bool reload_resume_dsp() noexcept = 0;
    virtual void reload_commit() noexcept = 0;
    virtual void reload_request_recovery() noexcept = 0;
};

[[nodiscard]] ReloadComponentResult coordinate_reload_component(
    ReloadComponentTarget& target) noexcept;

} // namespace safevst3
