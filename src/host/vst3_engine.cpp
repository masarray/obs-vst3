#ifdef _WIN32

#include "host/vst3_engine.hpp"

#include "common/parameter_utils.hpp"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <vector>

namespace safevst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

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

    parameters_.reserve(static_cast<std::size_t>(std::min<int32>(count, static_cast<int32>(kMaxParameters))));
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

void Vst3Engine::close() noexcept
{
    if (processor_)
        processor_->setProcessing(false);
    if (component_)
        component_->setActive(false);
    process_data_.unprepare();
    input_parameter_changes_.clearQueue();
    parameter_changes_pending_ = false;
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

bool Vst3Engine::queue_parameter(std::uint32_t id, double normalized) noexcept
{
    if (!controller_ || !processor_)
        return false;

    EngineParameter* parameter = find_parameter(id);
    if (!parameter || (parameter->flags & (ParameterReadOnly | ParameterHidden)) != 0)
        return false;

    normalized = normalize_parameter_value(normalized, parameter->step_count);
    if (controller_->setParamNormalized(static_cast<ParamID>(id), normalized) != kResultTrue)
        return false;

    int32 queue_index = 0;
    IParamValueQueue* queue = input_parameter_changes_.addParameterData(static_cast<ParamID>(id), queue_index);
    if (!queue)
        return false;

    int32 point_index = 0;
    if (queue->addPoint(0, normalized, point_index) != kResultTrue)
        return false;

    parameter->current_normalized = normalized;
    parameter_changes_pending_ = true;
    return true;
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
    flush.outputParameterChanges = nullptr;
    flush.processContext = &process_context_;
    apply_pending_parameter_changes(flush);

    const tresult result = processor_->process(flush);
    finish_parameter_changes();
    return result == kResultOk;
}

bool Vst3Engine::process(AudioSlot& slot) noexcept
{
    if (!processor_ || slot.frames == 0 || slot.frames > kMaxFrames || slot.channels != channels_)
        return false;

    Sample32* in[kMaxChannels]{};
    Sample32* out[kMaxChannels]{};
    for (std::uint32_t ch = 0; ch < channels_; ++ch) {
        in[ch] = slot.input[ch];
        out[ch] = slot.output[ch];
    }

    if (!process_data_.setChannelBuffers(kInput, main_input_bus_, in, static_cast<int32>(channels_)) ||
        !process_data_.setChannelBuffers(kOutput, main_output_bus_, out, static_cast<int32>(channels_)))
        return false;

    process_data_.numSamples = static_cast<int32>(slot.frames);
    process_data_.inputEvents = nullptr;
    process_data_.outputEvents = nullptr;
    process_data_.outputParameterChanges = nullptr;
    apply_pending_parameter_changes(process_data_);

    process_context_.projectTimeSamples = sample_position_;
    sample_position_ += slot.frames;

    const tresult result = processor_->process(process_data_);
    finish_parameter_changes();
    return result == kResultOk;
}

} // namespace safevst3

#endif
