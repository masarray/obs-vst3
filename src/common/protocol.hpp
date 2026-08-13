#pragma once

#include <cstddef>
#include <cstdint>

namespace safevst3 {

inline constexpr std::uint32_t kProtocolMagic = 0x3356534Fu; // "OSV3"
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxFrames = 2048;
inline constexpr std::uint32_t kSlotCount = 4;

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
    volatile long host_status = static_cast<long>(HostStatus::Booting);
    volatile long shutdown_requested = 0;
    volatile long last_error = 0;
    std::uint32_t reserved[7]{};
    alignas(64) AudioSlot slots[kSlotCount]{};
};

static_assert(alignof(SharedAudioRegion) >= 64);
static_assert(kMaxChannels == 2, "P0 intentionally supports mono/stereo only");

} // namespace safevst3
