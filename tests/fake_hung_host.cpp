#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <windows.h>

#include <cstdint>
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

    SharedAudioRegion* region = endpoint.region();
    const LONG64 now = static_cast<LONG64>(GetTickCount64());
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&region->helper_heartbeat_ms), now);
    InterlockedIncrement(&region->helper_progress_generation);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&region->dsp_heartbeat_ms), now);
    InterlockedIncrement(&region->dsp_progress_generation);
    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    // Deliberately remain alive without touching DSP heartbeat again. This
    // models processor code stuck while the helper process itself still exists.
    Sleep(30000);
    return 0;
}

#endif
