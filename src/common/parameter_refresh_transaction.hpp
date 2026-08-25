#pragma once

namespace safevst3 {

enum class ParameterRefreshScope {
    Values,
    MetadataAndValues,
};

enum class ParameterRefreshCoordinatorStep {
    None,
    PauseDsp,
    ReconcilePendingEdits,
    RefreshAndPublish,
    ResumeDsp,
};

class ParameterRefreshCoordinatorTarget {
public:
    virtual ~ParameterRefreshCoordinatorTarget() = default;

    virtual bool pause_dsp() noexcept = 0;
    virtual bool reconcile_pending_edits() noexcept = 0;
    virtual bool refresh_and_publish(ParameterRefreshScope scope) noexcept = 0;
    virtual bool resume_dsp() noexcept = 0;
    virtual void request_recovery() noexcept = 0;
};

struct ParameterRefreshCoordinatorResult {
    bool completed = false;
    ParameterRefreshCoordinatorStep failed_step = ParameterRefreshCoordinatorStep::None;
};

[[nodiscard]] ParameterRefreshCoordinatorResult coordinate_parameter_refresh(
    ParameterRefreshCoordinatorTarget& target,
    bool values_changed,
    bool metadata_changed) noexcept;

} // namespace safevst3
