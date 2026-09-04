#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Compile the exact production R3-2 helper/control implementation into this
// deterministic integration executable. Existing R3-1 separately proves the
// same helper as a child process through WinRackBridge; this test concentrates
// on real scanner -> catalog -> dynamic command -> DSP/lifetime behavior.
#include "rack/main_r3_2.cpp"

namespace {
namespace fs = std::filesystem;
using safevst3::rack::ui::RackPluginCatalogRecord;
using safevst3::rack::ui::RackUiCommand;
using safevst3::rack::ui::RackUiCommandResult;
using safevst3::rack::ui::RackUiCommandType;
using safevst3::rack::ui::RackVendorEditorManager;

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float actual, float expected) noexcept
{
    return std::fabs(actual - expected) <= 1.0e-5f;
}

class ScanRootsGuard {
public:
    explicit ScanRootsGuard(const std::wstring& roots)
    {
        const DWORD needed = GetEnvironmentVariableW(L"OBS_SAFE_VST3_SCAN_ROOTS", nullptr, 0);
        if (needed != 0) {
            old_.resize(needed);
            const DWORD copied = GetEnvironmentVariableW(
                L"OBS_SAFE_VST3_SCAN_ROOTS", old_.data(), needed);
            if (copied != 0 && !old_.empty() && old_.back() == L'\0')
                old_.pop_back();
            had_old_ = copied != 0;
        }
        SetEnvironmentVariableW(L"OBS_SAFE_VST3_SCAN_ROOTS", roots.c_str());
    }

    ~ScanRootsGuard()
    {
        if (had_old_)
            SetEnvironmentVariableW(L"OBS_SAFE_VST3_SCAN_ROOTS", old_.c_str());
        else
            SetEnvironmentVariableW(L"OBS_SAFE_VST3_SCAN_ROOTS", nullptr);
    }

private:
    std::wstring old_;
    bool had_old_ = false;
};

std::wstring scan_roots_for(const fs::path& a, const fs::path& b)
{
    const fs::path first = a.parent_path();
    const fs::path second = b.parent_path();
    if (first == second)
        return first.wstring();
    return first.wstring() + L";" + second.wstring();
}

bool run_audio_block(Endpoint& endpoint, RackSharedAudioRegion& region,
                     long request_generation, float input_value,
                     float expected_value, std::uint32_t expected_latency,
                     std::uint64_t expected_generation)
{
    constexpr std::uint32_t frames = 16;
    region.frames = frames;
    region.block_channels = 2;
    region.sequence = static_cast<std::uint64_t>(request_generation);
    for (std::uint32_t ch = 0; ch < 2; ++ch) {
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            region.input[ch][frame] = input_value;
    }

    MemoryBarrier();
    InterlockedExchange(&region.request_generation, request_generation);
    MemoryBarrier();
    SetEvent(endpoint.request);

    if (WaitForSingleObject(endpoint.response, 2000) != WAIT_OBJECT_0)
        return expect(false, "Rack DSP response must stay bounded");

    bool ok = true;
    ok &= expect(InterlockedCompareExchange(&region.response_generation, 0, 0) ==
                     request_generation,
                 "Rack DSP response must correlate request generation");
    ok &= expect(region.process_result == static_cast<long>(RackProcessResult::Ok),
                 "Rack DSP block must complete successfully");
    ok &= expect(region.total_latency_samples == expected_latency,
                 "Rack DSP latency must equal active non-bypassed chain latency");
    ok &= expect(static_cast<std::uint64_t>(InterlockedCompareExchange64(
                     reinterpret_cast<volatile LONG64*>(&region.processed_chain_generation), 0, 0)) ==
                     expected_generation,
                 "Rack DSP block must process one coherent immutable generation");
    for (std::uint32_t ch = 0; ch < 2; ++ch) {
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            ok &= expect(close_enough(region.output[ch][frame], expected_value),
                         "Rack DSP output must match deterministic serial fixture gain");
    }
    return ok;
}

RackUiCommand make_catalog_command(std::uint64_t id, RackUiCommandType type,
                                   std::uint64_t generation,
                                   std::uint64_t entry_id,
                                   std::uint32_t target_index = 0)
{
    RackUiCommand command{};
    command.command_id = id;
    command.type = type;
    command.catalog_generation = generation;
    command.catalog_entry_id = entry_id;
    command.target_index = target_index;
    return command;
}

bool run_test(const fs::path& scanner, const fs::path& fixture_a,
              const fs::path& fixture_b)
{
    bool ok = true;
    std::error_code ec;
    const fs::path temp_root = fs::temp_directory_path(ec) /
        (L"obs-safe-vst3-r3-2-product-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(temp_root, ec);
    fs::create_directories(temp_root, ec);
    const fs::path cache = temp_root / L"plugins.tsv";

    ScanRootsGuard roots(scan_roots_for(fixture_a, fixture_b));
    ok &= expect(safevst3::rack::ui::run_rack_scanner(scanner, cache),
                 "isolated production scanner must build deterministic Rack catalog cache");
    if (!ok) {
        fs::remove_all(temp_root, ec);
        return false;
    }

    CatalogRuntime catalog_runtime;
    PluginCatalogSnapshot catalog_snapshot{};
    std::vector<RackPluginCatalogRecord> records;
    {
        std::lock_guard lock(catalog_runtime.mutex);
        ok &= expect(catalog_runtime.catalog.load_cache(cache),
                     "production Rack catalog parser must accept scanner output");
        catalog_snapshot = catalog_runtime.catalog.snapshot();
        for (std::uint32_t i = 0; i < catalog_snapshot.entry_count; ++i) {
            const auto* record = catalog_runtime.catalog.resolve(
                catalog_snapshot.generation, catalog_snapshot.entries[i].entry_id);
            if (record)
                records.push_back(*record);
        }
    }
    ok &= expect(records.size() >= 2,
                 "scanner catalog must expose both deterministic VST3 fixture bundles");
    if (records.size() < 2) {
        fs::remove_all(temp_root, ec);
        return false;
    }

    auto second = std::find_if(records.begin() + 1, records.end(), [&](const auto& record) {
        return record.path != records.front().path;
    });
    ok &= expect(second != records.end(),
                 "scanner catalog must preserve distinct plug-in bundle identities");
    if (second == records.end()) {
        fs::remove_all(temp_root, ec);
        return false;
    }
    const RackPluginCatalogRecord record_a = records.front();
    const RackPluginCatalogRecord record_b = *second;

    RackSharedAudioRegion region{};
    region.magic = safevst3::rack::kRackProtocolMagic;
    region.version = safevst3::rack::kRackProtocolVersion;
    region.sample_rate = 48000;
    region.channels = 2;
    region.max_frames = kMaxFrames;
    region.host_status = static_cast<long>(RackHostStatus::Ready);
    region.process_result = static_cast<long>(RackProcessResult::Ok);

    Endpoint endpoint;
    endpoint.region = &region;
    endpoint.request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    endpoint.response = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ok &= expect(endpoint.request && endpoint.response,
                 "integration DSP transport events must be created");
    if (!endpoint.request || !endpoint.response) {
        endpoint.region = nullptr;
        fs::remove_all(temp_root, ec);
        return false;
    }

    GenerationStore store;
    initialize_empty_generation_store(store);
    DynamicRackState state;
    publish_dynamic_projection(region, state, store.generations[0].number);
    RackEditorWindow editor;
    RackVendorEditorManager vendor_editors;
    std::vector<RetiredDynamicPlugin> retired;
    bool topology_changed = false;
    std::uint64_t command_id = 1;

    std::thread dsp([&] { dsp_loop(endpoint, store); });

    RackUiCommand add_a = make_catalog_command(
        command_id++, RackUiCommandType::AddSlot,
        catalog_snapshot.generation, record_a.entry_id, 0);
    auto ack = execute_dynamic_command(add_a, state, store, region, catalog_runtime,
                                       editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted && topology_changed,
                 "AddSlot must open real HostedPlugin and publish generation");
    ok &= expect(state.slot_count == 1 && state.slots[0].plugin != nullptr,
                 "AddSlot must install one live logical slot");
    const RackSlotId first_slot_id = state.slots[0].id;
    const std::uint64_t add_a_generation = ack.committed_generation;
    ok &= run_audio_block(endpoint, region, 1, 1.0f, 0.5f, 64, add_a_generation);

    RackUiCommand add_b = make_catalog_command(
        command_id++, RackUiCommandType::AddSlot,
        catalog_snapshot.generation, record_b.entry_id, 0);
    ack = execute_dynamic_command(add_b, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted && state.slot_count == 2,
                 "second AddSlot must insert at requested index");
    const RackSlotId second_slot_id = state.slots[0].id;
    ok &= expect(state.slots[1].id == first_slot_id && second_slot_id != first_slot_id,
                 "insert must preserve first stable slot identity independent of index");
    ok &= run_audio_block(endpoint, region, 2, 1.0f, 0.25f, 128,
                          ack.committed_generation);

    RackUiCommand move{};
    move.command_id = command_id++;
    move.type = RackUiCommandType::MoveSlot;
    move.slot_id = first_slot_id;
    move.target_index = 0;
    ack = execute_dynamic_command(move, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted &&
                     state.slots[0].id == first_slot_id &&
                     state.slots[1].id == second_slot_id,
                 "MoveSlot must reorder real logical slots without changing stable IDs");

    RackUiCommand bypass{};
    bypass.command_id = command_id++;
    bypass.type = RackUiCommandType::SetBypass;
    bypass.slot_id = first_slot_id;
    bypass.bypass = true;
    ack = execute_dynamic_command(bypass, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted && state.slots[0].bypass,
                 "SetBypass must commit coherent logical bypass state");
    ok &= run_audio_block(endpoint, region, 3, 1.0f, 0.5f, 64,
                          ack.committed_generation);

    const std::string second_old_path = state.slots[1].path;
    RackUiCommand replace = make_catalog_command(
        command_id++, RackUiCommandType::ReplaceSlot,
        catalog_snapshot.generation, record_a.entry_id);
    replace.slot_id = second_slot_id;
    ack = execute_dynamic_command(replace, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted &&
                     state.slots[1].id == second_slot_id &&
                     state.slots[1].path != second_old_path,
                 "ReplaceSlot must preserve stable slot ID while replacing plug-in identity");
    ok &= run_audio_block(endpoint, region, 4, 1.0f, 0.5f, 64,
                          ack.committed_generation);

    const std::uint64_t before_failed_replace = ack.committed_generation;
    const std::string before_failed_path = state.slots[1].path;
    RackUiCommand failed_replace = make_catalog_command(
        command_id++, RackUiCommandType::ReplaceSlot,
        catalog_snapshot.generation + 99, record_b.entry_id);
    failed_replace.slot_id = second_slot_id;
    ack = execute_dynamic_command(failed_replace, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Rejected &&
                     ack.committed_generation == before_failed_replace &&
                     state.slots[1].path == before_failed_path,
                 "failed/stale ReplaceSlot must leave current generation and identity intact");

    const std::uint32_t old_generation_index =
        store.published_index.load(std::memory_order_acquire);
    store.readers[old_generation_index].fetch_add(1, std::memory_order_acq_rel);
    RackUiCommand remove{};
    remove.command_id = command_id++;
    remove.type = RackUiCommandType::RemoveSlot;
    remove.slot_id = second_slot_id;
    ack = execute_dynamic_command(remove, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted && state.slot_count == 1,
                 "RemoveSlot must publish new generation while old reader may still exist");
    ok &= expect(retired.size() == 1 && retired.front().plugin != nullptr,
                 "removed HostedPlugin must remain alive while old generation reader exists");
    store.readers[old_generation_index].fetch_sub(1, std::memory_order_release);
    reap_retired_plugins(store, retired);
    ok &= expect(retired.empty(),
                 "retired HostedPlugin must close only after old generation becomes unreachable");
    ok &= run_audio_block(endpoint, region, 5, 1.0f, 1.0f, 0,
                          ack.committed_generation);

    RackUiCommand enable{};
    enable.command_id = command_id++;
    enable.type = RackUiCommandType::SetBypass;
    enable.slot_id = first_slot_id;
    enable.bypass = false;
    ack = execute_dynamic_command(enable, state, store, region, catalog_runtime,
                                  editor, vendor_editors, retired, topology_changed);
    ok &= expect(ack.result == RackUiCommandResult::Accepted && !state.slots[0].bypass,
                 "Enable must restore active processing for same stable slot");
    ok &= run_audio_block(endpoint, region, 6, 1.0f, 0.5f, 64,
                          ack.committed_generation);

    InterlockedExchange(&region.shutdown_requested, 1);
    SetEvent(endpoint.request);
    if (dsp.joinable())
        dsp.join();
    vendor_editors.close_all();
    reap_retired_plugins(store, retired);
    close_dynamic_state(state, retired);
    endpoint.region = nullptr; // stack-backed region must not be UnmapViewOfFile'd.

    fs::remove_all(temp_root, ec);
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: r3-2-rack-product-integration <scanner.exe> <fixture-a.vst3> <fixture-b.vst3>\n";
        return 2;
    }
    if (!run_test(fs::path(argv[1]), fs::path(argv[2]), fs::path(argv[3])))
        return 1;
    std::cout << "R3-2 real scanner/catalog + dynamic Rack product integration passed\n";
    return 0;
}
