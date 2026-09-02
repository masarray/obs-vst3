#pragma once

#include "common/vst3_host_contract.hpp"

#include <cstdint>

namespace safevst3::rack {

// Independent Rack transport contract. Deliberately does not include or extend
// common/protocol.hpp, whose bytes/version remain the Single Host contract.
inline constexpr std::uint32_t kRackProtocolMagic = 0x33565352u; // "RSV3"
inline constexpr std::uint32_t kRackProtocolVersion = 2;

// R1-2 still owns exactly the two fixed tracer slots. Dynamic topology belongs
// to R1-3; these bits only describe whether the fixed A/B processors are skipped
// for the current bounded block transaction.
inline constexpr long kRackBypassSlotA = 1L << 0;
inline constexpr long kRackBypassSlotB = 1L << 1;
inline constexpr long kRackKnownBypassMask = kRackBypassSlotA | kRackBypassSlotB;

enum class RackHostStatus : long {
    Booting = 0,
    Ready = 1,
    Error = 2,
    ShuttingDown = 3,
};

enum class RackProcessResult : long {
    Ok = 0,
    InvalidBlock = 1,
    PluginAError = 2,
    PluginBError = 3,
};

struct alignas(64) RackSharedAudioRegion {
    std::uint32_t magic = kRackProtocolMagic;
    std::uint32_t version = kRackProtocolVersion;
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::uint32_t max_frames = kMaxFrames;
    std::uint32_t reserved0[3]{};

    volatile long host_status = static_cast<long>(RackHostStatus::Booting);
    volatile long shutdown_requested = 0;
    volatile long request_generation = 0;
    volatile long response_generation = 0;
    volatile long process_result = static_cast<long>(RackProcessResult::Ok);
    volatile long bypass_mask = 0;
    std::uint32_t total_latency_samples = 0;
    std::uint32_t sequence = 0;
    std::uint32_t frames = 0;
    std::uint32_t block_channels = 0;
    std::uint32_t reserved1[6]{};

    alignas(64) float input[kMaxChannels][kMaxFrames]{};
    alignas(64) float output[kMaxChannels][kMaxFrames]{};
};

static_assert(alignof(RackSharedAudioRegion) >= 64);
static_assert(kMaxChannels == 2, "R1 Rack tracer supports mono/stereo contract only");

} // namespace safevst3::rack
