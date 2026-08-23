#pragma once

#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <obs-module.h>

#include <cstdint>
#include <string>
#include <vector>

namespace safevst3::obsparam {

std::string parameter_scope(const std::string& path, const std::string& class_id);

void add_generic_parameter_properties(obs_properties_t* parent,
                                      const std::vector<ParameterSnapshot>& parameters,
                                      std::uint32_t total_parameter_count,
                                      obs_data_t* source_settings,
                                      const std::string& scope);

void apply_parameter_settings(WinObsBridge& bridge,
                              obs_data_t* settings,
                              const std::string& scope) noexcept;

} // namespace safevst3::obsparam

#endif
