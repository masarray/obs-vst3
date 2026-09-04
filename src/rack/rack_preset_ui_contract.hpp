#pragma once

#include "rack/rack_preset_library.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace safevst3::rack::ui {

inline constexpr std::uint32_t kRackPresetUiSnapshotVersion = 1;
inline constexpr std::uint32_t kRackPresetUiMaxEntries = 128;
inline constexpr std::size_t kRackPresetUiNameBytes = 128;

using RackPresetUiCommandId = std::uint64_t;

enum class RackPresetUiCommandType : std::uint32_t {
    None = 0,
    SaveAs = 1,
    Load = 2,
    Rename = 3,
    Delete = 4,
    Update = 5,
};

enum class RackPresetUiCommandResult : std::uint32_t {
    Idle = 0,
    Accepted = 1,
    Rejected = 2,
    Failed = 3,
};

struct RackPresetUiEntry {
    RackPresetId preset_id{};
    std::array<char, kRackPresetUiNameBytes> name{};
};

struct RackPresetUiSnapshot {
    std::uint32_t version = kRackPresetUiSnapshotVersion;
    std::uint64_t generation = 0;
    std::uint32_t entry_count = 0;
    RackPresetId active_preset_id{};
    std::array<char, kRackPresetUiNameBytes> active_preset_name{};
    std::array<RackPresetUiEntry, kRackPresetUiMaxEntries> entries{};
};

struct RackPresetUiCommand {
    RackPresetUiCommandId command_id = 0;
    RackPresetUiCommandType type = RackPresetUiCommandType::None;
    RackPresetId preset_id{};
    std::array<char, kRackPresetUiNameBytes> name{};
};

struct RackPresetUiAck {
    RackPresetUiCommandId command_id = 0;
    RackPresetUiCommandResult result = RackPresetUiCommandResult::Idle;
    std::uint64_t committed_generation = 0;
    RackPresetId preset_id{};
};

inline bool rack_preset_id_nonzero(const RackPresetId& id) noexcept
{
    return std::any_of(id.begin(), id.end(), [](std::uint8_t byte) { return byte != 0; });
}

inline bool rack_preset_ui_name_valid(std::string_view name) noexcept
{
    return !name.empty() && name.size() < kRackPresetUiNameBytes &&
           name.find('\0') == std::string_view::npos;
}

inline bool rack_preset_ui_copy_name(
    std::array<char, kRackPresetUiNameBytes>& destination,
    std::string_view source) noexcept
{
    if (!rack_preset_ui_name_valid(source))
        return false;
    destination.fill('\0');
    std::copy(source.begin(), source.end(), destination.begin());
    return true;
}

inline std::string_view rack_preset_ui_name_view(
    const std::array<char, kRackPresetUiNameBytes>& source) noexcept
{
    std::size_t length = 0;
    while (length < source.size() && source[length] != '\0')
        ++length;
    return std::string_view(source.data(), length);
}

inline bool validate_rack_preset_ui_snapshot(const RackPresetUiSnapshot& snapshot) noexcept
{
    if (snapshot.version != kRackPresetUiSnapshotVersion || snapshot.generation == 0 ||
        snapshot.entry_count > kRackPresetUiMaxEntries)
        return false;

    for (std::uint32_t index = 0; index < snapshot.entry_count; ++index) {
        const RackPresetUiEntry& entry = snapshot.entries[index];
        if (!rack_preset_id_nonzero(entry.preset_id) ||
            !rack_preset_ui_name_valid(rack_preset_ui_name_view(entry.name)))
            return false;
        for (std::uint32_t prior = 0; prior < index; ++prior) {
            if (snapshot.entries[prior].preset_id == entry.preset_id)
                return false;
        }
    }

    if (rack_preset_id_nonzero(snapshot.active_preset_id)) {
        bool found = false;
        for (std::uint32_t index = 0; index < snapshot.entry_count; ++index) {
            if (snapshot.entries[index].preset_id == snapshot.active_preset_id) {
                found = true;
                break;
            }
        }
        if (!found ||
            !rack_preset_ui_name_valid(rack_preset_ui_name_view(snapshot.active_preset_name)))
            return false;
    } else if (!rack_preset_ui_name_view(snapshot.active_preset_name).empty()) {
        return false;
    }
    return true;
}

class RackPresetEditorModel {
public:
    bool has_snapshot() const noexcept { return has_snapshot_; }
    const RackPresetUiSnapshot& snapshot() const noexcept { return snapshot_; }
    bool pending_command() const noexcept { return pending_.command.command_id != 0; }
    const RackPresetUiCommand& pending() const noexcept { return pending_.command; }

    bool publish_snapshot(const RackPresetUiSnapshot& candidate) noexcept
    {
        if (!validate_rack_preset_ui_snapshot(candidate))
            return false;
        if (has_snapshot_ && candidate.generation <= snapshot_.generation)
            return false;

        snapshot_ = candidate;
        has_snapshot_ = true;
        if (pending_command() && pending_.accepted_generation != 0 &&
            snapshot_.generation >= pending_.accepted_generation)
            pending_ = {};
        return true;
    }

    RackPresetUiCommand request_save_as(std::string_view name) noexcept
    {
        if (!has_snapshot_ || pending_command() || !rack_preset_ui_name_valid(name))
            return {};
        RackPresetUiCommand command{};
        command.type = RackPresetUiCommandType::SaveAs;
        if (!rack_preset_ui_copy_name(command.name, name))
            return {};
        return begin_command(command);
    }

    RackPresetUiCommand request_load(const RackPresetId& preset_id) noexcept
    {
        if (!can_target(preset_id))
            return {};
        RackPresetUiCommand command{};
        command.type = RackPresetUiCommandType::Load;
        command.preset_id = preset_id;
        return begin_command(command);
    }

    RackPresetUiCommand request_rename(const RackPresetId& preset_id,
                                       std::string_view name) noexcept
    {
        if (!can_target(preset_id) || !rack_preset_ui_name_valid(name))
            return {};
        RackPresetUiCommand command{};
        command.type = RackPresetUiCommandType::Rename;
        command.preset_id = preset_id;
        if (!rack_preset_ui_copy_name(command.name, name))
            return {};
        return begin_command(command);
    }

    RackPresetUiCommand request_delete(const RackPresetId& preset_id) noexcept
    {
        if (!can_target(preset_id))
            return {};
        RackPresetUiCommand command{};
        command.type = RackPresetUiCommandType::Delete;
        command.preset_id = preset_id;
        return begin_command(command);
    }

    RackPresetUiCommand request_update(const RackPresetId& preset_id) noexcept
    {
        if (!can_target(preset_id))
            return {};
        RackPresetUiCommand command{};
        command.type = RackPresetUiCommandType::Update;
        command.preset_id = preset_id;
        return begin_command(command);
    }

    bool apply_ack(const RackPresetUiAck& ack) noexcept
    {
        if (!pending_command() || ack.command_id != pending_.command.command_id)
            return false;

        switch (ack.result) {
        case RackPresetUiCommandResult::Rejected:
        case RackPresetUiCommandResult::Failed:
            pending_ = {};
            return true;
        case RackPresetUiCommandResult::Accepted:
            if (ack.committed_generation == 0 ||
                ack.committed_generation <= pending_.requested_from_generation)
                return false;
            if (pending_.command.type == RackPresetUiCommandType::SaveAs) {
                if (!rack_preset_id_nonzero(ack.preset_id))
                    return false;
            } else if (rack_preset_id_nonzero(ack.preset_id) &&
                       ack.preset_id != pending_.command.preset_id) {
                return false;
            }
            if (has_snapshot_ && snapshot_.generation >= ack.committed_generation)
                pending_ = {};
            else
                pending_.accepted_generation = ack.committed_generation;
            return true;
        case RackPresetUiCommandResult::Idle:
            return false;
        }
        return false;
    }

private:
    struct PendingState {
        RackPresetUiCommand command{};
        std::uint64_t requested_from_generation = 0;
        std::uint64_t accepted_generation = 0;
    };

    bool can_target(const RackPresetId& preset_id) const noexcept
    {
        if (!has_snapshot_ || pending_command() || !rack_preset_id_nonzero(preset_id))
            return false;
        for (std::uint32_t index = 0; index < snapshot_.entry_count; ++index) {
            if (snapshot_.entries[index].preset_id == preset_id)
                return true;
        }
        return false;
    }

    RackPresetUiCommand begin_command(RackPresetUiCommand command) noexcept
    {
        command.command_id = next_command_id_++;
        if (command.command_id == 0)
            command.command_id = next_command_id_++;
        if (command.command_id == 0)
            return {};
        pending_.command = command;
        pending_.requested_from_generation = snapshot_.generation;
        pending_.accepted_generation = 0;
        return command;
    }

    RackPresetUiSnapshot snapshot_{};
    PendingState pending_{};
    RackPresetUiCommandId next_command_id_ = 1;
    bool has_snapshot_ = false;
};

} // namespace safevst3::rack::ui
