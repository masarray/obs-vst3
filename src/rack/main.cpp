#ifdef _WIN32

#include "host/hosted_plugin.hpp"
#include "host/process_block_view.hpp"
#include "rack/rack_editor_window.hpp"
#include "rack/rack_protocol.hpp"
#include "rack/rack_ui_contract.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {
using safevst3::HostedPlugin;
using safevst3::ProcessBlockView;
using safevst3::kMaxChannels;
using safevst3::kMaxFrames;
using safevst3::rack::RackBreadcrumbPhase;
using safevst3::rack::RackHostStatus;
using safevst3::rack::RackProcessResult;
using safevst3::rack::RackSharedAudioRegion;
using safevst3::rack::RackSlotId;
using safevst3::rack::RackTopologyResult;
using safevst3::rack::ui::RackEditorWindow;
using safevst3::rack::ui::RackUiCommand;
using safevst3::rack::ui::RackUiCommandAck;
using safevst3::rack::ui::RackUiCommandResult;
using safevst3::rack::ui::RackUiSlotHealth;
using safevst3::rack::ui::RackUiSnapshot;

struct Options {
    std::wstring mapping;
    std::wstring request_event;
    std::wstring response_event;
    std::wstring ready_event;
    std::wstring topology_request_event;
    std::wstring topology_response_event;
    std::wstring ui_open_event;
    std::string plugin_a;
    std::string plugin_b;
    std::string plugin_c;
    bool fixture_plugins_enabled = false;
    bool topology_enabled = false;
    bool ui_enabled = false;
};

std::string narrow(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

bool parse_options(int argc, wchar_t** argv, Options& options)
{
    auto take = [&](int& index, std::wstring& destination) {
        if (index + 1 >= argc)
            return false;
        destination = argv[++index];
        return true;
    };

    std::wstring plugin_a;
    std::wstring plugin_b;
    std::wstring plugin_c;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--mapping") {
            if (!take(i, options.mapping)) return false;
        } else if (arg == L"--request-event") {
            if (!take(i, options.request_event)) return false;
        } else if (arg == L"--response-event") {
            if (!take(i, options.response_event)) return false;
        } else if (arg == L"--ready-event") {
            if (!take(i, options.ready_event)) return false;
        } else if (arg == L"--topology-request-event") {
            if (!take(i, options.topology_request_event)) return false;
        } else if (arg == L"--topology-response-event") {
            if (!take(i, options.topology_response_event)) return false;
        } else if (arg == L"--ui-open-event") {
            if (!take(i, options.ui_open_event)) return false;
        } else if (arg == L"--plugin-a") {
            if (!take(i, plugin_a)) return false;
        } else if (arg == L"--plugin-b") {
            if (!take(i, plugin_b)) return false;
        } else if (arg == L"--plugin-c") {
            if (!take(i, plugin_c)) return false;
        } else {
            return false;
        }
    }

    if (options.mapping.empty() || options.request_event.empty() || options.response_event.empty() ||
        options.ready_event.empty())
        return false;

    const bool has_plugin_a = !plugin_a.empty();
    const bool has_plugin_b = !plugin_b.empty();
    if (has_plugin_a != has_plugin_b)
        return false;

    const bool any_topology = !options.topology_request_event.empty() ||
                              !options.topology_response_event.empty() || !plugin_c.empty();
    const bool all_topology = !options.topology_request_event.empty() &&
                              !options.topology_response_event.empty() && !plugin_c.empty();
    if (any_topology && !all_topology)
        return false;
    if (all_topology && !has_plugin_a)
        return false;

    options.plugin_a = narrow(plugin_a);
    options.plugin_b = narrow(plugin_b);
    options.plugin_c = narrow(plugin_c);
    options.fixture_plugins_enabled = has_plugin_a;
    options.topology_enabled = all_topology;
    options.ui_enabled = !options.ui_open_event.empty();
    return true;
}

struct Endpoint {
    HANDLE mapping = nullptr;
    HANDLE request = nullptr;
    HANDLE response = nullptr;
    HANDLE ready = nullptr;
    HANDLE topology_request = nullptr;
    HANDLE topology_response = nullptr;
    HANDLE ui_open = nullptr;
    RackSharedAudioRegion* region = nullptr;

    ~Endpoint()
    {
        if (region) UnmapViewOfFile(region);
        if (ui_open) CloseHandle(ui_open);
        if (topology_response) CloseHandle(topology_response);
        if (topology_request) CloseHandle(topology_request);
        if (ready) CloseHandle(ready);
        if (response) CloseHandle(response);
        if (request) CloseHandle(request);
        if (mapping) CloseHandle(mapping);
    }

    bool open(const Options& options)
    {
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, options.mapping.c_str());
        if (!mapping) return false;
        region = static_cast<RackSharedAudioRegion*>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RackSharedAudioRegion)));
        if (!region) return false;
        request = OpenEventW(SYNCHRONIZE, FALSE, options.request_event.c_str());
        response = OpenEventW(EVENT_MODIFY_STATE, FALSE, options.response_event.c_str());
        ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, options.ready_event.c_str());
        if (!request || !response || !ready)
            return false;
        if (options.topology_enabled) {
            topology_request = OpenEventW(SYNCHRONIZE, FALSE, options.topology_request_event.c_str());
            topology_response = OpenEventW(EVENT_MODIFY_STATE, FALSE,
                                           options.topology_response_event.c_str());
            if (!topology_request || !topology_response)
                return false;
        }
        if (options.ui_enabled) {
            ui_open = OpenEventW(SYNCHRONIZE, FALSE, options.ui_open_event.c_str());
            if (!ui_open)
                return false;
        }
        return true;
    }
};

struct RackBuffers {
    alignas(64) std::array<std::array<float, kMaxFrames>, kMaxChannels> ping{};
    alignas(64) std::array<std::array<float, kMaxFrames>, kMaxChannels> pong{};
};

struct RackGenerationSlot {
    RackSlotId id = 0;
    HostedPlugin* plugin = nullptr;
    std::uint32_t latency_samples = 0;
};

struct RackChainGeneration {
    std::uint64_t number = 0;
    std::uint32_t slot_count = 0;
    std::array<RackGenerationSlot, safevst3::rack::kRackMaxSlots> slots{};
};

struct GenerationStore {
    std::array<RackChainGeneration, 2> generations{};
    std::array<std::atomic<std::uint32_t>, 2> readers{};
    std::atomic<std::uint32_t> published_index{0};
};

void write_shared_generation(volatile std::int64_t& destination, std::uint64_t value) noexcept
{
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&destination), static_cast<LONG64>(value));
}

void publish_process_breadcrumb(RackSharedAudioRegion& region,
                                std::uint64_t chain_generation,
                                std::uint64_t sequence,
                                RackSlotId slot_id,
                                std::uint64_t dsp_progress) noexcept
{
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&region.breadcrumb_epoch));
    region.breadcrumb_chain_generation = static_cast<std::int64_t>(chain_generation);
    region.breadcrumb_audio_sequence = static_cast<std::int64_t>(sequence);
    region.breadcrumb_slot_id = slot_id;
    InterlockedExchange(&region.breadcrumb_phase, static_cast<long>(RackBreadcrumbPhase::Process));
    region.breadcrumb_dsp_progress = static_cast<std::int64_t>(dsp_progress);
    MemoryBarrier();
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&region.breadcrumb_epoch));
    MemoryBarrier();
}

void clear_rack_breadcrumb(RackSharedAudioRegion& region) noexcept
{
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&region.breadcrumb_epoch));
    region.breadcrumb_chain_generation = 0;
    region.breadcrumb_audio_sequence = 0;
    region.breadcrumb_slot_id = 0;
    InterlockedExchange(&region.breadcrumb_phase, static_cast<long>(RackBreadcrumbPhase::None));
    region.breadcrumb_dsp_progress = 0;
    MemoryBarrier();
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&region.breadcrumb_epoch));
    MemoryBarrier();
}

std::uint64_t advance_dsp_progress(RackSharedAudioRegion& region) noexcept
{
    return static_cast<std::uint64_t>(InterlockedIncrement64(
        reinterpret_cast<volatile LONG64*>(&region.dsp_progress_generation)));
}

void publish_result(RackSharedAudioRegion& region, HANDLE response, long request_generation,
                    RackProcessResult result) noexcept
{
    InterlockedExchange(&region.process_result, static_cast<long>(result));
    MemoryBarrier();
    InterlockedExchange(&region.response_generation, request_generation);
    MemoryBarrier();
    SetEvent(response);
}

void publish_topology_result(RackSharedAudioRegion& region, HANDLE response,
                             long request_generation, RackTopologyResult result) noexcept
{
    InterlockedExchange(&region.topology_result, static_cast<long>(result));
    MemoryBarrier();
    InterlockedExchange(&region.topology_response_generation, request_generation);
    MemoryBarrier();
    SetEvent(response);
}

void copy_original_dry(RackSharedAudioRegion& region,
                       std::uint32_t channels,
                       std::uint32_t frames) noexcept
{
    for (std::uint32_t ch = 0; ch < channels; ++ch) {
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            region.output[ch][frame] = region.input[ch][frame];
    }
}

void copy_current_output(RackSharedAudioRegion& region,
                         float* const* current,
                         std::uint32_t channels,
                         std::uint32_t frames) noexcept
{
    for (std::uint32_t ch = 0; ch < channels; ++ch) {
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            region.output[ch][frame] = current[ch][frame];
    }
}

bool is_known_slot_id(RackSlotId id) noexcept
{
    return id == safevst3::rack::kRackSlotIdA ||
           id == safevst3::rack::kRackSlotIdB ||
           id == safevst3::rack::kRackSlotIdC;
}

bool contains_slot(const std::array<RackSlotId, safevst3::rack::kRackMaxSlots>& ids,
                   std::uint32_t count, RackSlotId id) noexcept
{
    for (std::uint32_t i = 0; i < count; ++i) {
        if (ids[i] == id)
            return true;
    }
    return false;
}

HostedPlugin* plugin_for_slot(RackSlotId id, HostedPlugin& plugin_a, HostedPlugin& plugin_b,
                              HostedPlugin& plugin_c, bool plugin_c_loaded) noexcept
{
    if (id == safevst3::rack::kRackSlotIdA)
        return &plugin_a;
    if (id == safevst3::rack::kRackSlotIdB)
        return &plugin_b;
    if (id == safevst3::rack::kRackSlotIdC && plugin_c_loaded)
        return &plugin_c;
    return nullptr;
}

bool slot_bypassed(RackSlotId id, long bypass_mask) noexcept
{
    if (id == safevst3::rack::kRackSlotIdA)
        return (bypass_mask & safevst3::rack::kRackBypassSlotA) != 0;
    if (id == safevst3::rack::kRackSlotIdB)
        return (bypass_mask & safevst3::rack::kRackBypassSlotB) != 0;
    return false;
}

RackProcessResult process_error_for_slot(RackSlotId id) noexcept
{
    if (id == safevst3::rack::kRackSlotIdA)
        return RackProcessResult::PluginAError;
    if (id == safevst3::rack::kRackSlotIdB)
        return RackProcessResult::PluginBError;
    return RackProcessResult::PluginCError;
}

void initialize_empty_generation_store(GenerationStore& store) noexcept
{
    auto& initial = store.generations[0];
    initial = RackChainGeneration{};
    initial.number = 1;
    initial.slot_count = 0;
    store.generations[1] = RackChainGeneration{};
    store.readers[0].store(0, std::memory_order_relaxed);
    store.readers[1].store(0, std::memory_order_relaxed);
    store.published_index.store(0, std::memory_order_release);
}

void initialize_generation_store(GenerationStore& store, HostedPlugin& plugin_a,
                                 HostedPlugin& plugin_b) noexcept
{
    auto& initial = store.generations[0];
    initial.number = 1;
    initial.slot_count = 2;
    initial.slots[0] = {safevst3::rack::kRackSlotIdA, &plugin_a, plugin_a.latency_samples()};
    initial.slots[1] = {safevst3::rack::kRackSlotIdB, &plugin_b, plugin_b.latency_samples()};
    store.readers[0].store(0, std::memory_order_relaxed);
    store.readers[1].store(0, std::memory_order_relaxed);
    store.published_index.store(0, std::memory_order_release);
}

void publish_committed_projection(RackSharedAudioRegion& region,
                                  const RackChainGeneration& generation) noexcept
{
    region.committed_slot_count = generation.slot_count;
    for (std::uint32_t i = 0; i < safevst3::rack::kRackMaxSlots; ++i)
        region.committed_slot_ids[i] = i < generation.slot_count ? generation.slots[i].id : 0;
    MemoryBarrier();
    write_shared_generation(region.committed_chain_generation, generation.number);
    MemoryBarrier();
}

template <std::size_t N>
void copy_ui_text(std::array<char, N>& destination, const std::string& source) noexcept
{
    destination.fill('\0');
    if constexpr (N > 1) {
        const std::size_t count = std::min<std::size_t>(source.size(), N - 1);
        std::copy_n(source.data(), count, destination.data());
    }
}

RackUiSnapshot build_ui_snapshot(const RackChainGeneration& generation,
                                 long bypass_mask) noexcept
{
    RackUiSnapshot snapshot{};
    snapshot.generation = generation.number;
    snapshot.slot_count = generation.slot_count;
    copy_ui_text(snapshot.rack_name, "VST3 Rack");
    for (std::uint32_t i = 0; i < generation.slot_count; ++i) {
        const RackGenerationSlot& source = generation.slots[i];
        auto& destination = snapshot.slots[i];
        destination.slot_id = source.id;
        destination.latency_samples = source.latency_samples;
        destination.bypass = slot_bypassed(source.id, bypass_mask);
        destination.health = destination.bypass ? RackUiSlotHealth::Bypassed : RackUiSlotHealth::Ready;
        if (!destination.bypass)
            snapshot.total_latency_samples += source.latency_samples;
        if (source.plugin)
            copy_ui_text(destination.plugin_name, source.plugin->plugin_name());
    }
    return snapshot;
}

void publish_editor_snapshot(RackEditorWindow* editor,
                             const RackChainGeneration& generation,
                             RackSharedAudioRegion& region) noexcept
{
    if (!editor)
        return;
    const long bypass_mask = InterlockedCompareExchange(&region.bypass_mask, 0, 0);
    editor->publish_snapshot(build_ui_snapshot(generation, bypass_mask));
}

bool wait_until_generation_unreachable(GenerationStore& store, std::uint32_t index,
                                       RackSharedAudioRegion& region) noexcept
{
    constexpr std::uint32_t kMaxWaitIterations = 1000;
    for (std::uint32_t i = 0; i < kMaxWaitIterations; ++i) {
        if (store.readers[index].load(std::memory_order_acquire) == 0)
            return true;
        if (InterlockedCompareExchange(&region.shutdown_requested, 0, 0) != 0)
            return false;
        Sleep(1);
    }
    return false;
}

bool acquire_published_generation(GenerationStore& store, std::uint32_t& index,
                                  const RackChainGeneration*& generation) noexcept
{
    constexpr std::uint32_t kMaxAcquireAttempts = 8;
    for (std::uint32_t attempt = 0; attempt < kMaxAcquireAttempts; ++attempt) {
        index = store.published_index.load(std::memory_order_acquire);
        store.readers[index].fetch_add(1, std::memory_order_acq_rel);
        if (store.published_index.load(std::memory_order_acquire) == index) {
            generation = &store.generations[index];
            return true;
        }
        store.readers[index].fetch_sub(1, std::memory_order_release);
    }
    generation = nullptr;
    return false;
}

void release_generation(GenerationStore& store, std::uint32_t index) noexcept
{
    store.readers[index].fetch_sub(1, std::memory_order_release);
}

void control_loop(Endpoint& endpoint, const Options& options, GenerationStore& store,
                  HostedPlugin& plugin_a, HostedPlugin& plugin_b, HostedPlugin& plugin_c,
                  bool& plugin_c_loaded, RackEditorWindow* editor) noexcept
{
    std::uint64_t next_chain_generation = 2;
    for (;;) {
        const DWORD wait = WaitForSingleObject(endpoint.topology_request, 100);
        if (InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) != 0)
            break;
        if (wait != WAIT_OBJECT_0)
            continue;

        const long request_generation = InterlockedCompareExchange(
            &endpoint.region->topology_request_generation, 0, 0);
        const std::uint32_t count = endpoint.region->topology_requested_slot_count;
        std::array<RackSlotId, safevst3::rack::kRackMaxSlots> ids{};
        bool valid = count <= safevst3::rack::kRackMaxSlots;
        if (valid) {
            for (std::uint32_t i = 0; i < count; ++i) {
                ids[i] = endpoint.region->topology_requested_slot_ids[i];
                if (!is_known_slot_id(ids[i]) || contains_slot(ids, i, ids[i])) {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid) {
            publish_topology_result(*endpoint.region, endpoint.topology_response, request_generation,
                                    RackTopologyResult::InvalidRequest);
            continue;
        }

        if (contains_slot(ids, count, safevst3::rack::kRackSlotIdC) && !plugin_c_loaded) {
            std::string error;
            if (!plugin_c.open(options.plugin_c, "", endpoint.region->sample_rate,
                               endpoint.region->channels, nullptr, error)) {
                std::cerr << "R1-3 slot C open failed: " << error << '\n';
                publish_topology_result(*endpoint.region, endpoint.topology_response,
                                        request_generation, RackTopologyResult::LoadFailed);
                continue;
            }
            plugin_c_loaded = true;
        }

        const std::uint32_t current_index = store.published_index.load(std::memory_order_acquire);
        const std::uint32_t next_index = current_index ^ 1u;
        if (!wait_until_generation_unreachable(store, next_index, *endpoint.region)) {
            publish_topology_result(*endpoint.region, endpoint.topology_response, request_generation,
                                    RackTopologyResult::GenerationBusy);
            continue;
        }

        RackChainGeneration& next = store.generations[next_index];
        next = RackChainGeneration{};
        next.number = next_chain_generation++;
        next.slot_count = count;
        bool resolved = true;
        for (std::uint32_t i = 0; i < count; ++i) {
            HostedPlugin* plugin = plugin_for_slot(ids[i], plugin_a, plugin_b, plugin_c,
                                                   plugin_c_loaded);
            if (!plugin) {
                resolved = false;
                break;
            }
            next.slots[i] = {ids[i], plugin, plugin->latency_samples()};
        }
        if (!resolved) {
            publish_topology_result(*endpoint.region, endpoint.topology_response, request_generation,
                                    RackTopologyResult::InvalidRequest);
            continue;
        }

        store.published_index.store(next_index, std::memory_order_release);
        publish_committed_projection(*endpoint.region, next);
        publish_editor_snapshot(editor, next, *endpoint.region);
        publish_topology_result(*endpoint.region, endpoint.topology_response, request_generation,
                                RackTopologyResult::Ok);
    }
}

void ui_open_loop(Endpoint& endpoint, RackEditorWindow& editor) noexcept
{
    for (;;) {
        const DWORD wait = WaitForSingleObject(endpoint.ui_open, 100);
        if (InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) != 0)
            break;
        if (wait == WAIT_OBJECT_0)
            editor.open_or_foreground();
    }
}

void dsp_loop(Endpoint& endpoint, GenerationStore& store) noexcept
{
    RackBuffers buffers;
    float* rack_input[kMaxChannels]{};
    float* ping[kMaxChannels]{};
    float* pong[kMaxChannels]{};

    for (std::uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        rack_input[ch] = endpoint.region->input[ch];
        ping[ch] = buffers.ping[ch].data();
        pong[ch] = buffers.pong[ch].data();
    }

    for (;;) {
        const DWORD wait = WaitForSingleObject(endpoint.request, 100);
        if (InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) != 0)
            break;
        if (wait != WAIT_OBJECT_0)
            continue;

        const long request_generation = InterlockedCompareExchange(
            &endpoint.region->request_generation, 0, 0);
        const std::uint32_t frames = endpoint.region->frames;
        const std::uint32_t channels = endpoint.region->block_channels;
        const std::uint64_t sequence = endpoint.region->sequence;
        const long bypass_mask = InterlockedCompareExchange(&endpoint.region->bypass_mask, 0, 0);
        if (frames == 0 || frames > kMaxFrames || channels == 0 || channels > kMaxChannels ||
            channels != endpoint.region->channels ||
            (bypass_mask & ~safevst3::rack::kRackKnownBypassMask) != 0) {
            endpoint.region->total_latency_samples = 0;
            write_shared_generation(endpoint.region->processed_chain_generation, 0);
            copy_original_dry(*endpoint.region, channels <= kMaxChannels ? channels : 0,
                              frames <= kMaxFrames ? frames : 0);
            publish_result(*endpoint.region, endpoint.response, request_generation,
                           RackProcessResult::InvalidBlock);
            continue;
        }

        std::uint32_t generation_index = 0;
        const RackChainGeneration* generation = nullptr;
        if (!acquire_published_generation(store, generation_index, generation)) {
            endpoint.region->total_latency_samples = 0;
            write_shared_generation(endpoint.region->processed_chain_generation, 0);
            copy_original_dry(*endpoint.region, channels, frames);
            publish_result(*endpoint.region, endpoint.response, request_generation,
                           RackProcessResult::InvalidBlock);
            continue;
        }

        write_shared_generation(endpoint.region->processed_chain_generation, generation->number);

        // Latency metadata is a property of the complete active immutable
        // generation for this block, not of how far processing happened to get
        // before a vendor failure. Calculate it first in one bounded pre-pass.
        std::uint32_t total_latency = 0;
        for (std::uint32_t slot_index = 0; slot_index < generation->slot_count; ++slot_index) {
            const RackGenerationSlot& slot = generation->slots[slot_index];
            if (!slot_bypassed(slot.id, bypass_mask))
                total_latency += slot.latency_samples;
        }
        endpoint.region->total_latency_samples = total_latency;

        float** current = rack_input;
        bool block_ok = true;
        RackProcessResult block_result = RackProcessResult::Ok;

        for (std::uint32_t slot_index = 0; slot_index < generation->slot_count; ++slot_index) {
            const RackGenerationSlot& slot = generation->slots[slot_index];
            if (slot_bypassed(slot.id, bypass_mask))
                continue;

            float** next_output = current == ping ? pong : ping;
            ProcessBlockView block{current, next_output, channels, frames, sequence};
            const std::uint64_t dsp_progress = advance_dsp_progress(*endpoint.region);
            publish_process_breadcrumb(*endpoint.region, generation->number, sequence,
                                       slot.id, dsp_progress);
            const bool slot_ok = slot.plugin->process(block);
            clear_rack_breadcrumb(*endpoint.region);
            if (!slot_ok) {
                block_ok = false;
                block_result = process_error_for_slot(slot.id);
                break;
            }
            current = next_output;
        }

        if (!block_ok) {
            copy_original_dry(*endpoint.region, channels, frames);
            release_generation(store, generation_index);
            publish_result(*endpoint.region, endpoint.response, request_generation, block_result);
            continue;
        }

        clear_rack_breadcrumb(*endpoint.region);
        copy_current_output(*endpoint.region, current, channels, frames);
        release_generation(store, generation_index);
        publish_result(*endpoint.region, endpoint.response, request_generation, RackProcessResult::Ok);
    }
}

bool open_required_plugin(HostedPlugin& plugin, const std::string& path,
                          RackSharedAudioRegion& region, const char* label)
{
    std::string error;
    if (plugin.open(path, "", region.sample_rate, region.channels, nullptr, error))
        return true;
    std::cerr << label << " open failed: " << error << '\n';
    return false;
}

int run(const Options& options)
{
    Endpoint endpoint;
    if (!endpoint.open(options)) {
        std::cerr << "R3-1 Rack helper could not open transport\n";
        return 3;
    }
    if (endpoint.region->magic != safevst3::rack::kRackProtocolMagic ||
        endpoint.region->version != safevst3::rack::kRackProtocolVersion ||
        endpoint.region->sample_rate == 0 || endpoint.region->channels == 0 ||
        endpoint.region->channels > kMaxChannels) {
        InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
        SetEvent(endpoint.ready);
        return 4;
    }

    clear_rack_breadcrumb(*endpoint.region);

    HostedPlugin plugin_a;
    HostedPlugin plugin_b;
    HostedPlugin plugin_c;
    bool plugin_c_loaded = false;
    if (options.fixture_plugins_enabled) {
        if (!open_required_plugin(plugin_a, options.plugin_a, *endpoint.region, "Gain A")) {
            InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
            SetEvent(endpoint.ready);
            return 5;
        }
        if (!open_required_plugin(plugin_b, options.plugin_b, *endpoint.region, "Gain B")) {
            InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
            SetEvent(endpoint.ready);
            plugin_a.close();
            return 6;
        }
    }

    GenerationStore store;
    if (options.fixture_plugins_enabled)
        initialize_generation_store(store, plugin_a, plugin_b);
    else
        initialize_empty_generation_store(store);
    publish_committed_projection(*endpoint.region, store.generations[0]);

    RackEditorWindow editor([&](const RackUiCommand& command) {
        RackUiCommandAck ack{};
        ack.command_id = command.command_id;
        // R3-0 proves command correlation only. Topology mutation from graphical
        // MoveSlot belongs to R3-2, so the real helper rejects it without
        // changing the authoritative generation.
        ack.result = RackUiCommandResult::Rejected;
        ack.committed_generation = static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(&endpoint.region->committed_chain_generation), 0, 0));
        return ack;
    });
    publish_editor_snapshot(&editor, store.generations[0], *endpoint.region);

    InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Ready));
    MemoryBarrier();
    SetEvent(endpoint.ready);

    std::thread dsp([&] { dsp_loop(endpoint, store); });
    std::thread control;
    if (options.topology_enabled) {
        control = std::thread([&] {
            control_loop(endpoint, options, store, plugin_a, plugin_b, plugin_c, plugin_c_loaded,
                         &editor);
        });
    }
    std::thread ui_control;
    if (options.ui_enabled)
        ui_control = std::thread([&] { ui_open_loop(endpoint, editor); });

    dsp.join();
    if (control.joinable())
        control.join();
    if (ui_control.joinable())
        ui_control.join();

    InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::ShuttingDown));
    editor.shutdown();
    if (plugin_c_loaded)
        plugin_c.close();
    if (options.fixture_plugins_enabled) {
        plugin_b.close();
        plugin_a.close();
    }
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "usage: obs-safe-vst3-rack-host --mapping <name> --request-event <name> "
                     "--response-event <name> --ready-event <name> "
                     "[--plugin-a <vst3> --plugin-b <vst3>] "
                     "[--topology-request-event <name> --topology-response-event <name> --plugin-c <vst3>] "
                     "[--ui-open-event <name>]\n";
        return 2;
    }
    return run(options);
}

#else
int main() { return 0; }
#endif