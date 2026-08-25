#pragma once

#include <array>
#include <string_view>

namespace safevst3 {

enum class StartupErrorCode : long {
    Generic = 1,
    ModuleLoad = 101,
    ClassSelect = 102,
    ComponentCreate = 103,
    ComponentInitialize = 104,
    ControllerCreate = 105,
    ControllerInitialize = 106,
    ComponentHandler = 107,
    ConnectComponentController = 108,
    ConnectControllerComponent = 109,
    ConnectionPoints = 110,
    ProcessorInterface = 111,
    SampleFormat = 112,
    ParameterCatalog = 113,
    BusNegotiation = 114,
    SetupProcessing = 115,
    BusActivation = 116,
    ProcessData = 117,
    SetActive = 118,
    SetProcessing = 119,
};

struct StartupErrorEntry {
    std::string_view marker;
    std::string_view phase;
    StartupErrorCode code;
};

inline constexpr std::array<StartupErrorEntry, 19> kStartupErrorEntries{{
    {"VST3 init[module-load]", "module-load", StartupErrorCode::ModuleLoad},
    {"VST3 init[class-select]", "class-select", StartupErrorCode::ClassSelect},
    {"VST3 init[component-create]", "component-create", StartupErrorCode::ComponentCreate},
    {"VST3 init[component-initialize]", "component-initialize", StartupErrorCode::ComponentInitialize},
    {"VST3 init[controller-create]", "controller-create", StartupErrorCode::ControllerCreate},
    {"VST3 init[controller-initialize]", "controller-initialize", StartupErrorCode::ControllerInitialize},
    {"VST3 init[component-handler]", "component-handler", StartupErrorCode::ComponentHandler},
    {"VST3 init[connect-component-controller]", "connect-component-controller", StartupErrorCode::ConnectComponentController},
    {"VST3 init[connect-controller-component]", "connect-controller-component", StartupErrorCode::ConnectControllerComponent},
    {"VST3 init[connection-points]", "connection-points", StartupErrorCode::ConnectionPoints},
    {"VST3 init[processor-interface]", "processor-interface", StartupErrorCode::ProcessorInterface},
    {"VST3 init[sample-format]", "sample-format", StartupErrorCode::SampleFormat},
    {"VST3 init[parameter-catalog]", "parameter-catalog", StartupErrorCode::ParameterCatalog},
    {"VST3 init[bus-negotiation]", "bus-negotiation", StartupErrorCode::BusNegotiation},
    {"VST3 init[setup-processing]", "setup-processing", StartupErrorCode::SetupProcessing},
    {"VST3 init[bus-activation]", "bus-activation", StartupErrorCode::BusActivation},
    {"VST3 init[process-data]", "process-data", StartupErrorCode::ProcessData},
    {"VST3 init[set-active]", "set-active", StartupErrorCode::SetActive},
    {"VST3 init[set-processing]", "set-processing", StartupErrorCode::SetProcessing},
}};

inline constexpr StartupErrorCode classify_startup_error(std::string_view message) noexcept
{
    for (const auto& entry : kStartupErrorEntries) {
        if (message.find(entry.marker) != std::string_view::npos)
            return entry.code;
    }
    return StartupErrorCode::Generic;
}

inline constexpr const char* startup_error_phase(StartupErrorCode code) noexcept
{
    for (const auto& entry : kStartupErrorEntries) {
        if (entry.code == code)
            return entry.phase.data();
    }
    if (code == StartupErrorCode::Generic)
        return "generic";
    return nullptr;
}

} // namespace safevst3
