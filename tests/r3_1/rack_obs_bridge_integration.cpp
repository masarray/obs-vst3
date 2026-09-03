#include "platform/windows/win_rack_bridge.hpp"
#include "rack/rack_editor_window.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>

namespace {
using safevst3::WinRackBridge;
using safevst3::rack::ui::kRackEditorWindowClassName;

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

bool run_test(const std::filesystem::path& helper)
{
    WinRackBridge bridge;
    std::string error;
    bool ok = true;

    ok &= expect(bridge.start(helper, 48000, 2, error),
                 "empty production Rack helper must start without fixture VST3 paths");
    if (!ok) {
        std::cerr << "start error: " << error << '\n';
        return false;
    }

    ok &= expect(bridge.running(), "Rack bridge must report Ready helper");
    const auto initial = bridge.status();
    ok &= expect(initial.running, "Rack status must report running");
    ok &= expect(initial.chain_generation == 1, "empty Rack must publish coherent generation one");
    ok &= expect(initial.effect_count == 0, "new production Rack must start with zero effects");
    ok &= expect(initial.total_latency_samples == 0, "empty Rack latency must be zero");
    ok &= expect(FindWindowW(kRackEditorWindowClassName, nullptr) == nullptr,
                 "Rack editor must stay hidden on filter/helper creation");

    constexpr std::uint32_t frames = 16;
    float left[frames] = {0.25f, -0.125f, 0.4f, -0.3f, 0.1f, 0.2f, -0.45f, 0.05f,
                          0.3f, -0.2f, 0.15f, -0.35f, 0.45f, -0.05f, 0.075f, -0.1f};
    float right[frames] = {-0.25f, 0.125f, -0.4f, 0.3f, -0.1f, -0.2f, 0.45f, -0.05f,
                           -0.3f, 0.2f, -0.15f, 0.35f, -0.45f, 0.05f, -0.075f, 0.1f};
    const float expected_left[frames] = {0.25f, -0.125f, 0.4f, -0.3f, 0.1f, 0.2f, -0.45f, 0.05f,
                                         0.3f, -0.2f, 0.15f, -0.35f, 0.45f, -0.05f, 0.075f, -0.1f};
    const float expected_right[frames] = {-0.25f, 0.125f, -0.4f, 0.3f, -0.1f, -0.2f, 0.45f, -0.05f,
                                          -0.3f, 0.2f, -0.15f, 0.35f, -0.45f, 0.05f, -0.075f, 0.1f};
    float* planes[2] = {left, right};
    ok &= expect(bridge.process(planes, 2, frames, 0.70),
                 "empty Rack must complete bounded pass-through processing");
    for (std::uint32_t i = 0; i < frames; ++i) {
        ok &= expect(close_enough(left[i], expected_left[i]), "empty Rack left must remain exact dry");
        ok &= expect(close_enough(right[i], expected_right[i]), "empty Rack right must remain exact dry");
    }

    ok &= expect(bridge.open_editor(), "Open Rack must publish lightweight UI-open command");
    HWND editor = wait_for_editor(true, 5000);
    ok &= expect(editor != nullptr, "Open Rack must show helper-owned Rack Editor");
    if (editor)
        PostMessageW(editor, WM_CLOSE, 0, 0);
    ok &= expect(wait_for_editor(false, 5000) == nullptr,
                 "closing Rack Editor must leave helper/Rack lifetime intact");
    ok &= expect(bridge.running(), "closing editor must not stop Rack helper");
    ok &= expect(bridge.status().chain_generation == 1,
                 "closing editor must not alter authoritative Rack generation");

    bridge.stop();
    ok &= expect(!bridge.running(), "Rack bridge stop must close helper ownership");
    return ok;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: r3-1-rack-obs-bridge <rack-helper.exe>\n";
        return 2;
    }
    if (!run_test(std::filesystem::path(argv[1])))
        return 1;
    std::cout << "R3-1 empty Rack OBS bridge + Open Rack integration passed\n";
    return 0;
}
