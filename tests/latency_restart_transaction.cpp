#include "common/latency_restart_transaction.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using safevst3::LatencyRestartStep;

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "latency restart transaction failure: " << message << '\n';
    std::exit(1);
}

class FakeTarget final : public safevst3::LatencyRestartTarget {
public:
    explicit FakeTarget(LatencyRestartStep failure = LatencyRestartStep::None)
        : failure_(failure)
    {
    }

    bool set_processing(bool enabled) noexcept override
    {
        const auto step = enabled ? LatencyRestartStep::StartProcessing
                                  : LatencyRestartStep::StopProcessing;
        calls.push_back(step);
        return failure_ != step;
    }

    bool set_active(bool enabled) noexcept override
    {
        const auto step = enabled ? LatencyRestartStep::Activate
                                  : LatencyRestartStep::Deactivate;
        calls.push_back(step);
        return failure_ != step;
    }

    std::uint32_t get_latency_samples() noexcept override
    {
        calls.push_back(LatencyRestartStep::QueryLatency);
        return candidate_latency;
    }

    std::uint32_t candidate_latency = 384;
    std::vector<LatencyRestartStep> calls;

private:
    LatencyRestartStep failure_;
};

enum class CoordinatorEvent {
    Pause,
    Refresh,
    Publish,
    Resume,
    Recovery,
};

class FakeCoordinatorTarget final : public safevst3::LatencyRestartCoordinatorTarget {
public:
    explicit FakeCoordinatorTarget(
        safevst3::LatencyRestartCoordinatorStep failure =
            safevst3::LatencyRestartCoordinatorStep::None)
        : failure_(failure)
    {
    }

    bool pause_dsp() noexcept override
    {
        events.push_back(CoordinatorEvent::Pause);
        return failure_ != safevst3::LatencyRestartCoordinatorStep::PauseDsp;
    }

    bool refresh_latency(std::uint32_t& latency_samples) noexcept override
    {
        events.push_back(CoordinatorEvent::Refresh);
        if (failure_ == safevst3::LatencyRestartCoordinatorStep::RefreshLifecycle)
            return false;
        latency_samples = candidate_latency;
        return true;
    }

    void publish_latency(std::uint32_t latency_samples) noexcept override
    {
        events.push_back(CoordinatorEvent::Publish);
        published_latency = latency_samples;
    }

    bool resume_dsp() noexcept override
    {
        events.push_back(CoordinatorEvent::Resume);
        return failure_ != safevst3::LatencyRestartCoordinatorStep::ResumeDsp;
    }

    void request_recovery() noexcept override
    {
        events.push_back(CoordinatorEvent::Recovery);
        ++recovery_requests;
    }

    std::uint32_t candidate_latency = 512;
    std::uint32_t published_latency = 0;
    std::uint32_t recovery_requests = 0;
    std::vector<CoordinatorEvent> events;

private:
    safevst3::LatencyRestartCoordinatorStep failure_;
};

} // namespace

int main()
{
    using safevst3::run_latency_restart_transaction;

    FakeTarget success;
    const auto result = run_latency_restart_transaction(success);
    const std::array expected{
        LatencyRestartStep::StopProcessing,
        LatencyRestartStep::Deactivate,
        LatencyRestartStep::Activate,
        LatencyRestartStep::QueryLatency,
        LatencyRestartStep::StartProcessing,
    };
    require(result.committed, "a complete transaction must commit");
    require(result.latency_samples == success.candidate_latency,
            "the post-activation latency must be committed");
    require(result.failed_step == LatencyRestartStep::None,
            "a successful transaction must not report a failed step");
    require(success.calls.size() == expected.size(),
            "success must execute every lifecycle step exactly once");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(success.calls[i] == expected[i], "success lifecycle ordering changed");

    const std::array fallible_steps{
        LatencyRestartStep::StopProcessing,
        LatencyRestartStep::Deactivate,
        LatencyRestartStep::Activate,
        LatencyRestartStep::StartProcessing,
    };
    const std::array<std::size_t, 4> expected_failure_call_counts{1, 2, 3, 5};
    for (std::size_t failure_index = 0; failure_index < fallible_steps.size();
         ++failure_index) {
        const auto failed_step = fallible_steps[failure_index];
        FakeTarget failure(failed_step);
        const auto rejected = run_latency_restart_transaction(failure);
        require(!rejected.committed, "a failed lifecycle step must reject the transaction");
        require(rejected.latency_samples == 0,
                "a failed transaction must not expose an uncommitted latency");
        require(rejected.failed_step == failed_step,
                "the failed lifecycle step must remain diagnosable");
        require(failure.calls.size() == expected_failure_call_counts[failure_index],
                "a lifecycle failure must stop the transaction immediately");
        for (std::size_t call = 0; call < failure.calls.size(); ++call)
            require(failure.calls[call] == expected[call],
                    "a failed transaction must preserve the successful ordering prefix");
    }

    FakeTarget activation_failure(LatencyRestartStep::Activate);
    (void)run_latency_restart_transaction(activation_failure);
    require(activation_failure.calls.size() == 3,
            "latency must not be queried after activation failure");

    FakeTarget processing_failure(LatencyRestartStep::StartProcessing);
    (void)run_latency_restart_transaction(processing_failure);
    require(processing_failure.calls.size() == expected.size(),
            "processing restart failure must occur after the latency query");
    require(processing_failure.calls[3] == LatencyRestartStep::QueryLatency,
            "latency query must follow activation and precede processing restart");

    using safevst3::LatencyRestartCoordinatorStep;
    using safevst3::coordinate_latency_restart;

    FakeCoordinatorTarget coordinator_success;
    const auto coordinated = coordinate_latency_restart(coordinator_success);
    const std::array coordinator_order{
        CoordinatorEvent::Pause,
        CoordinatorEvent::Refresh,
        CoordinatorEvent::Publish,
        CoordinatorEvent::Resume,
    };
    require(coordinated.completed, "a complete helper coordination must succeed");
    require(coordinated.failed_step == LatencyRestartCoordinatorStep::None,
            "successful helper coordination must not report a failure");
    require(coordinator_success.published_latency == coordinator_success.candidate_latency,
            "the committed latency must be published to the bridge");
    require(coordinator_success.recovery_requests == 0,
            "successful helper coordination must not request recovery");
    require(coordinator_success.events.size() == coordinator_order.size(),
            "successful helper coordination must execute every step once");
    for (std::size_t i = 0; i < coordinator_order.size(); ++i)
        require(coordinator_success.events[i] == coordinator_order[i],
                "helper coordination ordering changed");

    const std::array coordinator_failures{
        LatencyRestartCoordinatorStep::PauseDsp,
        LatencyRestartCoordinatorStep::RefreshLifecycle,
        LatencyRestartCoordinatorStep::ResumeDsp,
    };
    const std::array<std::size_t, 3> failure_event_counts{2, 3, 5};
    for (std::size_t i = 0; i < coordinator_failures.size(); ++i) {
        FakeCoordinatorTarget failure(coordinator_failures[i]);
        const auto rejected = coordinate_latency_restart(failure);
        require(!rejected.completed, "a coordinator failure must reject the operation");
        require(rejected.failed_step == coordinator_failures[i],
                "the failed coordinator frontier must remain diagnosable");
        require(failure.recovery_requests == 1,
                "every coordinator failure must request recovery exactly once");
        require(failure.events.size() == failure_event_counts[i],
                "coordinator failure must stop after its recovery request");
        require(failure.events.back() == CoordinatorEvent::Recovery,
                "recovery must be the final action after a coordinator failure");
        if (coordinator_failures[i] != LatencyRestartCoordinatorStep::ResumeDsp)
            require(failure.published_latency == 0,
                    "latency must not publish before pause and lifecycle succeed");
    }

    FakeCoordinatorTarget resume_failure(LatencyRestartCoordinatorStep::ResumeDsp);
    (void)coordinate_latency_restart(resume_failure);
    require(resume_failure.events[2] == CoordinatorEvent::Publish &&
                resume_failure.events[3] == CoordinatorEvent::Resume,
            "latency publication must precede the DSP resume attempt");

    std::cout << "latency restart transaction ok\n";
    return 0;
}
