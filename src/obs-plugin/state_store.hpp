#pragma once

#ifdef _WIN32

#include "common/state_snapshot.hpp"

#include <obs-module.h>

#include <string>

namespace safevst3::obsstate {

enum class LoadResult {
    Missing,
    Loaded,
    Invalid,
};

LoadResult load(obs_source_t* source,
                const std::string& vst_path,
                const std::string& class_id,
                PluginStateSnapshot& snapshot,
                std::string& error);

bool save(obs_source_t* source,
          const std::string& vst_path,
          const std::string& class_id,
          const PluginStateSnapshot& snapshot,
          std::string& error);

void discard(obs_source_t* source,
             const std::string& vst_path,
             const std::string& class_id) noexcept;

} // namespace safevst3::obsstate

#endif
