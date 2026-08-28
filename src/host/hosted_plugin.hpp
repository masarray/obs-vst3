#pragma once

#ifdef _WIN32

#include "host/process_block_view.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Steinberg::Vst {
class IComponentHandler;
class IEditController;
}

namespace safevst3 {

class StartupPhaseSink;
class Vst3Engine;
struct EngineParameter;
struct EngineParameterUpdate;
struct IoLayout;
struct PluginStateSnapshot;

// Protocol-neutral helper-side facade for exactly one VST3 audio effect.
// Single/Rack transports own their buffer/layout adapters outside this class.
// The implementation intentionally reuses the already-qualified Vst3Engine
// core so R0-2 introduces no second lifecycle/state implementation.
class HostedPlugin final {
public:
    HostedPlugin();
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
    std::unique_ptr<Vst3Engine> engine_;
};

} // namespace safevst3

#endif
