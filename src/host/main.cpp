#ifdef _WIN32

#include "common/io_restart_transaction.hpp"
#include "common/lifecycle_restart_policy.hpp"
#include "common/parameter_catalog_snapshot.hpp"
#include "common/parameter_refresh_transaction.hpp"
#include "common/parameter_utils.hpp"
#include "common/reload_component_transaction.hpp"
#include "common/spsc_ring.hpp"
#include "host/native_editor.hpp"
#include "host/vst3_engine.hpp"
#include "platform/windows/win_ipc.hpp"

#include "pluginterfaces/base/funknown.h"

#include <avrt.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#pragma comment(lib, "Avrt.lib")

namespace {

constexpr long kLatencyRestartFailedError = 17;
constexpr long kParameterRefreshFailedError = 18;
constexpr long kIoRestartFailedError = 19;
constexpr long kReloadComponentFailedError = 20;

constexpr std::size_t kParameterTransferCapacity = safevst3::kMaxParameters * safevst3::kSlotCount;
constexpr std::size_t kMaxProcessorCommandsPerWake = 32;
constexpr std::size_t kMaxPausedHostCatchupPasses = 4;
constexpr ULONGLONG kPausedHeartbeatGraceMs = 2000;
using ParameterQueue = safevst3::SpscRing<safevst3::EngineParameterUpdate, kParameterTransferCapacity>;

static_assert(safevst3::kRestartReloadComponent ==
              static_cast<std::uint32_t>(Steinberg::Vst::kReloadComponent));
static_assert(safevst3::kRestartIoChanged ==
              static_cast<std::uint32_t>(Steinberg::Vst::kIoChanged));
static_assert(safevst3::kRestartParamValuesChanged ==
              static_cast<std::uint32_t>(Steinberg::Vst::kParamValuesChanged));
static_assert(safevst3::kRestartLatencyChanged ==
              static_cast<std::uint32_t>(Steinberg::Vst::kLatencyChanged));
static_assert(safevst3::kRestartParamTitlesChanged ==
              static_cast<std::uint32_t>(Steinberg::Vst::kParamTitlesChanged));

class NativeOverrideBuffer {
public:
    bool record(const safevst3::EngineParameterUpdate& update) noexcept
    {
        for (std::size_t i = 0; i < count_; ++i) {
            if (updates_[i].id == update.id) {
                updates_[i].normalized = update.normalized;
                return true;
            }
        }
        if (count_ >= updates_.size()) {
            overflowed_ = true;
            return false;
        }
        updates_[count_++] = update;
        return true;
    }

    bool contains(std::uint32_t id) const noexcept
    {
        for (std::size_t i = 0; i < count_; ++i) {
            if (updates_[i].id == id)
                return true;
        }
        return false;
    }

    bool empty() const noexcept { return count_ == 0 && !overflowed_; }
    bool overflowed() const noexcept { return overflowed_; }
    std::size_t size() const noexcept { return count_; }
    const safevst3::EngineParameterUpdate& at(std::size_t index) const noexcept { return updates_[index]; }

    void clear() noexcept
    {
        count_ = 0;
        overflowed_ = false;
    }

private:
    std::array<safevst3::EngineParameterUpdate, kParameterTransferCapacity> updates_{};
    std::size_t count_ = 0;
    bool overflowed_ = false;
};

std::string narrow(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::map<std::wstring, std::wstring> parse_args(int argc, wchar_t** argv)
{
    std::map<std::wstring, std::wstring> values;
    for (int i = 1; i + 1 < argc; i += 2)
        values[argv[i]] = argv[i + 1];
    return values;
}

std::int64_t double_to_bits(double value) noexcept
{
    std::int64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double bits_to_double(std::int64_t bits) noexcept
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void copy_text(char* destination, std::size_t capacity, const std::string& value)
{
    if (!destination || capacity == 0)
        return;
    const std::size_t count = std::min(capacity - 1, value.size());
    std::memcpy(destination, value.data(), count);
    destination[count] = '\0';
}

void request_consistency_recovery(safevst3::SharedAudioRegion* region, long error_code) noexcept
{
    if (!region)
        return;
    InterlockedExchange(&region->last_error, error_code);
    InterlockedExchange(&region->shutdown_requested, 1);
}

void publish_control_heartbeat(safevst3::SharedAudioRegion* region) noexcept
{
    if (!region)
        return;
    InterlockedExchange64(
        reinterpret_cast<volatile LONG64*>(&region->helper_heartbeat_ms),
        static_cast<LONG64>(GetTickCount64()));
    InterlockedIncrement(&region->helper_progress_generation);
}

void publish_dsp_heartbeat(safevst3::SharedAudioRegion* region) noexcept
{
    if (!region)
        return;
    InterlockedExchange64(
        reinterpret_cast<volatile LONG64*>(&region->dsp_heartbeat_ms),
        static_cast<LONG64>(GetTickCount64()));
    InterlockedIncrement(&region->dsp_progress_generation);
}

void publish_parameter_value(safevst3::SharedAudioRegion* region,
                             std::uint32_t id,
                             double normalized) noexcept
{
    if (!region)
        return;
    for (std::uint32_t i = 0; i < region->parameter_count; ++i) {
        auto& descriptor = region->parameters[i];
        if (descriptor.id != id)
            continue;
        InterlockedExchange64(
            reinterpret_cast<volatile LONG64*>(&descriptor.current_value_bits),
            static_cast<LONG64>(double_to_bits(normalized)));
        return;
    }
}

bool enqueue_processor_feedback(safevst3::SharedAudioRegion* region,
                                safevst3::Vst3Engine& engine,
                                ParameterQueue& feedback,
                                std::atomic<bool>& resync_required) noexcept
{
    bool notify_control = false;
    std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> updates{};
    const std::size_t count = engine.take_parameter_updates(updates.data(), updates.size());
    for (std::size_t i = 0; i < count; ++i) {
        publish_parameter_value(region, updates[i].id, updates[i].normalized);
        if (!feedback.push(updates[i])) {
            resync_required.store(true, std::memory_order_release);
            if (region)
                InterlockedExchange(&region->last_error, 4);
            notify_control = true;
            continue;
        }
        notify_control = true;
    }
    return notify_control;
}

struct ProcessorCommandDrainResult {
    std::size_t drained = 0;
    bool failed = false;
};

ProcessorCommandDrainResult drain_processor_commands(safevst3::SharedAudioRegion* region,
                                                      safevst3::Vst3Engine& engine,
                                                      ParameterQueue& commands,
                                                      std::size_t max_commands) noexcept
{
    ProcessorCommandDrainResult result{};
    safevst3::EngineParameterUpdate update{};
    while (result.drained < max_commands && commands.pop(update)) {
        ++result.drained;
        if (engine.queue_processor_parameter(update.id, update.normalized))
            continue;

        // The command has already left the bounded SPSC queue. Continuing would
        // silently advance the controller/shared mirror without proving delivery
        // to the processor. Taint this helper and let the outer watchdog rebuild
        // it from S1's last-known-good checkpoint instead of serializing a mixed
        // component/controller state.
        result.failed = true;
        request_consistency_recovery(region, 5);
        break;
    }
    return result;
}

void drain_controller_feedback(safevst3::SharedAudioRegion* region,
                               safevst3::Vst3Engine& engine,
                               ParameterQueue& feedback) noexcept
{
    safevst3::EngineParameterUpdate update{};
    while (feedback.pop(update)) {
        (void)engine.set_controller_parameter(update.id, update.normalized);
        publish_parameter_value(region, update.id, update.normalized);
    }
}

void drain_controller_feedback_preserving_native_overrides(
    safevst3::SharedAudioRegion* region,
    safevst3::Vst3Engine& engine,
    ParameterQueue& feedback,
    const NativeOverrideBuffer& native_overrides) noexcept
{
    safevst3::EngineParameterUpdate update{};
    while (feedback.pop(update)) {
        if (native_overrides.contains(update.id))
            continue;
        (void)engine.set_controller_parameter(update.id, update.normalized);
        publish_parameter_value(region, update.id, update.normalized);
    }
}

void sync_engine_mirror_to_controller(safevst3::SharedAudioRegion* region,
                                      safevst3::Vst3Engine& engine) noexcept
{
    for (const auto& parameter : engine.parameters()) {
        (void)engine.set_controller_parameter(parameter.id, parameter.current_normalized);
        publish_parameter_value(region, parameter.id, parameter.current_normalized);
    }
}

void reconcile_controller_feedback_after_pause(safevst3::SharedAudioRegion* region,
                                               safevst3::Vst3Engine& engine,
                                               ParameterQueue& feedback,
                                               std::atomic<bool>& resync_required) noexcept
{
    drain_controller_feedback(region, engine, feedback);
    if (!resync_required.exchange(false, std::memory_order_acq_rel))
        return;
    sync_engine_mirror_to_controller(region, engine);
}

void publish_engine_updates_direct(safevst3::SharedAudioRegion* region,
                                   safevst3::Vst3Engine& engine) noexcept
{
    std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> updates{};
    const std::size_t count = engine.take_parameter_updates(updates.data(), updates.size());
    for (std::size_t i = 0; i < count; ++i)
        publish_parameter_value(region, updates[i].id, updates[i].normalized);
}


bool publish_parameter_catalog(safevst3::SharedAudioRegion* region,
                               safevst3::Vst3Engine& engine,
                               bool metadata_changed) noexcept
{
    if (!region)
        return false;

    // Parameter metadata publication and OBS-side parameter edits share the
    // same bounded odd/even write claim. A plain seqlock protects readers only;
    // the CAS claim also prevents an edit from writing into a descriptor while
    // this control-plane transaction changes its ID/topology.
    long writing_generation = 0;
    bool claimed = false;
    for (std::size_t attempt = 0; attempt < 8; ++attempt) {
        const long stable = InterlockedCompareExchange(
            &region->parameter_catalog_generation, 0, 0);
        if ((stable & 1L) != 0) {
            SwitchToThread();
            continue;
        }
        if (InterlockedCompareExchange(
                &region->parameter_catalog_generation, stable + 1, stable) == stable) {
            writing_generation = stable + 1;
            claimed = true;
            break;
        }
        SwitchToThread();
    }
    if (!claimed)
        return false;

    bool ok = true;
    if (metadata_changed) {
        const std::uint32_t previous_count =
            std::min(region->parameter_count, safevst3::kMaxParameters);

        // A host edit that arrived after the paused-frontier catch-up must not
        // be silently rebound to a newly enumerated parameter ID/topology.
        for (std::uint32_t i = 0; i < previous_count; ++i) {
            auto& descriptor = region->parameters[i];
            if (InterlockedCompareExchange(&descriptor.pending_generation, 0, 0) !=
                InterlockedCompareExchange(&descriptor.applied_generation, 0, 0)) {
                ok = false;
                break;
            }
        }

        if (ok) {
            const auto projection = safevst3::project_parameter_catalog(
                engine.parameters(), safevst3::kMaxParameters,
                [&](std::size_t index, const auto& source) noexcept {
                    auto& destination = region->parameters[index];
                    destination.id = source.id;
                    destination.step_count = source.step_count;
                    destination.flags = source.flags;
                    destination.default_normalized = source.default_normalized;
                    destination.current_value_bits = double_to_bits(source.current_normalized);
                    destination.pending_value_bits = destination.current_value_bits;
                    destination.pending_generation = 0;
                    destination.applied_generation = 0;
                    copy_text(destination.title, safevst3::kParameterTitleBytes, source.title);
                    copy_text(destination.units, safevst3::kParameterUnitsBytes, source.units);
                });
            const std::uint32_t count = static_cast<std::uint32_t>(projection.published_count);
            for (std::uint32_t i = count; i < previous_count; ++i)
                std::memset(&region->parameters[i], 0, sizeof(region->parameters[i]));

            region->parameter_total_count = projection.total_count;
            region->parameter_count = count;

            // enumerate_parameters() publishes current controller values
            // directly in the new catalog, so stale pre-enumeration updates are
            // discarded rather than replayed against a changed ID topology.
            std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> discarded{};
            (void)engine.take_parameter_updates(discarded.data(), discarded.size());
        }
    } else {
        // Values are 64-bit atomics, but wrapping the batch in the catalog
        // generation prevents OBS from accepting a vector containing a mix of
        // old and new values when several parameters change together.
        publish_engine_updates_direct(region, engine);
    }

    MemoryBarrier();
    const long stable_generation = writing_generation + 1;
    InterlockedExchange(&region->parameter_catalog_generation, stable_generation);
    return ok;
}

// S1.4 requires a stricter reconciliation frontier than the legacy best-effort
// paths: every controller synchronization failure must reject the transaction so
// the coordinator can recover instead of publishing a mixed catalog.
bool reconcile_parameter_refresh_feedback_checked(
    safevst3::SharedAudioRegion* region,
    safevst3::Vst3Engine& engine,
    ParameterQueue& feedback,
    std::atomic<bool>& resync_required,
    NativeOverrideBuffer& native_overrides) noexcept
{
    if (!region || native_overrides.overflowed())
        return false;

    safevst3::EngineParameterUpdate update{};
    while (feedback.pop(update)) {
        if (native_overrides.contains(update.id))
            continue;
        if (!engine.set_controller_parameter(update.id, update.normalized))
            return false;
        publish_parameter_value(region, update.id, update.normalized);
    }

    if (resync_required.exchange(false, std::memory_order_acq_rel)) {
        for (const auto& parameter : engine.parameters()) {
            if (native_overrides.contains(parameter.id))
                continue;
            if (!engine.set_controller_parameter(parameter.id, parameter.current_normalized))
                return false;
            publish_parameter_value(region, parameter.id, parameter.current_normalized);
        }
    }

    if (native_overrides.empty())
        return true;

    for (std::size_t i = 0; i < native_overrides.size(); ++i) {
        const auto& native = native_overrides.at(i);
        if (!engine.set_controller_parameter(native.id, native.normalized) ||
            !engine.queue_processor_parameter(native.id, native.normalized))
            return false;
        publish_parameter_value(region, native.id, native.normalized);
    }

    if (!engine.flush_parameter_changes())
        return false;

    // The zero-sample flush may itself emit processor feedback. The engine
    // mirror is authoritative at this paused frontier; apply it to the
    // controller and publish only if every synchronization call succeeds.
    std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> discarded{};
    (void)engine.take_parameter_updates(discarded.data(), discarded.size());
    for (const auto& parameter : engine.parameters()) {
        if (!engine.set_controller_parameter(parameter.id, parameter.current_normalized))
            return false;
        publish_parameter_value(region, parameter.id, parameter.current_normalized);
    }

    native_overrides.clear();
    return true;
}

bool reconcile_native_edits_after_pause(safevst3::SharedAudioRegion* region,
                                        safevst3::Vst3Engine& engine,
                                        ParameterQueue& feedback,
                                        NativeOverrideBuffer& native_overrides) noexcept
{
    if (native_overrides.empty())
        return true;
    if (native_overrides.overflowed()) {
        request_consistency_recovery(region, 15);
        return false;
    }

    drain_controller_feedback_preserving_native_overrides(
        region, engine, feedback, native_overrides);

    for (std::size_t i = 0; i < native_overrides.size(); ++i) {
        const auto& update = native_overrides.at(i);
        if (!engine.set_controller_parameter(update.id, update.normalized) ||
            !engine.queue_processor_parameter(update.id, update.normalized)) {
            request_consistency_recovery(region, 13);
            return false;
        }
        publish_parameter_value(region, update.id, update.normalized);
    }

    if (!engine.flush_parameter_changes()) {
        request_consistency_recovery(region, 13);
        return false;
    }

    publish_engine_updates_direct(region, engine);
    sync_engine_mirror_to_controller(region, engine);
    native_overrides.clear();
    return true;
}

bool catch_up_pending_host_parameters_after_pause(safevst3::SharedAudioRegion* region,
                                                  safevst3::Vst3Engine& engine) noexcept
{
    if (!region)
        return false;

    const std::uint32_t count = std::min(region->parameter_count, safevst3::kMaxParameters);
    for (std::size_t pass = 0; pass < kMaxPausedHostCatchupPasses; ++pass) {
        std::array<long, safevst3::kMaxParameters> ack_generations{};
        std::array<bool, safevst3::kMaxParameters> touched{};
        bool queued_any = false;

        for (std::uint32_t i = 0; i < count; ++i) {
            auto& descriptor = region->parameters[i];
            const long generation = InterlockedCompareExchange(&descriptor.pending_generation, 0, 0);
            const long applied = InterlockedCompareExchange(&descriptor.applied_generation, 0, 0);
            if (generation == applied)
                continue;

            const auto raw_bits = InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64*>(&descriptor.pending_value_bits), 0, 0);
            const double value = safevst3::normalize_parameter_value(
                bits_to_double(static_cast<std::int64_t>(raw_bits)), descriptor.step_count);

            if (!engine.set_controller_parameter(descriptor.id, value) ||
                !engine.queue_processor_parameter(descriptor.id, value)) {
                request_consistency_recovery(region, 12);
                return false;
            }

            publish_parameter_value(region, descriptor.id, value);
            ack_generations[i] = generation;
            touched[i] = true;
            queued_any = true;
        }

        if (queued_any && !engine.flush_parameter_changes()) {
            request_consistency_recovery(region, 12);
            return false;
        }
        if (queued_any) {
            publish_engine_updates_direct(region, engine);
            sync_engine_mirror_to_controller(region, engine);
        }

        for (std::uint32_t i = 0; i < count; ++i) {
            if (!touched[i])
                continue;
            InterlockedExchange(&region->parameters[i].applied_generation, ack_generations[i]);
            InterlockedIncrement(&region->state_dirty_generation);
        }

        MemoryBarrier();
        bool pending = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& descriptor = region->parameters[i];
            if (InterlockedCompareExchange(&descriptor.pending_generation, 0, 0) !=
                InterlockedCompareExchange(&descriptor.applied_generation, 0, 0)) {
                pending = true;
                break;
            }
        }
        if (!pending)
            return true;
    }

    InterlockedExchange(&region->last_error, 12);
    return false;
}

class ComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    ComponentHandler(safevst3::SharedAudioRegion* region,
                     ParameterQueue& control_to_dsp,
                     NativeOverrideBuffer& native_overrides,
                     HANDLE dsp_event,
                     HANDLE control_event)
        : region_(region), control_to_dsp_(control_to_dsp),
          native_overrides_(native_overrides), dsp_event_(dsp_event),
          control_event_(control_event)
    {
    }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override
    {
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override
    {
        const safevst3::EngineParameterUpdate update{static_cast<std::uint32_t>(id), value};

        if (native_overrides_.contains(update.id)) {
            const bool retained = native_overrides_.record(update);
            publish_parameter_value(region_, update.id, value);
            if (region_) {
                InterlockedIncrement(&region_->state_dirty_generation);
                InterlockedExchange(&region_->last_error, retained ? 6 : 15);
            }
            if (!retained && region_)
                InterlockedExchange(&region_->shutdown_requested, 1);
            if (dsp_event_)
                SetEvent(dsp_event_);
            if (control_event_)
                SetEvent(control_event_);
            edit_pending_.store(true, std::memory_order_release);
            return retained ? Steinberg::kResultOk : Steinberg::kResultFalse;
        }

        if (!control_to_dsp_.push(update)) {
            const bool retained = native_overrides_.record(update);
            publish_parameter_value(region_, update.id, value);
            if (region_) {
                InterlockedIncrement(&region_->state_dirty_generation);
                InterlockedExchange(&region_->last_error, retained ? 6 : 15);
            }
            if (!retained && region_)
                InterlockedExchange(&region_->shutdown_requested, 1);
            if (dsp_event_)
                SetEvent(dsp_event_);
            if (control_event_)
                SetEvent(control_event_);
            edit_pending_.store(true, std::memory_order_release);
            return retained ? Steinberg::kResultOk : Steinberg::kResultFalse;
        }
        publish_parameter_value(region_, update.id, value);
        if (region_)
            InterlockedIncrement(&region_->state_dirty_generation);
        if (dsp_event_)
            SetEvent(dsp_event_);
        edit_pending_.store(true, std::memory_order_release);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override
    {
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
    {
        restart_flags_.fetch_or(flags, std::memory_order_release);
        if (region_ && flags != 0)
            InterlockedIncrement(&region_->state_dirty_generation);
        return Steinberg::kResultOk;
    }

    bool take_edit_pending() noexcept
    {
        return edit_pending_.exchange(false, std::memory_order_acq_rel);
    }

    Steinberg::int32 take_restart_flags() noexcept
    {
        return restart_flags_.exchange(0, std::memory_order_acq_rel);
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
        if (!obj)
            return Steinberg::kInvalidArgument;
        *obj = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            addRef();
            return Steinberg::kResultTrue;
        }
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }

private:
    safevst3::SharedAudioRegion* region_ = nullptr;
    ParameterQueue& control_to_dsp_;
    NativeOverrideBuffer& native_overrides_;
    HANDLE dsp_event_ = nullptr;
    HANDLE control_event_ = nullptr;
    std::atomic<bool> edit_pending_{false};
    std::atomic<Steinberg::int32> restart_flags_{0};
};

class DspWorker {
public:
    DspWorker(safevst3::SharedAudioRegion* region,
              safevst3::Vst3Engine& engine,
              ParameterQueue& control_to_dsp,
              ParameterQueue& dsp_to_control,
              std::atomic<bool>& feedback_resync_required,
              HANDLE dsp_event,
              HANDLE control_event,
              HANDLE response_event)
        : region_(region), engine_(engine), control_to_dsp_(control_to_dsp),
          dsp_to_control_(dsp_to_control), feedback_resync_required_(feedback_resync_required),
          dsp_event_(dsp_event), control_event_(control_event), response_event_(response_event)
    {
    }

    ~DspWorker() { stop(); }

    bool start(std::string& error)
    {
        transition_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!transition_event_) {
            error = "Failed to create DSP transition acknowledgement event";
            return false;
        }
        try {
            thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
        } catch (...) {
            CloseHandle(transition_event_);
            transition_event_ = nullptr;
            error = "Failed to start dedicated DSP worker";
            return false;
        }
        return true;
    }

    void stop() noexcept
    {
        if (thread_.joinable()) {
            thread_.request_stop();
            if (dsp_event_)
                SetEvent(dsp_event_);
            if (transition_event_)
                SetEvent(transition_event_);
            thread_ = std::jthread{};
        }
        if (transition_event_) {
            CloseHandle(transition_event_);
            transition_event_ = nullptr;
        }
    }

    bool pause(DWORD timeout_ms) noexcept
    {
        if (!thread_.joinable() || !transition_event_)
            return false;

        const std::uint64_t token = request_transition(true);
        if (wait_for_transition(token, timeout_ms))
            return true;

        (void)request_transition(false);
        return false;
    }

    bool resume(DWORD timeout_ms) noexcept
    {
        if (!thread_.joinable() || !transition_event_)
            return false;
        const std::uint64_t token = request_transition(false);
        return wait_for_transition(token, timeout_ms);
    }

private:
    static constexpr std::uint64_t kPausedTokenBit = 1u;

    static bool token_requests_pause(std::uint64_t token) noexcept
    {
        return (token & kPausedTokenBit) != 0;
    }

    std::uint64_t request_transition(bool paused) noexcept
    {
        const std::uint64_t generation =
            next_transition_generation_.fetch_add(1u, std::memory_order_relaxed);
        const std::uint64_t token = (generation << 1u) | (paused ? kPausedTokenBit : 0u);
        requested_transition_.store(token, std::memory_order_release);
        if (dsp_event_)
            SetEvent(dsp_event_);
        return token;
    }

    bool wait_for_transition(std::uint64_t token, DWORD timeout_ms) noexcept
    {
        const ULONGLONG deadline = GetTickCount64() + timeout_ms;
        while (acknowledged_transition_.load(std::memory_order_acquire) != token) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                return false;
            const DWORD remaining = static_cast<DWORD>(deadline - now);
            const DWORD wait = WaitForSingleObject(transition_event_, remaining);
            if (wait == WAIT_FAILED)
                return false;
        }
        return true;
    }

    void acknowledge_transition(std::uint64_t token) noexcept
    {
        acknowledged_transition_.store(token, std::memory_order_release);
        if (transition_event_)
            SetEvent(transition_event_);
    }

    void run(std::stop_token stop) noexcept
    {
        DWORD task_index = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
        if (mmcss)
            AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

        std::uint64_t acknowledged = acknowledged_transition_.load(std::memory_order_relaxed);
        publish_dsp_heartbeat(region_);

        while (!stop.stop_requested() &&
               InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0) {
            const std::uint64_t requested = requested_transition_.load(std::memory_order_acquire);
            if (requested != acknowledged) {
                if (!token_requests_pause(requested)) {
                    acknowledged = requested;
                    acknowledge_transition(acknowledged);
                } else {
                    bool feedback_pending = false;
                    const auto drained = drain_processor_commands(
                        region_, engine_, control_to_dsp_, kParameterTransferCapacity);
                    if (drained.failed)
                        return;
                    if (!engine_.flush_parameter_changes()) {
                        request_consistency_recovery(region_, 2);
                        return;
                    }
                    feedback_pending |= enqueue_processor_feedback(
                        region_, engine_, dsp_to_control_, feedback_resync_required_);
                    if (feedback_pending && control_event_)
                        SetEvent(control_event_);

                    if (requested_transition_.load(std::memory_order_acquire) != requested)
                        continue;

                    acknowledged = requested;
                    acknowledge_transition(acknowledged);
                    ULONGLONG pause_started = GetTickCount64();
                    std::uint64_t running_to_ack = 0;

                    while (!stop.stop_requested() &&
                           InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0) {
                        const std::uint64_t latest =
                            requested_transition_.load(std::memory_order_acquire);
                        if (latest != acknowledged) {
                            if (!token_requests_pause(latest)) {
                                running_to_ack = latest;
                                break;
                            }

                            acknowledged = latest;
                            acknowledge_transition(acknowledged);
                            pause_started = GetTickCount64();
                        }

                        if (GetTickCount64() - pause_started <= kPausedHeartbeatGraceMs)
                            publish_dsp_heartbeat(region_);
                        WaitForSingleObject(dsp_event_, 100);
                    }

                    if (running_to_ack != 0) {
                        acknowledged = running_to_ack;
                        acknowledge_transition(acknowledged);
                    }
                    continue;
                }
            }

            publish_dsp_heartbeat(region_);
            const DWORD wait = WaitForSingleObject(dsp_event_, 250);
            if (stop.stop_requested())
                break;
            if (wait == WAIT_FAILED) {
                InterlockedExchange(&region_->last_error, 7);
                break;
            }
            if (wait == WAIT_TIMEOUT)
                continue;

            bool processed_any = false;
            bool feedback_pending = false;
            for (std::uint32_t i = 0; i < safevst3::kSlotCount; ++i) {
                auto& slot = region_->slots[i];
                if (InterlockedCompareExchange(&slot.state,
                                               static_cast<long>(safevst3::SlotState::Processing),
                                               static_cast<long>(safevst3::SlotState::Ready)) !=
                    static_cast<long>(safevst3::SlotState::Ready))
                    continue;

                processed_any = true;
                const bool ok = engine_.process(slot);
                if (!ok) {
                    InterlockedExchange(&slot.result,
                                        static_cast<long>(safevst3::ProcessResult::VstProcessError));
                    MemoryBarrier();
                    InterlockedExchange(&slot.state, static_cast<long>(safevst3::SlotState::Done));
                    SetEvent(response_event_);
                    request_consistency_recovery(region_, 16);
                    return;
                }

                feedback_pending |= enqueue_processor_feedback(
                    region_, engine_, dsp_to_control_, feedback_resync_required_);
                InterlockedExchange(&slot.result, static_cast<long>(safevst3::ProcessResult::Ok));
                MemoryBarrier();
                InterlockedExchange(&slot.state, static_cast<long>(safevst3::SlotState::Done));
                SetEvent(response_event_);
            }

            const auto drained = drain_processor_commands(
                region_, engine_, control_to_dsp_, kMaxProcessorCommandsPerWake);
            if (drained.failed)
                return;
            if (drained.drained > 0 && !processed_any) {
                if (!engine_.flush_parameter_changes()) {
                    request_consistency_recovery(region_, 2);
                    return;
                }
                feedback_pending |= enqueue_processor_feedback(
                    region_, engine_, dsp_to_control_, feedback_resync_required_);
            }
            if (drained.drained == kMaxProcessorCommandsPerWake && dsp_event_)
                SetEvent(dsp_event_);

            if (feedback_pending && control_event_)
                SetEvent(control_event_);
            publish_dsp_heartbeat(region_);
        }

        if (transition_event_)
            SetEvent(transition_event_);
        if (mmcss)
            AvRevertMmThreadCharacteristics(mmcss);
    }

    safevst3::SharedAudioRegion* region_ = nullptr;
    safevst3::Vst3Engine& engine_;
    ParameterQueue& control_to_dsp_;
    ParameterQueue& dsp_to_control_;
    std::atomic<bool>& feedback_resync_required_;
    HANDLE dsp_event_ = nullptr;
    HANDLE control_event_ = nullptr;
    HANDLE response_event_ = nullptr;
    HANDLE transition_event_ = nullptr;
    std::atomic<std::uint64_t> requested_transition_{0};
    std::atomic<std::uint64_t> acknowledged_transition_{0};
    std::atomic<std::uint64_t> next_transition_generation_{1};
    std::jthread thread_;
};

class HelperParameterRefreshTarget final : public safevst3::ParameterRefreshCoordinatorTarget {
public:
    HelperParameterRefreshTarget(DspWorker& dsp,
                                 safevst3::Vst3Engine& engine,
                                 safevst3::SharedAudioRegion* region,
                                 ParameterQueue& dsp_to_control,
                                 NativeOverrideBuffer& native_overrides,
                                 std::atomic<bool>& feedback_resync_required,
                                 bool metadata_refresh)
        : dsp_(dsp), engine_(engine), region_(region), dsp_to_control_(dsp_to_control),
          native_overrides_(native_overrides),
          feedback_resync_required_(feedback_resync_required),
          metadata_refresh_(metadata_refresh)
    {
    }

    bool pause_dsp() noexcept override { return dsp_.pause(2000); }

    bool reconcile_pending() noexcept override
    {
        if (!reconcile_parameter_refresh_feedback_checked(
                region_, engine_, dsp_to_control_, feedback_resync_required_,
                native_overrides_))
            return false;
        if (!catch_up_pending_host_parameters_after_pause(region_, engine_))
            return false;
        return native_overrides_.empty();
    }

    bool refresh_parameter_values() noexcept override
    {
        engine_.refresh_parameter_values();
        return true;
    }

    bool refresh_parameter_metadata() noexcept override
    {
        std::string error;
        if (engine_.refresh_parameter_metadata(error))
            return true;
        if (!error.empty())
            std::cerr << "VST3 parameter metadata refresh: " << error << '\n';
        return false;
    }

    bool publish_parameter_catalog() noexcept override
    {
        return ::publish_parameter_catalog(region_, engine_, metadata_refresh_);
    }

    bool resume_dsp() noexcept override { return dsp_.resume(2000); }

    void request_recovery() noexcept override
    {
        // Some legacy catch-up helpers already transition shutdown_requested on
        // their own hard failure. Preserve exactly one recovery transition and
        // avoid overwriting the first diagnostic reason.
        if (region_ && InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0)
            request_consistency_recovery(region_, kParameterRefreshFailedError);
    }

private:
    DspWorker& dsp_;
    safevst3::Vst3Engine& engine_;
    safevst3::SharedAudioRegion* region_ = nullptr;
    ParameterQueue& dsp_to_control_;
    NativeOverrideBuffer& native_overrides_;
    std::atomic<bool>& feedback_resync_required_;
    bool metadata_refresh_ = false;
};

class HelperReloadComponentTarget final : public safevst3::ReloadComponentTarget {
public:
    HelperReloadComponentTarget(DspWorker& dsp,
                                safevst3::Vst3Engine& engine,
                                safevst3::SharedAudioRegion* region,
                                ParameterQueue& dsp_to_control,
                                NativeOverrideBuffer& native_overrides,
                                std::atomic<bool>& feedback_resync_required,
                                safevst3::NativeEditorWindow& editor,
                                ComponentHandler& component_handler,
                                std::string path,
                                std::string class_id,
                                std::uint32_t sample_rate,
                                std::uint32_t channels)
        : dsp_(dsp), engine_(engine), region_(region), dsp_to_control_(dsp_to_control),
          native_overrides_(native_overrides), feedback_resync_required_(feedback_resync_required),
          editor_(editor), component_handler_(component_handler), path_(std::move(path)),
          class_id_(std::move(class_id)), sample_rate_(sample_rate), channels_(channels)
    {
    }

    bool reload_pause_dsp() noexcept override { return dsp_.pause(2000); }

    bool reload_reconcile_pending() noexcept override
    {
        if (!reconcile_parameter_refresh_feedback_checked(
                region_, engine_, dsp_to_control_, feedback_resync_required_, native_overrides_))
            return false;
        if (!catch_up_pending_host_parameters_after_pause(region_, engine_))
            return false;
        return native_overrides_.empty();
    }

    bool reload_capture_state() noexcept override
    {
        std::string error;
        if (engine_.capture_state(snapshot_, error))
            return true;
        if (!error.empty())
            std::cerr << "VST3 reload preflight state capture: " << error << '\n';
        return false;
    }

    bool reload_close_editor() noexcept override
    {
        editor_.close();
        if (region_)
            InterlockedExchange(&region_->editor_status,
                                static_cast<long>(safevst3::EditorStatus::Closed));
        return true;
    }

    bool reload_recreate_plugin() noexcept override
    {
        engine_.set_component_handler(nullptr);
        engine_.close();

        std::string error;
        if (!engine_.open(path_, class_id_, sample_rate_, channels_, error)) {
            if (!error.empty())
                std::cerr << "VST3 full reload recreate: " << error << '\n';
            return false;
        }
        engine_.set_component_handler(&component_handler_);
        return true;
    }

    bool reload_restore_state() noexcept override
    {
        std::string error;
        if (engine_.restore_state(snapshot_, error))
            return true;
        if (!error.empty())
            std::cerr << "VST3 full reload state restore: " << error << '\n';
        return false;
    }

    bool reload_reconcile_restored_state() noexcept override
    {
        if (!reconcile_parameter_refresh_feedback_checked(
                region_, engine_, dsp_to_control_, feedback_resync_required_, native_overrides_))
            return false;
        if (!catch_up_pending_host_parameters_after_pause(region_, engine_))
            return false;
        if (!native_overrides_.empty())
            return false;

        // restartComponent() can fire while the restored controller/component
        // settle. One broad regeneration below already covers the known
        // incremental requests (I/O, parameter values/titles and latency).
        // A recursive reload or unknown bit cannot be absorbed safely: fail the
        // transaction once and let the existing bounded recovery policy rebuild
        // from the last-known-good checkpoint instead of entering a reload loop.
        const auto post_restore_plan = safevst3::plan_restart_component(
            static_cast<std::uint32_t>(component_handler_.take_restart_flags()));
        if (!safevst3::can_absorb_restart_before_full_regeneration(post_restore_plan)) {
            std::cerr << "VST3 full reload did not reach a safe pre-regeneration frontier\n";
            return false;
        }
        return true;
    }

    bool reload_regenerate_runtime() noexcept override
    {
        safevst3::IoLayout layout{};
        std::uint32_t latency_samples = 0;
        std::string error;
        if (!engine_.reconfigure_io_after_restart(layout, latency_samples, error)) {
            if (!error.empty())
                std::cerr << "VST3 full reload I/O regeneration: " << error << '\n';
            return false;
        }

        // A plug-in may emit parameter callbacks while confirming its restored
        // topology. Reconcile them before rebuilding the public catalog so the
        // resumed processor/controller/shared-state trio has one fixed point.
        if (!reconcile_parameter_refresh_feedback_checked(
                region_, engine_, dsp_to_control_, feedback_resync_required_, native_overrides_))
            return false;
        if (!catch_up_pending_host_parameters_after_pause(region_, engine_))
            return false;
        if (!native_overrides_.empty())
            return false;

        // The pre-reload instance and the newly restored instance can expose
        // different parameter structure. Drop pre-regeneration feedback and
        // enumerate metadata/count/values again from the final controller.
        std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> discarded{};
        (void)engine_.take_parameter_updates(discarded.data(), discarded.size());
        if (!engine_.refresh_parameter_metadata(error)) {
            if (!error.empty())
                std::cerr << "VST3 full reload parameter regeneration: " << error << '\n';
            return false;
        }

        // This is the bounded fixed-point gate. Any restart request emitted by
        // the final regeneration means the recreated instance is still asking
        // the host to mutate lifecycle state. Do not recurse or loop here;
        // recover once from the OBS-side last-known-good path instead.
        const auto final_restart_plan = safevst3::plan_restart_component(
            static_cast<std::uint32_t>(component_handler_.take_restart_flags()));
        if (!safevst3::reload_regeneration_reached_fixed_point(final_restart_plan)) {
            std::cerr << "VST3 full reload regeneration did not reach a fixed point\n";
            return false;
        }

        regenerated_latency_samples_ = latency_samples;
        return true;
    }

    bool reload_republish_runtime() noexcept override
    {
        if (!::publish_parameter_catalog(region_, engine_, true))
            return false;
        ::copy_text(region_->plugin_name, safevst3::kPluginNameBytes, engine_.plugin_name());
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&region_->latency_samples),
            static_cast<LONG>(regenerated_latency_samples_));
        InterlockedExchange(
            &region_->editor_status,
            static_cast<long>(engine_.edit_controller() ? safevst3::EditorStatus::Unknown
                                                        : safevst3::EditorStatus::Unsupported));
        return true;
    }

    bool reload_resume_dsp() noexcept override { return dsp_.resume(2000); }

    void reload_commit() noexcept override
    {
        if (region_)
            InterlockedIncrement(&region_->state_dirty_generation);
    }

    void reload_request_recovery() noexcept override
    {
        if (region_ && InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0)
            request_consistency_recovery(region_, kReloadComponentFailedError);
    }

private:
    DspWorker& dsp_;
    safevst3::Vst3Engine& engine_;
    safevst3::SharedAudioRegion* region_ = nullptr;
    ParameterQueue& dsp_to_control_;
    NativeOverrideBuffer& native_overrides_;
    std::atomic<bool>& feedback_resync_required_;
    safevst3::NativeEditorWindow& editor_;
    ComponentHandler& component_handler_;
    std::string path_;
    std::string class_id_;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t channels_ = 0;
    std::uint32_t regenerated_latency_samples_ = 0;
    safevst3::PluginStateSnapshot snapshot_{};
};

class HelperIoRestartTarget final : public safevst3::IoRestartCoordinatorTarget {
public:
    HelperIoRestartTarget(DspWorker& dsp,
                          safevst3::Vst3Engine& engine,
                          safevst3::SharedAudioRegion* region,
                          ParameterQueue& dsp_to_control,
                          NativeOverrideBuffer& native_overrides,
                          std::atomic<bool>& feedback_resync_required)
        : dsp_(dsp), engine_(engine), region_(region), dsp_to_control_(dsp_to_control),
          native_overrides_(native_overrides), feedback_resync_required_(feedback_resync_required)
    {
    }

    bool pause_dsp() noexcept override { return dsp_.pause(2000); }

    bool reconcile_pending() noexcept override
    {
        if (!reconcile_parameter_refresh_feedback_checked(
                region_, engine_, dsp_to_control_, feedback_resync_required_, native_overrides_))
            return false;
        if (!catch_up_pending_host_parameters_after_pause(region_, engine_))
            return false;
        return native_overrides_.empty();
    }

    bool reconfigure_io(safevst3::IoLayout& layout,
                        std::uint32_t& latency_samples) noexcept override
    {
        std::string error;
        if (engine_.reconfigure_io_after_restart(layout, latency_samples, error))
            return true;
        if (!error.empty())
            std::cerr << error << '\n';
        return false;
    }

    bool publish_io(const safevst3::IoLayout&, std::uint32_t latency_samples) noexcept override
    {
        if (!region_)
            return false;
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&region_->latency_samples),
            static_cast<LONG>(latency_samples));
        return true;
    }

    bool resume_dsp() noexcept override { return dsp_.resume(2000); }

    void request_recovery() noexcept override
    {
        if (region_ && InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0)
            request_consistency_recovery(region_, kIoRestartFailedError);
    }

private:
    DspWorker& dsp_;
    safevst3::Vst3Engine& engine_;
    safevst3::SharedAudioRegion* region_ = nullptr;
    ParameterQueue& dsp_to_control_;
    NativeOverrideBuffer& native_overrides_;
    std::atomic<bool>& feedback_resync_required_;
};

class HelperLatencyRestartTarget final : public safevst3::LatencyRestartCoordinatorTarget {
public:
    HelperLatencyRestartTarget(DspWorker& dsp,
                               safevst3::Vst3Engine& engine,
                               safevst3::SharedAudioRegion* region)
        : dsp_(dsp), engine_(engine), region_(region)
    {
    }

    bool pause_dsp() noexcept override { return dsp_.pause(2000); }

    bool refresh_latency(std::uint32_t& latency_samples) noexcept override
    {
        std::string error;
        if (!engine_.refresh_latency_after_restart(error)) {
            std::cerr << error << '\n';
            return false;
        }
        latency_samples = engine_.latency_samples();
        return true;
    }

    void publish_latency(std::uint32_t latency_samples) noexcept override
    {
        InterlockedExchange(
            reinterpret_cast<volatile LONG*>(&region_->latency_samples),
            static_cast<LONG>(latency_samples));
    }

    bool resume_dsp() noexcept override { return dsp_.resume(2000); }

    void request_recovery() noexcept override
    {
        request_consistency_recovery(region_, kLatencyRestartFailedError);
    }

private:
    DspWorker& dsp_;
    safevst3::Vst3Engine& engine_;
    safevst3::SharedAudioRegion* region_ = nullptr;
};

void handle_editor_command(safevst3::SharedAudioRegion* region,
                           safevst3::Vst3Engine& engine,
                           safevst3::NativeEditorWindow& editor)
{
    const long requested = InterlockedCompareExchange(&region->editor_request_generation, 0, 0);
    const long applied = InterlockedCompareExchange(&region->editor_applied_generation, 0, 0);
    if (requested == applied)
        return;

    const auto command = static_cast<safevst3::EditorCommand>(
        InterlockedCompareExchange(&region->editor_command, 0, 0));

    switch (command) {
    case safevst3::EditorCommand::Open: {
        if (!engine.edit_controller()) {
            InterlockedExchange(&region->editor_status, static_cast<long>(safevst3::EditorStatus::Unsupported));
            break;
        }
        std::string error;
        if (editor.open(engine.edit_controller(), engine.plugin_name(), error)) {
            InterlockedExchange(&region->editor_status, static_cast<long>(safevst3::EditorStatus::Open));
        } else {
            const bool unsupported = error.find("does not provide") != std::string::npos ||
                                     error.find("does not support") != std::string::npos ||
                                     error.find("no edit controller") != std::string::npos;
            InterlockedExchange(&region->editor_status,
                                static_cast<long>(unsupported ? safevst3::EditorStatus::Unsupported
                                                              : safevst3::EditorStatus::Error));
            std::cerr << "VST3 native editor: " << error << '\n';
        }
        break;
    }
    case safevst3::EditorCommand::Hide:
        editor.hide();
        InterlockedExchange(&region->editor_status, static_cast<long>(safevst3::EditorStatus::Closed));
        break;
    case safevst3::EditorCommand::None:
    default:
        break;
    }

    InterlockedExchange(&region->editor_command, static_cast<long>(safevst3::EditorCommand::None));
    InterlockedExchange(&region->editor_applied_generation, requested);
}

void complete_state_failure(safevst3::SharedAudioRegion* region, HANDLE state_event) noexcept
{
    const long requested = InterlockedCompareExchange(&region->state_request_generation, 0, 0);
    InterlockedExchange(&region->state_status, static_cast<long>(safevst3::StateStatus::VstError));
    InterlockedExchange(&region->state_command, static_cast<long>(safevst3::StateCommand::None));
    InterlockedExchange(&region->state_applied_generation, requested);
    if (state_event)
        SetEvent(state_event);
}

void handle_state_command(safevst3::SharedAudioRegion* region,
                          safevst3::StateTransferRegion* transfer,
                          safevst3::Vst3Engine& engine,
                          HANDLE state_event)
{
    const long requested = InterlockedCompareExchange(&region->state_request_generation, 0, 0);
    const long applied = InterlockedCompareExchange(&region->state_applied_generation, 0, 0);
    if (requested == applied)
        return;

    const auto command = static_cast<safevst3::StateCommand>(
        InterlockedCompareExchange(&region->state_command, 0, 0));
    safevst3::StateStatus status = safevst3::StateStatus::Invalid;
    std::string error;

    if (!transfer || transfer->magic != safevst3::kStateTransferMagic ||
        transfer->version != safevst3::kStateTransferVersion ||
        transfer->capacity != safevst3::kMaxStateBytes) {
        status = safevst3::StateStatus::Invalid;
    } else if (command == safevst3::StateCommand::Capture) {
        safevst3::PluginStateSnapshot snapshot{};
        if (!engine.capture_state(snapshot, error)) {
            status = safevst3::StateStatus::VstError;
        } else if (snapshot.component.size() > safevst3::kMaxStateBytes ||
                   snapshot.controller.size() > safevst3::kMaxStateBytes - snapshot.component.size()) {
            status = safevst3::StateStatus::TooLarge;
        } else {
            if (!snapshot.component.empty())
                std::memcpy(transfer->payload, snapshot.component.data(), snapshot.component.size());
            if (!snapshot.controller.empty()) {
                std::memcpy(transfer->payload + snapshot.component.size(),
                            snapshot.controller.data(), snapshot.controller.size());
            }
            region->state_component_bytes = static_cast<std::uint32_t>(snapshot.component.size());
            region->state_controller_bytes = static_cast<std::uint32_t>(snapshot.controller.size());
            status = safevst3::StateStatus::Ok;
        }
    } else if (command == safevst3::StateCommand::Restore) {
        const std::size_t component_bytes = region->state_component_bytes;
        const std::size_t controller_bytes = region->state_controller_bytes;
        if (component_bytes > safevst3::kMaxStateBytes ||
            controller_bytes > safevst3::kMaxStateBytes - component_bytes) {
            status = safevst3::StateStatus::TooLarge;
        } else {
            safevst3::PluginStateSnapshot snapshot{};
            snapshot.component.assign(
                transfer->payload,
                transfer->payload + static_cast<std::ptrdiff_t>(component_bytes));
            snapshot.controller.assign(
                transfer->payload + static_cast<std::ptrdiff_t>(component_bytes),
                transfer->payload + static_cast<std::ptrdiff_t>(component_bytes + controller_bytes));
            if (engine.restore_state(snapshot, error)) {
                region->latency_samples = engine.latency_samples();
                publish_engine_updates_direct(region, engine);
                status = safevst3::StateStatus::Ok;
            } else {
                status = safevst3::StateStatus::VstError;
            }
        }
    }

    if (!error.empty())
        std::cerr << "VST3 state: " << error << '\n';
    MemoryBarrier();
    InterlockedExchange(&region->state_status, static_cast<long>(status));
    InterlockedExchange(&region->state_command, static_cast<long>(safevst3::StateCommand::None));
    InterlockedExchange(&region->state_applied_generation, requested);
    if (state_event)
        SetEvent(state_event);
}

void pump_windows_messages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT)
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace safevst3;
    const auto args = parse_args(argc, argv);
    const auto get = [&](const wchar_t* key) -> std::wstring {
        auto it = args.find(key);
        return it == args.end() ? std::wstring{} : it->second;
    };

    BridgeNames names{get(L"--mapping"), get(L"--state-mapping"), get(L"--request-event"),
                      get(L"--dsp-event"), get(L"--response-event"), get(L"--ready-event"),
                      get(L"--state-event")};
    const std::wstring vst_path_w = get(L"--vst");
    const std::string vst_path = narrow(vst_path_w);
    const std::string class_id = narrow(get(L"--class-id"));

    WinHostEndpoint endpoint;
    std::string error;
    if (!endpoint.open(names, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto* region = endpoint.region();
    Vst3Engine engine;
    if (!engine.open(vst_path, class_id, region->sample_rate, region->channels, error)) {
        region->last_error = 1;
        InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Error));
        SetEvent(endpoint.ready_event());
        std::cerr << "VST3 init failed: " << error << '\n';
        return 3;
    }

    copy_text(region->plugin_name, kPluginNameBytes, engine.plugin_name());
    region->latency_samples = engine.latency_samples();

    ParameterQueue control_to_dsp;
    ParameterQueue dsp_to_control;
    std::atomic<bool> feedback_resync_required{false};
    NativeOverrideBuffer native_overrides;
    ComponentHandler component_handler(
        region, control_to_dsp, native_overrides,
        endpoint.dsp_event(), endpoint.request_event());
    engine.set_component_handler(&component_handler);
    NativeEditorWindow editor;

    if (!publish_parameter_catalog(region, engine, true)) {
        region->last_error = kParameterRefreshFailedError;
        InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Error));
        SetEvent(endpoint.ready_event());
        std::cerr << "Failed to publish initial VST3 parameter catalog\n";
        return 4;
    }

    InterlockedExchange(
        &region->editor_status,
        static_cast<long>(engine.edit_controller() ? EditorStatus::Unknown
                                                    : EditorStatus::Unsupported));
    MemoryBarrier();

    DspWorker dsp(region, engine, control_to_dsp, dsp_to_control, feedback_resync_required,
                  endpoint.dsp_event(), endpoint.request_event(), endpoint.response_event());
    if (!dsp.start(error)) {
        region->last_error = 8;
        InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Error));
        SetEvent(endpoint.ready_event());
        std::cerr << error << '\n';
        return 4;
    }

    publish_control_heartbeat(region);
    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    HANDLE request_handle = endpoint.request_event();
    while (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0) {
        publish_control_heartbeat(region);
        const DWORD wait = MsgWaitForMultipleObjectsEx(1, &request_handle, 250,
                                                       QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED)
            break;
        if (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) != 0)
            break;

        if (native_overrides.empty()) {
            drain_controller_feedback(region, engine, dsp_to_control);
            if (feedback_resync_required.load(std::memory_order_acquire)) {
                if (dsp.pause(2000)) {
                    reconcile_controller_feedback_after_pause(
                        region, engine, dsp_to_control, feedback_resync_required);
                    if (!dsp.resume(2000))
                        InterlockedExchange(&region->last_error, 14);
                } else {
                    InterlockedExchange(&region->last_error, 11);
                }
            }
        } else {
            drain_controller_feedback_preserving_native_overrides(
                region, engine, dsp_to_control, native_overrides);
        }

        if (wait == WAIT_OBJECT_0 + 1)
            pump_windows_messages();

        bool native_resync_failed = false;
        if (!native_overrides.empty()) {
            if (dsp.pause(2000)) {
                if (reconcile_native_edits_after_pause(
                        region, engine, dsp_to_control, native_overrides)) {
                    feedback_resync_required.store(false, std::memory_order_release);
                } else {
                    native_resync_failed = true;
                }
                if (!dsp.resume(2000)) {
                    native_resync_failed = true;
                    InterlockedExchange(&region->last_error, 14);
                }
            } else {
                native_resync_failed = true;
                InterlockedExchange(&region->last_error, 13);
            }
        }

        if (wait != WAIT_TIMEOUT)
            handle_editor_command(region, engine, editor);

        for (std::uint32_t i = 0; i < region->parameter_count; ++i) {
            auto& descriptor = region->parameters[i];
            const long generation = InterlockedCompareExchange(&descriptor.pending_generation, 0, 0);
            const long applied = InterlockedCompareExchange(&descriptor.applied_generation, 0, 0);
            if (generation == applied)
                continue;

            const auto raw_bits = InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64*>(&descriptor.pending_value_bits), 0, 0);
            const double value = bits_to_double(static_cast<std::int64_t>(raw_bits));
            const EngineParameterUpdate update{descriptor.id,
                normalize_parameter_value(value, descriptor.step_count)};
            if (engine.set_controller_parameter(update.id, update.normalized) &&
                control_to_dsp.push(update)) {
                publish_parameter_value(region, update.id, update.normalized);
                InterlockedIncrement(&region->state_dirty_generation);
                InterlockedExchange(&descriptor.applied_generation, generation);
                SetEvent(endpoint.dsp_event());
            } else {
                InterlockedExchange(&region->last_error, 6);
            }
        }

        (void)component_handler.take_edit_pending();

        const Steinberg::int32 restart_flags = component_handler.take_restart_flags();
        const auto restart_plan = safevst3::plan_restart_component(
            static_cast<std::uint32_t>(restart_flags));
        if (restart_plan.reload_component) {
            HelperReloadComponentTarget reload_target(
                dsp, engine, region, dsp_to_control, native_overrides,
                feedback_resync_required, editor, component_handler,
                vst_path, engine.loaded_class_id(), region->sample_rate, region->channels);
            if (!coordinate_reload_component(reload_target).completed)
                break;
        } else if (should_run_incremental_restart_actions(restart_plan)) {
            if (restart_plan.reconfigure_io) {
                HelperIoRestartTarget io_target(
                    dsp, engine, region, dsp_to_control, native_overrides,
                    feedback_resync_required);
                if (!coordinate_io_restart(io_target).completed)
                    break;
            }
            if (restart_plan.refresh_parameter_values || restart_plan.refresh_parameter_metadata) {
                HelperParameterRefreshTarget parameter_target(
                    dsp, engine, region, dsp_to_control, native_overrides,
                    feedback_resync_required, restart_plan.refresh_parameter_metadata);
                const ParameterRefreshRequest refresh_request{
                    restart_plan.refresh_parameter_values,
                    restart_plan.refresh_parameter_metadata};
                if (!coordinate_parameter_refresh(parameter_target, refresh_request).completed)
                    break;
            }
            if (requires_standalone_latency_restart(restart_plan)) {
                HelperLatencyRestartTarget latency_target(dsp, engine, region);
                if (!coordinate_latency_restart(latency_target).completed)
                    break;
            }
        }
        if (restart_plan.unknown_flags != 0)
            InterlockedExchange(&region->last_error, 3);

        const long state_requested = InterlockedCompareExchange(&region->state_request_generation, 0, 0);
        const long state_applied = InterlockedCompareExchange(&region->state_applied_generation, 0, 0);
        if (state_requested != state_applied) {
            if (native_resync_failed || native_overrides.overflowed()) {
                complete_state_failure(region, endpoint.state_event());
                InterlockedExchange(&region->last_error, 12);
            } else if (dsp.pause(2000)) {
                bool frontier_ok = true;

                if (!native_overrides.empty()) {
                    frontier_ok = reconcile_native_edits_after_pause(
                        region, engine, dsp_to_control, native_overrides);
                    if (frontier_ok)
                        feedback_resync_required.store(false, std::memory_order_release);
                } else {
                    reconcile_controller_feedback_after_pause(
                        region, engine, dsp_to_control, feedback_resync_required);
                }

                if (frontier_ok)
                    frontier_ok = catch_up_pending_host_parameters_after_pause(region, engine);
                if (!native_overrides.empty())
                    frontier_ok = false;

                if (frontier_ok) {
                    handle_state_command(region, endpoint.state_region(), engine, endpoint.state_event());
                } else {
                    complete_state_failure(region, endpoint.state_event());
                    InterlockedExchange(&region->last_error, 12);
                }

                if (!dsp.resume(2000))
                    InterlockedExchange(&region->last_error, 14);
            } else {
                complete_state_failure(region, endpoint.state_event());
                InterlockedExchange(&region->last_error, 10);
            }
        }

        if (native_overrides.empty())
            drain_controller_feedback(region, engine, dsp_to_control);
        else
            drain_controller_feedback_preserving_native_overrides(
                region, engine, dsp_to_control, native_overrides);

        if (editor.created()) {
            InterlockedExchange(&region->editor_status,
                                static_cast<long>(editor.visible() ? EditorStatus::Open : EditorStatus::Closed));
        }
        publish_control_heartbeat(region);
    }

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::ShuttingDown));
    dsp.stop();
    editor.close();
    engine.set_component_handler(nullptr);
    engine.close();
    return 0;
}

#endif
