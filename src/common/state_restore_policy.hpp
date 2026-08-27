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

// Compatibility is intentionally narrow. A number of shipping VST3 processors
// return kNotImplemented from IAudioProcessor::setProcessing while continuing
// to implement process(). Treat only that result as advisory; explicit false
// and unexpected failures remain fatal.
[[nodiscard]] bool accepts_processing_state_result(PluginCallResult result) noexcept;

} // namespace safevst3
