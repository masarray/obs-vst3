#pragma once
#include <cstddef>
#include <obs-module.h>

// Minimum libobs API used by this plugin: OBS Studio 29.1.
#define SAFEVST3_OBS_MIN_API_VER MAKE_SEMANTIC_VERSION(29, 1, 0)
#undef LIBOBS_API_VER
#define LIBOBS_API_VER SAFEVST3_OBS_MIN_API_VER

// obs_register_source() always forwards sizeof(struct obs_source_info) from
// the SDK used to build the module. Newer OBS SDKs append fields to that
// structure, so forwarding the full current size makes otherwise-compatible
// modules fail registration on older libobs runtimes.
//
// Safe VST3 only populates source-info fields through `save`; all fields after
// it are intentionally unused. Register exactly that ABI prefix so old libobs
// zero-fills its own trailing fields while current libobs receives every
// callback this module actually uses. Keep this boundary in sync if a future
// source implementation starts using a field after `save`.
#define SAFEVST3_OBS_SOURCE_INFO_COMPAT_SIZE \
    (offsetof(struct obs_source_info, save) + sizeof(((struct obs_source_info*)0)->save))

static_assert(SAFEVST3_OBS_SOURCE_INFO_COMPAT_SIZE <= sizeof(struct obs_source_info),
              "OBS source-info compatibility prefix exceeds the build SDK structure");

#undef obs_register_source
#define obs_register_source(info) \
    obs_register_source_s((info), SAFEVST3_OBS_SOURCE_INFO_COMPAT_SIZE)
