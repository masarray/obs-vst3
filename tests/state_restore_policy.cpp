#include "common/state_restore_policy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "state restore policy failure: " << message << '\n';
    std::exit(1);
}

} // namespace

int main()
{
    using safevst3::PluginCallResult;
    using safevst3::StateRestoreCall;
    using safevst3::accepts_state_restore_result;

    for (const auto call : {StateRestoreCall::ComponentState,
                            StateRestoreCall::ControllerComponentState,
                            StateRestoreCall::ControllerPrivateState}) {
        require(accepts_state_restore_result(call, PluginCallResult::Success),
                "every restore call must accept success");
    }

    require(!accepts_state_restore_result(StateRestoreCall::ComponentState,
                                          PluginCallResult::ResultFalse),
            "component setState failure must reject the snapshot");
    require(!accepts_state_restore_result(StateRestoreCall::ComponentState,
                                          PluginCallResult::NotImplemented),
            "component setState must be implemented");
    require(!accepts_state_restore_result(StateRestoreCall::ComponentState,
                                          PluginCallResult::UnexpectedFailure),
            "unexpected component setState failures must reject the snapshot");

    require(accepts_state_restore_result(StateRestoreCall::ControllerComponentState,
                                         PluginCallResult::ResultFalse),
            "Steinberg requires hosts to tolerate controller setComponentState kResultFalse");
    require(accepts_state_restore_result(StateRestoreCall::ControllerComponentState,
                                         PluginCallResult::NotImplemented),
            "the SDK base controller's setComponentState kNotImplemented is advisory");
    require(accepts_state_restore_result(StateRestoreCall::ControllerComponentState,
                                         PluginCallResult::UnexpectedFailure),
            "all controller component-sync return codes are advisory");

    require(!accepts_state_restore_result(StateRestoreCall::ControllerPrivateState,
                                          PluginCallResult::ResultFalse),
            "controller-private setState failure must reject when its blob is present");
    require(!accepts_state_restore_result(StateRestoreCall::ControllerPrivateState,
                                          PluginCallResult::NotImplemented),
            "a captured controller-private blob requires a working setState");
    require(!accepts_state_restore_result(StateRestoreCall::ControllerPrivateState,
                                          PluginCallResult::UnexpectedFailure),
            "unexpected controller-private setState failures must reject the snapshot");

    std::cout << "state restore result policy ok\n";
    return 0;
}
