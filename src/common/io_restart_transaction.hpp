#pragma once

#include <cstdint>

namespace safevst3 {

struct IoLayout {
    std::int32_t main_input_bus = -1;
    std::int32_t main_output_bus = -1;
    std::uint32_t input_channels = 0;
    std::uint32_t output_channels = 0;
};

[[nodiscard]] bool is_supported_io_layout(const IoLayout& layout) noexcept;
[[nodiscard]] bool has_unambiguous_main_io(std::uint32_t main_inputs,
                                            std::uint32_t main_outputs) noexcept;

enum class IoArrangementResult {
    Accepted,
    AdvisoryRejected,
    FatalFailure,
};

enum class IoRestartLifecycleStep {
    None,
    StopProcessing,
    Deactivate,
    InspectRequested,
    ConfirmRequested,
    InspectConfirmed,
    RebuildProcessing,
    Activate,
    QueryLatency,
    StartProcessing,
    Commit,
};

struct IoRestartLifecycleResult {
    bool committed = false;
    bool arrangement_was_advisory_rejected = false;
    IoRestartLifecycleStep failed_step = IoRestartLifecycleStep::None;
    IoLayout layout{};
    std::uint32_t latency_samples = 0;
};

class IoRestartLifecycleTarget {
public:
    virtual ~IoRestartLifecycleTarget() = default;
    virtual bool io_stop_processing() noexcept = 0;
    virtual bool io_deactivate() noexcept = 0;
    virtual bool io_inspect_requested_layout(IoLayout& layout) noexcept = 0;
    virtual IoArrangementResult io_confirm_requested_layout(const IoLayout& layout) noexcept = 0;
    virtual bool io_inspect_confirmed_layout(IoLayout& layout) noexcept = 0;
    virtual bool io_rebuild_processing(const IoLayout& layout) noexcept = 0;
    virtual bool io_activate() noexcept = 0;
    virtual bool io_query_latency(std::uint32_t& latency_samples) noexcept = 0;
    virtual bool io_start_processing() noexcept = 0;
    virtual void io_commit_layout(const IoLayout& layout, std::uint32_t latency_samples) noexcept = 0;
};

[[nodiscard]] IoRestartLifecycleResult run_io_restart_lifecycle(
    IoRestartLifecycleTarget& target) noexcept;

enum class IoRestartCoordinatorStep {
    None,
    PauseDsp,
    Reconcile,
    Reconfigure,
    Publish,
    ResumeDsp,
};

struct IoRestartCoordinatorResult {
    bool completed = false;
    IoRestartCoordinatorStep failed_step = IoRestartCoordinatorStep::None;
    IoLayout layout{};
    std::uint32_t latency_samples = 0;
};

class IoRestartCoordinatorTarget {
public:
    virtual ~IoRestartCoordinatorTarget() = default;
    virtual bool pause_dsp() noexcept = 0;
    virtual bool reconcile_pending() noexcept = 0;
    virtual bool reconfigure_io(IoLayout& layout, std::uint32_t& latency_samples) noexcept = 0;
    virtual bool publish_io(const IoLayout& layout, std::uint32_t latency_samples) noexcept = 0;
    virtual bool resume_dsp() noexcept = 0;
    virtual void request_recovery() noexcept = 0;
};

[[nodiscard]] IoRestartCoordinatorResult coordinate_io_restart(
    IoRestartCoordinatorTarget& target) noexcept;

} // namespace safevst3
