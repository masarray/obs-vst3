#include "common/startup_error.hpp"

#include <cassert>
#include <string>
#include <string_view>

int main()
{
    using safevst3::StartupErrorCode;
    using safevst3::classify_startup_error;
    using safevst3::kStartupErrorEntries;
    using safevst3::startup_error_phase;

    for (const auto& entry : kStartupErrorEntries) {
        const std::string message = std::string(entry.marker) + ": deterministic failure";
        assert(classify_startup_error(message) == entry.code);
        const char* phase = startup_error_phase(entry.code);
        assert(phase != nullptr);
        assert(std::string_view(phase) == entry.phase);
    }

    assert(classify_startup_error("unstructured startup failure") == StartupErrorCode::Generic);
    assert(std::string_view(startup_error_phase(StartupErrorCode::Generic)) == "generic");
    assert(startup_error_phase(static_cast<StartupErrorCode>(9999)) == nullptr);
    return 0;
}
