#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <cmath>

namespace safevst3::r1_1_fixture_b {
using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUid(0xB1311102, 0x46D249AB, 0x8C4F7D21, 0x01B10002);
constexpr auto kPluginName = "SafeVST3 R1-1 Gain B x0.5";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kStereo;

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

    tresult PLUGIN_API setProcessing(TBool) override { return kResultTrue; }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.numSamples <= 0 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs || data.inputs[0].numChannels != 2 ||
            data.outputs[0].numChannels != 2 || !data.inputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32)
            return kResultFalse;

        auto* left = data.inputs[0].channelBuffers32[0];
        auto* right = data.inputs[0].channelBuffers32[1];
        if (!left || !right || std::fabs(left[0] - 0.5f) > 1.0e-6f ||
            std::fabs(right[0] + 0.5f) > 1.0e-6f)
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
} // namespace safevst3::r1_1_fixture_b

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::r1_1_fixture_b::GainB;
using safevst3::r1_1_fixture_b::kPluginName;
using safevst3::r1_1_fixture_b::kProcessorUid;

BEGIN_FACTORY_DEF("OBS Safe VST3 Tests", "https://github.com/masarray/obs-vst3", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid), PClassInfo::kManyInstances, kVstAudioEffectClass,
           kPluginName, 0, "Fx", "1.0.0", kVstVersionString, GainB::create_instance)
END_FACTORY
