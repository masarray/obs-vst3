#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

namespace safevst3 {

bool WinObsBridge::send_editor_command(EditorCommand command) noexcept
{
    if (!running() || !region_ || !request_event_)
        return false;

    InterlockedExchange(&region_->editor_command, static_cast<long>(command));
    MemoryBarrier();
    InterlockedIncrement(&region_->editor_request_generation);
    SetEvent(request_event_);
    return true;
}

bool WinObsBridge::open_editor() noexcept
{
    return send_editor_command(EditorCommand::Open);
}

bool WinObsBridge::hide_editor() noexcept
{
    return send_editor_command(EditorCommand::Hide);
}

EditorStatus WinObsBridge::editor_status() const noexcept
{
    if (!region_)
        return EditorStatus::Unknown;
    return static_cast<EditorStatus>(
        InterlockedCompareExchange(&region_->editor_status, 0, 0));
}

} // namespace safevst3

#endif
