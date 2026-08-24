#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <windows.h>

#include <cstring>
#include <iostream>
#include <map>
#include <stop_token>
#include <string>
#include <thread>

namespace {

std::map<std::wstring, std::wstring> parse_args(int argc, wchar_t** argv)
{
    std::map<std::wstring, std::wstring> values;
    for (int i = 1; i + 1 < argc; i += 2)
        values[argv[i]] = argv[i + 1];
    return values;
}

void publish_dsp_heartbeat(safevst3::SharedAudioRegion* region) noexcept
{
    InterlockedExchange64(
        reinterpret_cast<volatile LONG64*>(&region->dsp_heartbeat_ms),
        static_cast<LONG64>(GetTickCount64()));
    InterlockedIncrement(&region->dsp_progress_generation);
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
    std::jthread dsp([&](std::stop_token stop) {
        publish_dsp_heartbeat(region);
        while (!stop.stop_requested() &&
               InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0) {
            publish_dsp_heartbeat(region);
            const DWORD wait = WaitForSingleObject(endpoint.dsp_event(), 100);
            if (wait == WAIT_FAILED)
                break;

            for (std::uint32_t i = 0; i < kSlotCount; ++i) {
                auto& slot = region->slots[i];
                if (InterlockedCompareExchange(&slot.state,
                                               static_cast<long>(SlotState::Processing),
                                               static_cast<long>(SlotState::Ready)) !=
                    static_cast<long>(SlotState::Ready))
                    continue;

                for (std::uint32_t ch = 0; ch < slot.channels && ch < kMaxChannels; ++ch)
                    std::memcpy(slot.output[ch], slot.input[ch], sizeof(float) * slot.frames);
                InterlockedExchange(&slot.result, static_cast<long>(ProcessResult::Ok));
                MemoryBarrier();
                InterlockedExchange(&slot.state, static_cast<long>(SlotState::Done));
                SetEvent(endpoint.response_event());
            }
            publish_dsp_heartbeat(region);
        }
    });

    // Control heartbeat is intentionally published once and then frozen.
    InterlockedExchange64(
        reinterpret_cast<volatile LONG64*>(&region->helper_heartbeat_ms),
        static_cast<LONG64>(GetTickCount64()));
    InterlockedIncrement(&region->helper_progress_generation);
    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    // Model vendor editor/message-pump code blocking the control thread. The
    // independent DSP worker above must keep audio and DSP heartbeat alive.
    while (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0)
        Sleep(100);

    dsp.request_stop();
    SetEvent(endpoint.dsp_event());
    return 0;
}

#endif
