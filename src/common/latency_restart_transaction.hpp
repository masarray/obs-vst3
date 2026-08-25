#pragma once

#include <cstdint>

namespace safevst3 {

enum class LatencyRestartStep {
    None,
    StopProcessing,
    Deactivate,
    Activate,
    QueryLatency,
    StartProcessing,
};

class LatencyRestartTarget {
public:
    virtual ~LatencyRestartTarget() = default;

    virtual bool set_processing(bool enabled) noexcept = 0;
    virtual bool set_active(bool enabled) noexcept = 0;
    virtual std::uint32_t get_latency_samples() noexcept = 0;
};

struct LatencyRestartResult {
    bool committed = false;
    std::uint32_t latency_samples = 0;
    LatencyRestartStep failed_step = LatencyRestartStep::None;
};

[[nodiscard]] LatencyRestartResult
run_latency_restart_transaction(LatencyRestartTarget& target) noexcept;

enum class LatencyRestartCoordinatorStep {
    None,
    PauseDsp,
    RefreshLifecycle,
    ResumeDsp,
};

class LatencyRestartCoordinatorTarget {
public:
    virtual ~LatencyRestartCoordinatorTarget() = default;

    virtual bool pause_dsp() noexcept = 0;
    virtual bool refresh_latency(std::uint32_t& latency_samples) noexcept = 0;
    virtual void publish_latency(std::uint32_t latency_samples) noexcept = 0;
    virtual bool resume_dsp() noexcept = 0;
    virtual void request_recovery() noexcept = 0;
};

struct LatencyRestartCoordinatorResult {
    bool completed = false;
    LatencyRestartCoordinatorStep failed_step = LatencyRestartCoordinatorStep::None;
};

[[nodiscard]] LatencyRestartCoordinatorResult
coordinate_latency_restart(LatencyRestartCoordinatorTarget& target) noexcept;

} // namespace safevst3
