#ifdef _WIN32

#include "common/parameter_utils.hpp"
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

#pragma comment(lib, "Avrt.lib")

namespace {
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

void publish_parameter_feedback(safevst3::SharedAudioRegion* region, safevst3::Vst3Engine& engine) noexcept
{
    if (!region)
        return;

    std::array<safevst3::EngineParameterUpdate, safevst3::kMaxParameters> updates{};
    const std::size_t count = engine.take_parameter_updates(updates.data(), updates.size());
    for (std::size_t i = 0; i < count; ++i)
        publish_parameter_value(region, updates[i].id, updates[i].normalized);
}

class ComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    ComponentHandler(safevst3::Vst3Engine& engine, safevst3::SharedAudioRegion* region)
        : engine_(engine), region_(region)
    {
    }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override
    {
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override
    {
        if (!engine_.queue_parameter_from_controller(static_cast<std::uint32_t>(id), value))
            return Steinberg::kResultFalse;
        publish_parameter_value(region_, static_cast<std::uint32_t>(id), value);
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
    safevst3::Vst3Engine& engine_;
    safevst3::SharedAudioRegion* region_ = nullptr;
    std::atomic<bool> edit_pending_{false};
    std::atomic<Steinberg::int32> restart_flags_{0};
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

    BridgeNames names{get(L"--mapping"), get(L"--request-event"), get(L"--response-event"), get(L"--ready-event")};
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

    ComponentHandler component_handler(engine, region);
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
    InterlockedExchange(&region->editor_status,
                        static_cast<long>(engine.edit_controller() ? EditorStatus::Closed : EditorStatus::Unsupported));
    MemoryBarrier();

    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    if (mmcss)
        AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    HANDLE request_handle = endpoint.request_event();
    while (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0) {
        const DWORD wait = MsgWaitForMultipleObjectsEx(1, &request_handle, INFINITE,
                                                       QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED)
            break;
        if (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) != 0)
            break;

        if (wait == WAIT_OBJECT_0 + 1)
            pump_windows_messages();

        handle_editor_command(region, engine, editor);

        bool parameter_edits = false;
        for (std::uint32_t i = 0; i < region->parameter_count; ++i) {
            auto& descriptor = region->parameters[i];
            const long generation = InterlockedCompareExchange(&descriptor.pending_generation, 0, 0);
            const long applied = InterlockedCompareExchange(&descriptor.applied_generation, 0, 0);
            if (generation == applied)
                continue;

            const auto raw_bits = InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64*>(&descriptor.pending_value_bits), 0, 0);
            const double value = bits_to_double(static_cast<std::int64_t>(raw_bits));
            if (engine.queue_parameter(descriptor.id, value)) {
                publish_parameter_value(region, descriptor.id,
                    normalize_parameter_value(value, descriptor.step_count));
                parameter_edits = true;
            }
            InterlockedExchange(&descriptor.applied_generation, generation);
        }

        const bool editor_edits = component_handler.take_edit_pending();

        bool processed_any = false;
        for (std::uint32_t i = 0; i < kSlotCount; ++i) {
            auto& slot = region->slots[i];
            if (InterlockedCompareExchange(&slot.state,
                                           static_cast<long>(SlotState::Processing),
                                           static_cast<long>(SlotState::Ready)) != static_cast<long>(SlotState::Ready))
                continue;

            processed_any = true;
            const bool ok = engine.process(slot);
            publish_parameter_feedback(region, engine);
            InterlockedExchange(&slot.result, static_cast<long>(ok ? ProcessResult::Ok : ProcessResult::VstProcessError));
            MemoryBarrier();
            InterlockedExchange(&slot.state, static_cast<long>(SlotState::Done));
            SetEvent(endpoint.response_event());
        }

        if ((parameter_edits || editor_edits) && !processed_any) {
            if (!engine.flush_parameter_changes())
                region->last_error = 2;
            publish_parameter_feedback(region, engine);
        }

        const Steinberg::int32 restart_flags = component_handler.take_restart_flags();
        if ((restart_flags & Steinberg::Vst::kParamValuesChanged) != 0) {
            engine.refresh_parameter_values();
            publish_parameter_feedback(region, engine);
        }
        if ((restart_flags & (Steinberg::Vst::kReloadComponent | Steinberg::Vst::kIoChanged)) != 0)
            region->last_error = 3; // Deferred to a later routing/reload phase; OBS remains fail-open.

        if (editor.created()) {
            InterlockedExchange(&region->editor_status,
                                static_cast<long>(editor.visible() ? EditorStatus::Open : EditorStatus::Closed));
        }
    }

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::ShuttingDown));
    editor.close();
    engine.set_component_handler(nullptr);
    engine.close();
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    return 0;
}

#endif
