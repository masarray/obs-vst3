#pragma once
#include <obs-module.h>

// Minimum libobs API used by this plugin: OBS Studio 29.1.
#define SAFEVST3_OBS_MIN_API_VER MAKE_SEMANTIC_VERSION(29, 1, 0)
#undef LIBOBS_API_VER
#define LIBOBS_API_VER SAFEVST3_OBS_MIN_API_VER
