#include "common/parameter_refresh_transaction.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using safevst3::ParameterRefreshCoordinatorStep;
using safevst3::ParameterRefreshScope;

enum class Event {
    Pause,
    Reconcile,
    RefreshValues,
    RefreshMetadata,
    Resume,
    Recovery,
};

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "parameter refresh transaction failure: " << message << '\n';
    std::exit(1);
}

class FakeTarget final : public safevst3::ParameterRefreshCoordinatorTarget {
public:
    explicit FakeTarget(ParameterRefreshCoordinatorStep failure =
                            ParameterRefreshCoordinatorStep::None)
        : failure_(failure)
    {
    }

    bool pause_dsp() noexcept override
    {
        events.push_back(Event::Pause);
        return failure_ != ParameterRefreshCoordinatorStep::PauseDsp;
    }

    bool reconcile_pending_edits() noexcept override
    {
        events.push_back(Event::Reconcile);
        return failure_ != ParameterRefreshCoordinatorStep::ReconcilePendingEdits;
    }

    bool refresh_and_publish(ParameterRefreshScope scope) noexcept override
    {
        observed_scope = scope;
        events.push_back(scope == ParameterRefreshScope::Values
                             ? Event::RefreshValues
                             : Event::RefreshMetadata);
        return failure_ != ParameterRefreshCoordinatorStep::RefreshAndPublish;
    }

    bool resume_dsp() noexcept override
    {
        events.push_back(Event::Resume);
        return failure_ != ParameterRefreshCoordinatorStep::ResumeDsp;
    }

    void request_recovery() noexcept override
    {
        events.push_back(Event::Recovery);
        ++recovery_requests;
    }

    ParameterRefreshScope observed_scope = ParameterRefreshScope::Values;
    unsigned recovery_requests = 0;
    std::vector<Event> events;

private:
    ParameterRefreshCoordinatorStep failure_;
};

void verify_success(bool values_changed,
                    bool metadata_changed,
                    ParameterRefreshScope expected_scope)
{
    FakeTarget target;
    const auto result = safevst3::coordinate_parameter_refresh(
        target, values_changed, metadata_changed);
    const std::array expected{
        Event::Pause,
        Event::Reconcile,
        expected_scope == ParameterRefreshScope::Values
            ? Event::RefreshValues
            : Event::RefreshMetadata,
        Event::Resume,
    };

    require(result.completed, "requested refresh must complete");
    require(result.failed_step == ParameterRefreshCoordinatorStep::None,
            "successful refresh must not report a failure");
    require(target.observed_scope == expected_scope,
            "coordinator selected the wrong refresh scope");
    require(target.recovery_requests == 0,
            "successful refresh must not request recovery");
    require(target.events.size() == expected.size(),
            "successful refresh must execute one bounded transaction");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(target.events[i] == expected[i], "refresh transaction ordering changed");
}

} // namespace

int main()
{
    verify_success(true, false, ParameterRefreshScope::Values);
    verify_success(false, true, ParameterRefreshScope::MetadataAndValues);
    verify_success(true, true, ParameterRefreshScope::MetadataAndValues);

    FakeTarget no_request;
    const auto skipped = safevst3::coordinate_parameter_refresh(no_request, false, false);
    require(skipped.completed && no_request.events.empty(),
            "an empty restart plan must not pause DSP or touch vendor state");

    const std::array failures{
        ParameterRefreshCoordinatorStep::PauseDsp,
        ParameterRefreshCoordinatorStep::ReconcilePendingEdits,
        ParameterRefreshCoordinatorStep::RefreshAndPublish,
        ParameterRefreshCoordinatorStep::ResumeDsp,
    };
    const std::array<std::size_t, 4> expected_event_counts{2, 3, 4, 5};
    for (std::size_t i = 0; i < failures.size(); ++i) {
        FakeTarget target(failures[i]);
        const auto rejected = safevst3::coordinate_parameter_refresh(
            target, true, true);
        require(!rejected.completed, "a failed refresh frontier must reject");
        require(rejected.failed_step == failures[i],
                "the failed refresh frontier must remain diagnosable");
        require(target.recovery_requests == 1,
                "every failed refresh must request recovery exactly once");
        require(target.events.size() == expected_event_counts[i],
                "a failed refresh must stop after requesting recovery");
        require(target.events.back() == Event::Recovery,
                "recovery must be the final failed-transaction action");
    }

    std::cout << "parameter refresh transaction ok\n";
    return 0;
}
