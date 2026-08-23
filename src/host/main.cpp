#ifdef _WIN32

#include "common/parameter_utils.hpp"
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

constexpr std::size_t kParameterTransferCapacity = safevst3::kMaxParameters * safevst3::kSlotCount;
constexpr ULONGLONG kPausedHeartbeatGraceMs = 2000;
using ParameterQueue = safevst3::SpscRing<safevst3::EngineParameterUpdate, kParameterTransferCapacity>;

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
        // The shared mirror is latest-value storage and is safe to update from
        // DSP with an interlocked write. Even if the bounded feedback queue is
        // saturated by a long control/UI stall, OBS never regresses its value.
        publish_parameter_value(region, updates[i].id, updates[i].normalized);
        if (!feedback.push(updates[i])) {
            // Never block realtime and never silently lose the semantic update.
            // The control thread will pause DSP and replay engine.parameters()
            // into IEditController once it resumes.
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

bool drain_processor_commands(safevst3::SharedAudioRegion* region,
                              safevst3::Vst3Engine& engine,
                              ParameterQueue& commands) noexcept
{
    bool applied_any = false;
    safevst3::EngineParameterUpdate update{};
    while (commands.pop(update)) {
        if (engine.queue_processor_parameter(update.id, update.normalized)) {
            applied_any = true;
        } else if (region) {
            InterlockedExchange(&region->last_error, 5);
        }
    }
    return applied_any;
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

void discard_controller_feedback(ParameterQueue& feedback) noexcept
{
    safevst3::EngineParameterUpdate ignored{};
    while (feedback.pop(ignored)) {
    }
}

void reconcile_controller_feedback_after_pause(safevst3::SharedAudioRegion* region,
                                               safevst3::Vst3Engine& engine,
                                               ParameterQueue& feedback,
                                               std::atomic<bool>& resync_required) noexcept
{
    // DSP is paused: no producer can mutate the engine parameter mirror or add
    // new feedback while this transaction runs. Apply queued feedback first so
    // old queue entries can never overwrite the authoritative full resync.
    drain_controller_feedback(region, engine, feedback);
    if (!resync_required.exchange(false, std::memory_order_acq_rel))
        return;

    for (const auto& parameter : engine.parameters()) {
        (void)engine.set_controller_parameter(parameter.id, parameter.current_normalized);
        publish_parameter_value(region, parameter.id, parameter.current_normalized);
    }
}

bool reconcile_native_edits_after_pause(safevst3::SharedAudioRegion* region,
                                        safevst3::Vst3Engine& engine,
                                        ParameterQueue& feedback,
                                        std::atomic<bool>& native_resync_required) noexcept
{
    if (!native_resync_required.load(std::memory_order_acquire))
        return true;

    // A rejected performEdit means IEditController already owns a value newer
    // than the saturated control->DSP ring. Do not apply older DSP feedback to
    // the controller first. Snapshot controller values into the engine mirror,
    // discard stale queued feedback, then replay that complete latest-value
    // mirror to the paused processor.
    engine.refresh_parameter_values();
    discard_controller_feedback(feedback);

    bool queued_all = true;
    for (const auto& parameter : engine.parameters()) {
        if (!engine.queue_processor_parameter(parameter.id, parameter.current_normalized))
            queued_all = false;
    }
    const bool flushed = engine.flush_parameter_changes();

    // flush may itself produce canonical processor feedback. Clear its compact
    // update buffer into the shared mirror, then make the controller match the
    // final processor-owned mirror before allowing state capture.
    publish_engine_updates_direct(region, engine);
    for (const auto& parameter : engine.parameters()) {
        (void)engine.set_controller_parameter(parameter.id, parameter.current_normalized);
        publish_parameter_value(region, parameter.id, parameter.current_normalized);
    }

    if (queued_all && flushed) {
        native_resync_required.store(false, std::memory_order_release);
        return true;
    }
    if (region)
        InterlockedExchange(&region->last_error, 13);
    return false;
}

void publish_engine_updates_direct(safevst3::SharedAudioRegion* region,
                                   safevst3::Vst3Engine& engine) noexcept
{
    std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> updates{};
    const std::size_t count = engine.take_parameter_updates(updates.data(), updates.size());
    for (std::size_t i = 0; i < count; ++i)
        publish_parameter_value(region, updates[i].id, updates[i].normalized);
}

class ComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    ComponentHandler(safevst3::SharedAudioRegion* region,
                     ParameterQueue& control_to_dsp,
                     std::atomic<bool>& native_resync_required,
                     HANDLE dsp_event,
                     HANDLE control_event)
        : region_(region), control_to_dsp_(control_to_dsp),
          native_resync_required_(native_resync_required), dsp_event_(dsp_event),
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
        if (!control_to_dsp_.push(update)) {
            // The controller has already accepted this native edit. Preserve it
            // as the source of truth and schedule a paused full controller->DSP
            // resync instead of dropping the user's newest value.
            native_resync_required_.store(true, std::memory_order_release);
            publish_parameter_value(region_, update.id, value);
            if (region_) {
                InterlockedIncrement(&region_->state_dirty_generation);
                InterlockedExchange(&region_->last_error, 6);
            }
            if (dsp_event_)
                SetEvent(dsp_event_);
            if (control_event_)
                SetEvent(control_event_);
            edit_pending_.store(true, std::memory_order_release);
            return Steinberg::kResultOk;
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
    std::atomic<bool>& native_resync_required_;
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
        paused_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!paused_event_) {
            error = "Failed to create DSP pause acknowledgement event";
            return false;
        }
        try {
            thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
        } catch (...) {
            CloseHandle(paused_event_);
            paused_event_ = nullptr;
            error = "Failed to start dedicated DSP worker";
            return false;
        }
        return true;
    }

    void stop() noexcept
    {
        if (thread_.joinable()) {
            thread_.request_stop();
            pause_requested_.store(false, std::memory_order_release);
            if (dsp_event_)
                SetEvent(dsp_event_);
            thread_ = std::jthread{};
        }
        if (paused_event_) {
            CloseHandle(paused_event_);
            paused_event_ = nullptr;
        }
    }

    bool pause(DWORD timeout_ms) noexcept
    {
        if (!thread_.joinable() || !paused_event_)
            return false;
        pause_requested_.store(true, std::memory_order_release);
        SetEvent(dsp_event_);
        if (WaitForSingleObject(paused_event_, timeout_ms) == WAIT_OBJECT_0)
            return true;

        // A DSP that cannot acknowledge pause is likely stuck in vendor code.
        // Never leave a latent pause request behind if the control operation
        // gives up; if DSP returns before watchdog replacement, it may continue.
        pause_requested_.store(false, std::memory_order_release);
        ResetEvent(paused_event_);
        SetEvent(dsp_event_);
        return false;
    }

    void resume() noexcept
    {
        pause_requested_.store(false, std::memory_order_release);
        if (paused_event_)
            ResetEvent(paused_event_);
        if (dsp_event_)
            SetEvent(dsp_event_);
    }

private:
    void run(std::stop_token stop) noexcept
    {
        DWORD task_index = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
        if (mmcss)
            AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

        publish_dsp_heartbeat(region_);
        while (!stop.stop_requested() &&
               InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0) {
            if (pause_requested_.load(std::memory_order_acquire)) {
                // Drain and flush every control->DSP edit before acknowledging
                // pause. This preserves S1's invariant that getState() cannot
                // capture one knob edit behind the visible controller.
                bool feedback_pending = false;
                if (drain_processor_commands(region_, engine_, control_to_dsp_)) {
                    if (!engine_.flush_parameter_changes())
                        InterlockedExchange(&region_->last_error, 2);
                    feedback_pending |= enqueue_processor_feedback(
                        region_, engine_, dsp_to_control_, feedback_resync_required_);
                }
                if (feedback_pending && control_event_)
                    SetEvent(control_event_);

                const ULONGLONG pause_started = GetTickCount64();
                SetEvent(paused_event_);
                while (!stop.stop_requested() &&
                       pause_requested_.load(std::memory_order_acquire)) {
                    // State/lifecycle calls are allowed a short grace window.
                    // If vendor control code hangs beyond it, stop refreshing
                    // DSP heartbeat so the outer watchdog can replace helper.
                    if (GetTickCount64() - pause_started <= kPausedHeartbeatGraceMs)
                        publish_dsp_heartbeat(region_);
                    WaitForSingleObject(dsp_event_, 100);
                }
                if (paused_event_)
                    ResetEvent(paused_event_);
                continue;
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

            const bool parameter_edits = drain_processor_commands(region_, engine_, control_to_dsp_);
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
                feedback_pending |= enqueue_processor_feedback(
                    region_, engine_, dsp_to_control_, feedback_resync_required_);
                InterlockedExchange(&slot.result,
                                    static_cast<long>(ok ? safevst3::ProcessResult::Ok
                                                        : safevst3::ProcessResult::VstProcessError));
                MemoryBarrier();
                InterlockedExchange(&slot.state, static_cast<long>(safevst3::SlotState::Done));
                SetEvent(response_event_);
            }

            if (parameter_edits && !processed_any) {
                if (!engine_.flush_parameter_changes())
                    InterlockedExchange(&region_->last_error, 2);
                feedback_pending |= enqueue_processor_feedback(
                    region_, engine_, dsp_to_control_, feedback_resync_required_);
            }

            if (feedback_pending && control_event_)
                SetEvent(control_event_);
            publish_dsp_heartbeat(region_);
        }

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
    HANDLE paused_event_ = nullptr;
    std::atomic<bool> pause_requested_{false};
    std::jthread thread_;
};

void publish_engine_updates_direct(safevst3::SharedAudioRegion* region,
                                   safevst3::Vst3Engine& engine) noexcept;

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
    const std::string class_id = narrow(get(L"--class-id"));

    WinHostEndpoint endpoint;
    std::string error;
    if (!endpoint.open(names, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto* region = endpoint.region();
    Vst3Engine engine;
    if (!engine.open(narrow(vst_path_w), class_id, region->sample_rate, region->channels, error)) {
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
    std::atomic<bool> native_resync_required{false};
    ComponentHandler component_handler(
        region, control_to_dsp, native_resync_required,
        endpoint.dsp_event(), endpoint.request_event());
    engine.set_component_handler(&component_handler);
    NativeEditorWindow editor;

    const auto& parameters = engine.parameters();
    region->parameter_total_count = static_cast<std::uint32_t>(parameters.size());
    region->parameter_count = static_cast<std::uint32_t>(std::min<std::size_t>(parameters.size(), kMaxParameters));
    for (std::uint32_t i = 0; i < region->parameter_count; ++i) {
        const auto& source = parameters[i];
        auto& destination = region->parameters[i];
        destination.id = source.id;
        destination.step_count = source.step_count;
        destination.flags = source.flags;
        destination.default_normalized = source.default_normalized;
        destination.current_value_bits = double_to_bits(source.current_normalized);
        destination.pending_value_bits = destination.current_value_bits;
        destination.pending_generation = 0;
        destination.applied_generation = 0;
        copy_text(destination.title, kParameterTitleBytes, source.title);
        copy_text(destination.units, kParameterUnitsBytes, source.units);
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

        // Apply processor feedback before pumping a new native UI message. That
        // prevents older feedback from overwriting a newer performEdit value in
        // the controller after the user gesture has already occurred.
        drain_controller_feedback(region, engine, dsp_to_control);
        if (feedback_resync_required.load(std::memory_order_acquire)) {
            if (dsp.pause(2000)) {
                reconcile_controller_feedback_after_pause(
                    region, engine, dsp_to_control, feedback_resync_required);
                dsp.resume();
            } else {
                InterlockedExchange(&region->last_error, 11);
            }
        }

        if (wait == WAIT_OBJECT_0 + 1)
            pump_windows_messages();

        bool native_resync_failed = false;
        if (native_resync_required.load(std::memory_order_acquire)) {
            if (dsp.pause(2000)) {
                if (!reconcile_native_edits_after_pause(
                        region, engine, dsp_to_control, native_resync_required))
                    native_resync_failed = true;
                dsp.resume();
            } else {
                native_resync_failed = true;
                InterlockedExchange(&region->last_error, 13);
            }
        }

        if (wait != WAIT_TIMEOUT)
            handle_editor_command(region, engine, editor);

        bool pending_parameter_delivery_failed = false;
        // Always retry pending host edits, including on an idle 250 ms control
        // tick. A temporary full control->DSP ring therefore cannot strand an
        // unacknowledged generation until some unrelated future UI event.
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
                // Do not acknowledge an edit the processor never received.
                // The unchanged generation makes the next control tick retry.
                pending_parameter_delivery_failed = true;
                InterlockedExchange(&region->last_error, 6);
            }
        }

        (void)component_handler.take_edit_pending();

        const Steinberg::int32 restart_flags = component_handler.take_restart_flags();
        if ((restart_flags & Steinberg::Vst::kParamValuesChanged) != 0) {
            if (dsp.pause(2000)) {
                reconcile_controller_feedback_after_pause(
                    region, engine, dsp_to_control, feedback_resync_required);
                engine.refresh_parameter_values();
                publish_engine_updates_direct(region, engine);
                dsp.resume();
            } else {
                InterlockedExchange(&region->last_error, 9);
            }
        }
        if ((restart_flags & (Steinberg::Vst::kReloadComponent | Steinberg::Vst::kIoChanged)) != 0)
            InterlockedExchange(&region->last_error, 3);

        const long state_requested = InterlockedCompareExchange(&region->state_request_generation, 0, 0);
        const long state_applied = InterlockedCompareExchange(&region->state_applied_generation, 0, 0);
        if (state_requested != state_applied) {
            if (pending_parameter_delivery_failed || native_resync_failed ||
                native_resync_required.load(std::memory_order_acquire)) {
                // Never serialize a component/controller pair while any visible
                // host/native edit is still waiting for processor delivery. OBS
                // treats this failure as transient and preserves S1 LKG state.
                complete_state_failure(region, endpoint.state_event());
                InterlockedExchange(&region->last_error, 12);
            } else if (dsp.pause(2000)) {
                // A block already in flight may have produced final processor
                // feedback just before pause acknowledgement. Apply it to the
                // controller before getState(), so component/controller blobs
                // represent the same exact processing frontier.
                reconcile_controller_feedback_after_pause(
                    region, engine, dsp_to_control, feedback_resync_required);
                handle_state_command(region, endpoint.state_region(), engine, endpoint.state_event());
                dsp.resume();
            } else {
                complete_state_failure(region, endpoint.state_event());
                InterlockedExchange(&region->last_error, 10);
            }
        }

        drain_controller_feedback(region, engine, dsp_to_control);
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
