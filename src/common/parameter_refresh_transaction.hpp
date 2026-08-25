#pragma once

namespace safevst3 {

struct ParameterRefreshRequest {
    bool refresh_values = false;
    bool refresh_metadata = false;

    [[nodiscard]] bool empty() const noexcept
    {
        return !refresh_values && !refresh_metadata;
    }
};

enum class ParameterRefreshCoordinatorStep {
    None,
    PauseDsp,
    Reconcile,
    RefreshValues,
    RefreshMetadata,
    PublishCatalog,
    ResumeDsp,
};

struct ParameterRefreshCoordinatorResult {
    bool completed = false;
    ParameterRefreshCoordinatorStep failed_step = ParameterRefreshCoordinatorStep::None;
};

class ParameterRefreshCoordinatorTarget {
public:
    virtual ~ParameterRefreshCoordinatorTarget() = default;
    virtual bool pause_dsp() noexcept = 0;
    virtual bool reconcile_pending() noexcept = 0;
    virtual bool refresh_parameter_values() noexcept = 0;
    virtual bool refresh_parameter_metadata() noexcept = 0;
    virtual bool publish_parameter_catalog() noexcept = 0;
    virtual bool resume_dsp() noexcept = 0;
    virtual void request_recovery() noexcept = 0;
};

[[nodiscard]] ParameterRefreshCoordinatorResult coordinate_parameter_refresh(
    ParameterRefreshCoordinatorTarget& target,
    ParameterRefreshRequest request) noexcept;

} // namespace safevst3
