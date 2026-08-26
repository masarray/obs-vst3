#ifdef _WIN32

#include "common/startup_error.hpp"
#include "platform/windows/win_ipc.hpp"

#include <windows.h>

#include <iostream>
#include <map>
#include <string>

namespace {

std::map<std::wstring, std::wstring> parse_args(int argc, wchar_t** argv)
{
    std::map<std::wstring, std::wstring> values;
    for (int i = 1; i + 1 < argc; i += 2)
        values[argv[i]] = argv[i + 1];
    return values;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace safevst3;

    const auto args = parse_args(argc, argv);
    const auto get = [&](const wchar_t* key) -> std::wstring {
        auto it = args.find(key);
        return it == args.end() ? std::wstring{} : it->second;
    };

    BridgeNames names{get(L"--mapping"), get(L"--state-mapping"), get(L"--request-event"),
                      get(L"--dsp-event"), get(L"--response-event"), get(L"--ready-event"),
                      get(L"--state-event")};

    WinHostEndpoint endpoint;
    std::string error;
    if (!endpoint.open(names, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto* region = endpoint.region(); // Binds the shared-memory StartupPhaseSink.
    StartupPhaseSink* sink = current_startup_phase_sink();
    if (!region || !sink)
        return 3;

    const std::wstring mode = get(L"--vst");
    if (mode.find(L"crash") != std::wstring::npos) {
        sink->publish(StartupErrorCode::ConnectComponentController);
        MemoryBarrier();
        ExitProcess(0xC0000005u);
    }

    if (mode.find(L"vendor-result") != std::wstring::npos) {
        sink->publish(StartupErrorCode::SetProcessing);
        sink->publish_vendor_result(-1);
        MemoryBarrier();
        InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Error));
        SetEvent(endpoint.ready_event());
        return 4;
    }

    sink->publish(StartupErrorCode::ControllerInitialize);
    MemoryBarrier();
    Sleep(30000);
    return 0;
}

#endif
