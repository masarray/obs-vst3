#pragma once

#include <cstdint>

namespace safevst3 {

struct IoLayout {
    std::uint32_t input_channels = 0;
    std::uint32_t output_channels = 0;

    [[nodiscard]] bool supported() const noexcept
    {
        return (input_channels == 1 || input_channels == 2) &&
               (output_channels == 1 || output_channels == 2);
    }
};

enum class IoRestartStep {
    None,
    StopProcessing,
    Deactivate,
    InspectRequested,
    ConfirmArrangements,
    InspectConfirmed,
    RebuildProcessData,
    Activate,
    QueryLatency,
    StartProcessing,
    Commit,
};

struct IoRestartResult {
    bool committed = false;
    IoRestartStep failed_step = IoRestartStep::None;
    IoLayout layout{};
    std::uint32_t latency_samples = 0;
};

class IoRestartTarget {
public:
    virtual ~IoRestartTarget() = default;
    virtual bool set_processing(bool enabled) noexcept = 0;
    virtual bool set_active(bool enabled) noexcept = 0;
    virtual bool inspect_requested_io(IoLayout& layout) noexcept = 0;
    // false is advisory only when inspect_confirmed_io still proves a supported layout.
    virtual bool confirm_requested_io(const IoLayout& requested) noexcept = 0;
    virtual bool inspect_confirmed_io(IoLayout& layout) noexcept = 0;
    virtual bool rebuild_process_data(const IoLayout& layout) noexcept = 0;
    virtual std::uint32_t query_latency() noexcept = 0;
    virtual bool commit_io(const IoLayout& layout, std::uint32_t latency_samples) noexcept = 0;
    virtual void request_recovery() noexcept = 0;
};

[[nodiscard]] IoRestartResult run_io_restart_transaction(IoRestartTarget& target) noexcept;

} // namespace safevst3
