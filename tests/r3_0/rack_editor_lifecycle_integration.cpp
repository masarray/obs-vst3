#include "rack/rack_editor_window.hpp"
#include "rack/rack_protocol.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
using safevst3::rack::RackHostStatus;
using safevst3::rack::RackProcessResult;
using safevst3::rack::RackSharedAudioRegion;
using safevst3::rack::ui::kRackEditorWindowClassName;

// Editor creation is a non-realtime Win32/D3D control-path operation. Shared
// GitHub Windows runners can occasionally spend more than five seconds inside
// D3D device/swap-chain recreation while many matrix jobs are active. Keep the
// audio/shutdown bounds strict, but give visible editor creation enough room to
// distinguish a genuine lifecycle failure from runner scheduling/driver jitter.
constexpr DWORD kEditorOpenTimeoutMs = 15000;
constexpr DWORD kEditorCloseTimeoutMs = 5000;

std::wstring widen(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float a, float b)
{
    return std::fabs(a - b) <= 1.0e-6f;
}

HWND wait_for_editor(bool present, DWORD timeout_ms)
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        HWND window = FindWindowW(kRackEditorWindowClassName, nullptr);
        if ((window != nullptr) == present)
            return window;
        if (GetTickCount64() >= deadline)
            return window;
        Sleep(25);
    }
}

struct Handles {
    HANDLE mapping = nullptr;
    HANDLE request = nullptr;
    HANDLE response = nullptr;
    HANDLE ready = nullptr;
    HANDLE ui_open = nullptr;
    PROCESS_INFORMATION process{};
    RackSharedAudioRegion* region = nullptr;

    ~Handles()
    {
        if (region) UnmapViewOfFile(region);
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) CloseHandle(process.hProcess);
        if (ui_open) CloseHandle(ui_open);
        if (ready) CloseHandle(ready);
        if (response) CloseHandle(response);
        if (request) CloseHandle(request);
        if (mapping) CloseHandle(mapping);
    }
};

bool process_block(Handles& h, long request_generation, std::uint64_t sequence)
{
    constexpr std::uint32_t frames = 8;
    const float left[frames] = {0.25f, -0.125f, 0.4f, -0.3f, 0.1f, 0.2f, -0.45f, 0.05f};
    const float right[frames] = {-0.25f, 0.125f, -0.4f, 0.3f, -0.1f, -0.2f, 0.45f, -0.05f};

    h.region->frames = frames;
    h.region->block_channels = 2;
    h.region->sequence = sequence;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        h.region->input[0][frame] = left[frame];
        h.region->input[1][frame] = right[frame];
    }
    InterlockedExchange(&h.region->request_generation, request_generation);
    MemoryBarrier();
    SetEvent(h.request);

    bool ok = expect(WaitForSingleObject(h.response, 5000) == WAIT_OBJECT_0,
                     "bounded Rack audio response while editor lifecycle changes");
    ok &= expect(h.region->response_generation == request_generation,
                 "audio response generation must match request");
    ok &= expect(h.region->process_result == static_cast<long>(RackProcessResult::Ok),
                 "Rack audio must remain wet/valid");
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        ok &= expect(close_enough(h.region->output[0][frame], left[frame]),
                     "left A x2 then B x0.5 remains exact");
        ok &= expect(close_enough(h.region->output[1][frame], right[frame]),
                     "right A x2 then B x0.5 remains exact");
    }
    return ok;
}

bool run_test(const char* helper_utf8, const char* plugin_a_utf8, const char* plugin_b_utf8)
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"_" +
                        std::to_wstring(GetTickCount64());
    const std::wstring mapping_name = L"Local\\SafeVst3RackR30Map_" + suffix;
    const std::wstring request_name = L"Local\\SafeVst3RackR30Req_" + suffix;
    const std::wstring response_name = L"Local\\SafeVst3RackR30Rsp_" + suffix;
    const std::wstring ready_name = L"Local\\SafeVst3RackR30Ready_" + suffix;
    const std::wstring ui_open_name = L"Local\\SafeVst3RackR30UiOpen_" + suffix;

    Handles h;
    h.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(RackSharedAudioRegion)),
                                   mapping_name.c_str());
    if (!expect(h.mapping != nullptr, "create Rack mapping"))
        return false;
    h.region = static_cast<RackSharedAudioRegion*>(MapViewOfFile(
        h.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RackSharedAudioRegion)));
    if (!expect(h.region != nullptr, "map Rack region"))
        return false;
    *h.region = RackSharedAudioRegion{};
    h.region->sample_rate = 48000;
    h.region->channels = 2;

    h.request = CreateEventW(nullptr, FALSE, FALSE, request_name.c_str());
    h.response = CreateEventW(nullptr, FALSE, FALSE, response_name.c_str());
    h.ready = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
    h.ui_open = CreateEventW(nullptr, FALSE, FALSE, ui_open_name.c_str());
    if (!expect(h.request && h.response && h.ready && h.ui_open, "create Rack/UI events"))
        return false;

    const std::wstring helper = widen(helper_utf8);
    const std::wstring plugin_a = widen(plugin_a_utf8);
    const std::wstring plugin_b = widen(plugin_b_utf8);
    std::wstring command = quote(helper) + L" --mapping " + quote(mapping_name) +
        L" --request-event " + quote(request_name) + L" --response-event " + quote(response_name) +
        L" --ready-event " + quote(ready_name) + L" --ui-open-event " + quote(ui_open_name) +
        L" --plugin-a " + quote(plugin_a) + L" --plugin-b " + quote(plugin_b);
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!expect(CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
                               nullptr, nullptr, &startup, &h.process) != FALSE,
                "launch real Rack helper with optional UI-open seam"))
        return false;

    bool ok = true;
    ok &= expect(WaitForSingleObject(h.ready, 15000) == WAIT_OBJECT_0, "Rack helper ready");
    ok &= expect(h.region->host_status == static_cast<long>(RackHostStatus::Ready),
                 "Rack helper status Ready");
    ok &= expect(h.region->committed_chain_generation == 1,
                 "initial immutable Rack generation must be one");
    ok &= expect(FindWindowW(kRackEditorWindowClassName, nullptr) == nullptr,
                 "helper/session restore must start with Rack editor hidden");

    SetEvent(h.ui_open);
    HWND first_window = wait_for_editor(true, kEditorOpenTimeoutMs);
    ok &= expect(first_window != nullptr, "OpenRack control event must create/show editor");
    ok &= process_block(h, 1, 101);
    ok &= expect(h.region->committed_chain_generation == 1,
                 "opening editor must not alter Rack generation");

    if (first_window)
        PostMessageW(first_window, WM_CLOSE, 0, 0);
    ok &= expect(wait_for_editor(false, kEditorCloseTimeoutMs) == nullptr,
                 "closing Rack editor must tear down only editor window");
    ok &= process_block(h, 2, 102);
    ok &= expect(h.region->committed_chain_generation == 1,
                 "closing editor must not alter Rack generation");

    SetEvent(h.ui_open);
    HWND reopened_window = wait_for_editor(true, kEditorOpenTimeoutMs);
    ok &= expect(reopened_window != nullptr, "Rack editor must reopen after close");
    if (reopened_window) {
        SetEvent(h.ui_open);
        Sleep(250);
        HWND foregrounded_window = FindWindowW(kRackEditorWindowClassName, nullptr);
        ok &= expect(foregrounded_window == reopened_window,
                     "OpenRack on visible editor must foreground existing ownership, not duplicate");
    }
    ok &= process_block(h, 3, 103);

    InterlockedExchange(&h.region->shutdown_requested, 1);
    SetEvent(h.request);
    SetEvent(h.ui_open);
    ok &= expect(WaitForSingleObject(h.process.hProcess, 5000) == WAIT_OBJECT_0,
                 "helper shutdown must close editor before process exit");
    ok &= expect(wait_for_editor(false, 2000) == nullptr,
                 "helper shutdown must leave no Rack editor window");
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: r3-0-rack-editor-lifecycle <rack-helper.exe> <gain-a.vst3> <gain-b.vst3>\n";
        return 2;
    }
    if (!run_test(argv[1], argv[2], argv[3]))
        return 1;
    std::cout << "R3-0 real helper editor hidden/open/close/reopen lifecycle preserved Rack DSP\n";
    return 0;
}
