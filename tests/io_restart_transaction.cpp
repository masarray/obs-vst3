#include "common/audio_channel_adapter.hpp"
#include "common/io_restart_transaction.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "io restart transaction failure: " << message << '\n';
    std::exit(1);
}

bool close_enough(float a, float b)
{
    return std::fabs(a - b) < 0.00001f;
}

enum class LifecycleEvent {
    Stop,
    Deactivate,
    InspectRequested,
    Confirm,
    InspectConfirmed,
    Rebuild,
    Activate,
    QueryLatency,
    Start,
    Commit,
};

class FakeLifecycle final : public safevst3::IoRestartLifecycleTarget {
public:
    safevst3::IoRestartLifecycleStep fail = safevst3::IoRestartLifecycleStep::None;
    safevst3::IoArrangementResult arrangement = safevst3::IoArrangementResult::Accepted;
    safevst3::IoLayout requested{0, 0, 2, 2};
    safevst3::IoLayout confirmed{0, 0, 2, 2};
    std::uint32_t latency = 64;
    std::vector<LifecycleEvent> events;
    bool committed = false;
    safevst3::IoLayout committed_layout{};
    std::uint32_t committed_latency = 0;

    bool io_stop_processing() noexcept override
    {
        events.push_back(LifecycleEvent::Stop);
        return fail != safevst3::IoRestartLifecycleStep::StopProcessing;
    }
    bool io_deactivate() noexcept override
    {
        events.push_back(LifecycleEvent::Deactivate);
        return fail != safevst3::IoRestartLifecycleStep::Deactivate;
    }
    bool io_inspect_requested_layout(safevst3::IoLayout& layout) noexcept override
    {
        events.push_back(LifecycleEvent::InspectRequested);
        layout = requested;
        return fail != safevst3::IoRestartLifecycleStep::InspectRequested;
    }
    safevst3::IoArrangementResult io_confirm_requested_layout(
        const safevst3::IoLayout&) noexcept override
    {
        events.push_back(LifecycleEvent::Confirm);
        if (fail == safevst3::IoRestartLifecycleStep::ConfirmRequested)
            return safevst3::IoArrangementResult::FatalFailure;
        return arrangement;
    }
    bool io_inspect_confirmed_layout(safevst3::IoLayout& layout) noexcept override
    {
        events.push_back(LifecycleEvent::InspectConfirmed);
        layout = confirmed;
        return fail != safevst3::IoRestartLifecycleStep::InspectConfirmed;
    }
    bool io_rebuild_processing(const safevst3::IoLayout&) noexcept override
    {
        events.push_back(LifecycleEvent::Rebuild);
        return fail != safevst3::IoRestartLifecycleStep::RebuildProcessing;
    }
    bool io_activate() noexcept override
    {
        events.push_back(LifecycleEvent::Activate);
        return fail != safevst3::IoRestartLifecycleStep::Activate;
    }
    bool io_query_latency(std::uint32_t& value) noexcept override
    {
        events.push_back(LifecycleEvent::QueryLatency);
        value = latency;
        return fail != safevst3::IoRestartLifecycleStep::QueryLatency;
    }
    bool io_start_processing() noexcept override
    {
        events.push_back(LifecycleEvent::Start);
        return fail != safevst3::IoRestartLifecycleStep::StartProcessing;
    }
    void io_commit_layout(const safevst3::IoLayout& layout,
                          std::uint32_t latency_samples) noexcept override
    {
        events.push_back(LifecycleEvent::Commit);
        committed = true;
        committed_layout = layout;
        committed_latency = latency_samples;
    }
};

enum class CoordinatorEvent { Pause, Reconcile, Reconfigure, Publish, Resume, Recovery };

class FakeCoordinator final : public safevst3::IoRestartCoordinatorTarget {
public:
    safevst3::IoRestartCoordinatorStep fail = safevst3::IoRestartCoordinatorStep::None;
    std::vector<CoordinatorEvent> events;
    unsigned recovery_count = 0;

    bool pause_dsp() noexcept override
    {
        events.push_back(CoordinatorEvent::Pause);
        return fail != safevst3::IoRestartCoordinatorStep::PauseDsp;
    }
    bool reconcile_pending() noexcept override
    {
        events.push_back(CoordinatorEvent::Reconcile);
        return fail != safevst3::IoRestartCoordinatorStep::Reconcile;
    }
    bool reconfigure_io(safevst3::IoLayout& layout,
                        std::uint32_t& latency_samples) noexcept override
    {
        events.push_back(CoordinatorEvent::Reconfigure);
        layout = {1, 2, 1, 2};
        latency_samples = 128;
        return fail != safevst3::IoRestartCoordinatorStep::Reconfigure;
    }
    bool publish_io(const safevst3::IoLayout&, std::uint32_t) noexcept override
    {
        events.push_back(CoordinatorEvent::Publish);
        return fail != safevst3::IoRestartCoordinatorStep::Publish;
    }
    bool resume_dsp() noexcept override
    {
        events.push_back(CoordinatorEvent::Resume);
        return fail != safevst3::IoRestartCoordinatorStep::ResumeDsp;
    }
    void request_recovery() noexcept override
    {
        events.push_back(CoordinatorEvent::Recovery);
        ++recovery_count;
    }
};

} // namespace

int main()
{
    using namespace safevst3;

    FakeLifecycle success;
    success.confirmed = {3, 4, 1, 2};
    success.latency = 321;
    const auto success_result = run_io_restart_lifecycle(success);
    const std::array expected_order{
        LifecycleEvent::Stop, LifecycleEvent::Deactivate,
        LifecycleEvent::InspectRequested, LifecycleEvent::Confirm,
        LifecycleEvent::InspectConfirmed, LifecycleEvent::Rebuild,
        LifecycleEvent::Activate, LifecycleEvent::QueryLatency,
        LifecycleEvent::Start, LifecycleEvent::Commit};
    require(success_result.committed, "supported I/O transaction must commit");
    require(success.events.size() == expected_order.size(), "success ordering length changed");
    for (std::size_t i = 0; i < expected_order.size(); ++i)
        require(success.events[i] == expected_order[i], "success lifecycle ordering changed");
    require(success.committed_layout.main_input_bus == 3 &&
                success.committed_layout.main_output_bus == 4 &&
                success.committed_layout.input_channels == 1 &&
                success.committed_layout.output_channels == 2,
            "confirmed mono/stereo topology must be the committed topology");
    require(success.committed_latency == 321,
            "latency must be queried after activation and committed with topology");

    FakeLifecycle advisory;
    advisory.arrangement = IoArrangementResult::AdvisoryRejected;
    advisory.confirmed = {0, 0, 1, 1};
    const auto advisory_result = run_io_restart_lifecycle(advisory);
    require(advisory_result.committed && advisory_result.arrangement_was_advisory_rejected,
            "setBusArrangements false is advisory when confirmed topology remains supported");

    for (const auto input_channels : {1u, 2u}) {
        for (const auto output_channels : {1u, 2u}) {
            FakeLifecycle permutation;
            permutation.requested = {0, 0, input_channels, output_channels};
            permutation.confirmed = permutation.requested;
            const auto result = run_io_restart_lifecycle(permutation);
            require(result.committed && result.layout.input_channels == input_channels &&
                        result.layout.output_channels == output_channels,
                    "all mono/stereo main-bus permutations must be supported");
        }
    }

    require(has_unambiguous_main_io(1, 1),
            "exactly one main input and output must be accepted");
    require(!has_unambiguous_main_io(0, 1) && !has_unambiguous_main_io(1, 0) &&
                !has_unambiguous_main_io(2, 1) && !has_unambiguous_main_io(1, 2),
            "missing or multiple main buses must be rejected as ambiguous topology");

    FakeLifecycle unsupported_requested;
    unsupported_requested.requested = {0, 0, 6, 2};
    const auto unsupported_requested_result = run_io_restart_lifecycle(unsupported_requested);
    require(!unsupported_requested_result.committed &&
                unsupported_requested_result.failed_step == IoRestartLifecycleStep::InspectRequested,
            "unsupported requested topology must fail before confirmation");
    require(!unsupported_requested.committed,
            "unsupported requested topology must not commit");

    FakeLifecycle unsupported_confirmed;
    unsupported_confirmed.arrangement = IoArrangementResult::AdvisoryRejected;
    unsupported_confirmed.confirmed = {0, 0, 2, 8};
    const auto unsupported_confirmed_result = run_io_restart_lifecycle(unsupported_confirmed);
    require(!unsupported_confirmed_result.committed &&
                unsupported_confirmed_result.failed_step == IoRestartLifecycleStep::InspectConfirmed,
            "advisory rejection must still reject an unsupported confirmed topology");

    const std::array fallible_steps{
        IoRestartLifecycleStep::StopProcessing,
        IoRestartLifecycleStep::Deactivate,
        IoRestartLifecycleStep::InspectRequested,
        IoRestartLifecycleStep::ConfirmRequested,
        IoRestartLifecycleStep::InspectConfirmed,
        IoRestartLifecycleStep::RebuildProcessing,
        IoRestartLifecycleStep::Activate,
        IoRestartLifecycleStep::QueryLatency,
        IoRestartLifecycleStep::StartProcessing,
    };
    for (const auto step : fallible_steps) {
        FakeLifecycle failure;
        failure.fail = step;
        const auto result = run_io_restart_lifecycle(failure);
        require(!result.committed && result.failed_step == step,
                "each failed lifecycle frontier must remain diagnosable");
        require(!failure.committed,
                "candidate topology/latency must never commit after lifecycle failure");
    }

    FakeCoordinator coordinator;
    const auto coordinator_result = coordinate_io_restart(coordinator);
    const std::array coordinator_order{
        CoordinatorEvent::Pause, CoordinatorEvent::Reconcile,
        CoordinatorEvent::Reconfigure, CoordinatorEvent::Publish,
        CoordinatorEvent::Resume};
    require(coordinator_result.completed && coordinator.recovery_count == 0,
            "successful helper coordinator must not request recovery");
    require(coordinator.events.size() == coordinator_order.size(),
            "coordinator success ordering length changed");
    for (std::size_t i = 0; i < coordinator_order.size(); ++i)
        require(coordinator.events[i] == coordinator_order[i],
                "coordinator ordering changed");

    const std::array coordinator_failures{
        IoRestartCoordinatorStep::PauseDsp,
        IoRestartCoordinatorStep::Reconcile,
        IoRestartCoordinatorStep::Reconfigure,
        IoRestartCoordinatorStep::Publish,
        IoRestartCoordinatorStep::ResumeDsp,
    };
    for (const auto step : coordinator_failures) {
        FakeCoordinator failure;
        failure.fail = step;
        const auto result = coordinate_io_restart(failure);
        require(!result.completed && result.failed_step == step,
                "coordinator failed frontier must remain diagnosable");
        require(failure.recovery_count == 1,
                "coordinator failure must request recovery exactly once");
        require(!failure.events.empty() && failure.events.back() == CoordinatorEvent::Recovery,
                "recovery must be the final coordinator action");
    }

    const std::array<float, 4> mono{1.0f, -0.5f, 0.25f, 0.0f};
    std::array<float, 4> left{};
    std::array<float, 4> right{};
    duplicate_mono_to_stereo(mono.data(), left.data(), right.data(), mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i)
        require(close_enough(left[i], mono[i]) && close_enough(right[i], mono[i]),
                "mono->stereo adapter must duplicate samples exactly");

    const std::array<float, 4> stereo_left{1.0f, 0.5f, -1.0f, 0.25f};
    const std::array<float, 4> stereo_right{-1.0f, 0.5f, 1.0f, -0.25f};
    std::array<float, 4> downmixed{};
    average_stereo_to_mono(
        stereo_left.data(), stereo_right.data(), downmixed.data(), downmixed.size());
    require(close_enough(downmixed[0], 0.0f) && close_enough(downmixed[1], 0.5f) &&
                close_enough(downmixed[2], 0.0f) && close_enough(downmixed[3], 0.0f),
            "stereo->mono adapter must average channels deterministically");

    std::cout << "dynamic I/O lifecycle transaction ok\n";
    return 0;
}
