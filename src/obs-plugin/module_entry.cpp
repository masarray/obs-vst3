#ifdef _WIN32

#include "obs-plugin/rack_filter.hpp"

#include <obs-module.h>

extern "C" bool single_obs_module_load(void);
extern "C" void single_obs_module_unload(void);

namespace {
obs_source_info rack_source_info = safevst3::obsrack::make_source_info();
}

MODULE_EXPORT bool obs_module_load(void)
{
    if (!single_obs_module_load())
        return false;
    obs_register_source(&rack_source_info);
    return true;
}

MODULE_EXPORT void obs_module_unload(void)
{
    safevst3::obsrack::module_unload();
    single_obs_module_unload();
}

MODULE_EXPORT const char* obs_module_description(void)
{
    return "Crash-isolated VST3 Single Host and serial VST3 Rack for OBS Studio";
}

#endif
