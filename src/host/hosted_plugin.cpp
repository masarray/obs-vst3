#ifdef _WIN32

#include "host/hosted_plugin.hpp"
#include "host/vst3_engine.hpp"

#include <memory>

namespace safevst3 {

struct HostedPlugin::Impl {
    Vst3Engine engine;
};

HostedPlugin::HostedPlugin() : impl_(std::make_unique<Impl>()) {}
HostedPlugin::~HostedPlugin() = default;

bool HostedPlugin::open(const std::string& path,
                        const std::string& class_id,
                        std::uint32_t sample_rate,
                        std::uint32_t channels,
                        Steinberg::Vst::IComponentHandler* component_handler,
                        StartupPhaseSink* startup_phase_sink,
                        std::string& error)
{
    return impl_->engine.open(path, class_id, sample_rate, channels,
                              component_handler, startup_phase_sink, error);
}

bool HostedPlugin::open(const std::string& path,
                        const std::string& class_id,
                        std::uint32_t sample_rate,
                        std::uint32_t channels,
                        Steinberg::Vst::IComponentHandler* component_handler,
                        std::string& error)
{
    return impl_->engine.open(path, class_id, sample_rate, channels,
                              component_handler, error);
}

void HostedPlugin::close() noexcept
{
    impl_->engine.close();
}

bool HostedPlugin::process(const ProcessBlockView& block) noexcept
{
    return impl_->engine.process(block);
}

bool HostedPlugin::capture_state(PluginStateSnapshot& snapshot, std::string& error)
{
    return impl_->engine.capture_state(snapshot, error);
}

bool HostedPlugin::restore_state(const PluginStateSnapshot& snapshot, std::string& error)
{
    return impl_->engine.restore_state(snapshot, error);
}

bool HostedPlugin::refresh_latency_after_restart(std::string& error)
{
    return impl_->engine.refresh_latency_after_restart(error);
}

bool HostedPlugin::reconfigure_io_after_restart(IoLayout& layout,
                                                std::uint32_t& latency_samples,
                                                std::string& error)
{
    return impl_->engine.reconfigure_io_after_restart(layout, latency_samples, error);
}

bool HostedPlugin::queue_parameter(std::uint32_t id, double normalized) noexcept
{
    return impl_->engine.queue_parameter(id, normalized);
}

bool HostedPlugin::queue_parameter_from_controller(std::uint32_t id,
                                                   double normalized) noexcept
{
    return impl_->engine.queue_parameter_from_controller(id, normalized);
}

bool HostedPlugin::set_controller_parameter(std::uint32_t id, double normalized) noexcept
{
    return impl_->engine.set_controller_parameter(id, normalized);
}

bool HostedPlugin::queue_processor_parameter(std::uint32_t id, double normalized) noexcept
{
    return impl_->engine.queue_processor_parameter(id, normalized);
}

bool HostedPlugin::flush_parameter_changes() noexcept
{
    return impl_->engine.flush_parameter_changes();
}

void HostedPlugin::refresh_parameter_values() noexcept
{
    impl_->engine.refresh_parameter_values();
}

bool HostedPlugin::refresh_parameter_metadata(std::string& error)
{
    return impl_->engine.refresh_parameter_metadata(error);
}

std::size_t HostedPlugin::take_parameter_updates(EngineParameterUpdate* destination,
                                                 std::size_t capacity) noexcept
{
    return impl_->engine.take_parameter_updates(destination, capacity);
}

void HostedPlugin::set_component_handler(Steinberg::Vst::IComponentHandler* handler) noexcept
{
    impl_->engine.set_component_handler(handler);
}

Steinberg::Vst::IEditController* HostedPlugin::edit_controller() const noexcept
{
    return impl_->engine.edit_controller();
}

const std::string& HostedPlugin::plugin_name() const noexcept
{
    return impl_->engine.plugin_name();
}

const std::string& HostedPlugin::loaded_class_id() const noexcept
{
    return impl_->engine.loaded_class_id();
}

std::uint32_t HostedPlugin::latency_samples() const noexcept
{
    return impl_->engine.latency_samples();
}

std::uint32_t HostedPlugin::process_context_requirements() const noexcept
{
    return impl_->engine.process_context_requirements();
}

std::uint32_t HostedPlugin::unsupported_process_context_requirements() const noexcept
{
    return impl_->engine.unsupported_process_context_requirements();
}

const std::vector<EngineParameter>& HostedPlugin::parameters() const noexcept
{
    return impl_->engine.parameters();
}

} // namespace safevst3

#endif
