#ifdef _WIN32

#include "obs-plugin/parameter_controls.hpp"

#include "common/parameter_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace safevst3::obsparam {
namespace {

std::uint64_t fnv1a_append(std::uint64_t hash, std::string_view value) noexcept
{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (unsigned char c : value) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= prime;
    }
    return hash;
}

std::string parameter_key(const std::string& scope, std::uint32_t id)
{
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "vst_param_%s_%08x", scope.c_str(), id);
    return buffer;
}

std::string parameter_label(const ParameterSnapshot& parameter)
{
    std::string label = parameter.title.empty() ? ("Parameter " + std::to_string(parameter.id)) : parameter.title;
    if (!parameter.units.empty())
        label += " [" + parameter.units + "]";
    return label;
}

} // namespace

std::string parameter_scope(const std::string& path, const std::string& class_id)
{
    std::uint64_t hash = 14695981039346656037ull;
    hash = fnv1a_append(hash, path);
    hash ^= 0xffu;
    hash *= 1099511628211ull;
    hash = fnv1a_append(hash, class_id);

    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

void add_generic_parameter_properties(obs_properties_t* parent,
                                      const std::vector<ParameterSnapshot>& parameters,
                                      std::uint32_t total_parameter_count,
                                      obs_data_t* source_settings,
                                      const std::string& scope)
{
    if (!parent || parameters.empty())
        return;

    obs_properties_t* group = obs_properties_create();
    obs_properties_add_text(group, "generic_parameters_help", obs_module_text("GenericParametersHelp"), OBS_TEXT_INFO);

    if (total_parameter_count > kMaxParameters) {
        auto* warning = obs_properties_add_text(group, "generic_parameters_limit", obs_module_text("GenericParametersLimit"), OBS_TEXT_INFO);
        obs_property_text_set_info_type(warning, OBS_TEXT_INFO_WARNING);
    }

    std::size_t visible_count = 0;
    for (const auto& parameter : parameters) {
        if ((parameter.flags & ParameterHidden) != 0)
            continue;

        const std::string key = parameter_key(scope, parameter.id);
        const std::string label = parameter_label(parameter);
        const double step = parameter.step_count > 0
                                ? std::max(0.000001, 1.0 / static_cast<double>(parameter.step_count))
                                : 0.001;
        auto* property = obs_properties_add_float_slider(group, key.c_str(), label.c_str(), 0.0, 1.0, step);
        if ((parameter.flags & ParameterReadOnly) != 0)
            obs_property_set_enabled(property, false);

        if (source_settings && !obs_data_has_default_value(source_settings, key.c_str()))
            obs_data_set_default_double(source_settings, key.c_str(), parameter.current_normalized);
        ++visible_count;
    }

    if (visible_count == 0) {
        obs_properties_destroy(group);
        return;
    }

    obs_properties_add_group(parent, "generic_parameters", obs_module_text("GenericParameters"), OBS_GROUP_NORMAL, group);
}

void apply_parameter_settings(WinObsBridge& bridge,
                              obs_data_t* settings,
                              const std::string& scope) noexcept
{
    if (!settings || !bridge.running())
        return;

    const auto parameters = bridge.parameters();
    for (const auto& parameter : parameters) {
        if ((parameter.flags & (ParameterHidden | ParameterReadOnly)) != 0)
            continue;

        const std::string key = parameter_key(scope, parameter.id);
        if (!obs_data_has_user_value(settings, key.c_str()))
            continue;

        const double value = normalize_parameter_value(obs_data_get_double(settings, key.c_str()), parameter.step_count);
        (void)bridge.set_parameter(parameter.id, value);
    }
}

} // namespace safevst3::obsparam

#endif
