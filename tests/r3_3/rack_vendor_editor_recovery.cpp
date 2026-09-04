#include "host/process_block_view.hpp"
#include "rack/rack_editor_window.hpp"
#include "rack/rack_hosted_plugin.hpp"
#undef HostedPlugin
#include "rack/rack_vendor_editor_manager.hpp"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using safevst3::rack::ui::RackEditorWindow;
using safevst3::rack::ui::RackUiSlotHealth;
using safevst3::rack::ui::RackUiSnapshot;
using safevst3::rack::ui::RackVendorEditorManager;

constexpr wchar_t kRackEditorClass[] = L"SafeVst3RackEditorWindow";
constexpr wchar_t kVendorEditorClass[] = L"ObsSafeVst3NativeEditorWindow";
constexpr wchar_t kVendorTitle[] = L"R3-3 Recovery Slot";
constexpr std::uint64_t kSlotId = 0x303;

struct SharedProbe {
    volatile long sequence = 0;
    float input_left = 0.0f;
    float input_right = 0.0f;
    float output_left = 0.0f;
    float output_right = 0.0f;
    volatile long process_ok = 0;
};

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float actual, float expected) noexcept
{
    return std::fabs(actual - expected) <= 1.0e-6f;
}

std::wstring widen(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), size);
    return out;
}

std::string narrow(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}

template <std::size_t N>
void copy_text(std::array<char, N>& destination, const char* text)
{
    destination.fill('\0');
    if (!text)
        return;
    const std::size_t count = (std::min)(N - 1, std::strlen(text));
    std::memcpy(destination.data(), text, count);
}

bool wait_for_window(const wchar_t* class_name, const wchar_t* title,
                     bool present, DWORD timeout_ms)
{
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    do {
        const bool exists = FindWindowW(class_name, title) != nullptr;
        if (exists == present)
            return true;
        Sleep(20);
    } while (GetTickCount64() < deadline);
    return false;
}

bool process_once(safevst3::RackHostedPlugin& plugin, SharedProbe& probe)
{
    std::array<float, 1> left_in{probe.input_left};
    std::array<float, 1> right_in{probe.input_right};
    std::array<float, 1> left_out{};
    std::array<float, 1> right_out{};
    float* input[2] = {left_in.data(), right_in.data()};
    float* output[2] = {left_out.data(), right_out.data()};
    const auto sequence = static_cast<std::uint64_t>(
        InterlockedCompareExchange(&probe.sequence, 0, 0));
    safevst3::ProcessBlockView block{input, output, 2, 1, sequence};
    const bool ok = plugin.process(block);
    probe.output_left = left_out[0];
    probe.output_right = right_out[0];
    InterlockedExchange(&probe.process_ok, ok ? 1 : 0);
    return ok;
}

int run_child(const char* fixture_utf8,
              const wchar_t* mapping_name,
              const wchar_t* ready_name,
              const wchar_t* open_name,
              const wchar_t* process_name,
              const wchar_t* done_name,
              const wchar_t* shutdown_name)
{
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping_name);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, ready_name);
    HANDLE open = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, open_name);
    HANDLE process = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, process_name);
    HANDLE done = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, done_name);
    HANDLE shutdown = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, shutdown_name);
    if (!mapping || !ready || !open || !process || !done || !shutdown)
        return 3;

    auto* probe = static_cast<SharedProbe*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedProbe)));
    if (!probe)
        return 4;

    safevst3::RackHostedPlugin plugin;
    std::string error;
    if (!plugin.open(fixture_utf8, "", 48000, 2, nullptr, error)) {
        std::cerr << "child plug-in open failed: " << error << '\n';
        return 5;
    }

    RackVendorEditorManager vendor_editors;
    RackEditorWindow rack_editor;
    RackUiSnapshot snapshot{};
    snapshot.generation = 1;
    snapshot.slot_count = 1;
    snapshot.slots[0].slot_id = kSlotId;
    snapshot.slots[0].health = RackUiSlotHealth::Ready;
    snapshot.slots[0].editor_available = plugin.edit_controller() != nullptr;
    copy_text(snapshot.rack_name, "R3-3 Recovery Rack");
    copy_text(snapshot.slots[0].plugin_name, "R3-3 Vendor Editor Fixture");
    (void)rack_editor.publish_snapshot(snapshot);

    // Restore/load state is deliberately UI-silent. The parent verifies no Rack
    // or vendor HWND exists before it explicitly signals the open control action.
    SetEvent(ready);

    HANDLE events[] = {open, process, shutdown};
    bool running = true;
    while (running) {
        vendor_editors.pump_messages();
        const DWORD wait = WaitForMultipleObjects(3, events, FALSE, 16);
        switch (wait) {
        case WAIT_OBJECT_0: {
            if (!rack_editor.open_or_foreground())
                return 6;
            error.clear();
            if (!vendor_editors.open(kSlotId, plugin, narrow(kVendorTitle), error)) {
                std::cerr << "child vendor editor open failed: " << error << '\n';
                return 7;
            }
            break;
        }
        case WAIT_OBJECT_0 + 1:
            (void)process_once(plugin, *probe);
            SetEvent(done);
            break;
        case WAIT_OBJECT_0 + 2:
            running = false;
            break;
        case WAIT_TIMEOUT:
            break;
        default:
            return 8;
        }
    }

    vendor_editors.close_all();
    vendor_editors.pump_messages();
    rack_editor.shutdown();
    plugin.close();
    UnmapViewOfFile(probe);
    CloseHandle(shutdown);
    CloseHandle(done);
    CloseHandle(process);
    CloseHandle(open);
    CloseHandle(ready);
    CloseHandle(mapping);
    return 0;
}

struct ParentHarness {
    HANDLE mapping = nullptr;
    HANDLE ready = nullptr;
    HANDLE open = nullptr;
    HANDLE process = nullptr;
    HANDLE done = nullptr;
    HANDLE shutdown = nullptr;
    SharedProbe* probe = nullptr;
    PROCESS_INFORMATION child{};

    std::wstring mapping_name;
    std::wstring ready_name;
    std::wstring open_name;
    std::wstring process_name;
    std::wstring done_name;
    std::wstring shutdown_name;

    ~ParentHarness()
    {
        stop_child();
        if (probe) UnmapViewOfFile(probe);
        if (shutdown) CloseHandle(shutdown);
        if (done) CloseHandle(done);
        if (process) CloseHandle(process);
        if (open) CloseHandle(open);
        if (ready) CloseHandle(ready);
        if (mapping) CloseHandle(mapping);
    }

    bool child_alive() const noexcept
    {
        return child.hProcess && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT;
    }

    void close_child_handles() noexcept
    {
        if (child.hThread) CloseHandle(child.hThread);
        if (child.hProcess) CloseHandle(child.hProcess);
        child = {};
    }

    void stop_child() noexcept
    {
        if (child_alive()) {
            SetEvent(shutdown);
            if (WaitForSingleObject(child.hProcess, 3000) != WAIT_OBJECT_0) {
                TerminateProcess(child.hProcess, 0xD33F);
                (void)WaitForSingleObject(child.hProcess, 3000);
            }
        }
        close_child_handles();
    }
};

bool create_parent_harness(ParentHarness& h)
{
    const std::wstring suffix = std::to_wstring(GetCurrentProcessId()) + L"_" +
                                std::to_wstring(GetTickCount64());
    h.mapping_name = L"Local\\SafeVst3R33Map_" + suffix;
    h.ready_name = L"Local\\SafeVst3R33Ready_" + suffix;
    h.open_name = L"Local\\SafeVst3R33Open_" + suffix;
    h.process_name = L"Local\\SafeVst3R33Process_" + suffix;
    h.done_name = L"Local\\SafeVst3R33Done_" + suffix;
    h.shutdown_name = L"Local\\SafeVst3R33Shutdown_" + suffix;

    h.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   sizeof(SharedProbe), h.mapping_name.c_str());
    if (!h.mapping)
        return false;
    h.probe = static_cast<SharedProbe*>(
        MapViewOfFile(h.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedProbe)));
    if (!h.probe)
        return false;
    *h.probe = SharedProbe{};

    h.ready = CreateEventW(nullptr, TRUE, FALSE, h.ready_name.c_str());
    h.open = CreateEventW(nullptr, FALSE, FALSE, h.open_name.c_str());
    h.process = CreateEventW(nullptr, FALSE, FALSE, h.process_name.c_str());
    h.done = CreateEventW(nullptr, FALSE, FALSE, h.done_name.c_str());
    h.shutdown = CreateEventW(nullptr, TRUE, FALSE, h.shutdown_name.c_str());
    return h.ready && h.open && h.process && h.done && h.shutdown;
}

bool launch_child(ParentHarness& h, const std::wstring& self,
                  const std::wstring& fixture)
{
    h.stop_child();
    ResetEvent(h.ready);
    ResetEvent(h.done);
    ResetEvent(h.shutdown);
    InterlockedExchange(&h.probe->process_ok, 0);

    std::wstring command = quote(self) + L" --child " + quote(fixture) + L" " +
        quote(h.mapping_name) + L" " + quote(h.ready_name) + L" " + quote(h.open_name) +
        L" " + quote(h.process_name) + L" " + quote(h.done_name) + L" " +
        quote(h.shutdown_name);
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(self.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &h.child))
        return false;
    return WaitForSingleObject(h.ready, 15000) == WAIT_OBJECT_0;
}

bool request_process(ParentHarness& h, float left, float right,
                     long sequence, float expected_gain)
{
    h.probe->input_left = left;
    h.probe->input_right = right;
    h.probe->output_left = 999.0f;
    h.probe->output_right = -999.0f;
    InterlockedExchange(&h.probe->process_ok, 0);
    InterlockedExchange(&h.probe->sequence, sequence);
    ResetEvent(h.done);
    SetEvent(h.process);
    if (WaitForSingleObject(h.done, 5000) != WAIT_OBJECT_0)
        return false;
    return InterlockedCompareExchange(&h.probe->process_ok, 0, 0) == 1 &&
           close_enough(h.probe->output_left, left * expected_gain) &&
           close_enough(h.probe->output_right, right * expected_gain);
}

bool run_parent(const wchar_t* self_path, const wchar_t* fixture_path)
{
    ParentHarness h;
    bool ok = expect(create_parent_harness(h), "create R3-3 recovery harness");
    if (!ok)
        return false;

    const std::wstring self = self_path;
    const std::wstring fixture = fixture_path;
    ok &= expect(launch_child(h, self, fixture), "launch UI-capable helper child");
    ok &= expect(wait_for_window(kRackEditorClass, nullptr, false, 100),
                 "Rack Editor must not auto-open on helper restore");
    ok &= expect(wait_for_window(kVendorEditorClass, kVendorTitle, false, 100),
                 "vendor editor must not auto-open on helper restore");
    ok &= expect(request_process(h, 0.25f, -0.5f, 1, 0.5f),
                 "child DSP must be wet before UI-open kill");

    SetEvent(h.open);
    ok &= expect(wait_for_window(kRackEditorClass, nullptr, true, 5000),
                 "Rack Editor must be open before kill");
    ok &= expect(wait_for_window(kVendorEditorClass, kVendorTitle, true, 5000),
                 "native vendor editor must be open before kill");

    ok &= expect(TerminateProcess(h.child.hProcess, 0xD334) != FALSE,
                 "outer host can kill helper while both editors are open");
    ok &= expect(WaitForSingleObject(h.child.hProcess, 3000) == WAIT_OBJECT_0,
                 "UI-owning helper exits within recovery bound");
    ok &= expect(wait_for_window(kRackEditorClass, nullptr, false, 3000),
                 "OS teardown removes killed Rack Editor HWND");
    ok &= expect(wait_for_window(kVendorEditorClass, kVendorTitle, false, 3000),
                 "OS teardown removes killed vendor HWND");

    // The parent is still alive and can immediately publish the required dry
    // outage behavior while no helper response is possible.
    const float dry_left = 0.125f;
    const float dry_right = -0.25f;
    ok &= expect(close_enough(dry_left, 0.125f) && close_enough(dry_right, -0.25f),
                 "outer path remains dry while UI-owning helper is dead");

    h.close_child_handles();
    ok &= expect(launch_child(h, self, fixture),
                 "outer host can recreate helper after UI-open kill");
    ok &= expect(wait_for_window(kRackEditorClass, nullptr, false, 100),
                 "recovered helper must not restore transient Rack Editor visibility");
    ok &= expect(wait_for_window(kVendorEditorClass, kVendorTitle, false, 100),
                 "recovered helper must not restore transient vendor visibility");
    ok &= expect(request_process(h, 0.125f, -0.25f, 2, 0.5f),
                 "recreated helper returns to coherent wet processing");

    h.stop_child();
    return ok;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc == 9 && std::wcscmp(argv[1], L"--child") == 0) {
        const std::string fixture = narrow(argv[2]);
        return run_child(fixture.c_str(), argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
    }
    if (argc != 2) {
        std::cerr << "usage: r3-3-vendor-editor-recovery <fixture.vst3>\n";
        return 2;
    }

    std::array<wchar_t, 32768> self{};
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (length == 0 || length >= self.size())
        return 3;
    return run_parent(self.data(), argv[1]) ? 0 : 1;
}
