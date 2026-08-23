#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace safevst3 {

enum class RecoveryHealth : std::uint8_t {
    Healthy = 0,
    DeadlinePressure = 1,
    Hung = 2,
    Exited = 3,
    Backoff = 4,
};

struct RecoveryObservation {
    bool process_alive = false;
    std::uint64_t heartbeat_age_ms = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t deadline_miss_delta = 0;
};

struct RecoveryDecision {
    RecoveryHealth health = RecoveryHealth::Healthy;
    bool restart = false;
    std::uint64_t retry_after_ms = 0;
};

class RecoveryPolicy {
public:
    static constexpr std::uint64_t kHeartbeatTimeoutMs = 2500;
    static constexpr std::uint64_t kStableResetMs = 10000;
    static constexpr std::uint64_t kBaseBackoffMs = 1000;
    static constexpr std::uint64_t kMaxBackoffMs = 30000;
    static constexpr std::uint64_t kDeadlinePressureMissesPerObservation = 8;

    RecoveryDecision observe(std::uint64_t now_ms, const RecoveryObservation& observation) noexcept
    {
        const bool responsive = observation.process_alive &&
                                observation.heartbeat_age_ms <= kHeartbeatTimeoutMs;
        if (responsive) {
            if (healthy_since_ms_ == 0)
                healthy_since_ms_ = now_ms;
            if (recovery_attempts_ != 0 && now_ms >= healthy_since_ms_ &&
                now_ms - healthy_since_ms_ >= kStableResetMs) {
                recovery_attempts_ = 0;
                next_retry_ms_ = 0;
            }

            return {
                observation.deadline_miss_delta >= kDeadlinePressureMissesPerObservation
                    ? RecoveryHealth::DeadlinePressure
                    : RecoveryHealth::Healthy,
                false,
                0,
            };
        }

        healthy_since_ms_ = 0;
        const RecoveryHealth unhealthy = observation.process_alive
                                             ? RecoveryHealth::Hung
                                             : RecoveryHealth::Exited;
        if (now_ms < next_retry_ms_)
            return {RecoveryHealth::Backoff, false, next_retry_ms_ - now_ms};
        return {unhealthy, true, 0};
    }

    void record_restart_attempt(std::uint64_t now_ms) noexcept
    {
        recovery_attempts_ = std::min<std::uint32_t>(recovery_attempts_ + 1u, 31u);
        const std::uint32_t shift = std::min<std::uint32_t>(recovery_attempts_ - 1u, 5u);
        const std::uint64_t delay = std::min<std::uint64_t>(kBaseBackoffMs << shift, kMaxBackoffMs);
        next_retry_ms_ = now_ms + delay;
        healthy_since_ms_ = 0;
    }

    void reset() noexcept
    {
        recovery_attempts_ = 0;
        next_retry_ms_ = 0;
        healthy_since_ms_ = 0;
    }

    std::uint32_t recovery_attempts() const noexcept { return recovery_attempts_; }
    std::uint64_t next_retry_ms() const noexcept { return next_retry_ms_; }

private:
    std::uint32_t recovery_attempts_ = 0;
    std::uint64_t next_retry_ms_ = 0;
    std::uint64_t healthy_since_ms_ = 0;
};

} // namespace safevst3
