#ifdef _WIN32

#include "host/vst3_engine.hpp"
#include "platform/windows/win_ipc.hpp"

#include <avrt.h>
#include <windows.h>

#include <iostream>
#include <map>
#include <string>

#pragma comment(lib, "Avrt.lib")

namespace {
std::string narrow(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

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

    BridgeNames names{get(L"--mapping"), get(L"--request-event"), get(L"--response-event"), get(L"--ready-event")};
    const std::wstring vst_path_w = get(L"--vst");
    const std::string class_id = narrow(get(L"--class-id"));

    WinHostEndpoint endpoint;
    std::string error;
    if (!endpoint.open(names, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto* region = endpoint.region();
    Vst3Engine engine;
    if (!engine.open(narrow(vst_path_w), class_id, region->sample_rate, region->channels, error)) {
        region->last_error = 1;
        InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Error));
        SetEvent(endpoint.ready_event());
        std::cerr << "VST3 init failed: " << error << '\n';
        return 3;
    }

    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    if (mmcss)
        AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    while (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0) {
        const DWORD wait = WaitForSingleObject(endpoint.request_event(), INFINITE);
        if (wait != WAIT_OBJECT_0)
            break;
        if (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) != 0)
            break;

        bool processed_any = false;
        for (std::uint32_t i = 0; i < kSlotCount; ++i) {
            auto& slot = region->slots[i];
            if (InterlockedCompareExchange(&slot.state,
                                           static_cast<long>(SlotState::Processing),
                                           static_cast<long>(SlotState::Ready)) != static_cast<long>(SlotState::Ready))
                continue;

            processed_any = true;
            const bool ok = engine.process(slot);
            InterlockedExchange(&slot.result, static_cast<long>(ok ? ProcessResult::Ok : ProcessResult::VstProcessError));
            MemoryBarrier();
            InterlockedExchange(&slot.state, static_cast<long>(SlotState::Done));
            SetEvent(endpoint.response_event());
        }

        if (!processed_any)
            Sleep(0);
    }

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::ShuttingDown));
    engine.close();
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    return 0;
}

#endif
