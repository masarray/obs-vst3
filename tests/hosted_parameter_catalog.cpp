#include "common/hosted_parameter_catalog.hpp"
#include "common/protocol.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "hosted parameter catalog failure: " << message << '\n';
    std::exit(1);
}

safevst3::HostedParameter parameter(std::uint32_t id,
                                    const char* title,
                                    const char* units,
                                    std::int32_t steps,
                                    std::uint32_t flags,
                                    double default_value,
                                    double current_value)
{
    return {id, steps, flags, default_value, current_value, title, units};
}

class FakeController final : public safevst3::HostedParameterControllerSource {
public:
    std::int32_t parameter_count() const noexcept override { return reported_count; }

    bool read_parameter(std::int32_t index,
                        safevst3::HostedParameter& destination) const override
    {
        if (index < 0 || static_cast<std::size_t>(index) >= catalog.size())
            return false;
        destination = catalog[static_cast<std::size_t>(index)];
        return true;
    }

    double normalized_value(std::uint32_t id) const noexcept override
    {
        for (const auto& value : values) {
            if (value.id == id)
                return value.normalized;
        }
        return 0.0;
    }

    std::int32_t reported_count = 0;
    std::vector<safevst3::HostedParameter> catalog;
    std::vector<safevst3::HostedParameterValueChange> values;
};

} // namespace

int main()
{
    using safevst3::HostedParameterValueChange;

    FakeController controller;
    controller.reported_count = 2;
    controller.catalog = {
        parameter(10, "Gain", "dB", 0, safevst3::ParameterCanAutomate, 0.5, 0.25),
        parameter(20, "Mode", "", 3, safevst3::ParameterList, 0.0, 0.5),
    };

    std::vector<safevst3::HostedParameter> hosted{
        parameter(99, "Last known good", "%", 0, 0, 0.1, 0.2),
    };
    std::uint32_t total_count = 1;
    std::string error;
    require(safevst3::rebuild_hosted_parameter_catalog(
                controller, hosted, total_count, error),
            "valid controller metadata must rebuild the catalog");
    require(total_count == 2 && hosted.size() == 2,
            "metadata refresh must publish the controller's new count");
    require(hosted[0].id == 10 && hosted[0].title == "Gain" &&
                hosted[0].units == "dB" && hosted[0].default_normalized == 0.5,
            "metadata refresh lost title/unit/default information");
    require(hosted[1].step_count == 3 && hosted[1].flags == safevst3::ParameterList,
            "metadata refresh lost step/flag information");

    controller.values = {{10, 0.8}, {20, 1.2}};
    const auto metadata_before = hosted;
    std::array<HostedParameterValueChange, 4> changes{};
    const std::size_t changed = safevst3::refresh_hosted_parameter_values(
        controller, hosted, changes.data(), changes.size());
    require(changed == 2 && changes[0].id == 10 && changes[0].normalized == 0.8,
            "values refresh must report changed normalized values");
    require(hosted[0].current_normalized == 0.8 && hosted[1].current_normalized == 1.0,
            "values refresh must clamp and update the hosted mirror");
    require(hosted[0].title == metadata_before[0].title &&
                hosted[0].units == metadata_before[0].units &&
                hosted[0].default_normalized == metadata_before[0].default_normalized &&
                hosted[0].step_count == metadata_before[0].step_count &&
                hosted[0].flags == metadata_before[0].flags,
            "values-only refresh must not alter parameter metadata");

    controller.reported_count = -1;
    const auto last_known_good = hosted;
    require(!safevst3::rebuild_hosted_parameter_catalog(
                controller, hosted, total_count, error),
            "negative controller count must reject metadata refresh");
    require(hosted.size() == last_known_good.size() &&
                hosted[0].id == last_known_good[0].id && total_count == 2,
            "failed metadata refresh must preserve the last-known-good catalog");

    std::cout << "hosted parameter catalog ok\n";
    return 0;
}
