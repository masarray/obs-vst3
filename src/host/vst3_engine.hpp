#pragma once

#ifdef _WIN32

#include "common/protocol.hpp"
#include "common/state_snapshot.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <array>
#include <cstddef>
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

struct EngineParameterUpdate {
    std::uint32_t id = 0;
    double normalized = 0.0;
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

    bool capture_state(PluginStateSnapshot& snapshot, std::string& error);
    bool restore_state(const PluginStateSnapshot& snapshot, std::string& error);

    // Transitional combined seam retained for callers outside the S2 helper.
    // S2 helper code uses the explicit controller/processor ownership methods
    // below so moving process() to its own thread does not introduce cross-
    // thread IEditController calls.
    bool queue_parameter(std::uint32_t id, double normalized) noexcept;
    bool queue_parameter_from_controller(std::uint32_t id, double normalized) noexcept;
    bool set_controller_parameter(std::uint32_t id, double normalized) noexcept;
    bool queue_processor_parameter(std::uint32_t id, double normalized) noexcept;

    bool flush_parameter_changes() noexcept;
    void refresh_parameter_values() noexcept;
    std::size_t take_parameter_updates(EngineParameterUpdate* destination, std::size_t capacity) noexcept;

    void set_component_handler(Steinberg::Vst::IComponentHandler* handler) noexcept;
    Steinberg::Vst::IEditController* edit_controller() const noexcept { return controller_.get(); }

    const std::string& plugin_name() const noexcept { return plugin_name_; }
    std::uint32_t latency_samples() const noexcept { return latency_samples_; }
    const std::vector<EngineParameter>& parameters() const noexcept { return parameters_; }

private:
    bool configure_buses(std::uint32_t channels, std::string& error);
    bool enumerate_parameters(std::string& error);
    bool queue_parameter_impl(std::uint32_t id, double normalized, bool update_controller) noexcept;
    bool apply_pending_parameter_changes(Steinberg::Vst::ProcessData& data) noexcept;
    void finish_parameter_changes() noexcept;
    void capture_output_parameter_changes() noexcept;
    void record_parameter_update(std::uint32_t id, double normalized) noexcept;
    EngineParameter* find_parameter(std::uint32_t id) noexcept;

    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    VST3::Hosting::Module::Ptr module_;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor_;
    Steinberg::Vst::HostProcessData process_data_;
    Steinberg::Vst::ParameterChanges input_parameter_changes_{static_cast<Steinberg::int32>(kMaxParameters)};
    Steinberg::Vst::ParameterChanges output_parameter_changes_{static_cast<Steinberg::int32>(kMaxParameters)};
    Steinberg::Vst::ProcessSetup process_setup_{};
    Steinberg::Vst::ProcessContext process_context_{};
    Steinberg::int32 main_input_bus_ = -1;
    Steinberg::int32 main_output_bus_ = -1;
    std::vector<EngineParameter> parameters_;
    std::array<EngineParameterUpdate, kMaxParameters> parameter_updates_{};
    std::size_t parameter_update_count_ = 0;
    std::string plugin_name_;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t channels_ = 0;
    std::uint32_t latency_samples_ = 0;
    Steinberg::int64 sample_position_ = 0;
    bool parameter_changes_pending_ = false;
};

} // namespace safevst3

#endif
