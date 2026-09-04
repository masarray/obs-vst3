#pragma once

#include "rack/rack_session_snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace safevst3::rack {

inline constexpr std::uint32_t kRackPresetMagic = 0x31505252u; // "RRP1"
inline constexpr std::uint32_t kRackPresetFormatVersion = 1;
inline constexpr std::size_t kRackPresetMaxNameBytes = 256;
inline constexpr std::size_t kRackPresetMaxBytes =
    40u + kRackPresetMaxNameBytes + kRackSessionMaxSnapshotBytes;

using RackPresetId = std::array<std::uint8_t, 16>;

enum class RackPresetLoadSource : std::uint8_t {
    None = 0,
    Current = 1,
    Previous = 2,
};

struct RackPreset {
    RackPresetId preset_id{};
    std::string name;
    std::vector<RackSessionSlotSnapshot> slots;
};

bool generate_rack_preset_id(RackPresetId& destination, std::string& error) noexcept;

std::filesystem::path rack_preset_library_path();
std::filesystem::path rack_preset_file_path(const std::filesystem::path& library,
                                            const RackPresetId& preset_id);

bool encode_rack_preset(const RackPreset& preset,
                        std::vector<std::uint8_t>& destination,
                        std::string& error);

bool decode_rack_preset(const std::vector<std::uint8_t>& bytes,
                        RackPreset& destination,
                        std::string& error);

bool write_rack_preset_atomic(const std::filesystem::path& library,
                              const RackPreset& preset,
                              std::string& error);

bool load_rack_preset_lkg(const std::filesystem::path& library,
                          const RackPresetId& preset_id,
                          RackPreset& destination,
                          RackPresetLoadSource& source,
                          std::string& error);

// Materialize a reusable preset into one independent working Rack identity.
// The returned Session Snapshot is a detached value copy: later working-Rack
// edits cannot mutate the persisted preset artifact.
bool make_working_rack_from_preset(const RackPreset& preset,
                                   const std::array<std::uint8_t, 16>& destination_rack_id,
                                   std::uint64_t destination_generation,
                                   RackSessionSnapshot& destination,
                                   std::string& error);

} // namespace safevst3::rack
