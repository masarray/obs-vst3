#ifdef _WIN32

// Keep the proven Single implementation byte-for-byte while giving R3-1 a
// tiny composition entrypoint that can register the separate Rack filter.
#define obs_module_load single_obs_module_load
#define obs_module_unload single_obs_module_unload
#define obs_module_description single_obs_module_description

// obs_compat_floor.hpp is force-included by the module target before this
// translation unit, so obs-module.h has already declared the original OBS
// entrypoints with C linkage. The rename macros above only affect tokens that
// follow them. Declare the renamed functions explicitly with the same linkage
// before including the byte-for-byte Single implementation; module_entry.cpp
// can then call these stable internal entrypoints without a linker mismatch.
extern "C" bool single_obs_module_load(void);
extern "C" void single_obs_module_unload(void);
extern "C" const char* single_obs_module_description(void);

#include "plugin.cpp"

#endif
