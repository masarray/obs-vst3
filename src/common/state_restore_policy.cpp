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

bool accepts_processing_state_result(PluginCallResult result) noexcept
{
    return result == PluginCallResult::Success ||
           result == PluginCallResult::NotImplemented;
}

} // namespace safevst3
