#pragma once

#ifdef _WIN32

#include <obs-module.h>

namespace safevst3::obsrack {

obs_source_info make_source_info();
void module_unload() noexcept;

} // namespace safevst3::obsrack

#endif
