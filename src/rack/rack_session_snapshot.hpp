#pragma once

#include "common/state_snapshot.hpp"
#include "rack/rack_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace safevst3 {
class HostedPlugin;
}

namespace safevst3::rack {

inline constexpr std::uint32_t kRackSessionMagic = 0x31534e52u; // "RNS1"
inline constexpr std::uint32_t kRackSessionFormatVersion = 1;
inline constexpr std::size_t kRackSessionMaxPluginPathBytes = 4096;
inline constexpr std::size_t kRackSessionMaxClassIdBytes = 256;
inline constexpr std::size_t kRackSessionMaxSnapshotBytes =
    (static_cast<std::size_t>(kRackMaxSlots) *
     (kMaxStateBytes + kRackSessionMaxPluginPathBytes + kRackSessionMaxClassIdBytes + 128u)) +
    4096u;

enum class RackPersistedSlotHealth : std::uint32_t {
    Ready = 0,
    Missing = 1,
    Failed = 2,
    Suspect = 3,
    Quarantined = 4,
};

enum class RackSessionLoadSource : std::uint8_t {
    None = 0,
    Current = 1,
    Previous = 2,
};

struct RackSessionSlotSnapshot {
    RackSlotId slot_id = 0;
    std::string plugin_path;
    std::string class_id;
    bool bypass = false;
    RackPersistedSlotHealth health = RackPersistedSlotHealth::Ready;
    PluginStateSnapshot state;
};

struct RackSessionSnapshot {
    std::array<std::uint8_t, 16> rack_id{};
    std::uint64_t generation = 0;
    std::vector<RackSessionSlotSnapshot> slots;
};

bool capture_rack_session_slot(HostedPlugin& plugin,
                               RackSlotId slot_id,
                               const std::string& plugin_path,
                               bool bypass,
                               RackPersistedSlotHealth health,
                               RackSessionSlotSnapshot& destination,
                               std::string& error);

bool restore_rack_session_slot_state(HostedPlugin& plugin,
                                     const RackSessionSlotSnapshot& slot,
                                     std::string& error);

bool encode_rack_session_snapshot(const RackSessionSnapshot& snapshot,
                                  std::vector<std::uint8_t>& destination,
                                  std::string& error);

bool decode_rack_session_snapshot(const std::vector<std::uint8_t>& bytes,
                                  RackSessionSnapshot& destination,
                                  std::string& error);

bool write_rack_session_snapshot_atomic(const std::filesystem::path& path,
                                        const RackSessionSnapshot& snapshot,
                                        std::string& error);

bool load_rack_session_snapshot_lkg(const std::filesystem::path& path,
                                    RackSessionSnapshot& destination,
                                    RackSessionLoadSource& source,
                                    std::string& error);

} // namespace safevst3::rack
