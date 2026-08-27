#pragma once
#include <obs-module.h>

// Minimum libobs API used by this plugin: OBS Studio 29.1.
// clean-test3 rebuild marker: also validates stale-copy installer cleanup.
#define SAFEVST3_OBS_MIN_API_VER MAKE_SEMANTIC_VERSION(29, 1, 0)
#undef LIBOBS_API_VER
#define LIBOBS_API_VER SAFEVST3_OBS_MIN_API_VER

// The OBS module already force-includes this compatibility header. Keep the
// novice-facing Properties polish in the same audited boundary so host/DSP
// code remains untouched and no OBS frontend/Qt dependency is introduced.
#include "obs-plugin/properties_ux_shim.hpp"
