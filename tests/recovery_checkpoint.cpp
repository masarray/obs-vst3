#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "recovery-checkpoint-test failed: " << message << '\n';
        std::exit(1);
    }
}

void require_state_equal(const safevst3::PluginStateSnapshot& actual,
                         const safevst3::PluginStateSnapshot& expected,
                         const char* phase)
{
    if (actual.component != expected.component || actual.controller != expected.controller) {
        std::cerr << "recovery-checkpoint-test failed: state mismatch after " << phase << '\n';
        std::exit(1);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace safevst3;

    require(argc >= 2, "fake stateful helper path is required");
    const std::filesystem::path helper = argv[1];

    PluginStateSnapshot checkpoint{};
    checkpoint.component = {0x53, 0x31, 0x2D, 0x43, 0x4F, 0x4D, 0x50, 0x01, 0x02, 0x03};
    checkpoint.controller = {0x53, 0x31, 0x2D, 0x43, 0x54, 0x52, 0x4C, 0x10, 0x20};

    std::string error;
    WinObsBridge first;
    require(first.start(helper, L"ignored.vst3", "crash", 48000, 2, error),
            error.empty() ? "first helper failed to start" : error.c_str());
    require(first.running(), "first helper must report running");
    require(first.restore_state(checkpoint, error),
            error.empty() ? "first helper restore failed" : error.c_str());

    PluginStateSnapshot captured{};
    require(first.capture_state(captured, error),
            error.empty() ? "first helper capture failed" : error.c_str());
    require_state_equal(captured, checkpoint, "first helper round-trip");

    const ULONGLONG exit_deadline = GetTickCount64() + 4000;
    while (first.running() && GetTickCount64() < exit_deadline)
        Sleep(25);
    require(!first.running(), "first helper must actually exit before recovery");
    first.stop();

    WinObsBridge recovered;
    error.clear();
    require(recovered.start(helper, L"ignored.vst3", "stable", 48000, 2, error),
            error.empty() ? "recovered helper failed to start" : error.c_str());
    require(recovered.running(), "recovered helper must report running");
    require(recovered.restore_state(checkpoint, error),
            error.empty() ? "recovered helper restore failed" : error.c_str());

    captured = {};
    require(recovered.capture_state(captured, error),
            error.empty() ? "recovered helper capture failed" : error.c_str());
    require_state_equal(captured, checkpoint, "helper recreation");
    require(recovered.heartbeat_age_ms() < 1000,
            "recovered helper must publish a fresh DSP heartbeat");

    recovered.stop();
    std::cout << "last-known-good checkpoint survived helper death and recreation\n";
    return 0;
}

#endif
