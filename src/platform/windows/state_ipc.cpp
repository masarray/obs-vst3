#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include <algorithm>
#include <cstring>

namespace safevst3 {
namespace {

const char* state_status_text(StateStatus status) noexcept
{
    switch (status) {
    case StateStatus::Ok: return "ok";
    case StateStatus::TooLarge: return "state exceeds transfer capacity";
    case StateStatus::Invalid: return "invalid state transfer";
    case StateStatus::VstError: return "VST3 state operation failed";
    case StateStatus::Idle:
    default: return "state operation did not complete";
    }
}

} // namespace

bool WinObsBridge::wait_for_state_request(long request_generation, std::string& error)
{
    if (!state_event_ || !process_.hProcess || !region_) {
        error = "VST3 state IPC is unavailable";
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + 2000;
    HANDLE wait_handles[] = {state_event_, process_.hProcess};
    while (true) {
        if (InterlockedCompareExchange(&region_->state_applied_generation, 0, 0) == request_generation)
            return true;

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        const DWORD remaining = static_cast<DWORD>(
            std::min<ULONGLONG>(deadline - now, 100));
        const DWORD wait = WaitForMultipleObjects(2, wait_handles, FALSE, remaining);
        if (wait == WAIT_OBJECT_0)
            continue;
        if (wait == WAIT_OBJECT_0 + 1) {
            error = "VST3 helper exited during state operation";
            return false;
        }
        if (wait == WAIT_FAILED) {
            error = "Waiting for VST3 state operation failed";
            return false;
        }
    }

    error = "VST3 state operation timed out";
    return false;
}

bool WinObsBridge::capture_state(PluginStateSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    error.clear();
    if (!running() || !region_ || !state_region_ || !request_event_) {
        error = "VST3 helper is not available for state capture";
        return false;
    }

    region_->state_component_bytes = 0;
    region_->state_controller_bytes = 0;
    InterlockedExchange(&region_->state_status, static_cast<long>(StateStatus::Idle));
    InterlockedExchange(&region_->state_command, static_cast<long>(StateCommand::Capture));
    MemoryBarrier();
    const long request = InterlockedIncrement(&region_->state_request_generation);
    SetEvent(request_event_);

    if (!wait_for_state_request(request, error))
        return false;

    const auto status = static_cast<StateStatus>(
        InterlockedCompareExchange(&region_->state_status, 0, 0));
    if (status != StateStatus::Ok) {
        error = state_status_text(status);
        return false;
    }

    MemoryBarrier();
    const std::size_t component_bytes = region_->state_component_bytes;
    const std::size_t controller_bytes = region_->state_controller_bytes;
    if (component_bytes > kMaxStateBytes || controller_bytes > kMaxStateBytes - component_bytes) {
        error = "VST3 helper returned an oversized state snapshot";
        return false;
    }

    snapshot.component.assign(state_region_->payload,
                              state_region_->payload + static_cast<std::ptrdiff_t>(component_bytes));
    snapshot.controller.assign(
        state_region_->payload + static_cast<std::ptrdiff_t>(component_bytes),
        state_region_->payload + static_cast<std::ptrdiff_t>(component_bytes + controller_bytes));
    return true;
}

bool WinObsBridge::restore_state(const PluginStateSnapshot& snapshot, std::string& error)
{
    error.clear();
    if (!running() || !region_ || !state_region_ || !request_event_) {
        error = "VST3 helper is not available for state restore";
        return false;
    }
    if (snapshot.component.size() > kMaxStateBytes ||
        snapshot.controller.size() > kMaxStateBytes - snapshot.component.size()) {
        error = "VST3 state exceeds transfer capacity";
        return false;
    }

    if (!snapshot.component.empty())
        std::memcpy(state_region_->payload, snapshot.component.data(), snapshot.component.size());
    if (!snapshot.controller.empty()) {
        std::memcpy(state_region_->payload + snapshot.component.size(),
                    snapshot.controller.data(), snapshot.controller.size());
    }
    region_->state_component_bytes = static_cast<std::uint32_t>(snapshot.component.size());
    region_->state_controller_bytes = static_cast<std::uint32_t>(snapshot.controller.size());
    InterlockedExchange(&region_->state_status, static_cast<long>(StateStatus::Idle));
    InterlockedExchange(&region_->state_command, static_cast<long>(StateCommand::Restore));
    MemoryBarrier();
    const long request = InterlockedIncrement(&region_->state_request_generation);
    SetEvent(request_event_);

    if (!wait_for_state_request(request, error))
        return false;

    const auto status = static_cast<StateStatus>(
        InterlockedCompareExchange(&region_->state_status, 0, 0));
    if (status != StateStatus::Ok) {
        error = state_status_text(status);
        return false;
    }
    return true;
}

std::uint32_t WinObsBridge::state_dirty_generation() const noexcept
{
    if (!region_)
        return 0;
    return static_cast<std::uint32_t>(
        InterlockedCompareExchange(&region_->state_dirty_generation, 0, 0));
}

} // namespace safevst3

#endif