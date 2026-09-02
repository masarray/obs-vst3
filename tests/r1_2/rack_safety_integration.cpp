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

constexpr std::uint32_t kFrames = 8;
using Block = std::array<float, kFrames>;

std::wstring widen(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

bool close_enough(float a, float b) { return std::fabs(a - b) <= 1.0e-6f; }

bool expect(bool condition, const std::string& message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

struct Handles {
    HANDLE mapping = nullptr;
    HANDLE request = nullptr;
    HANDLE response = nullptr;
    HANDLE ready = nullptr;
    PROCESS_INFORMATION process{};
    RackSharedAudioRegion* region = nullptr;

    ~Handles()
    {
        if (region)
            UnmapViewOfFile(region);
        if (process.hThread)
            CloseHandle(process.hThread);
        if (process.hProcess)
            CloseHandle(process.hProcess);
        if (ready)
            CloseHandle(ready);
        if (response)
            CloseHandle(response);
        if (request)
            CloseHandle(request);
        if (mapping)
            CloseHandle(mapping);
    }
};

bool send_block(Handles& h,
                long generation,
                long bypass_mask,
                const Block& left,
                const Block& right,
                RackProcessResult expected_result,
                float expected_gain,
                std::uint32_t expected_latency,
                const char* label)
{
    h.region->frames = kFrames;
    h.region->block_channels = 2;
    h.region->sequence = static_cast<std::uint32_t>(40 + generation);
    InterlockedExchange(&h.region->bypass_mask, bypass_mask);

    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        h.region->input[0][frame] = left[frame];
        h.region->input[1][frame] = right[frame];
        h.region->output[0][frame] = 91.0f;
        h.region->output[1][frame] = -91.0f;
    }

    InterlockedExchange(&h.region->request_generation, generation);
    MemoryBarrier();
    SetEvent(h.request);

    bool ok = expect(WaitForSingleObject(h.response, 5000) == WAIT_OBJECT_0,
                     std::string(label) + ": bounded Rack response");
    ok &= expect(h.region->response_generation == generation,
                 std::string(label) + ": response generation matches request");
    ok &= expect(h.region->process_result == static_cast<long>(expected_result),
                 std::string(label) + ": expected process result");
    ok &= expect(h.region->total_latency_samples == expected_latency,
                 std::string(label) + ": coherent active latency sum");

    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        ok &= expect(close_enough(h.region->output[0][frame], left[frame] * expected_gain),
                     std::string(label) + ": left output");
        ok &= expect(close_enough(h.region->output[1][frame], right[frame] * expected_gain),
                     std::string(label) + ": right output");
    }
    return ok;
}

bool run_test(const char* helper_utf8, const char* plugin_a_utf8, const char* plugin_b_utf8)
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    const std::wstring mapping_name = L"Local\\SafeVst3RackR12Map_" + suffix;
    const std::wstring request_name = L"Local\\SafeVst3RackR12Req_" + suffix;
    const std::wstring response_name = L"Local\\SafeVst3RackR12Rsp_" + suffix;
    const std::wstring ready_name = L"Local\\SafeVst3RackR12Ready_" + suffix;

    Handles h;
    h.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(RackSharedAudioRegion)), mapping_name.c_str());
    if (!expect(h.mapping != nullptr, "create Rack mapping"))
        return false;
    h.region = static_cast<RackSharedAudioRegion*>(MapViewOfFile(h.mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                                                 sizeof(RackSharedAudioRegion)));
    if (!expect(h.region != nullptr, "map Rack region"))
        return false;
    *h.region = RackSharedAudioRegion{};
    h.region->sample_rate = 48000;
    h.region->channels = 2;

    h.request = CreateEventW(nullptr, FALSE, FALSE, request_name.c_str());
    h.response = CreateEventW(nullptr, FALSE, FALSE, response_name.c_str());
    h.ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
    if (!expect(h.request && h.response && h.ready, "create Rack events"))
        return false;

    const std::wstring helper = widen(helper_utf8);
    const std::wstring plugin_a = widen(plugin_a_utf8);
    const std::wstring plugin_b = widen(plugin_b_utf8);
    std::wstring command = quote(helper) + L" --mapping " + quote(mapping_name) +
        L" --request-event " + quote(request_name) + L" --response-event " + quote(response_name) +
        L" --ready-event " + quote(ready_name) + L" --plugin-a " + quote(plugin_a) +
        L" --plugin-b " + quote(plugin_b);
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!expect(CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
                               nullptr, nullptr, &startup, &h.process) != FALSE,
                "launch separate Rack helper process"))
        return false;

    if (!expect(WaitForSingleObject(h.ready, 15000) == WAIT_OBJECT_0, "Rack helper ready"))
        return false;
    if (!expect(h.region->magic == safevst3::rack::kRackProtocolMagic, "Rack protocol magic"))
        return false;
    if (!expect(h.region->version == safevst3::rack::kRackProtocolVersion, "Rack protocol version"))
        return false;
    if (!expect(h.region->host_status == static_cast<long>(RackHostStatus::Ready), "Rack helper status Ready"))
        return false;

    const Block normal_left = {0.25f, -0.125f, 0.4f, -0.3f, 0.1f, 0.2f, -0.45f, 0.05f};
    const Block normal_right = {-0.25f, 0.125f, -0.4f, 0.3f, -0.1f, -0.2f, 0.45f, -0.05f};
    const Block trigger_left = {0.75f, -0.2f, 0.1f, -0.4f, 0.3f, 0.15f, -0.35f, 0.05f};
    const Block trigger_right = {-0.75f, 0.2f, -0.1f, 0.4f, -0.3f, -0.15f, 0.35f, -0.05f};

    bool ok = true;
    ok &= send_block(h, 1, 0, normal_left, normal_right,
                     RackProcessResult::Ok, 1.0f, 34, "both active");
    ok &= send_block(h, 2, safevst3::rack::kRackBypassSlotA, trigger_left, trigger_right,
                     RackProcessResult::Ok, 0.5f, 23, "A bypassed");
    ok &= send_block(h, 3, safevst3::rack::kRackBypassSlotB, trigger_left, trigger_right,
                     RackProcessResult::Ok, 2.0f, 11, "B bypassed");
    ok &= send_block(h, 4, 0, trigger_left, trigger_right,
                     RackProcessResult::PluginBError, 1.0f, 34,
                     "B failure after A succeeds returns original dry");

    InterlockedExchange(&h.region->shutdown_requested, 1);
    SetEvent(h.request);
    ok &= expect(WaitForSingleObject(h.process.hProcess, 5000) == WAIT_OBJECT_0,
                 "Rack helper exits cleanly");
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: r1-2-rack-safety-integration <rack-helper.exe> <gain-a.vst3> <gain-b.vst3>\n";
        return 2;
    }
    if (!run_test(argv[1], argv[2], argv[3]))
        return 1;
    std::cout << "R1-2 real Rack transport proved bypass, latency sum and whole-block fail-dry\n";
    return 0;
}
