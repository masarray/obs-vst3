#include "rack/rack_protocol.hpp"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
using safevst3::rack::RackHostStatus;
using safevst3::rack::RackProcessResult;
using safevst3::rack::RackSharedAudioRegion;
using safevst3::rack::RackSlotId;
using safevst3::rack::RackTopologyResult;

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float a, float b) { return std::fabs(a - b) <= 1.0e-6f; }

std::wstring widen(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring quote(const std::wstring& value) { return L"\"" + value + L"\""; }

struct Handles {
    HANDLE mapping = nullptr;
    HANDLE audio_request = nullptr;
    HANDLE audio_response = nullptr;
    HANDLE ready = nullptr;
    HANDLE topology_request = nullptr;
    HANDLE topology_response = nullptr;
    HANDLE a_entered = nullptr;
    HANDLE c_loading = nullptr;
    PROCESS_INFORMATION process{};
    RackSharedAudioRegion* region = nullptr;

    ~Handles()
    {
        if (region) UnmapViewOfFile(region);
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) CloseHandle(process.hProcess);
        if (c_loading) CloseHandle(c_loading);
        if (a_entered) CloseHandle(a_entered);
        if (topology_response) CloseHandle(topology_response);
        if (topology_request) CloseHandle(topology_request);
        if (ready) CloseHandle(ready);
        if (audio_response) CloseHandle(audio_response);
        if (audio_request) CloseHandle(audio_request);
        if (mapping) CloseHandle(mapping);
    }
};

struct AudioResult {
    float left = 0.0f;
    float right = 0.0f;
    std::uint64_t chain_generation = 0;
    std::uint32_t latency = 0;
};

bool committed_order_is(const RackSharedAudioRegion& region,
                        const RackSlotId* ids,
                        std::uint32_t count)
{
    if (region.committed_slot_count != count)
        return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (region.committed_slot_ids[i] != ids[i])
            return false;
    }
    return true;
}

bool begin_audio(Handles& h, float left, float right, long generation, std::uint32_t sequence)
{
    h.region->frames = 1;
    h.region->block_channels = 2;
    h.region->sequence = sequence;
    h.region->input[0][0] = left;
    h.region->input[1][0] = right;
    InterlockedExchange(&h.region->request_generation, generation);
    MemoryBarrier();
    return expect(SetEvent(h.audio_request) != FALSE, "signal audio request");
}

bool finish_audio(Handles& h, long generation, AudioResult& result, DWORD timeout_ms = 5000)
{
    bool ok = expect(WaitForSingleObject(h.audio_response, timeout_ms) == WAIT_OBJECT_0,
                     "bounded audio response");
    if (!ok)
        return false;
    ok &= expect(h.region->response_generation == generation, "audio response generation matches");
    ok &= expect(h.region->process_result == static_cast<long>(RackProcessResult::Ok),
                 "Rack block succeeds");
    result.left = h.region->output[0][0];
    result.right = h.region->output[1][0];
    result.chain_generation = static_cast<std::uint64_t>(h.region->processed_chain_generation);
    result.latency = h.region->total_latency_samples;
    return ok;
}

bool process_one(Handles& h, float left, float right, long generation, std::uint32_t sequence,
                 AudioResult& result)
{
    return begin_audio(h, left, right, generation, sequence) &&
           finish_audio(h, generation, result);
}

bool begin_topology(Handles& h, const RackSlotId* ids, std::uint32_t count, long request_generation)
{
    if (!expect(count <= safevst3::rack::kRackMaxSlots, "topology count bounded"))
        return false;
    h.region->topology_requested_slot_count = count;
    for (std::uint32_t i = 0; i < safevst3::rack::kRackMaxSlots; ++i)
        h.region->topology_requested_slot_ids[i] = i < count ? ids[i] : 0;
    InterlockedExchange(&h.region->topology_request_generation, request_generation);
    MemoryBarrier();
    return expect(SetEvent(h.topology_request) != FALSE, "signal topology request");
}

bool finish_topology(Handles& h, const RackSlotId* ids, std::uint32_t count,
                     long request_generation, std::uint64_t previous_chain_generation,
                     std::uint64_t& committed_generation, DWORD timeout_ms = 5000)
{
    bool ok = expect(WaitForSingleObject(h.topology_response, timeout_ms) == WAIT_OBJECT_0,
                     "bounded topology response");
    if (!ok)
        return false;
    ok &= expect(h.region->topology_response_generation == request_generation,
                 "topology response generation matches");
    ok &= expect(h.region->topology_result == static_cast<long>(RackTopologyResult::Ok),
                 "topology transaction accepted");
    committed_generation = static_cast<std::uint64_t>(h.region->committed_chain_generation);
    ok &= expect(committed_generation > previous_chain_generation,
                 "committed Rack generation advances");
    ok &= expect(committed_order_is(*h.region, ids, count),
                 "committed stable slot order matches request");
    return ok;
}

bool apply_topology(Handles& h, const RackSlotId* ids, std::uint32_t count,
                    long request_generation, std::uint64_t previous_chain_generation,
                    std::uint64_t& committed_generation)
{
    return begin_topology(h, ids, count, request_generation) &&
           finish_topology(h, ids, count, request_generation, previous_chain_generation,
                           committed_generation);
}

bool check_audio(const AudioResult& result, float left, float right,
                 std::uint64_t generation, std::uint32_t latency, const char* label)
{
    bool ok = true;
    ok &= expect(close_enough(result.left, left), label);
    ok &= expect(close_enough(result.right, right), label);
    ok &= expect(result.chain_generation == generation, "processed chain generation is coherent");
    ok &= expect(result.latency == latency, "processed latency matches that generation");
    return ok;
}

bool launch(Handles& h, const char* helper_utf8, const char* plugin_a_utf8,
            const char* plugin_b_utf8, const char* plugin_c_utf8,
            const std::wstring& mapping_name, const std::wstring& request_name,
            const std::wstring& response_name, const std::wstring& ready_name,
            const std::wstring& topology_request_name, const std::wstring& topology_response_name,
            const std::wstring& a_entered_name, const std::wstring& c_loading_name)
{
    h.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(RackSharedAudioRegion)), mapping_name.c_str());
    if (!expect(h.mapping != nullptr, "create Rack mapping")) return false;
    h.region = static_cast<RackSharedAudioRegion*>(MapViewOfFile(
        h.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RackSharedAudioRegion)));
    if (!expect(h.region != nullptr, "map Rack region")) return false;
    *h.region = RackSharedAudioRegion{};
    h.region->sample_rate = 48000;
    h.region->channels = 2;

    h.audio_request = CreateEventW(nullptr, FALSE, FALSE, request_name.c_str());
    h.audio_response = CreateEventW(nullptr, FALSE, FALSE, response_name.c_str());
    h.ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
    h.topology_request = CreateEventW(nullptr, FALSE, FALSE, topology_request_name.c_str());
    h.topology_response = CreateEventW(nullptr, FALSE, FALSE, topology_response_name.c_str());
    h.a_entered = CreateEventW(nullptr, TRUE, FALSE, a_entered_name.c_str());
    h.c_loading = CreateEventW(nullptr, TRUE, FALSE, c_loading_name.c_str());
    if (!expect(h.audio_request && h.audio_response && h.ready && h.topology_request &&
                h.topology_response && h.a_entered && h.c_loading,
                "create Rack topology/audio events"))
        return false;

    SetEnvironmentVariableW(L"SAFEVST3_R13_A_ENTER_EVENT", a_entered_name.c_str());
    SetEnvironmentVariableW(L"SAFEVST3_R13_C_LOAD_EVENT", c_loading_name.c_str());

    const std::wstring helper = widen(helper_utf8);
    std::wstring command = quote(helper) + L" --mapping " + quote(mapping_name) +
        L" --request-event " + quote(request_name) +
        L" --response-event " + quote(response_name) +
        L" --ready-event " + quote(ready_name) +
        L" --topology-request-event " + quote(topology_request_name) +
        L" --topology-response-event " + quote(topology_response_name) +
        L" --plugin-a " + quote(widen(plugin_a_utf8)) +
        L" --plugin-b " + quote(widen(plugin_b_utf8)) +
        L" --plugin-c " + quote(widen(plugin_c_utf8));
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    const BOOL created = CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr,
                                        FALSE, 0, nullptr, nullptr, &startup, &h.process);
    SetEnvironmentVariableW(L"SAFEVST3_R13_A_ENTER_EVENT", nullptr);
    SetEnvironmentVariableW(L"SAFEVST3_R13_C_LOAD_EVENT", nullptr);
    if (!expect(created != FALSE, "launch separate Rack helper process"))
        return false;
    if (!expect(WaitForSingleObject(h.ready, 15000) == WAIT_OBJECT_0, "Rack helper ready"))
        return false;
    return expect(h.region->host_status == static_cast<long>(RackHostStatus::Ready),
                  "Rack helper status Ready");
}

bool run_test(const char* helper, const char* plugin_a, const char* plugin_b, const char* plugin_c)
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    const std::wstring mapping_name = L"Local\\SafeVst3RackR13Map_" + suffix;
    const std::wstring request_name = L"Local\\SafeVst3RackR13Req_" + suffix;
    const std::wstring response_name = L"Local\\SafeVst3RackR13Rsp_" + suffix;
    const std::wstring ready_name = L"Local\\SafeVst3RackR13Ready_" + suffix;
    const std::wstring topology_request_name = L"Local\\SafeVst3RackR13TopoReq_" + suffix;
    const std::wstring topology_response_name = L"Local\\SafeVst3RackR13TopoRsp_" + suffix;
    const std::wstring a_entered_name = L"Local\\SafeVst3RackR13AEntered_" + suffix;
    const std::wstring c_loading_name = L"Local\\SafeVst3RackR13CLoading_" + suffix;

    Handles h;
    if (!launch(h, helper, plugin_a, plugin_b, plugin_c, mapping_name, request_name, response_name,
                ready_name, topology_request_name, topology_response_name, a_entered_name,
                c_loading_name))
        return false;

    bool ok = true;
    ok &= expect(h.region->version == safevst3::rack::kRackProtocolVersion,
                 "current Rack protocol version");
    const RackSlotId ab[] = {safevst3::rack::kRackSlotIdA, safevst3::rack::kRackSlotIdB};
    const RackSlotId ba[] = {safevst3::rack::kRackSlotIdB, safevst3::rack::kRackSlotIdA};
    const RackSlotId b_only[] = {safevst3::rack::kRackSlotIdB};
    const RackSlotId bc[] = {safevst3::rack::kRackSlotIdB, safevst3::rack::kRackSlotIdC};
    const RackSlotId cb[] = {safevst3::rack::kRackSlotIdC, safevst3::rack::kRackSlotIdB};

    const std::uint64_t initial_generation = static_cast<std::uint64_t>(h.region->committed_chain_generation);
    ok &= expect(initial_generation != 0, "initial chain generation exists");
    ok &= expect(committed_order_is(*h.region, ab, 2), "initial stable order is A then B");

    AudioResult audio{};
    ok &= process_one(h, 0.25f, -0.5f, 1, 101, audio);
    ok &= check_audio(audio, 1.5f, 0.0f, initial_generation, 16, "initial A then B audio");

    // Reorder while generation N is definitely in-flight inside slow A.
    ResetEvent(h.a_entered);
    ok &= begin_audio(h, 0.25f, -0.5f, 2, 102);
    ok &= expect(WaitForSingleObject(h.a_entered, 3000) == WAIT_OBJECT_0,
                 "A entered old generation process before reorder");
    std::uint64_t ba_generation = 0;
    ok &= apply_topology(h, ba, 2, 1, initial_generation, ba_generation);
    ok &= expect(WaitForSingleObject(h.audio_response, 0) == WAIT_TIMEOUT,
                 "old-generation DSP reader still alive after new generation published");
    ok &= finish_audio(h, 2, audio);
    ok &= check_audio(audio, 1.5f, 0.0f, initial_generation, 16,
                      "in-flight block remains pure A then B");
    ok &= process_one(h, 0.25f, -0.5f, 3, 103, audio);
    ok &= check_audio(audio, 2.5f, 1.0f, ba_generation, 16,
                      "next block uses pure B then A");
    ok &= expect(committed_order_is(*h.region, ba, 2),
                 "reorder preserves A/B stable identities");

    // Remove A while the B->A generation is in-flight inside A.
    ResetEvent(h.a_entered);
    ok &= begin_audio(h, 0.25f, -0.5f, 4, 104);
    ok &= expect(WaitForSingleObject(h.a_entered, 3000) == WAIT_OBJECT_0,
                 "A entered old B then A generation before remove");
    std::uint64_t b_generation = 0;
    ok &= apply_topology(h, b_only, 1, 2, ba_generation, b_generation);
    ok &= expect(WaitForSingleObject(h.audio_response, 0) == WAIT_TIMEOUT,
                 "remove does not invalidate reachable old generation");
    ok &= finish_audio(h, 4, audio);
    ok &= check_audio(audio, 2.5f, 1.0f, ba_generation, 16,
                      "in-flight removed-A generation completes coherently");
    ok &= process_one(h, 0.25f, -0.5f, 5, 105, audio);
    ok &= check_audio(audio, 1.25f, 0.5f, b_generation, 11,
                      "next block uses B-only generation");

    // Add C: C initialize is deliberately slow. Audio must continue on B-only
    // while the control path loads C, proving vendor lifecycle is not on DSP.
    ResetEvent(h.c_loading);
    ok &= begin_topology(h, bc, 2, 3);
    ok &= expect(WaitForSingleObject(h.c_loading, 3000) == WAIT_OBJECT_0,
                 "C load entered on topology control path");
    ok &= begin_audio(h, 0.25f, -0.5f, 6, 106);
    ok &= finish_audio(h, 6, audio, 1000);
    ok &= check_audio(audio, 1.25f, 0.5f, b_generation, 11,
                      "audio advances on old generation while C loads");
    ok &= expect(WaitForSingleObject(h.topology_response, 0) == WAIT_TIMEOUT,
                 "C topology transaction still pending while old DSP advances");
    std::uint64_t bc_generation = 0;
    ok &= finish_topology(h, bc, 2, 3, b_generation, bc_generation);
    ok &= process_one(h, 0.25f, -0.5f, 7, 107, audio);
    ok &= check_audio(audio, 3.75f, 1.5f, bc_generation, 34,
                      "B then C generation is coherent");

    // Repeated bounded topology swaps must keep stable IDs/order coherent.
    std::uint64_t current_generation = bc_generation;
    long topology_request_generation = 4;
    long audio_generation = 8;
    std::uint32_t sequence = 108;
    for (int i = 0; i < 8; ++i) {
        const bool want_cb = (i % 2) == 0;
        const RackSlotId* order = want_cb ? cb : bc;
        std::uint64_t next_generation = 0;
        ok &= apply_topology(h, order, 2, topology_request_generation++, current_generation,
                             next_generation);
        ok &= process_one(h, 0.25f, -0.5f, audio_generation++, sequence++, audio);
        const float expected_left = want_cb ? 1.75f : 3.75f;
        const float expected_right = want_cb ? -0.5f : 1.5f;
        ok &= check_audio(audio, expected_left, expected_right, next_generation, 34,
                          "repeated topology swap output/generation");
        current_generation = next_generation;
    }

    InterlockedExchange(&h.region->shutdown_requested, 1);
    SetEvent(h.audio_request);
    SetEvent(h.topology_request);
    ok &= expect(WaitForSingleObject(h.process.hProcess, 5000) == WAIT_OBJECT_0,
                 "Rack helper exits cleanly");
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: r1-3-rack-topology-integration <rack-helper.exe> <a.vst3> <b.vst3> <c.vst3>\n";
        return 2;
    }
    if (!run_test(argv[1], argv[2], argv[3], argv[4]))
        return 1;
    std::cout << "R1-3 immutable Rack generations preserved block-frontier coherence and stable slot identity\n";
    return 0;
}
