#pragma once

#include "rack/rack_preset_library.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace safevst3::rack {

inline constexpr std::size_t kRackPresetLibraryMaxEntries = 128;

struct RackPresetSummary {
    RackPresetId preset_id{};
    std::string name;
};

bool list_rack_presets(const std::filesystem::path& library,
                       std::vector<RackPresetSummary>& destination,
                       std::string& error);

bool rename_rack_preset_atomic(const std::filesystem::path& library,
                               const RackPresetId& preset_id,
                               const std::string& new_name,
                               std::string& error);

bool delete_rack_preset(const std::filesystem::path& library,
                        const RackPresetId& preset_id,
                        std::string& error);

} // namespace safevst3::rack
