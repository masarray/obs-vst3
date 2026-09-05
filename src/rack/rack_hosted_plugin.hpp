#pragma once

#ifdef _WIN32

#include "common/spsc_ring.hpp"
#include "host/hosted_plugin.hpp"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace safevst3 {

class RackHostedPlugin;

// Rack-owned component handler. Vendor/native UI callbacks are control-thread
// events; they must never mutate HostedPlugin's VST3 ProcessData queues directly
// because those queues belong to the Rack DSP thread. performEdit therefore
// hands parameter values to the containing RackHostedPlugin's bounded SPSC
// bridge. restartComponent requests are retained for the control owner to
// service after vendor message pumping.
class RackComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    explicit RackComponentHandler(RackHostedPlugin* owner = nullptr) noexcept
        : owner_(owner)
    {
    }

    void set_owner(RackHostedPlugin* owner) noexcept { owner_ = owner; }

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

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override;

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override
    {
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
    {
        restart_flags_.fetch_or(static_cast<std::uint32_t>(flags),
                                std::memory_order_release);
        return Steinberg::kResultTrue;
    }

    std::uint32_t take_restart_flags() noexcept
    {
        return restart_flags_.exchange(0, std::memory_order_acq_rel);
    }

private:
    RackHostedPlugin* owner_ = nullptr;
    std::atomic<Steinberg::uint32> refs_{1};
    std::atomic<std::uint32_t> restart_flags_{0};
};

// Rack-only wrapper. In addition to owning a stable IComponentHandler, it owns
// the control->DSP parameter bridge required by split-component VST3s such as
// FabFilter. A native editor changes the controller first; the host must then
// deliver those edits to the processor as inputParameterChanges. Without this,
// controller-private state (for example a preset title) can survive a restart
// while the actual processor knobs/bands silently return to defaults.
class RackHostedPlugin : public HostedPlugin {
public:
    RackHostedPlugin() noexcept
        : handler_(this)
    {
    }

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
        reset_controller_bridge();
        using_internal_handler_ = component_handler == nullptr;
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
        reset_controller_bridge();
        using_internal_handler_ = component_handler == nullptr;
        return HostedPlugin::open(path, class_id, sample_rate, channels,
                                  component_handler ? component_handler : &handler_,
                                  error);
    }

    // Called by RackComponentHandler on the vendor/control thread. The ring is
    // fixed-size, lock-free and allocation-free. If an extreme edit burst fills
    // it, retain semantic correctness by requesting a full controller resync;
    // the latest controller values will be copied once DSP creates room.
    bool enqueue_controller_edit(std::uint32_t id, double normalized) noexcept
    {
        const EngineParameterUpdate update{id, normalized};
        if (!controller_edits_.push(update)) {
            force_controller_resync_.store(true, std::memory_order_release);
            return true;
        }
        submitted_edits_.fetch_add(1, std::memory_order_release);
        return true;
    }

    // Called by the native-editor manager on the same control owner immediately
    // after vendor Win32 messages are pumped. Some plug-ins load an internal
    // preset by updating controller values and issuing only
    // restartComponent(kParamValuesChanged), rather than performEdit for every
    // parameter. Mirror that controller snapshot into the same DSP queue.
    void service_component_handler_callbacks() noexcept override
    {
        if (!using_internal_handler_)
            return;

        const std::uint32_t restart_flags = handler_.take_restart_flags();
        if ((restart_flags & static_cast<std::uint32_t>(Steinberg::Vst::kParamValuesChanged)) != 0)
            force_controller_resync_.store(true, std::memory_order_release);
        const std::uint32_t unhandled =
            restart_flags & ~static_cast<std::uint32_t>(Steinberg::Vst::kParamValuesChanged);
        if (unhandled != 0)
            unhandled_restart_flags_.fetch_or(unhandled, std::memory_order_relaxed);

        if (force_controller_resync_.exchange(false, std::memory_order_acq_rel)) {
            controller_resync_active_ = true;
            controller_resync_index_ = 0;
        }
        if (!controller_resync_active_)
            return;

        Steinberg::Vst::IEditController* controller = edit_controller();
        if (!controller) {
            controller_resync_active_ = false;
            controller_resync_index_ = 0;
            return;
        }

        const auto& catalog = parameters();
        while (controller_resync_index_ < catalog.size()) {
            const auto& parameter = catalog[controller_resync_index_];
            const EngineParameterUpdate update{
                parameter.id,
                controller->getParamNormalized(
                    static_cast<Steinberg::Vst::ParamID>(parameter.id))};
            if (!controller_edits_.push(update))
                return;
            submitted_edits_.fetch_add(1, std::memory_order_release);
            ++controller_resync_index_;
        }

        controller_resync_active_ = false;
        controller_resync_index_ = 0;
    }

    // RackGenerationSlot is RackHostedPlugin* in the shipping translation unit,
    // so this hides HostedPlugin::process there. Only the DSP thread drains the
    // controller queue and mutates HostedPlugin's inputParameterChanges object.
    bool process(const ProcessBlockView& block) noexcept
    {
        std::size_t drained = 0;
        const bool queued_ok = drain_controller_edits(drained);
        const bool processed = HostedPlugin::process(block);
        finish_controller_delivery(drained, queued_ok && processed);
        return queued_ok && processed &&
               !controller_delivery_failed_.load(std::memory_order_acquire);
    }

    // The Rack DSP loop calls this on its bounded idle wake too. Therefore a GUI
    // edit made while OBS is not currently delivering an audio block still
    // reaches the processor before a close/save snapshot is serialized.
    bool flush_component_handler_edits() noexcept override
    {
        std::size_t drained = 0;
        const bool queued_ok = drain_controller_edits(drained);
        if (drained == 0)
            return queued_ok &&
                   !controller_delivery_failed_.load(std::memory_order_acquire);

        const bool flushed = HostedPlugin::flush_parameter_changes();
        finish_controller_delivery(drained, queued_ok && flushed);
        return queued_ok && flushed &&
               !controller_delivery_failed_.load(std::memory_order_acquire);
    }

    // State capture is a control-plane transaction. Before getState(), require
    // every accepted editor update (including a preset-wide restart resync) to
    // have crossed the DSP-owned inputParameterChanges frontier. This prevents a
    // snapshot containing a current preset title but stale/default processor DSP.
    bool synchronize_component_handler_state(std::string& error) noexcept override
    {
        if (!using_internal_handler_)
            return true;

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(300);
        for (;;) {
            service_component_handler_callbacks();

            if (controller_delivery_failed_.load(std::memory_order_acquire)) {
                error = "Rack VST3 controller edit delivery failed before state capture";
                return false;
            }

            const std::uint64_t submitted =
                submitted_edits_.load(std::memory_order_acquire);
            const std::uint64_t applied =
                applied_edits_.load(std::memory_order_acquire);
            if (!controller_resync_active_ &&
                !force_controller_resync_.load(std::memory_order_acquire) &&
                applied >= submitted)
                return true;

            if (std::chrono::steady_clock::now() >= deadline) {
                error = "Timed out synchronizing Rack VST3 controller edits before state capture";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool capture_state(PluginStateSnapshot& snapshot, std::string& error) override
    {
        snapshot = {};
        if (!synchronize_component_handler_state(error))
            return false;
        return HostedPlugin::capture_state(snapshot, error);
    }

private:
    static constexpr std::size_t kControllerEditQueueCapacity =
        static_cast<std::size_t>(kMaxParameters) * 4u;

    void reset_controller_bridge() noexcept
    {
        EngineParameterUpdate discarded{};
        while (controller_edits_.pop(discarded)) {
        }
        (void)handler_.take_restart_flags();
        submitted_edits_.store(0, std::memory_order_relaxed);
        applied_edits_.store(0, std::memory_order_relaxed);
        force_controller_resync_.store(false, std::memory_order_relaxed);
        controller_delivery_failed_.store(false, std::memory_order_relaxed);
        unhandled_restart_flags_.store(0, std::memory_order_relaxed);
        controller_resync_active_ = false;
        controller_resync_index_ = 0;
    }

    bool drain_controller_edits(std::size_t& drained) noexcept
    {
        drained = 0;
        bool ok = true;
        EngineParameterUpdate update{};
        while (drained < kControllerEditQueueCapacity && controller_edits_.pop(update)) {
            ++drained;
            if (!HostedPlugin::queue_parameter_from_controller(
                    update.id, update.normalized))
                ok = false;
        }
        if (!ok)
            controller_delivery_failed_.store(true, std::memory_order_release);
        return ok;
    }

    void finish_controller_delivery(std::size_t drained, bool delivered) noexcept
    {
        if (drained != 0)
            applied_edits_.fetch_add(static_cast<std::uint64_t>(drained),
                                     std::memory_order_release);
        if (!delivered)
            controller_delivery_failed_.store(true, std::memory_order_release);
    }

    RackComponentHandler handler_;
    SpscRing<EngineParameterUpdate, kControllerEditQueueCapacity> controller_edits_;
    std::atomic<std::uint64_t> submitted_edits_{0};
    std::atomic<std::uint64_t> applied_edits_{0};
    std::atomic<bool> force_controller_resync_{false};
    std::atomic<bool> controller_delivery_failed_{false};
    std::atomic<std::uint32_t> unhandled_restart_flags_{0};
    std::size_t controller_resync_index_ = 0;
    bool controller_resync_active_ = false;
    bool using_internal_handler_ = true;
};

inline Steinberg::tresult PLUGIN_API RackComponentHandler::performEdit(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value)
{
    if (!owner_)
        return Steinberg::kResultFalse;
    return owner_->enqueue_controller_edit(static_cast<std::uint32_t>(id), value)
               ? Steinberg::kResultTrue
               : Steinberg::kResultFalse;
}

} // namespace safevst3

// main_r3_2_hosted.cpp includes this header before the existing Rack source so
// all Rack-local HostedPlugin tokens resolve to the wrapper above. The macro is
// intentionally confined to that translation unit.
#define HostedPlugin RackHostedPlugin

#endif
