#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::wstring, std::wstring> parse_args(int argc, wchar_t** argv)
{
    std::map<std::wstring, std::wstring> values;
    for (int i = 1; i + 1 < argc; i += 2)
        values[argv[i]] = argv[i + 1];
    return values;
}

void publish_heartbeat(safevst3::SharedAudioRegion* region) noexcept
{
    const auto now = static_cast<LONG64>(GetTickCount64());
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&region->helper_heartbeat_ms), now);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&region->dsp_heartbeat_ms), now);
    InterlockedIncrement(&region->helper_progress_generation);
    InterlockedIncrement(&region->dsp_progress_generation);
}

void complete_state(safevst3::SharedAudioRegion* region,
                    HANDLE state_event,
                    safevst3::StateStatus status,
                    long requested) noexcept
{
    MemoryBarrier();
    InterlockedExchange(&region->state_status, static_cast<long>(status));
    InterlockedExchange(&region->state_command, static_cast<long>(safevst3::StateCommand::None));
    InterlockedExchange(&region->state_applied_generation, requested);
    if (state_event)
        SetEvent(state_event);
}

bool handle_state(safevst3::WinHostEndpoint& endpoint,
                  std::vector<std::uint8_t>& component,
                  std::vector<std::uint8_t>& controller)
{
    auto* region = endpoint.region();
    auto* transfer = endpoint.state_region();
    const long requested = InterlockedCompareExchange(&region->state_request_generation, 0, 0);
    const long applied = InterlockedCompareExchange(&region->state_applied_generation, 0, 0);
    if (requested == applied)
        return false;

    const auto command = static_cast<safevst3::StateCommand>(
        InterlockedCompareExchange(&region->state_command, 0, 0));
    safevst3::StateStatus status = safevst3::StateStatus::Invalid;
    bool captured = false;

    if (!transfer || transfer->magic != safevst3::kStateTransferMagic ||
        transfer->version != safevst3::kStateTransferVersion ||
        transfer->capacity != safevst3::kMaxStateBytes) {
        complete_state(region, endpoint.state_event(), status, requested);
        return false;
    }

    if (command == safevst3::StateCommand::Restore) {
        const std::size_t component_bytes = region->state_component_bytes;
        const std::size_t controller_bytes = region->state_controller_bytes;
        if (component_bytes <= safevst3::kMaxStateBytes &&
            controller_bytes <= safevst3::kMaxStateBytes - component_bytes) {
            component.assign(transfer->payload,
                             transfer->payload + static_cast<std::ptrdiff_t>(component_bytes));
            controller.assign(transfer->payload + static_cast<std::ptrdiff_t>(component_bytes),
                              transfer->payload + static_cast<std::ptrdiff_t>(component_bytes + controller_bytes));
            InterlockedIncrement(&region->state_dirty_generation);
            status = safevst3::StateStatus::Ok;
        } else {
            status = safevst3::StateStatus::TooLarge;
        }
    } else if (command == safevst3::StateCommand::Capture) {
        if (component.size() <= safevst3::kMaxStateBytes &&
            controller.size() <= safevst3::kMaxStateBytes - component.size()) {
            if (!component.empty())
                std::memcpy(transfer->payload, component.data(), component.size());
            if (!controller.empty())
                std::memcpy(transfer->payload + component.size(), controller.data(), controller.size());
            region->state_component_bytes = static_cast<std::uint32_t>(component.size());
            region->state_controller_bytes = static_cast<std::uint32_t>(controller.size());
            status = safevst3::StateStatus::Ok;
            captured = true;
        } else {
            status = safevst3::StateStatus::TooLarge;
        }
    }

    complete_state(region, endpoint.state_event(), status, requested);
    return captured && status == safevst3::StateStatus::Ok;
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
    const bool crash_after_capture = get(L"--class-id") == L"crash";

    WinHostEndpoint endpoint;
    std::string error;
    if (!endpoint.open(names, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto* region = endpoint.region();
    std::vector<std::uint8_t> component;
    std::vector<std::uint8_t> controller;

    publish_heartbeat(region);
    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    HANDLE handles[] = {endpoint.request_event(), endpoint.dsp_event()};
    while (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0) {
        publish_heartbeat(region);
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 50);
        if (wait == WAIT_FAILED)
            return 3;
        if (wait == WAIT_OBJECT_0) {
            const bool captured = handle_state(endpoint, component, controller);
            if (crash_after_capture && captured) {
                // The state completion event has already been published. Die
                // only after the bridge has a valid checkpoint to recover.
                Sleep(50);
                return 77;
            }
        }
    }

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::ShuttingDown));
    return 0;
}

#endif
