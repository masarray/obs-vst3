#pragma once

#ifdef _WIN32

#include "common/vst3_host_contract.hpp"
#include "common/io_restart_transaction.hpp"
#include "common/latency_restart_transaction.hpp"
#include "common/process_context_policy.hpp"
#include "common/startup_error.hpp"
#include "common/state_snapshot.hpp"
#include "host/process_block_view.hpp"
#include "host/vst3_processing_compat.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
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

class HostedPlugin : public LatencyRestartTarget, public IoRestartLifecycleTarget {
public:
    HostedPlugin() = default;
    ~HostedPlugin();

    HostedPlugin(const HostedPlugin&) = delete;
    HostedPlugin& operator=(const HostedPlugin&) = delete;

    bool open(const std::string& path,
              const std::string& class_id,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              Steinberg::Vst::IComponentHandler* component_handler,
              StartupPhaseSink* startup_phase_sink,
              std::string& error);
    bool open(const std::string& path,
              const std::string& class_id,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              Steinberg::Vst::IComponentHandler* component_handler,
              std::string& error)
    {
        StartupPhaseSink* sink = current_startup_phase_sink();
        const bool opened = open(path, class_id, sample_rate, channels,
                                 component_handler, sink, error);
        if (sink)
            set_current_startup_phase_sink(nullptr);
        return opened;
    }
    void close() noexcept;
    bool process(const ProcessBlockView& block) noexcept;

    // Optional control-owner hooks. The base Single host does not need them;
    // RackHostedPlugin overrides them so the separately compiled native-editor
    // manager can service vendor restartComponent callbacks through the base
    // HostedPlugin reference, and state capture can wait until controller edits
    // have actually reached processor/component state.
    virtual void service_component_handler_callbacks() noexcept {}
    virtual bool synchronize_component_handler_state(std::string& error) noexcept
    {
        (void)error;
        return true;
    }

    bool capture_state(PluginStateSnapshot& snapshot, std::string& error);
    bool restore_state(const PluginStateSnapshot& snapshot, std::string& error);
    bool refresh_latency_after_restart(std::string& error);
    bool reconfigure_io_after_restart(IoLayout& layout,
                                      std::uint32_t& latency_samples,
                                      std::string& error);

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
    bool refresh_parameter_metadata(std::string& error);
    std::size_t take_parameter_updates(EngineParameterUpdate* destination, std::size_t capacity) noexcept;

    void set_component_handler(Steinberg::Vst::IComponentHandler* handler) noexcept;
    Steinberg::Vst::IEditController* edit_controller() const noexcept { return controller_.get(); }

    const std::string& plugin_name() const noexcept { return plugin_name_; }
    const std::string& loaded_class_id() const noexcept { return loaded_class_id_; }
    std::uint32_t latency_samples() const noexcept { return latency_samples_; }
    std::uint32_t process_context_requirements() const noexcept {
        return process_context_policy_.requested_requirements;
    }
    std::uint32_t unsupported_process_context_requirements() const noexcept {
        return process_context_policy_.unsupported_requirements;
    }
    const std::vector<EngineParameter>& parameters() const noexcept { return parameters_; }

private:
    bool set_processing(bool enabled) noexcept override;
    bool set_active(bool enabled) noexcept override;
    std::uint32_t get_latency_samples() noexcept override;

    bool io_stop_processing() noexcept override;
    bool io_deactivate() noexcept override;
    bool io_inspect_requested_layout(IoLayout& layout) noexcept override;
    IoArrangementResult io_confirm_requested_layout(const IoLayout& layout) noexcept override;
    bool io_inspect_confirmed_layout(IoLayout& layout) noexcept override;
    bool io_rebuild_processing(const IoLayout& layout) noexcept override;
    bool io_activate() noexcept override;
    bool io_query_latency(std::uint32_t& latency_samples) noexcept override;
    bool io_start_processing() noexcept override;
    void io_commit_layout(const IoLayout& layout, std::uint32_t latency_samples) noexcept override;

    void report_startup_phase(StartupErrorCode phase) noexcept;
    bool configure_buses(std::uint32_t channels, std::string& error);
    bool activate_configured_buses(std::string& error);
    bool collect_io_layout_candidate(IoLayout& layout) noexcept;
    bool enumerate_parameters(std::string& error);
    bool queue_parameter_impl(std::uint32_t id, double normalized, bool update_controller) noexcept;
    bool apply_pending_parameter_changes(Steinberg::Vst::ProcessData& data) noexcept;
    void finish_parameter_changes() noexcept;
    void capture_output_parameter_changes() noexcept;
    void record_parameter_update(std::uint32_t id, double normalized) noexcept;
    EngineParameter* find_parameter(std::uint32_t id) noexcept;

    Steinberg::IPtr<Steinberg::Vst::HostApplication> host_;
    VST3::Hosting::Module::Ptr module_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> component_connection_;
    Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> controller_connection_;
    CompatibleAudioProcessorPtr processor_;
    Steinberg::Vst::HostProcessData process_data_;
    Steinberg::Vst::ParameterChanges input_parameter_changes_{static_cast<Steinberg::int32>(kMaxParameters)};
    Steinberg::Vst::ParameterChanges output_parameter_changes_{static_cast<Steinberg::int32>(kMaxParameters)};
    Steinberg::Vst::ProcessSetup process_setup_{};
    Steinberg::Vst::ProcessContext process_context_{};
    ProcessContextPolicy process_context_policy_{};
    StartupPhaseSink* startup_phase_sink_ = nullptr;
    Steinberg::int32 main_input_bus_ = -1;
    Steinberg::int32 main_output_bus_ = -1;
    static constexpr std::size_t kMaxDynamicAudioBuses = 16;
    std::uint32_t plugin_input_channels_ = 0;
    std::uint32_t plugin_output_channels_ = 0;
    std::array<Steinberg::Vst::SpeakerArrangement, kMaxDynamicAudioBuses> io_candidate_inputs_{};
    std::array<Steinberg::Vst::SpeakerArrangement, kMaxDynamicAudioBuses> io_candidate_outputs_{};
    Steinberg::int32 io_candidate_input_count_ = 0;
    Steinberg::int32 io_candidate_output_count_ = 0;
    Steinberg::int32 io_candidate_main_input_bus_ = -1;
    Steinberg::int32 io_candidate_main_output_bus_ = -1;
    alignas(64) std::array<std::array<Steinberg::Vst::Sample32, kMaxFrames>, kMaxChannels>
        input_adapter_{};
    alignas(64) std::array<std::array<Steinberg::Vst::Sample32, kMaxFrames>, kMaxChannels>
        output_adapter_{};
    std::vector<EngineParameter> parameters_;
    std::array<EngineParameterUpdate, kMaxParameters> parameter_updates_{};
    std::size_t parameter_update_count_ = 0;
    std::string plugin_name_;
    std::string loaded_class_id_;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t channels_ = 0;
    std::uint32_t latency_samples_ = 0;
    Steinberg::int64 sample_position_ = 0;
    bool parameter_changes_pending_ = false;
    bool component_initialized_ = false;
    bool controller_initialized_ = false;
    bool controller_is_component_ = false;
};

} // namespace safevst3

#endif
