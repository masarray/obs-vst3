#ifdef _WIN32

#include "common/recovery_policy.hpp"
#include "platform/windows/win_ipc.hpp"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "control-stall-dsp-test failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace safevst3;

    require(argc >= 2, "fake control-stall helper path missing");
    const std::filesystem::path helper(argv[1]);
    require(std::filesystem::exists(helper), "fake control-stall helper missing");

    WinObsBridge bridge;
    std::string error;
    require(bridge.start(helper, L"ignored.vst3", "", 48000, 2, error),
            error.empty() ? "fake helper did not start" : error.c_str());
    require(bridge.running(), "fake helper must start ready");

    // Wait long enough that the deliberately frozen control heartbeat would be
    // considered hung by the old S2.1 policy. DSP heartbeat must remain fresh.
    Sleep(static_cast<DWORD>(RecoveryPolicy::kHeartbeatTimeoutMs + 700));
    require(bridge.running(), "control stall must not terminate helper");
    require(bridge.heartbeat_age_ms() < 500,
            "watchdog health must follow fresh DSP heartbeat, not stalled control heartbeat");

    constexpr std::uint32_t frames = 128;
    float left[frames]{};
    float right[frames]{};
    for (std::uint32_t i = 0; i < frames; ++i) {
        left[i] = static_cast<float>(i) / static_cast<float>(frames);
        right[i] = -left[i];
    }
    float* channels[2]{left, right};

    for (int block = 0; block < 8; ++block) {
        require(bridge.process(channels, 2, frames, 0.90),
                "DSP must continue completing audio while control thread is stalled");
        require(std::fabs(left[64] - 0.5f) < 0.0001f,
                "left passthrough sample changed unexpectedly");
        require(std::fabs(right[64] + 0.5f) < 0.0001f,
                "right passthrough sample changed unexpectedly");
    }

    RecoveryPolicy policy;
    const RecoveryDecision decision = policy.observe(
        static_cast<std::uint64_t>(GetTickCount64()),
        {true, bridge.heartbeat_age_ms(), bridge.deadline_misses()});
    require(decision.health != RecoveryHealth::Hung && !decision.restart,
            "healthy DSP must not be restarted because control/UI is stalled");

    bridge.abort();
    std::cout << "control-thread stall with healthy DSP audio passed\n";
    return 0;
}

#endif
