#ifdef _WIN32

#include "common/recovery_policy.hpp"
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
        std::cerr << "hang-watchdog-test failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace safevst3;

    require(argc >= 2, "fake helper path argument missing");
    const std::filesystem::path fake_helper(argv[1]);
    require(std::filesystem::exists(fake_helper), "fake helper executable missing");

    WinObsBridge bridge;
    std::string error;
    require(bridge.start(fake_helper, L"ignored.vst3", "", 48000, 2, error),
            error.empty() ? "fake helper did not start" : error.c_str());
    require(bridge.running(), "fake helper must be reported alive/ready");
    require(bridge.heartbeat_age_ms() < RecoveryPolicy::kHeartbeatTimeoutMs,
            "fresh heartbeat must not start stale");

    Sleep(static_cast<DWORD>(RecoveryPolicy::kHeartbeatTimeoutMs + 500));

    require(bridge.running(), "fake helper process must still be alive after heartbeat stalls");
    const std::uint64_t age = bridge.heartbeat_age_ms();
    require(age > RecoveryPolicy::kHeartbeatTimeoutMs,
            "heartbeat age must cross hang threshold while process remains alive");

    RecoveryPolicy policy;
    const RecoveryDecision decision = policy.observe(
        static_cast<std::uint64_t>(GetTickCount64()),
        {true, age, bridge.deadline_misses()});
    require(decision.health == RecoveryHealth::Hung && decision.restart,
            "live stale-heartbeat helper must request recovery");

    bridge.abort();
    std::cout << "live-but-hung helper watchdog seam passed\n";
    return 0;
}

#endif
