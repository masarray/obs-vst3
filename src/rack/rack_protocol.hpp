#pragma once

#include "common/vst3_host_contract.hpp"

#include <cstdint>

namespace safevst3::rack {

// Independent Rack transport contract. Deliberately does not include or extend
// common/protocol.hpp, whose bytes/version remain the Single Host contract.
inline constexpr std::uint32_t kRackProtocolMagic = 0x33565352u; // "RSV3"
inline constexpr std::uint32_t kRackProtocolVersion = 4;

using RackSlotId = std::uint64_t;
inline constexpr std::uint32_t kRackMaxSlots = 8;

// Stable IDs for the bounded R1 tracer runtimes. Order is carried separately;
// reorder never changes identity. Later production control may allocate IDs,
// but the generation/DSP contract already treats them as opaque stable values.
inline constexpr RackSlotId kRackSlotIdA = 0xA001u;
inline constexpr RackSlotId kRackSlotIdB = 0xB002u;
inline constexpr RackSlotId kRackSlotIdC = 0xC003u;

// R1-2 bypass bits remain keyed to stable A/B tracer identity. R1-3 changes
// topology/order only; broader slot-control encoding belongs to later control UI.
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
    PluginCError = 4,
};

enum class RackTopologyResult : long {
    Idle = 0,
    Ok = 1,
    InvalidRequest = 2,
    LoadFailed = 3,
    GenerationBusy = 4,
};

// OBS save uses a separate named-event handshake from realtime Rack processing.
// Completion and outcome are intentionally distinct: the helper always signals
// completion, while this status lets the OBS control callback report an actual
// capture/write failure immediately instead of misclassifying it as a timeout.
enum class RackSessionSaveResult : long {
    Idle = 0,
    Ok = 1,
    Failed = 2,
};

enum class RackBreadcrumbPhase : long {
    None = 0,
    Load = 1,
    RestoreState = 2,
    Process = 3,
    CaptureState = 4,
    OpenEditor = 5,
    CloseEditor = 6,
    LifecycleRestart = 7,
    RackUiControl = 8,
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

    // Bounded audio request/response transaction.
    volatile long request_generation = 0;
    volatile long response_generation = 0;
    volatile long process_result = static_cast<long>(RackProcessResult::Ok);
    volatile long bypass_mask = 0;
    std::uint32_t total_latency_samples = 0;
    std::uint32_t sequence = 0;
    std::uint32_t frames = 0;
    std::uint32_t block_channels = 0;
    volatile long session_save_result = static_cast<long>(RackSessionSaveResult::Idle);
    volatile std::int64_t processed_chain_generation = 0;

    // R1-3 topology transaction. The requester writes one bounded ordered list
    // of stable IDs, then advances topology_request_generation and signals the
    // topology event. The helper publishes only a completely built generation.
    volatile long topology_request_generation = 0;
    volatile long topology_response_generation = 0;
    volatile long topology_result = static_cast<long>(RackTopologyResult::Idle);
    std::uint32_t topology_requested_slot_count = 0;
    RackSlotId topology_requested_slot_ids[kRackMaxSlots]{};

    // Coherent control-plane projection of the currently published generation.
    volatile std::int64_t committed_chain_generation = 0;
    std::uint32_t committed_slot_count = 0;
    std::uint32_t reserved2 = 0;
    RackSlotId committed_slot_ids[kRackMaxSlots]{};

    // R1-4 diagnostic breadcrumb. breadcrumb_epoch is a tiny seqlock: odd
    // means a writer is mutating the fields, even means a coherent snapshot.
    // A process crash after the even publication leaves the parent enough
    // evidence to identify the work that was active without claiming proof of
    // plug-in guilt. The active breadcrumb is cleared after vendor work returns.
    volatile std::int64_t breadcrumb_epoch = 0;
    volatile std::int64_t breadcrumb_chain_generation = 0;
    volatile std::int64_t breadcrumb_audio_sequence = 0;
    RackSlotId breadcrumb_slot_id = 0;
    volatile long breadcrumb_phase = static_cast<long>(RackBreadcrumbPhase::None);
    std::uint32_t reserved3 = 0;
    volatile std::int64_t breadcrumb_dsp_progress = 0;
    volatile std::int64_t dsp_progress_generation = 0;

    alignas(64) float input[kMaxChannels][kMaxFrames]{};
    alignas(64) float output[kMaxChannels][kMaxFrames]{};
};

static_assert(alignof(RackSharedAudioRegion) >= 64);
static_assert(kMaxChannels == 2, "R1 Rack tracer supports mono/stereo contract only");
static_assert(kRackMaxSlots == 8, "Rack v2 qualification bound is eight serial slots");

} // namespace safevst3::rack
