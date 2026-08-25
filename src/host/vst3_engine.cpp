#ifdef _WIN32

#include "host/vst3_engine.hpp"

#include "common/parameter_utils.hpp"
#include "common/channel_adapter.hpp"
#include "common/state_restore_policy.hpp"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <vector>

namespace safevst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

bool copy_stream(MemoryStream& stream, std::vector<std::uint8_t>& destination,
                 const char* label, std::string& error)
{
    const auto raw_size = stream.getSize();
    if (raw_size < 0 || static_cast<std::uint64_t>(raw_size) > kMaxStateBytes) {
        error = std::string(label) + " state exceeds the supported snapshot size";
        return false;
    }

    const auto size = static_cast<std::size_t>(raw_size);
    if (size == 0) {
        destination.clear();
        return true;
    }

    const char* data = stream.getData();
    if (!data) {
        error = std::string(label) + " state stream returned no data";
        return false;
    }

    const auto* begin = reinterpret_cast<const std::uint8_t*>(data);
    destination.assign(begin, begin + static_cast<std::ptrdiff_t>(size));
    return true;
}

MemoryStream read_stream(const std::vector<std::uint8_t>& bytes)
{
    return MemoryStream(bytes.empty() ? nullptr : const_cast<std::uint8_t*>(bytes.data()),
                        static_cast<TSize>(bytes.size()));
}

PluginCallResult classify_plugin_call_result(tresult result) noexcept
{
    if (result == kResultTrue)
        return PluginCallResult::Success;
    if (result == kResultFalse)
        return PluginCallResult::ResultFalse;
    if (result == kNotImplemented)
        return PluginCallResult::NotImplemented;
    return PluginCallResult::UnexpectedFailure;
}

const char* latency_restart_step_name(LatencyRestartStep step) noexcept
{
    switch (step) {
    case LatencyRestartStep::StopProcessing:
        return "setProcessing(false)";
    case LatencyRestartStep::Deactivate:
        return "setActive(false)";
    case LatencyRestartStep::Activate:
        return "setActive(true)";
    case LatencyRestartStep::QueryLatency:
        return "getLatencySamples()";
    case LatencyRestartStep::StartProcessing:
        return "setProcessing(true)";
    case LatencyRestartStep::None:
    default:
        return "unknown step";
    }
}

} // namespace

Vst3Engine::~Vst3Engine() { close(); }

bool Vst3Engine::configure_buses(std::uint32_t channels, std::string& error)
{
    const int32 input_count = component_->getBusCount(kAudio, kInput);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    if (input_count <= 0 || output_count <= 0) {
        error = "VST3 effect has no audio input/output bus";
        return false;
    }

    std::vector<SpeakerArrangement> input_arrangements(static_cast<std::size_t>(input_count), SpeakerArr::kEmpty);
    std::vector<SpeakerArrangement> output_arrangements(static_cast<std::size_t>(output_count), SpeakerArr::kEmpty);

    for (int32 i = 0; i < input_count; ++i) {
        BusInfo info{};
        component_->getBusInfo(kAudio, kInput, i, info);
        component_->activateBus(kAudio, kInput, i, false);
        if (processor_->getBusArrangement(kInput, i, input_arrangements[static_cast<std::size_t>(i)]) != kResultTrue)
            input_arrangements[static_cast<std::size_t>(i)] = info.channelCount == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        if (main_input_bus_ < 0 && info.busType == BusTypes::kMain)
            main_input_bus_ = i;
    }

    for (int32 i = 0; i < output_count; ++i) {
        BusInfo info{};
        component_->getBusInfo(kAudio, kOutput, i, info);
        component_->activateBus(kAudio, kOutput, i, false);
        if (processor_->getBusArrangement(kOutput, i, output_arrangements[static_cast<std::size_t>(i)]) != kResultTrue)
            output_arrangements[static_cast<std::size_t>(i)] = info.channelCount == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        if (main_output_bus_ < 0 && info.busType == BusTypes::kMain)
            main_output_bus_ = i;
    }

    if (main_input_bus_ < 0 || main_output_bus_ < 0) {
        error = "VST3 effect has no main audio input/output bus";
        return false;
    }

    const SpeakerArrangement requested = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    input_arrangements[static_cast<std::size_t>(main_input_bus_)] = requested;
    output_arrangements[static_cast<std::size_t>(main_output_bus_)] = requested;

    const tresult set_result = processor_->setBusArrangements(input_arrangements.data(), input_count,
                                                               output_arrangements.data(), output_count);
    if (set_result != kResultTrue) {
        SpeakerArrangement actual_in{}, actual_out{};
        if (processor_->getBusArrangement(kInput, main_input_bus_, actual_in) != kResultTrue ||
            processor_->getBusArrangement(kOutput, main_output_bus_, actual_out) != kResultTrue ||
            actual_in != requested || actual_out != requested) {
            error = channels == 1 ? "VST3 does not accept mono I/O" : "VST3 does not accept stereo I/O";
            return false;
        }
    }

    component_->activateBus(kAudio, kInput, main_input_bus_, true);
    component_->activateBus(kAudio, kOutput, main_output_bus_, true);
    return true;
}

bool Vst3Engine::enumerate_parameters(std::string& error)
{
    parameters_.clear();
    if (!controller_)
        return true;

    const int32 count = controller_->getParameterCount();
    if (count < 0) {
        error = "VST3 controller returned an invalid parameter count";
        return false;
    }

    parameters_.reserve(static_cast<std::size_t>(count));
    for (int32 index = 0; index < count; ++index) {
        ParameterInfo info{};
        if (controller_->getParameterInfo(index, info) != kResultTrue)
            continue;

        std::uint32_t flags = 0;
        if (info.flags & ParameterInfo::kCanAutomate) flags |= ParameterCanAutomate;
        if (info.flags & ParameterInfo::kIsReadOnly) flags |= ParameterReadOnly;
        if (info.flags & ParameterInfo::kIsHidden) flags |= ParameterHidden;
        if (info.flags & ParameterInfo::kIsList) flags |= ParameterList;
        if (info.flags & ParameterInfo::kIsProgramChange) flags |= ParameterProgramChange;
        if (info.flags & ParameterInfo::kIsBypass) flags |= ParameterBypass;

        EngineParameter parameter{};
        parameter.id = static_cast<std::uint32_t>(info.id);
        parameter.step_count = info.stepCount;
        parameter.flags = flags;
        parameter.default_normalized = normalize_parameter_value(info.defaultNormalizedValue, info.stepCount);
        parameter.current_normalized = normalize_parameter_value(controller_->getParamNormalized(info.id), info.stepCount);
        parameter.title = StringConvert::convert(info.title);
        parameter.units = StringConvert::convert(info.units);
        parameters_.push_back(std::move(parameter));
    }

    const auto queue_capacity = static_cast<int32>(parameters_.size());
    input_parameter_changes_.setMaxParameters(queue_capacity);
    output_parameter_changes_.setMaxParameters(queue_capacity);
    return true;
}

bool Vst3Engine::open(const std::string& path,
                      const std::string& class_id,
                      std::uint32_t sample_rate,
                      std::uint32_t channels,
                      std::string& error)
{
    close();
    if (channels == 0 || channels > kMaxChannels) {
        error = "Public preview supports only mono or stereo";
        return false;
    }

    host_ = owned(new HostApplication());
    PluginContextFactory::instance().setPluginContext(host_.get());

    module_ = VST3::Hosting::Module::create(path, error);
    if (!module_)
        return false;

    auto factory = module_->getFactory();
    factory.setHostContext(host_.get());
    const VST3::Hosting::ClassInfo* chosen = nullptr;
    auto classes = factory.classInfos();
    for (const auto& info : classes) {
        if (info.category() != kVstAudioEffectClass)
            continue;
        if (!class_id.empty() && info.ID().toString() != class_id)
            continue;
        chosen = &info;
        break;
    }
    if (!chosen) {
        error = class_id.empty() ? "No VST3 audio-effect class found in module" : "Requested VST3 class ID not found";
        return false;
    }

    plugin_name_ = chosen->name();
    provider_ = owned(new PlugProvider(factory, *chosen, true));
    if (!provider_->initialize()) {
        error = "Failed to initialize VST3 component/controller";
        return false;
    }

    component_ = provider_->getComponentPtr();
    controller_ = provider_->getControllerPtr();
    if (!component_) {
        error = "VST3 component unavailable";
        return false;
    }

    processor_ = FUnknownPtr<IAudioProcessor>(component_).getInterface();
    if (!processor_) {
        error = "VST3 component does not implement IAudioProcessor";
        return false;
    }

    if (processor_->canProcessSampleSize(kSample32) != kResultTrue) {
        error = "Public preview requires float32-capable VST3 processing";
        return false;
    }

    if (!enumerate_parameters(error))
        return false;
    if (!configure_buses(channels, error))
        return false;

    sample_rate_ = sample_rate;
    channels_ = channels;
    plugin_input_channels_ = channels;
    plugin_output_channels_ = channels;
    process_setup_.processMode = kRealtime;
    process_setup_.symbolicSampleSize = kSample32;
    process_setup_.maxSamplesPerBlock = static_cast<int32>(kMaxFrames);
    process_setup_.sampleRate = static_cast<SampleRate>(sample_rate);

    if (processor_->setupProcessing(process_setup_) != kResultOk) {
        error = "VST3 setupProcessing failed";
        return false;
    }

    if (!process_data_.prepare(*component_, 0, kSample32)) {
        error = "Failed to prepare VST3 ProcessData bus containers";
        return false;
    }

    process_context_.sampleRate = static_cast<double>(sample_rate);
    process_context_.state = 0;
    process_data_.processContext = &process_context_;

    if (component_->setActive(true) != kResultTrue) {
        error = "VST3 setActive(true) failed";
        return false;
    }
    if (processor_->setProcessing(true) != kResultTrue) {
        component_->setActive(false);
        error = "VST3 setProcessing(true) failed";
        return false;
    }

    latency_samples_ = processor_->getLatencySamples();
    return true;
}

bool Vst3Engine::capture_state(PluginStateSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    error.clear();
    if (!component_) {
        error = "VST3 component unavailable while capturing state";
        return false;
    }

    MemoryStream component_stream;
    if (component_->getState(&component_stream) != kResultTrue) {
        error = "VST3 component getState failed";
        return false;
    }
    if (!copy_stream(component_stream, snapshot.component, "VST3 component", error))
        return false;

    if (controller_) {
        MemoryStream controller_stream;
        if (controller_->getState(&controller_stream) == kResultTrue &&
            !copy_stream(controller_stream, snapshot.controller, "VST3 controller", error))
            return false;
    }

    if (snapshot.total_bytes() > kMaxStateBytes) {
        error = "Combined VST3 component/controller state exceeds the supported snapshot size";
        snapshot = {};
        return false;
    }
    return true;
}

bool Vst3Engine::restore_state(const PluginStateSnapshot& snapshot, std::string& error)
{
    error.clear();
    if (!component_ || !processor_) {
        error = "VST3 component unavailable while restoring state";
        return false;
    }
    if (snapshot.total_bytes() > kMaxStateBytes) {
        error = "VST3 state exceeds the supported snapshot size";
        return false;
    }

    // State restoration is a control-plane transaction. Suspend the processor,
    // restore in the ordering required by the VST3 specification, and always
    // try to resume so a corrupt vendor blob cannot leave the helper inactive.
    (void)processor_->setProcessing(false);
    (void)component_->setActive(false);
    input_parameter_changes_.clearQueue();
    output_parameter_changes_.clearQueue();
    parameter_changes_pending_ = false;

    bool restored = true;
    auto component_stream = read_stream(snapshot.component);
    if (!accepts_state_restore_result(
            StateRestoreCall::ComponentState,
            classify_plugin_call_result(component_->setState(&component_stream)))) {
        error = "VST3 component setState failed";
        restored = false;
    }

    if (restored && controller_) {
        auto controller_component_stream = read_stream(snapshot.component);
        if (!accepts_state_restore_result(
                StateRestoreCall::ControllerComponentState,
                classify_plugin_call_result(
                    controller_->setComponentState(&controller_component_stream)))) {
            error = "VST3 controller setComponentState returned an unexpected failure";
            restored = false;
        }
    }

    if (restored && controller_ && !snapshot.controller.empty()) {
        auto controller_stream = read_stream(snapshot.controller);
        if (!accepts_state_restore_result(
                StateRestoreCall::ControllerPrivateState,
                classify_plugin_call_result(controller_->setState(&controller_stream)))) {
            error = "VST3 controller setState failed";
            restored = false;
        }
    }

    const bool activated = component_->setActive(true) == kResultTrue;
    const bool processing = activated && processor_->setProcessing(true) == kResultTrue;
    if (!activated || !processing) {
        error = !activated ? "VST3 setActive(true) failed after state restore"
                           : "VST3 setProcessing(true) failed after state restore";
        return false;
    }

    if (!restored)
        return false;

    latency_samples_ = processor_->getLatencySamples();
    refresh_parameter_values();
    return true;
}

bool Vst3Engine::inspect_io_topology(IoLayout& layout, bool capture_arrangements) noexcept
{
    layout = {};
    if (!component_ || !processor_)
        return false;

    const int32 input_count = component_->getBusCount(kAudio, kInput);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    if (input_count <= 0 || output_count <= 0)
        return false;

    if (capture_arrangements) {
        candidate_input_arrangements_.assign(static_cast<std::size_t>(input_count), SpeakerArr::kEmpty);
        candidate_output_arrangements_.assign(static_cast<std::size_t>(output_count), SpeakerArr::kEmpty);
    } else if (candidate_input_arrangements_.size() != static_cast<std::size_t>(input_count) ||
               candidate_output_arrangements_.size() != static_cast<std::size_t>(output_count)) {
        return false;
    }

    candidate_main_input_bus_ = -1;
    candidate_main_output_bus_ = -1;
    unsigned main_inputs = 0;
    unsigned main_outputs = 0;

    for (int32 i = 0; i < input_count; ++i) {
        BusInfo info{};
        if (component_->getBusInfo(kAudio, kInput, i, info) != kResultTrue)
            return false;
        SpeakerArrangement arrangement = SpeakerArr::kEmpty;
        if (processor_->getBusArrangement(kInput, i, arrangement) != kResultTrue)
            return false;
        if (capture_arrangements)
            candidate_input_arrangements_[static_cast<std::size_t>(i)] = arrangement;
        if (info.busType == BusTypes::kMain) {
            ++main_inputs;
            candidate_main_input_bus_ = i;
            layout.input_channels = static_cast<std::uint32_t>(SpeakerArr::getChannelCount(arrangement));
        } else {
            (void)component_->activateBus(kAudio, kInput, i, false);
        }
    }

    for (int32 i = 0; i < output_count; ++i) {
        BusInfo info{};
        if (component_->getBusInfo(kAudio, kOutput, i, info) != kResultTrue)
            return false;
        SpeakerArrangement arrangement = SpeakerArr::kEmpty;
        if (processor_->getBusArrangement(kOutput, i, arrangement) != kResultTrue)
            return false;
        if (capture_arrangements)
            candidate_output_arrangements_[static_cast<std::size_t>(i)] = arrangement;
        if (info.busType == BusTypes::kMain) {
            ++main_outputs;
            candidate_main_output_bus_ = i;
            layout.output_channels = static_cast<std::uint32_t>(SpeakerArr::getChannelCount(arrangement));
        } else {
            (void)component_->activateBus(kAudio, kOutput, i, false);
        }
    }

    return main_inputs == 1 && main_outputs == 1 &&
           candidate_main_input_bus_ >= 0 && candidate_main_output_bus_ >= 0 &&
           layout.supported();
}

bool Vst3Engine::inspect_requested_io(IoLayout& layout) noexcept
{
    return inspect_io_topology(layout, true);
}

bool Vst3Engine::confirm_requested_io(const IoLayout&) noexcept
{
    if (!processor_ || candidate_input_arrangements_.empty() || candidate_output_arrangements_.empty())
        return false;
    return processor_->setBusArrangements(
               candidate_input_arrangements_.data(), static_cast<int32>(candidate_input_arrangements_.size()),
               candidate_output_arrangements_.data(), static_cast<int32>(candidate_output_arrangements_.size())) == kResultTrue;
}

bool Vst3Engine::inspect_confirmed_io(IoLayout& layout) noexcept
{
    return inspect_io_topology(layout, false);
}

bool Vst3Engine::rebuild_process_data(const IoLayout&) noexcept
{
    if (!component_ || candidate_main_input_bus_ < 0 || candidate_main_output_bus_ < 0)
        return false;

    process_data_.unprepare();
    for (int32 i = 0; i < component_->getBusCount(kAudio, kInput); ++i)
        (void)component_->activateBus(kAudio, kInput, i, i == candidate_main_input_bus_);
    for (int32 i = 0; i < component_->getBusCount(kAudio, kOutput); ++i)
        (void)component_->activateBus(kAudio, kOutput, i, i == candidate_main_output_bus_);

    if (!process_data_.prepare(*component_, 0, kSample32))
        return false;
    process_data_.processContext = &process_context_;
    return true;
}

std::uint32_t Vst3Engine::query_latency() noexcept
{
    return processor_ ? processor_->getLatencySamples() : 0;
}

bool Vst3Engine::commit_io(const IoLayout& layout, std::uint32_t latency_samples) noexcept
{
    if (!layout.supported() || candidate_main_input_bus_ < 0 || candidate_main_output_bus_ < 0)
        return false;
    main_input_bus_ = candidate_main_input_bus_;
    main_output_bus_ = candidate_main_output_bus_;
    plugin_input_channels_ = layout.input_channels;
    plugin_output_channels_ = layout.output_channels;
    latency_samples_ = latency_samples;
    return true;
}

void Vst3Engine::request_recovery() noexcept
{
    io_recovery_requested_ = true;
}

bool Vst3Engine::refresh_io_after_restart(std::string& error)
{
    error.clear();
    io_recovery_requested_ = false;
    const auto result = run_io_restart_transaction(*this);
    if (!result.committed) {
        error = "VST3 dynamic I/O restart failed at step " + std::to_string(static_cast<int>(result.failed_step));
        return false;
    }
    return true;
}

bool Vst3Engine::refresh_latency_after_restart(std::string& error)
{
    error.clear();
    if (!component_ || !processor_) {
        error = "VST3 component unavailable while refreshing latency";
        return false;
    }

    const auto result = run_latency_restart_transaction(*this);
    if (!result.committed) {
        error = std::string("VST3 latency restart failed at ") +
                latency_restart_step_name(result.failed_step);
        return false;
    }

    latency_samples_ = result.latency_samples;
    return true;
}

bool Vst3Engine::set_processing(bool enabled) noexcept
{
    return processor_ && processor_->setProcessing(enabled) == kResultTrue;
}

bool Vst3Engine::set_active(bool enabled) noexcept
{
    return component_ && component_->setActive(enabled) == kResultTrue;
}

std::uint32_t Vst3Engine::get_latency_samples() noexcept
{
    return processor_ ? processor_->getLatencySamples() : 0;
}

void Vst3Engine::set_component_handler(IComponentHandler* handler) noexcept
{
    if (controller_)
        (void)controller_->setComponentHandler(handler);
}

void Vst3Engine::close() noexcept
{
    if (controller_)
        (void)controller_->setComponentHandler(nullptr);
    if (processor_)
        processor_->setProcessing(false);
    if (component_)
        component_->setActive(false);
    process_data_.unprepare();
    input_parameter_changes_.clearQueue();
    output_parameter_changes_.clearQueue();
    parameter_changes_pending_ = false;
    parameter_update_count_ = 0;
    parameters_.clear();
    processor_ = nullptr;
    controller_ = nullptr;
    component_ = nullptr;
    provider_ = nullptr;
    module_.reset();
    PluginContextFactory::instance().setPluginContext(nullptr);
    host_ = nullptr;
    main_input_bus_ = -1;
    main_output_bus_ = -1;
    candidate_main_input_bus_ = -1;
    candidate_main_output_bus_ = -1;
    candidate_input_arrangements_.clear();
    candidate_output_arrangements_.clear();
    plugin_input_channels_ = 0;
    plugin_output_channels_ = 0;
    io_recovery_requested_ = false;
    plugin_name_.clear();
    latency_samples_ = 0;
    sample_position_ = 0;
}

EngineParameter* Vst3Engine::find_parameter(std::uint32_t id) noexcept
{
    auto it = std::find_if(parameters_.begin(), parameters_.end(), [id](const EngineParameter& parameter) {
        return parameter.id == id;
    });
    return it == parameters_.end() ? nullptr : &*it;
}

bool Vst3Engine::queue_parameter_impl(std::uint32_t id, double normalized, bool update_controller) noexcept
{
    if (!processor_)
        return false;

    EngineParameter* parameter = find_parameter(id);
    if (!parameter)
        return false;

    // Hidden/read-only flags restrict host-authored fallback controls. A native
    // vendor editor is the plug-in's own controller UI; its performEdit() still
    // has to be transferred by the host to the processor, even for parameters
    // that are not meant to be exposed as generic host controls.
    if (update_controller && (parameter->flags & (ParameterReadOnly | ParameterHidden)) != 0)
        return false;

    normalized = normalize_parameter_value(normalized, parameter->step_count);
    if (update_controller) {
        if (!controller_ || controller_->setParamNormalized(static_cast<ParamID>(id), normalized) != kResultTrue)
            return false;
    }

    int32 queue_index = 0;
    IParamValueQueue* queue = input_parameter_changes_.addParameterData(static_cast<ParamID>(id), queue_index);
    if (!queue)
        return false;

    int32 point_index = 0;
    if (queue->addPoint(0, normalized, point_index) != kResultTrue)
        return false;

    parameter->current_normalized = normalized;
    record_parameter_update(id, normalized);
    parameter_changes_pending_ = true;
    return true;
}

bool Vst3Engine::queue_parameter(std::uint32_t id, double normalized) noexcept
{
    return queue_parameter_impl(id, normalized, true);
}

bool Vst3Engine::queue_parameter_from_controller(std::uint32_t id, double normalized) noexcept
{
    return queue_processor_parameter(id, normalized);
}

bool Vst3Engine::set_controller_parameter(std::uint32_t id, double normalized) noexcept
{
    if (!controller_)
        return false;
    EngineParameter* parameter = find_parameter(id);
    if (!parameter)
        return false;
    normalized = normalize_parameter_value(normalized, parameter->step_count);
    return controller_->setParamNormalized(static_cast<ParamID>(id), normalized) == kResultTrue;
}

bool Vst3Engine::queue_processor_parameter(std::uint32_t id, double normalized) noexcept
{
    return queue_parameter_impl(id, normalized, false);
}

void Vst3Engine::refresh_parameter_values() noexcept
{
    if (!controller_)
        return;
    for (auto& parameter : parameters_) {
        const double value = normalize_parameter_value(
            controller_->getParamNormalized(static_cast<ParamID>(parameter.id)), parameter.step_count);
        if (value == parameter.current_normalized)
            continue;
        parameter.current_normalized = value;
        record_parameter_update(parameter.id, value);
    }
}

bool Vst3Engine::refresh_parameter_metadata(std::string& error)
{
    error.clear();
    return enumerate_parameters(error);
}

bool Vst3Engine::apply_pending_parameter_changes(ProcessData& data) noexcept
{
    if (!parameter_changes_pending_) {
        data.inputParameterChanges = nullptr;
        return false;
    }
    data.inputParameterChanges = &input_parameter_changes_;
    return true;
}

void Vst3Engine::finish_parameter_changes() noexcept
{
    input_parameter_changes_.clearQueue();
    parameter_changes_pending_ = false;
}

void Vst3Engine::record_parameter_update(std::uint32_t id, double normalized) noexcept
{
    for (std::size_t i = 0; i < parameter_update_count_; ++i) {
        if (parameter_updates_[i].id == id) {
            parameter_updates_[i].normalized = normalized;
            return;
        }
    }
    if (parameter_update_count_ < parameter_updates_.size())
        parameter_updates_[parameter_update_count_++] = {id, normalized};
}

void Vst3Engine::capture_output_parameter_changes() noexcept
{
    const int32 count = output_parameter_changes_.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = output_parameter_changes_.getParameterData(i);
        if (!queue || queue->getPointCount() <= 0)
            continue;

        int32 sample_offset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) != kResultTrue)
            continue;

        const auto id = static_cast<std::uint32_t>(queue->getParameterId());
        EngineParameter* parameter = find_parameter(id);
        if (!parameter)
            continue;

        value = normalize_parameter_value(value, parameter->step_count);
        parameter->current_normalized = value;
        // Processor feedback is intentionally controller-free. S2.2 transfers
        // this update over DSP->control SPSC and the control thread applies it
        // to IEditController. This keeps vendor UI calls out of process().
        record_parameter_update(id, value);
    }
    output_parameter_changes_.clearQueue();
}

std::size_t Vst3Engine::take_parameter_updates(EngineParameterUpdate* destination, std::size_t capacity) noexcept
{
    if (!destination || capacity == 0)
        return 0;
    const std::size_t count = std::min(capacity, parameter_update_count_);
    std::copy_n(parameter_updates_.begin(), count, destination);
    if (count < parameter_update_count_)
        std::move(parameter_updates_.begin() + static_cast<std::ptrdiff_t>(count),
                  parameter_updates_.begin() + static_cast<std::ptrdiff_t>(parameter_update_count_),
                  parameter_updates_.begin());
    parameter_update_count_ -= count;
    return count;
}

bool Vst3Engine::flush_parameter_changes() noexcept
{
    if (!processor_ || !parameter_changes_pending_)
        return true;

    ProcessData flush{};
    flush.processMode = kRealtime;
    flush.symbolicSampleSize = kSample32;
    flush.numSamples = 0;
    flush.numInputs = 0;
    flush.numOutputs = 0;
    flush.inputs = nullptr;
    flush.outputs = nullptr;
    flush.inputEvents = nullptr;
    flush.outputEvents = nullptr;
    flush.outputParameterChanges = &output_parameter_changes_;
    flush.processContext = &process_context_;
    apply_pending_parameter_changes(flush);

    const tresult result = processor_->process(flush);
    finish_parameter_changes();
    capture_output_parameter_changes();
    return result == kResultOk;
}

bool Vst3Engine::process(AudioSlot& slot) noexcept
{
    if (!processor_ || slot.frames == 0 || slot.frames > kMaxFrames || slot.channels != channels_)
        return false;

    Sample32* in[kMaxChannels]{};
    Sample32* out[kMaxChannels]{};
    const float* transport_in1 = channels_ == 2 ? slot.input[1] : nullptr;
    float* transport_out1 = channels_ == 2 ? slot.output[1] : nullptr;
    if (!prepare_input_channels(slot.input[0], transport_in1, channels_, plugin_input_channels_,
                                slot.frames, input_adapter_[0].data(), input_adapter_[1].data(), in) ||
        !prepare_output_channels(slot.output[0], transport_out1, channels_, plugin_output_channels_,
                                 output_adapter_[0].data(), output_adapter_[1].data(), out))
        return false;

    if (!process_data_.setChannelBuffers(kInput, main_input_bus_, in, static_cast<int32>(plugin_input_channels_)) ||
        !process_data_.setChannelBuffers(kOutput, main_output_bus_, out, static_cast<int32>(plugin_output_channels_)))
        return false;

    process_data_.numSamples = static_cast<int32>(slot.frames);
    process_data_.inputEvents = nullptr;
    process_data_.outputEvents = nullptr;
    process_data_.outputParameterChanges = &output_parameter_changes_;
    apply_pending_parameter_changes(process_data_);

    process_context_.projectTimeSamples = sample_position_;
    sample_position_ += slot.frames;

    const tresult result = processor_->process(process_data_);
    finish_parameter_changes();
    capture_output_parameter_changes();
    if (result != kResultOk)
        return false;
    return finalize_output_channels(slot.output[0], transport_out1, channels_, plugin_output_channels_,
                                    slot.frames, out[0], out[1]);
}

} // namespace safevst3

#endif
