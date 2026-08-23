#pragma once

#include <cstddef>
#include <cstdint>

namespace safevst3 {

inline constexpr std::uint32_t kProtocolMagic = 0x3356534Fu; // "OSV3"
inline constexpr std::uint32_t kProtocolVersion = 4;
inline constexpr std::uint32_t kStateTransferMagic = 0x3154534Fu; // "OST1"
inline constexpr std::uint32_t kStateTransferVersion = 1;
inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxFrames = 2048;
inline constexpr std::uint32_t kSlotCount = 4;
inline constexpr std::uint32_t kMaxParameters = 256;
inline constexpr std::size_t kParameterTitleBytes = 64;
inline constexpr std::size_t kParameterUnitsBytes = 32;
inline constexpr std::size_t kPluginNameBytes = 128;
inline constexpr std::size_t kMaxStateBytes = 16u * 1024u * 1024u;

enum class SlotState : long {
    Free = 0,
    Claimed = 1,
    Ready = 2,
    Processing = 3,
    Done = 4,
};

enum class HostStatus : long {
    Booting = 0,
    Ready = 1,
    Error = 2,
    ShuttingDown = 3,
};

enum class ProcessResult : long {
    Ok = 0,
    VstProcessError = 1,
    InvalidBlock = 2,
};

enum class EditorCommand : long {
    None = 0,
    Open = 1,
    Hide = 2,
};

enum class EditorStatus : long {
    Unknown = 0,
    Closed = 1,
    Open = 2,
    Unsupported = 3,
    Error = 4,
};

enum class StateCommand : long {
    None = 0,
    Capture = 1,
    Restore = 2,
};

enum class StateStatus : long {
    Idle = 0,
    Ok = 1,
    TooLarge = 2,
    Invalid = 3,
    VstError = 4,
};

enum ParameterFlags : std::uint32_t {
    ParameterCanAutomate = 1u << 0,
    ParameterReadOnly = 1u << 1,
    ParameterHidden = 1u << 2,
    ParameterList = 1u << 3,
    ParameterProgramChange = 1u << 4,
    ParameterBypass = 1u << 5,
};

struct alignas(64) ParameterDescriptor {
    std::uint32_t id = 0;
    std::int32_t step_count = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved0 = 0;
    double default_normalized = 0.0;
    volatile std::int64_t current_value_bits = 0;
    volatile std::int64_t pending_value_bits = 0;
    volatile long pending_generation = 0;
    volatile long applied_generation = 0;
    char title[kParameterTitleBytes]{};
    char units[kParameterUnitsBytes]{};
    std::uint32_t reserved[4]{};
};

struct alignas(64) AudioSlot {
    volatile long state = static_cast<long>(SlotState::Free);
    std::uint32_t sequence = 0;
    std::uint32_t frames = 0;
    std::uint32_t channels = 0;
    volatile long result = static_cast<long>(ProcessResult::Ok);
    std::uint32_t reserved[10]{};
    alignas(64) float input[kMaxChannels][kMaxFrames]{};
    alignas(64) float output[kMaxChannels][kMaxFrames]{};
};

struct alignas(64) SharedAudioRegion {
    std::uint32_t magic = kProtocolMagic;
    std::uint32_t version = kProtocolVersion;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::uint32_t max_frames = kMaxFrames;
    std::uint32_t slot_count = kSlotCount;
    std::uint32_t parameter_count = 0;
    std::uint32_t parameter_total_count = 0;
    volatile long host_status = static_cast<long>(HostStatus::Booting);
    volatile long shutdown_requested = 0;
    volatile long last_error = 0;
    volatile long editor_command = static_cast<long>(EditorCommand::None);
    volatile long editor_request_generation = 0;
    volatile long editor_applied_generation = 0;
    volatile long editor_status = static_cast<long>(EditorStatus::Unknown);
    volatile long state_command = static_cast<long>(StateCommand::None);
    volatile long state_request_generation = 0;
    volatile long state_applied_generation = 0;
    volatile long state_status = static_cast<long>(StateStatus::Idle);
    volatile long state_dirty_generation = 0;
    std::uint32_t state_component_bytes = 0;
    std::uint32_t state_controller_bytes = 0;
    std::uint32_t latency_samples = 0;
    char plugin_name[kPluginNameBytes]{};
    alignas(64) ParameterDescriptor parameters[kMaxParameters]{};
    alignas(64) AudioSlot slots[kSlotCount]{};
};

struct alignas(64) StateTransferRegion {
    std::uint32_t magic = kStateTransferMagic;
    std::uint32_t version = kStateTransferVersion;
    std::uint32_t capacity = static_cast<std::uint32_t>(kMaxStateBytes);
    std::uint32_t reserved = 0;
    alignas(64) std::uint8_t payload[kMaxStateBytes]{};
};

static_assert(alignof(ParameterDescriptor) >= 64);
static_assert(alignof(SharedAudioRegion) >= 64);
static_assert(alignof(StateTransferRegion) >= 64);
static_assert(kMaxChannels == 2, "Public preview intentionally supports mono/stereo only");

} // namespace safevst3
