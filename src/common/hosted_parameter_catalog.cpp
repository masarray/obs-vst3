#include "common/hosted_parameter_catalog.hpp"

#include "common/parameter_utils.hpp"

namespace safevst3 {

bool rebuild_hosted_parameter_catalog(
    const HostedParameterControllerSource& source,
    std::vector<HostedParameter>& destination,
    std::uint32_t& total_count,
    std::string& error)
{
    error.clear();
    const std::int32_t count = source.parameter_count();
    if (count < 0) {
        error = "VST3 controller returned an invalid parameter count";
        return false;
    }

    std::vector<HostedParameter> candidate;
    try {
        candidate.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            HostedParameter parameter{};
            if (!source.read_parameter(index, parameter))
                continue;
            parameter.default_normalized = normalize_parameter_value(
                parameter.default_normalized, parameter.step_count);
            parameter.current_normalized = normalize_parameter_value(
                parameter.current_normalized, parameter.step_count);
            candidate.push_back(std::move(parameter));
        }
    } catch (...) {
        error = "VST3 parameter metadata allocation failed";
        return false;
    }

    destination = std::move(candidate);
    total_count = static_cast<std::uint32_t>(count);
    return true;
}

std::size_t refresh_hosted_parameter_values(
    const HostedParameterControllerSource& source,
    std::vector<HostedParameter>& parameters,
    HostedParameterValueChange* changes,
    std::size_t change_capacity) noexcept
{
    std::size_t written = 0;
    for (auto& parameter : parameters) {
        const double value = normalize_parameter_value(
            source.normalized_value(parameter.id), parameter.step_count);
        if (value == parameter.current_normalized)
            continue;
        parameter.current_normalized = value;
        if (changes && written < change_capacity)
            changes[written++] = {parameter.id, value};
    }
    return written;
}

} // namespace safevst3
