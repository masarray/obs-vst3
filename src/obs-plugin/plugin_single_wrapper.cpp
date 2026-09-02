#ifdef _WIN32

// Keep the proven Single implementation byte-for-byte while giving R3-1 a
// tiny composition entrypoint that can register the separate Rack filter.
#define obs_module_load single_obs_module_load
#define obs_module_unload single_obs_module_unload
#define obs_module_description single_obs_module_description
#include "plugin.cpp"

#endif
