#include "rack/rack_protocol.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
using safevst3::rack::RackProcessResult;
using safevst3::rack::RackSharedAudioRegion;
using safevst3::rack::RackHostStatus;

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

bool expect(bool condition, const char* message)
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

bool run_test(const char* helper_utf8, const char* plugin_a_utf8, const char* plugin_b_utf8)
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    const std::wstring mapping_name = L"Local\\SafeVst3RackR11Map_" + suffix;
    const std::wstring request_name = L"Local\\SafeVst3RackR11Req_" + suffix;
    const std::wstring response_name = L"Local\\SafeVst3RackR11Rsp_" + suffix;
    const std::wstring ready_name = L"Local\\SafeVst3RackR11Ready_" + suffix;

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

    constexpr std::uint32_t frames = 8;
    const float left[frames] = {0.25f, -0.125f, 0.4f, -0.3f, 0.1f, 0.2f, -0.45f, 0.05f};
    const float right[frames] = {-0.25f, 0.125f, -0.4f, 0.3f, -0.1f, -0.2f, 0.45f, -0.05f};
    h.region->frames = frames;
    h.region->block_channels = 2;
    h.region->sequence = 41;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        h.region->input[0][frame] = left[frame];
        h.region->input[1][frame] = right[frame];
    }
    InterlockedExchange(&h.region->request_generation, 1);
    MemoryBarrier();
    SetEvent(h.request);

    bool ok = expect(WaitForSingleObject(h.response, 5000) == WAIT_OBJECT_0, "bounded Rack response");
    ok &= expect(h.region->response_generation == 1, "response generation matches request");
    ok &= expect(h.region->process_result == static_cast<long>(RackProcessResult::Ok),
                 "A then B must complete successfully");
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        ok &= expect(close_enough(h.region->output[0][frame], left[frame]), "left A x2 then B x0.5");
        ok &= expect(close_enough(h.region->output[1][frame], right[frame]), "right A x2 then B x0.5");
    }

    InterlockedExchange(&h.region->shutdown_requested, 1);
    SetEvent(h.request);
    ok &= expect(WaitForSingleObject(h.process.hProcess, 5000) == WAIT_OBJECT_0, "Rack helper exits cleanly");
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: r1-1-rack-serial-integration <rack-helper.exe> <gain-a.vst3> <gain-b.vst3>\n";
        return 2;
    }
    if (!run_test(argv[1], argv[2], argv[3]))
        return 1;
    std::cout << "R1-1 real Rack transport processed deterministic Gain A -> Gain B in serial\n";
    return 0;
}
