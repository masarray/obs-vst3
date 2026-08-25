#include "common/coherent_generation.hpp"
#include "common/parameter_catalog_snapshot.hpp"
#include "common/parameter_refresh_transaction.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "parameter refresh transaction failure: " << message << '\n';
    std::exit(1);
}

enum class Event {
    Pause,
    Reconcile,
    RefreshValues,
    RefreshMetadata,
    Publish,
    Resume,
    Recovery,
};

class FakeTarget final : public safevst3::ParameterRefreshCoordinatorTarget {
public:
    explicit FakeTarget(safevst3::ParameterRefreshCoordinatorStep failure =
                            safevst3::ParameterRefreshCoordinatorStep::None)
        : failure_(failure)
    {
    }

    bool pause_dsp() noexcept override
    {
        events.push_back(Event::Pause);
        return failure_ != safevst3::ParameterRefreshCoordinatorStep::PauseDsp;
    }

    bool reconcile_pending() noexcept override
    {
        events.push_back(Event::Reconcile);
        return failure_ != safevst3::ParameterRefreshCoordinatorStep::Reconcile;
    }

    bool refresh_parameter_values() noexcept override
    {
        events.push_back(Event::RefreshValues);
        return failure_ != safevst3::ParameterRefreshCoordinatorStep::RefreshValues;
    }

    bool refresh_parameter_metadata() noexcept override
    {
        events.push_back(Event::RefreshMetadata);
        return failure_ != safevst3::ParameterRefreshCoordinatorStep::RefreshMetadata;
    }

    bool publish_parameter_catalog() noexcept override
    {
        events.push_back(Event::Publish);
        return failure_ != safevst3::ParameterRefreshCoordinatorStep::PublishCatalog;
    }

    bool resume_dsp() noexcept override
    {
        events.push_back(Event::Resume);
        return failure_ != safevst3::ParameterRefreshCoordinatorStep::ResumeDsp;
    }

    void request_recovery() noexcept override
    {
        events.push_back(Event::Recovery);
        ++recovery_requests;
    }

    std::vector<Event> events;
    unsigned recovery_requests = 0;

private:
    safevst3::ParameterRefreshCoordinatorStep failure_;
};

} // namespace

int main()
{
    using safevst3::ParameterRefreshCoordinatorStep;
    using safevst3::ParameterRefreshRequest;
    using safevst3::coordinate_parameter_refresh;

    FakeTarget values;
    const auto values_result = coordinate_parameter_refresh(values, {true, false});
    const std::array values_order{
        Event::Pause, Event::Reconcile, Event::RefreshValues, Event::Publish, Event::Resume};
    require(values_result.completed, "values-only refresh must complete");
    require(values.events.size() == values_order.size(), "values-only event count changed");
    for (std::size_t i = 0; i < values_order.size(); ++i)
        require(values.events[i] == values_order[i], "values-only ordering changed");

    FakeTarget metadata;
    const auto metadata_result = coordinate_parameter_refresh(metadata, {false, true});
    const std::array metadata_order{
        Event::Pause, Event::Reconcile, Event::RefreshMetadata, Event::Publish, Event::Resume};
    require(metadata_result.completed, "metadata refresh must complete");
    require(metadata.events.size() == metadata_order.size(), "metadata event count changed");
    for (std::size_t i = 0; i < metadata_order.size(); ++i)
        require(metadata.events[i] == metadata_order[i], "metadata refresh ordering changed");

    FakeTarget combined;
    const auto combined_result = coordinate_parameter_refresh(combined, {true, true});
    require(combined_result.completed, "combined values/titles refresh must complete");
    require(combined.events == metadata.events,
            "combined flags must coalesce into one metadata transaction");

    const std::array failures{
        ParameterRefreshCoordinatorStep::PauseDsp,
        ParameterRefreshCoordinatorStep::Reconcile,
        ParameterRefreshCoordinatorStep::RefreshValues,
        ParameterRefreshCoordinatorStep::RefreshMetadata,
        ParameterRefreshCoordinatorStep::PublishCatalog,
        ParameterRefreshCoordinatorStep::ResumeDsp,
    };
    for (const auto failure : failures) {
        FakeTarget target(failure);
        const ParameterRefreshRequest request{
            failure != ParameterRefreshCoordinatorStep::RefreshMetadata,
            failure == ParameterRefreshCoordinatorStep::RefreshMetadata};
        const auto result = coordinate_parameter_refresh(target, request);
        require(!result.completed, "a failed frontier must reject the transaction");
        require(result.failed_step == failure, "failed frontier must remain diagnosable");
        require(target.recovery_requests == 1,
                "every failed transaction must request recovery exactly once");
        require(!target.events.empty() && target.events.back() == Event::Recovery,
                "recovery must be the final coordinator action");
    }

    FakeTarget empty;
    const auto empty_result = coordinate_parameter_refresh(empty, {});
    require(empty_result.completed && empty.events.empty(),
            "empty request must be a no-op without pause/recovery");

    struct CatalogFixture {
        std::uint32_t id;
        std::int32_t step_count;
        std::uint32_t flags;
        double default_normalized;
        double current_normalized;
        const char* title;
        const char* units;
    };
    const std::vector<CatalogFixture> changed_catalog{
        {101, 0, 1, 0.25, 0.50, "Drive", "dB"},
        {202, 7, 8, 0.10, 0.75, "Mode", "type"},
        {303, 1, 32, 0.00, 1.00, "Bypass", ""},
    };
    std::vector<CatalogFixture> projected;
    const auto catalog_projection = safevst3::project_parameter_catalog(
        changed_catalog, 8, [&](std::size_t, const CatalogFixture& entry) {
            projected.push_back(entry);
        });
    require(catalog_projection.total_count == 3 &&
                catalog_projection.published_count == 3 && projected.size() == 3,
            "metadata refresh projection must publish the new parameter count");
    require(projected[0].id == 101 && std::string(projected[0].title) == "Drive" &&
                std::string(projected[0].units) == "dB" &&
                projected[0].default_normalized == 0.25 &&
                projected[0].current_normalized == 0.50 &&
                projected[0].step_count == 0 && projected[0].flags == 1,
            "metadata refresh projection must preserve every ParameterInfo/value field");
    require(projected[1].id == 202 && std::string(projected[1].title) == "Mode" &&
                std::string(projected[1].units) == "type" &&
                projected[1].default_normalized == 0.10 &&
                projected[1].current_normalized == 0.75 &&
                projected[1].step_count == 7 && projected[1].flags == 8,
            "changed title/unit/default/step/flags/current value must project coherently");
    projected.clear();
    const auto bounded_projection = safevst3::project_parameter_catalog(
        changed_catalog, 2, [&](std::size_t, const CatalogFixture& entry) {
            projected.push_back(entry);
        });
    require(bounded_projection.total_count == 3 &&
                bounded_projection.published_count == 2 && projected.size() == 2,
            "catalog must report full vendor count while bounding published descriptors");

    std::uint32_t generation = 2;
    unsigned copies = 0;
    std::vector<int> snapshot;
    const bool stabilized = safevst3::read_coherent_generation(
        snapshot,
        [&] { return generation; },
        [&](std::vector<int>& candidate) {
            candidate = {static_cast<int>(copies + 1)};
            ++copies;
            if (copies == 1)
                generation = 4; // first copy becomes stale before verification
            return true;
        },
        3);
    require(stabilized, "reader must retry after a changing generation");
    require(copies == 2 && snapshot.size() == 1 && snapshot[0] == 2,
            "reader must return only the stable-generation candidate");

    generation = 7; // writer permanently in progress
    snapshot = {99};
    copies = 0;
    const bool unstable = safevst3::read_coherent_generation(
        snapshot,
        [&] { return generation; },
        [&](std::vector<int>& candidate) {
            ++copies;
            candidate = {1};
            return true;
        },
        3);
    require(!unstable, "odd generation must fail closed after bounded attempts");
    require(snapshot.empty(), "failed coherent read must return no snapshot");
    require(copies == 0, "reader must not copy while a writer generation is odd");

    std::cout << "parameter refresh transaction ok\n";
    return 0;
}
