#ifdef _WIN32

#include "host/vst3_engine.hpp"

#include "pluginterfaces/vst/vstspeaker.h"

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
            error = channels == 1 ? "VST3 does not accept mono I/O for P0" : "VST3 does not accept stereo I/O for P0";
            return false;
        }
    }

    component_->activateBus(kAudio, kInput, main_input_bus_, true);
    component_->activateBus(kAudio, kOutput, main_output_bus_, true);
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
        error = "P0 supports only mono or stereo";
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
        error = "P0 requires float32-capable VST3 processing";
        return false;
    }

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
    processor_ = nullptr;
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
    process_data_.inputParameterChanges = nullptr;
    process_data_.outputParameterChanges = nullptr;

    process_context_.projectTimeSamples = sample_position_;
    sample_position_ += slot.frames;

    return processor_->process(process_data_) == kResultOk;
}

} // namespace safevst3

#endif
