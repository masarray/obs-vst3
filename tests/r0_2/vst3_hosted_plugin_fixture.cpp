#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <algorithm>

namespace safevst3::test_fixture {

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUid(0x6EDEB158, 0x7E794DE6, 0xA1C3E02A, 0x19E90D44);
static const FUID kControllerUid(0x39E8D25F, 0xE6914122, 0x8A4E8B59, 0xD9279E16);
constexpr ParamID kGainParameterId = 100;
constexpr auto kPluginName = "SafeVST3 R0-2 HostedPlugin Fixture";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kStereo;
constexpr uint32 kLatencySamples = 64;

bool write_double(IBStream* stream, double value)
{
    if (!stream)
        return false;
    int32 written = 0;
    return stream->write(&value, static_cast<int32>(sizeof(value)), &written) == kResultTrue &&
           written == static_cast<int32>(sizeof(value));
}

bool read_double(IBStream* stream, double& value)
{
    if (!stream)
        return false;
    int32 read = 0;
    return stream->read(&value, static_cast<int32>(sizeof(value)), &read) == kResultTrue &&
           read == static_cast<int32>(sizeof(value));
}

class HostedPluginFixtureProcessor final : public AudioEffect {
public:
    static FUnknown* create_instance(void*)
    {
        return static_cast<IAudioProcessor*>(new HostedPluginFixtureProcessor());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult result = AudioEffect::initialize(context);
        if (result != kResultOk)
            return result;
        addAudioInput(STR16("Input"), kArrangement);
        addAudioOutput(STR16("Output"), kArrangement);
        return kResultOk;
    }

    tresult PLUGIN_API getControllerClassId(TUID class_id) override
    {
        kControllerUid.toTUID(class_id);
        return kResultTrue;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs,
                                           int32 num_inputs,
                                           SpeakerArrangement* outputs,
                                           int32 num_outputs) override
    {
        if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1)
            return kResultFalse;
        return inputs[0] == kArrangement && outputs[0] == kArrangement
            ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setProcessing(TBool) override { return kResultTrue; }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolic_sample_size) override
    {
        return symbolic_sample_size == kSample32 ? kResultTrue : kResultFalse;
    }

    uint32 PLUGIN_API getLatencySamples() override { return kLatencySamples; }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        return write_double(state, gain_) ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        double value = 0.0;
        if (!read_double(state, value))
            return kResultFalse;
        gain_ = std::clamp(value, 0.0, 1.0);
        return kResultTrue;
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.inputParameterChanges) {
            const int32 count = data.inputParameterChanges->getParameterCount();
            for (int32 index = 0; index < count; ++index) {
                auto* queue = data.inputParameterChanges->getParameterData(index);
                if (!queue || queue->getParameterId() != kGainParameterId || queue->getPointCount() <= 0)
                    continue;
                int32 sample_offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == kResultTrue)
                    gain_ = std::clamp(value, 0.0, 1.0);
            }
        }

        if (data.numSamples == 0)
            return kResultOk;
        if (data.numSamples < 0 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs ||
            data.inputs[0].numChannels != 2 || data.outputs[0].numChannels != 2 ||
            !data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32)
            return kResultFalse;

        for (int32 channel = 0; channel < 2; ++channel) {
            auto* input = data.inputs[0].channelBuffers32[channel];
            auto* output = data.outputs[0].channelBuffers32[channel];
            if (!input || !output)
                return kResultFalse;
            for (int32 frame = 0; frame < data.numSamples; ++frame)
                output[frame] = input[frame] * static_cast<float>(gain_);
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }

private:
    double gain_ = 0.5;
};

class HostedPluginFixtureController final : public EditController {
public:
    static FUnknown* create_instance(void*)
    {
        return static_cast<IEditController*>(new HostedPluginFixtureController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult result = EditController::initialize(context);
        if (result != kResultOk)
            return result;
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5,
                                ParameterInfo::kCanAutomate, kGainParameterId);
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        double value = 0.0;
        if (!read_double(state, value))
            return kResultFalse;
        return setParamNormalized(kGainParameterId, std::clamp(value, 0.0, 1.0));
    }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        return write_double(state, getParamNormalized(kGainParameterId)) ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        double value = 0.0;
        if (!read_double(state, value))
            return kResultFalse;
        return setParamNormalized(kGainParameterId, std::clamp(value, 0.0, 1.0));
    }
};

} // namespace safevst3::test_fixture

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::test_fixture::HostedPluginFixtureController;
using safevst3::test_fixture::HostedPluginFixtureProcessor;
using safevst3::test_fixture::kControllerUid;
using safevst3::test_fixture::kPluginName;
using safevst3::test_fixture::kProcessorUid;

BEGIN_FACTORY_DEF("OBS Safe VST3 Tests", "https://github.com/masarray/obs-vst3", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           kPluginName,
           0,
           "Fx",
           "1.0.0",
           kVstVersionString,
           HostedPluginFixtureProcessor::create_instance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUid),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           "SafeVST3 R0-2 HostedPlugin Fixture Controller",
           0,
           "",
           "1.0.0",
           kVstVersionString,
           HostedPluginFixtureController::create_instance)

END_FACTORY
