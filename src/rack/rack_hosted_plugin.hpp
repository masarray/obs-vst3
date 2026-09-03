#pragma once

#ifdef _WIN32

#include "host/hosted_plugin.hpp"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace safevst3 {

// R3-2 has no vendor editor or parameter-control surface yet, but VST3
// controllers are still allowed to require a host IComponentHandler during
// initialization. Keep that host lifetime attached to each Rack-owned plug-in
// instead of passing nullptr. R3-3 may extend the callbacks when vendor editor
// orchestration is introduced; this R3-2 handler deliberately performs no UI
// work and never touches the Rack DSP path.
class RackComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                 void** object) override
    {
        if (!object)
            return Steinberg::kInvalidArgument;
        *object = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(
                iid, Steinberg::Vst::IComponentHandler::iid)) {
            *object = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            addRef();
            return Steinberg::kResultTrue;
        }
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return refs_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    Steinberg::uint32 PLUGIN_API release() override
    {
        const auto previous = refs_.fetch_sub(1, std::memory_order_relaxed);
        // Ownership is the containing RackHostedPlugin, never COM self-delete.
        if (previous <= 1) {
            refs_.store(1, std::memory_order_relaxed);
            return 1;
        }
        return previous - 1;
    }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override
    {
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID,
                                              Steinberg::Vst::ParamValue) override
    {
        // Parameter/editor orchestration belongs to later Rack tickets. R3-2
        // only needs a valid host callback surface for controller lifecycle.
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override
    {
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
    {
        restart_flags_.fetch_or(static_cast<std::uint32_t>(flags),
                                std::memory_order_relaxed);
        return Steinberg::kResultTrue;
    }

    std::uint32_t take_restart_flags() noexcept
    {
        return restart_flags_.exchange(0, std::memory_order_acq_rel);
    }

private:
    std::atomic<Steinberg::uint32> refs_{1};
    std::atomic<std::uint32_t> restart_flags_{0};
};

// Rack-only wrapper: every null handler supplied by the legacy Rack seams or
// the R3-2 dynamic browser becomes a stable Rack-owned handler. The shared
// HostedPlugin implementation and Single helper remain unchanged.
class RackHostedPlugin : public HostedPlugin {
public:
    // Derived members are destroyed before the base-class destructor runs. Close
    // explicitly here so a controller can release its component handler while
    // handler_ is still alive; HostedPlugin's later close is then idempotent.
    ~RackHostedPlugin() { close(); }

    bool open(const std::string& path,
              const std::string& class_id,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              Steinberg::Vst::IComponentHandler* component_handler,
              StartupPhaseSink* startup_phase_sink,
              std::string& error)
    {
        return HostedPlugin::open(path, class_id, sample_rate, channels,
                                  component_handler ? component_handler : &handler_,
                                  startup_phase_sink, error);
    }

    bool open(const std::string& path,
              const std::string& class_id,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              Steinberg::Vst::IComponentHandler* component_handler,
              std::string& error)
    {
        return HostedPlugin::open(path, class_id, sample_rate, channels,
                                  component_handler ? component_handler : &handler_,
                                  error);
    }

private:
    RackComponentHandler handler_;
};

} // namespace safevst3

// main_r3_2_hosted.cpp includes this header before the existing Rack source so
// all Rack-local HostedPlugin tokens resolve to the wrapper above. The macro is
// intentionally confined to that translation unit.
#define HostedPlugin RackHostedPlugin

#endif
