#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <cstdint>

#ifndef SAFEVST3_FIXTURE_CHANNELS
#error "SAFEVST3_FIXTURE_CHANNELS must be defined to 1 or 2"
#endif

#if SAFEVST3_FIXTURE_CHANNELS != 1 && SAFEVST3_FIXTURE_CHANNELS != 2
#error "SAFEVST3_FIXTURE_CHANNELS must be 1 or 2"
#endif

namespace safevst3::test_fixture {

using namespace Steinberg;
using namespace Steinberg::Vst;

#if SAFEVST3_FIXTURE_CHANNELS == 1
static const FUID kProcessorUid(0x3B1069C1, 0x5C474C0E, 0xAA2E925E, 0x6D6B4F01);
constexpr auto kPluginName = "SafeVST3 R0-1 Mono Fixture";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kMono;
#else
static const FUID kProcessorUid(0x51C2A2D7, 0xE2E047FA, 0xBF0C133E, 0x71A0B202);
constexpr auto kPluginName = "SafeVST3 R0-1 Stereo Fixture";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kStereo;
#endif

class ProcessFixture final : public AudioEffect {
public:
    static FUnknown* create_instance(void*) {
        return static_cast<IAudioProcessor*>(new ProcessFixture());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        const tresult result = AudioEffect::initialize(context);
        if (result != kResultOk)
            return result;
        addAudioInput(STR16("Input"), kArrangement);
        addAudioOutput(STR16("Output"), kArrangement);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs,
                                           int32 num_inputs,
                                           SpeakerArrangement* outputs,
                                           int32 num_outputs) override {
        if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1)
            return kResultFalse;
        if (inputs[0] != kArrangement || outputs[0] != kArrangement)
            return kResultFalse;
        return kResultTrue;
    }

    tresult PLUGIN_API setProcessing(TBool) override {
        return kResultTrue;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolic_sample_size) override {
        return symbolic_sample_size == kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API process(ProcessData& data) override {
        if (data.numSamples < 0)
            return kInvalidArgument;
        if (data.numSamples == 0)
            return kResultOk;
        if (data.numInputs != 1 || data.numOutputs != 1 || !data.inputs || !data.outputs)
            return kResultFalse;

        constexpr int32 kChannels = SAFEVST3_FIXTURE_CHANNELS;
        if (data.inputs[0].numChannels != kChannels ||
            data.outputs[0].numChannels != kChannels ||
            !data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32)
            return kResultFalse;

        auto** input = data.inputs[0].channelBuffers32;
        auto** output = data.outputs[0].channelBuffers32;
        for (int32 channel = 0; channel < kChannels; ++channel) {
            if (!input[channel] || !output[channel])
                return kResultFalse;
        }

        // A sentinel forces a real IAudioProcessor::process failure. The engine
        // characterization uses this to lock the current rule that a failed
        // block returns false and does not advance projectTimeSamples.
        if (input[0][0] <= -900.0f)
            return kResultFalse;

        if (!data.processContext)
            return kResultFalse;
        const float block_position =
            static_cast<float>(data.processContext->projectTimeSamples);

        // Channel-specific gains make stereo->mono and mono->stereo adaptation
        // observable without parameters or controller state. Mono uses x2;
        // stereo uses x2 on L and x4 on R.
        for (int32 channel = 0; channel < kChannels; ++channel) {
            const float gain = channel == 0 ? 2.0f : 4.0f;
            for (int32 frame = 0; frame < data.numSamples; ++frame)
                output[channel][frame] = input[channel][frame] * gain + block_position;
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
};

} // namespace safevst3::test_fixture

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::test_fixture::ProcessFixture;
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
           ProcessFixture::create_instance)

END_FACTORY
