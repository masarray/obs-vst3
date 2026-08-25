#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "startup-diagnostic-test failed: " << message << '\n';
        std::exit(1);
    }
}

void require_contains(const std::string& value, const char* needle)
{
    if (value.find(needle) == std::string::npos) {
        std::cerr << "startup-diagnostic-test failed: expected '" << needle
                  << "' in '" << value << "'\n";
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

    {
        WinObsBridge bridge;
        std::string error;
        require(!bridge.start(fake_helper, L"crash.vst3", "", 48000, 2, error),
                "crashing fake helper must not report startup success");
        require_contains(error, "phase=connect-component-controller");
        require_contains(error, "exit=0xC0000005");
    }

    {
        WinObsBridge bridge;
        std::string error;
        require(!bridge.start(fake_helper, L"timeout.vst3", "", 48000, 2, error),
                "stalled fake helper must not report startup success");
        require_contains(error, "VST3 helper startup timed out after");
        require_contains(error, "phase=controller-initialize");
    }

    std::cout << "crash-safe startup diagnostic seam passed\n";
    return 0;
}

#endif
