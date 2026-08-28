#ifdef _WIN32

#include "host/hosted_plugin.hpp"

#include "host/vst3_engine.hpp"

#include <utility>

namespace safevst3 {

HostedPlugin::HostedPlugin()
    : engine_(std::make_unique<Vst3Engine>())
{
}

HostedPlugin::~HostedPlugin() = default;

bool HostedPlugin::open(const std::string& path,
                        const std::string& class_id,
                        std::uint32_t sample_rate,
                        std::uint32_t channels,
                        Steinberg::Vst::IComponentHandler* component_handler,
                        StartupPhaseSink* startup_phase_sink,
                        std::string& error)
{
    return engine_->open(path, class_id, sample_rate, channels,
                         component_handler, startup_phase_sink, error);
}

bool HostedPlugin::open(const std::string& path,
                        const std::string& class_id,
                        std::uint32_t sample_rate,
                        std::uint32_t channels,
                        Steinberg::Vst::IComponentHandler* component_handler,
                        std::string& error)
{
    return engine_->open(path, class_id, sample_rate, channels,
                         component_handler, error);
}

void HostedPlugin::close() noexcept
{
    engine_->close();
}

bool HostedPlugin::process(const ProcessBlockView& block) noexcept
{
    return engine_->process(block);
}

bool HostedPlugin::capture_state(PluginStateSnapshot& snapshot, std::string& error)
{
    return engine_->capture_state(snapshot, error);
}

bool HostedPlugin::restore_state(const PluginStateSnapshot& snapshot, std::string& error)
{
    return engine_->restore_state(snapshot, error);
}

bool HostedPlugin::refresh_latency_after_restart(std::string& error)
{
    return engine_->refresh_latency_after_restart(error);
}

bool HostedPlugin::reconfigure_io_after_restart(IoLayout& layout,
                                                 std::uint32_t& latency_samples,
                                                 std::string& error)
{
    return engine_->reconfigure_io_after_restart(layout, latency_samples, error);
}

bool HostedPlugin::queue_parameter(std::uint32_t id, double normalized) noexcept
{
    return engine_->queue_parameter(id, normalized);
}

bool HostedPlugin::queue_parameter_from_controller(std::uint32_t id, double normalized) noexcept
{
    return engine_->queue_parameter_from_controller(id, normalized);
}

bool HostedPlugin::set_controller_parameter(std::uint32_t id, double normalized) noexcept
{
    return engine_->set_controller_parameter(id, normalized);
}

bool HostedPlugin::queue_processor_parameter(std::uint32_t id, double normalized) noexcept
{
    return engine_->queue_processor_parameter(id, normalized);
}

bool HostedPlugin::flush_parameter_changes() noexcept
{
    return engine_->flush_parameter_changes();
}

void HostedPlugin::refresh_parameter_values() noexcept
{
    engine_->refresh_parameter_values();
}

bool HostedPlugin::refresh_parameter_metadata(std::string& error)
{
    return engine_->refresh_parameter_metadata(error);
}

std::size_t HostedPlugin::take_parameter_updates(EngineParameterUpdate* destination,
                                                 std::size_t capacity) noexcept
{
    return engine_->take_parameter_updates(destination, capacity);
}

void HostedPlugin::set_component_handler(Steinberg::Vst::IComponentHandler* handler) noexcept
{
    engine_->set_component_handler(handler);
}

Steinberg::Vst::IEditController* HostedPlugin::edit_controller() const noexcept
{
    return engine_->edit_controller();
}

const std::string& HostedPlugin::plugin_name() const noexcept
{
    return engine_->plugin_name();
}

const std::string& HostedPlugin::loaded_class_id() const noexcept
{
    return engine_->loaded_class_id();
}

std::uint32_t HostedPlugin::latency_samples() const noexcept
{
    return engine_->latency_samples();
}

std::uint32_t HostedPlugin::process_context_requirements() const noexcept
{
    return engine_->process_context_requirements();
}

std::uint32_t HostedPlugin::unsupported_process_context_requirements() const noexcept
{
    return engine_->unsupported_process_context_requirements();
}

const std::vector<EngineParameter>& HostedPlugin::parameters() const noexcept
{
    return engine_->parameters();
}

} // namespace safevst3

#endif
