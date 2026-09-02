#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace safevst3::rack::ui {

inline constexpr std::uint32_t kRackUiSnapshotVersion = 1;
inline constexpr std::uint32_t kRackUiMaxSlots = 8;
inline constexpr std::size_t kRackUiNameBytes = 96;
inline constexpr std::size_t kRackUiVendorBytes = 64;

using RackUiSlotId = std::uint64_t;
using RackUiCommandId = std::uint64_t;

enum class RackUiSlotHealth : std::uint32_t {
    Ready = 0,
    Bypassed = 1,
    Loading = 2,
    Missing = 3,
    Recovering = 4,
    NeedsAttention = 5,
    Quarantined = 6,
};

enum class RackUiCommandType : std::uint32_t {
    None = 0,
    OpenRack = 1,
    MoveSlot = 2,
};

enum class RackUiCommandResult : std::uint32_t {
    Idle = 0,
    Accepted = 1,
    Rejected = 2,
    Failed = 3,
};

struct RackUiSlotSnapshot {
    RackUiSlotId slot_id = 0;
    std::uint32_t latency_samples = 0;
    RackUiSlotHealth health = RackUiSlotHealth::Ready;
    bool bypass = false;
    std::array<char, kRackUiNameBytes> plugin_name{};
    std::array<char, kRackUiVendorBytes> vendor{};
};

struct RackUiSnapshot {
    std::uint32_t version = kRackUiSnapshotVersion;
    std::uint64_t generation = 0;
    std::uint32_t total_latency_samples = 0;
    std::uint32_t slot_count = 0;
    std::array<char, kRackUiNameBytes> rack_name{};
    std::array<RackUiSlotSnapshot, kRackUiMaxSlots> slots{};
};

struct RackUiCommand {
    RackUiCommandId command_id = 0;
    RackUiCommandType type = RackUiCommandType::None;
    RackUiSlotId slot_id = 0;
    std::uint32_t target_index = 0;
};

struct RackUiCommandAck {
    RackUiCommandId command_id = 0;
    RackUiCommandResult result = RackUiCommandResult::Idle;
    std::uint64_t committed_generation = 0;
};

inline bool rack_ui_health_valid(RackUiSlotHealth health) noexcept
{
    switch (health) {
    case RackUiSlotHealth::Ready:
    case RackUiSlotHealth::Bypassed:
    case RackUiSlotHealth::Loading:
    case RackUiSlotHealth::Missing:
    case RackUiSlotHealth::Recovering:
    case RackUiSlotHealth::NeedsAttention:
    case RackUiSlotHealth::Quarantined:
        return true;
    }
    return false;
}

inline bool validate_rack_ui_snapshot(const RackUiSnapshot& snapshot) noexcept
{
    if (snapshot.version != kRackUiSnapshotVersion || snapshot.generation == 0 ||
        snapshot.slot_count > kRackUiMaxSlots)
        return false;

    for (std::uint32_t i = 0; i < snapshot.slot_count; ++i) {
        const RackUiSlotSnapshot& slot = snapshot.slots[i];
        if (slot.slot_id == 0 || !rack_ui_health_valid(slot.health))
            return false;
        for (std::uint32_t prior = 0; prior < i; ++prior) {
            if (snapshot.slots[prior].slot_id == slot.slot_id)
                return false;
        }
    }
    return true;
}

class RackEditorModel {
public:
    bool has_snapshot() const noexcept { return has_snapshot_; }
    const RackUiSnapshot& snapshot() const noexcept { return snapshot_; }
    bool pending_command() const noexcept { return pending_.command.command_id != 0; }
    const RackUiCommand& pending() const noexcept { return pending_.command; }

    bool publish_snapshot(const RackUiSnapshot& candidate) noexcept
    {
        if (!validate_rack_ui_snapshot(candidate))
            return false;
        if (has_snapshot_ && candidate.generation < snapshot_.generation)
            return false;

        snapshot_ = candidate;
        has_snapshot_ = true;

        if (pending_command() && pending_.accepted_generation != 0 &&
            snapshot_.generation >= pending_.accepted_generation)
            pending_ = {};
        return true;
    }

    RackUiCommand request_move(RackUiSlotId slot_id, std::uint32_t target_index) noexcept
    {
        if (!has_snapshot_ || pending_command() || slot_id == 0 ||
            target_index >= snapshot_.slot_count)
            return {};

        std::uint32_t current_index = snapshot_.slot_count;
        for (std::uint32_t i = 0; i < snapshot_.slot_count; ++i) {
            if (snapshot_.slots[i].slot_id == slot_id) {
                current_index = i;
                break;
            }
        }
        if (current_index == snapshot_.slot_count || current_index == target_index)
            return {};

        RackUiCommand command{};
        command.command_id = next_command_id_++;
        if (command.command_id == 0)
            command.command_id = next_command_id_++;
        command.type = RackUiCommandType::MoveSlot;
        command.slot_id = slot_id;
        command.target_index = target_index;
        pending_.command = command;
        pending_.accepted_generation = 0;
        return command;
    }

    bool apply_ack(const RackUiCommandAck& ack) noexcept
    {
        if (!pending_command() || ack.command_id != pending_.command.command_id)
            return false;

        switch (ack.result) {
        case RackUiCommandResult::Rejected:
        case RackUiCommandResult::Failed:
            pending_ = {};
            return true;
        case RackUiCommandResult::Accepted:
            if (ack.committed_generation == 0)
                return false;
            if (has_snapshot_ && snapshot_.generation >= ack.committed_generation)
                pending_ = {};
            else
                pending_.accepted_generation = ack.committed_generation;
            return true;
        case RackUiCommandResult::Idle:
            return false;
        }
        return false;
    }

private:
    struct PendingState {
        RackUiCommand command{};
        std::uint64_t accepted_generation = 0;
    };

    RackUiSnapshot snapshot_{};
    PendingState pending_{};
    RackUiCommandId next_command_id_ = 1;
    bool has_snapshot_ = false;
};

} // namespace safevst3::rack::ui
