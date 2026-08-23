#pragma once

#ifdef _WIN32

#include "common/protocol.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <memory>
#include <string>
#include <vector>

namespace safevst3 {

struct EngineParameter {
    std::uint32_t id = 0;
    std::int32_t step_count = 0;
    std::uint32_t flags = 0;
    double default_normalized = 0.0;
    double current_normalized = 0.0;
    std::string title;
    std::string units;
};

class Vst3Engine {
public:
    Vst3Engine() = default;
    ~Vst3Engine();

    Vst3Engine(const Vst3Engine&) = delete;
    Vst3Engine& operator=(const Vst3Engine&) = delete;

    bool open(const std::string& path,
              const std::string& class_id,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              std::string& error);
    void close() noexcept;
    bool process(AudioSlot& slot) noexcept;

    bool queue_parameter(std::uint32_t id, double normalized) noexcept;
    bool flush_parameter_changes() noexcept;

    const std::string& plugin_name() const noexcept { return plugin_name_; }
    std::uint32_t latency_samples() const noexcept { return latency_samples_; }
    const std::vector<EngineParameter>& parameters() const noexcept { return parameters_; }

private:
    bool configure_buses(std::uint32_t channels, std::string& error);
    bool enumerate_parameters(std::string& error);
    bool apply_pending_parameter_changes(Steinberg::Vst::ProcessData& data) noexcept;
    void finish_parameter_changes() noexcept;
    EngineParameter* find_parameter(std::uint32_t id) noexcept;

    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    VST3::Hosting::Module::Ptr module_;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor_;
    Steinberg::Vst::HostProcessData process_data_;
    Steinberg::Vst::ParameterChanges input_parameter_changes_{static_cast<Steinberg::int32>(kMaxParameters)};
    Steinberg::Vst::ProcessSetup process_setup_{};
    Steinberg::Vst::ProcessContext process_context_{};
    Steinberg::int32 main_input_bus_ = -1;
    Steinberg::int32 main_output_bus_ = -1;
    std::vector<EngineParameter> parameters_;
    std::string plugin_name_;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t channels_ = 0;
    std::uint32_t latency_samples_ = 0;
    Steinberg::int64 sample_position_ = 0;
    bool parameter_changes_pending_ = false;
};

} // namespace safevst3

#endif
