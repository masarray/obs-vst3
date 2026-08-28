#pragma once

#ifdef _WIN32

#include "common/io_restart_transaction.hpp"
#include "common/startup_error.hpp"
#include "common/state_snapshot.hpp"
#include "host/hosted_plugin_types.hpp"
#include "host/process_block_view.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Steinberg::Vst {
class IComponentHandler;
class IEditController;
} // namespace Steinberg::Vst

namespace safevst3 {

// Deep helper-side owner for exactly one VST3 audio-effect instance.
//
// This is the protocol-neutral reuse boundary for Rack work. Single/Rack
// transport adapters, OBS recovery orchestration, shared-memory layouts and
// topology remain outside this object. The implementation deliberately wraps
// the already-qualified Vst3Engine during R0-2 so lifecycle/state behavior is
// reused rather than rewritten.
class HostedPlugin final {
public:
    HostedPlugin();
    ~HostedPlugin();

    HostedPlugin(const HostedPlugin&) = delete;
    HostedPlugin& operator=(const HostedPlugin&) = delete;
    HostedPlugin(HostedPlugin&&) = delete;
    HostedPlugin& operator=(HostedPlugin&&) = delete;

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
              std::string& error);
    void close() noexcept;

    bool process(const ProcessBlockView& block) noexcept;

    bool capture_state(PluginStateSnapshot& snapshot, std::string& error);
    bool restore_state(const PluginStateSnapshot& snapshot, std::string& error);
    bool refresh_latency_after_restart(std::string& error);
    bool reconfigure_io_after_restart(IoLayout& layout,
                                      std::uint32_t& latency_samples,
                                      std::string& error);

    bool queue_parameter(std::uint32_t id, double normalized) noexcept;
    bool queue_parameter_from_controller(std::uint32_t id, double normalized) noexcept;
    bool set_controller_parameter(std::uint32_t id, double normalized) noexcept;
    bool queue_processor_parameter(std::uint32_t id, double normalized) noexcept;
    bool flush_parameter_changes() noexcept;
    void refresh_parameter_values() noexcept;
    bool refresh_parameter_metadata(std::string& error);
    std::size_t take_parameter_updates(EngineParameterUpdate* destination,
                                       std::size_t capacity) noexcept;

    void set_component_handler(Steinberg::Vst::IComponentHandler* handler) noexcept;
    Steinberg::Vst::IEditController* edit_controller() const noexcept;

    const std::string& plugin_name() const noexcept;
    const std::string& loaded_class_id() const noexcept;
    std::uint32_t latency_samples() const noexcept;
    std::uint32_t process_context_requirements() const noexcept;
    std::uint32_t unsupported_process_context_requirements() const noexcept;
    const std::vector<EngineParameter>& parameters() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace safevst3

#endif
