#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace safevst3 {

struct HostedParameter {
    std::uint32_t id = 0;
    std::int32_t step_count = 0;
    std::uint32_t flags = 0;
    double default_normalized = 0.0;
    double current_normalized = 0.0;
    std::string title;
    std::string units;
};

struct HostedParameterValueChange {
    std::uint32_t id = 0;
    double normalized = 0.0;
};

class HostedParameterControllerSource {
public:
    virtual ~HostedParameterControllerSource() = default;

    virtual std::int32_t parameter_count() const noexcept = 0;
    virtual bool read_parameter(std::int32_t index,
                                HostedParameter& destination) const = 0;
    virtual double normalized_value(std::uint32_t id) const noexcept = 0;
};

[[nodiscard]] bool rebuild_hosted_parameter_catalog(
    const HostedParameterControllerSource& source,
    std::vector<HostedParameter>& destination,
    std::uint32_t& total_count,
    std::string& error);

[[nodiscard]] std::size_t refresh_hosted_parameter_values(
    const HostedParameterControllerSource& source,
    std::vector<HostedParameter>& parameters,
    HostedParameterValueChange* changes,
    std::size_t change_capacity) noexcept;

} // namespace safevst3
