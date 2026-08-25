#include "common/state_restore_policy.hpp"

namespace safevst3 {

bool accepts_state_restore_result(StateRestoreCall call, PluginCallResult result) noexcept
{
    if (call == StateRestoreCall::ControllerComponentState)
        return true;

    if (result == PluginCallResult::Success)
        return true;

    return false;
}

} // namespace safevst3
