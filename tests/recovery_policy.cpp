#include "common/recovery_policy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "recovery-policy-test failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using namespace safevst3;

    RecoveryPolicy policy;

    auto decision = policy.observe(1000, {true, 100, 0});
    require(decision.health == RecoveryHealth::Healthy && !decision.restart,
            "responsive helper must stay healthy");

    decision = policy.observe(2000, {true, 100, 8});
    require(decision.health == RecoveryHealth::DeadlinePressure && !decision.restart,
            "deadline pressure must not trigger a restart by itself");

    decision = policy.observe(3000, {true, RecoveryPolicy::kHeartbeatTimeoutMs + 1, 0});
    require(decision.health == RecoveryHealth::Hung && decision.restart,
            "live process with stale heartbeat must be treated as hung");
    policy.record_restart_attempt(3000);
    require(policy.recovery_attempts() == 1, "first restart attempt must be counted");

    decision = policy.observe(3500, {false, 0, 0});
    require(decision.health == RecoveryHealth::Backoff && !decision.restart && decision.retry_after_ms == 500,
            "first crash-loop retry must be delayed");

    decision = policy.observe(4000, {false, 0, 0});
    require(decision.health == RecoveryHealth::Exited && decision.restart,
            "retry must become eligible when backoff expires");
    policy.record_restart_attempt(4000);
    require(policy.next_retry_ms() == 6000, "second recovery delay must double to two seconds");

    // A helper that comes back briefly must not erase crash-loop history.
    decision = policy.observe(5000, {true, 100, 0});
    require(decision.health == RecoveryHealth::Healthy && policy.recovery_attempts() == 2,
            "short healthy run must not reset crash-loop history");

    // Ten seconds of stable operation resets the backoff frontier.
    decision = policy.observe(15000, {true, 100, 0});
    require(decision.health == RecoveryHealth::Healthy && policy.recovery_attempts() == 0,
            "stable helper must reset recovery history");

    decision = policy.observe(16000, {false, 0, 0});
    require(decision.health == RecoveryHealth::Exited && decision.restart,
            "stable reset must allow immediate recovery again");

    std::cout << "recovery policy hang/backoff behavior passed\n";
    return 0;
}
