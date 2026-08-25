#include "common/startup_error.hpp"

#include <cassert>
#include <string>
#include <string_view>

int main()
{
    using safevst3::StartupErrorCode;
    using safevst3::classify_startup_error;
    using safevst3::format_startup_process_exit;
    using safevst3::format_startup_timeout;
    using safevst3::kStartupErrorEntries;
    using safevst3::startup_error_phase;
    using safevst3::startup_phase_name;

    for (const auto& entry : kStartupErrorEntries) {
        const std::string message = std::string(entry.marker) + ": deterministic failure";
        assert(classify_startup_error(message) == entry.code);
        const char* phase = startup_error_phase(entry.code);
        assert(phase != nullptr);
        assert(std::string_view(phase) == entry.phase);
    }

    assert(classify_startup_error("unstructured startup failure") == StartupErrorCode::Generic);
    assert(std::string_view(startup_error_phase(StartupErrorCode::None)) == "none");
    assert(std::string_view(startup_error_phase(StartupErrorCode::Generic)) == "generic");
    assert(startup_error_phase(static_cast<StartupErrorCode>(9999)) == nullptr);
    assert(startup_phase_name(9999) == "unknown");

    assert(format_startup_process_exit(
               static_cast<long>(StartupErrorCode::ConnectComponentController), 0xC0000005u) ==
           "VST3 helper exited before becoming ready (phase=connect-component-controller, exit=0xC0000005)");
    assert(format_startup_timeout(
               static_cast<long>(StartupErrorCode::ControllerInitialize), 5000u) ==
           "VST3 helper startup timed out after 5000 ms (phase=controller-initialize)");
    assert(format_startup_timeout(static_cast<long>(StartupErrorCode::None), 5000u) ==
           "VST3 helper startup timed out after 5000 ms (phase=none)");
    return 0;
}
