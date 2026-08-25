#pragma once

namespace safevst3 {

enum class StateRestoreCall {
    ComponentState,
    ControllerComponentState,
    ControllerPrivateState,
};

enum class PluginCallResult {
    Success,
    ResultFalse,
    NotImplemented,
    UnexpectedFailure,
};

[[nodiscard]] bool accepts_state_restore_result(StateRestoreCall call,
                                                PluginCallResult result) noexcept;

} // namespace safevst3
