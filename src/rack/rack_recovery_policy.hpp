#pragma once

#include "common/recovery_policy.hpp"
#include "rack/rack_protocol.hpp"

#include <atomic>
#include <cstdint>

namespace safevst3::rack {

enum class RackFailureConfidence : std::uint8_t {
    Unknown = 0,
    Suspect = 1,
};

struct RackBreadcrumbSnapshot {
    bool coherent = false;
    std::uint64_t chain_generation = 0;
    std::uint64_t audio_sequence = 0;
    RackSlotId slot_id = 0;
    RackBreadcrumbPhase phase = RackBreadcrumbPhase::None;
    std::uint64_t dsp_progress = 0;
};

struct RackFailureAttribution {
    RackFailureConfidence confidence = RackFailureConfidence::Unknown;
    RackSlotId slot_id = 0;
    RackBreadcrumbPhase phase = RackBreadcrumbPhase::None;
};

inline bool read_rack_breadcrumb(const RackSharedAudioRegion& region,
                                 RackBreadcrumbSnapshot& snapshot) noexcept
{
    constexpr std::uint32_t kMaxSnapshotAttempts = 8;
    for (std::uint32_t attempt = 0; attempt < kMaxSnapshotAttempts; ++attempt) {
        const std::int64_t begin = region.breadcrumb_epoch;
        if ((begin & 1) != 0)
            continue;
        std::atomic_thread_fence(std::memory_order_acquire);

        RackBreadcrumbSnapshot candidate{};
        candidate.chain_generation = static_cast<std::uint64_t>(region.breadcrumb_chain_generation);
        candidate.audio_sequence = static_cast<std::uint64_t>(region.breadcrumb_audio_sequence);
        candidate.slot_id = region.breadcrumb_slot_id;
        candidate.phase = static_cast<RackBreadcrumbPhase>(region.breadcrumb_phase);
        candidate.dsp_progress = static_cast<std::uint64_t>(region.breadcrumb_dsp_progress);

        std::atomic_thread_fence(std::memory_order_acquire);
        const std::int64_t end = region.breadcrumb_epoch;
        if (begin == end && (end & 1) == 0) {
            candidate.coherent = true;
            snapshot = candidate;
            return true;
        }
    }

    snapshot = RackBreadcrumbSnapshot{};
    return false;
}

inline RackFailureAttribution classify_rack_helper_death(
    const RackBreadcrumbSnapshot& breadcrumb,
    bool supervisor_terminated) noexcept
{
    if (supervisor_terminated || !breadcrumb.coherent ||
        breadcrumb.phase == RackBreadcrumbPhase::None || breadcrumb.slot_id == 0)
        return {};

    return {
        RackFailureConfidence::Suspect,
        breadcrumb.slot_id,
        breadcrumb.phase,
    };
}

class RackRecoveryPolicy {
public:
    RecoveryDecision observe(std::uint64_t now_ms,
                             const RecoveryObservation& observation) noexcept
    {
        return policy_.observe(now_ms, observation);
    }

    void record_restart_attempt(std::uint64_t now_ms) noexcept
    {
        policy_.record_restart_attempt(now_ms);
    }

    void reset() noexcept { policy_.reset(); }

    std::uint32_t recovery_attempts() const noexcept { return policy_.recovery_attempts(); }
    std::uint64_t next_retry_ms() const noexcept { return policy_.next_retry_ms(); }

private:
    RecoveryPolicy policy_;
};

} // namespace safevst3::rack
