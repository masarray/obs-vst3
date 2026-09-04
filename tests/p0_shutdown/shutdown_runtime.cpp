#ifdef _WIN32

#include "platform/windows/win_rack_bridge.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        std::wcerr << L"usage: p0-rack-shutdown-runtime <hung-helper.exe>\n";
        return 2;
    }

    const std::filesystem::path helper = argv[1];
    safevst3::WinRackBridge bridge;
    std::string error;
    if (!bridge.start(helper, 48000, 2, error)) {
        std::cerr << "bridge start failed: " << error << '\n';
        return 3;
    }
    if (!bridge.running()) {
        std::cerr << "bridge did not report running before shutdown\n";
        return 4;
    }

    const auto started = std::chrono::steady_clock::now();
    bridge.stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    std::cout << "bounded hung-helper shutdown: " << elapsed.count() << " ms\n";

    // Source-level contract is 250 ms graceful + 250 ms force-observation.
    // Allow runner scheduling overhead while still making the historical
    // 2000 + 1000 ms path impossible to pass.
    if (elapsed > std::chrono::milliseconds(900)) {
        std::cerr << "P0 failure: Rack bridge shutdown exceeded 900 ms\n";
        return 5;
    }

    return 0;
}

#else
int main() { return 0; }
#endif
