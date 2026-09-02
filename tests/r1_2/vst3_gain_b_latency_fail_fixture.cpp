#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace safevst3::r1_2_fixture_b {
using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUid(0xB1311202, 0x82D64A66, 0xB18C3E23, 0x02B20002);
constexpr auto kPluginName = "SafeVST3 R1-2 Gain B x0.5 latency 23 fail trigger";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kStereo;
constexpr uint32 kLatencySamples = 23;

class GainB final : public AudioEffect {
public:
    static FUnknown* create_instance(void*) { return static_cast<IAudioProcessor*>(new GainB()); }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const auto result = AudioEffect::initialize(context);
        if (result != kResultOk)
            return result;
        addAudioInput(STR16("Input"), kArrangement);
        addAudioOutput(STR16("Output"), kArrangement);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 num_inputs,
                                           SpeakerArrangement* outputs, int32 num_outputs) override
    {
        if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1)
            return kResultFalse;
        return inputs[0] == kArrangement && outputs[0] == kArrangement ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
    }

    uint32 PLUGIN_API getLatencySamples() override { return kLatencySamples; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultTrue; }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.numSamples < 0 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs || data.inputs[0].numChannels != 2 ||
            data.outputs[0].numChannels != 2 || !data.inputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32)
            return kResultFalse;

        auto* in_left = data.inputs[0].channelBuffers32[0];
        auto* in_right = data.inputs[0].channelBuffers32[1];
        if (!in_left || !in_right)
            return kResultFalse;

        // R1-2 deterministic failure trigger. The harness sends +0.75/-0.75 dry;
        // Gain A turns that into +1.5/-1.5, proving B fails only after A succeeds.
        if (data.numSamples > 0 && in_left[0] == 1.5f && in_right[0] == -1.5f)
            return kResultFalse;

        for (int32 ch = 0; ch < 2; ++ch) {
            auto* in = data.inputs[0].channelBuffers32[ch];
            auto* out = data.outputs[0].channelBuffers32[ch];
            if (!in || !out)
                return kResultFalse;
            for (int32 frame = 0; frame < data.numSamples; ++frame)
                out[frame] = in[frame] * 0.5f;
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
};
} // namespace safevst3::r1_2_fixture_b

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::r1_2_fixture_b::GainB;
using safevst3::r1_2_fixture_b::kPluginName;
using safevst3::r1_2_fixture_b::kProcessorUid;

BEGIN_FACTORY_DEF("OBS Safe VST3 Tests", "https://github.com/masarray/obs-vst3", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid), PClassInfo::kManyInstances, kVstAudioEffectClass,
           kPluginName, 0, "Fx", "1.0.0", kVstVersionString, GainB::create_instance)
END_FACTORY
