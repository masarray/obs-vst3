#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <windows.h>

namespace safevst3::r1_3_fixture_a {
using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUid(0xA1313001, 0x92D74B77, 0xA29D2F12, 0x01300001);
constexpr auto kPluginName = "SafeVST3 R1-3 Affine A x2 slow";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kStereo;

void signal_entered_process() noexcept
{
    wchar_t event_name[256]{};
    const DWORD size = GetEnvironmentVariableW(L"SAFEVST3_R13_A_ENTER_EVENT", event_name, 256);
    if (size == 0 || size >= 256)
        return;
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name);
    if (!event)
        return;
    SetEvent(event);
    CloseHandle(event);
}

class AffineA final : public AudioEffect {
public:
    static FUnknown* create_instance(void*) { return static_cast<IAudioProcessor*>(new AffineA()); }

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
    uint32 PLUGIN_API getLatencySamples() override { return 5; }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.numSamples < 0 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs || data.inputs[0].numChannels != 2 ||
            data.outputs[0].numChannels != 2 || !data.inputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32)
            return kResultFalse;

        signal_entered_process();
        Sleep(200);
        for (int32 ch = 0; ch < 2; ++ch) {
            auto* in = data.inputs[0].channelBuffers32[ch];
            auto* out = data.outputs[0].channelBuffers32[ch];
            if (!in || !out)
                return kResultFalse;
            for (int32 frame = 0; frame < data.numSamples; ++frame)
                out[frame] = in[frame] * 2.0f;
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }
};
} // namespace safevst3::r1_3_fixture_a

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::r1_3_fixture_a::AffineA;
using safevst3::r1_3_fixture_a::kPluginName;
using safevst3::r1_3_fixture_a::kProcessorUid;

BEGIN_FACTORY_DEF("OBS Safe VST3 Tests", "https://github.com/masarray/obs-vst3", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid), PClassInfo::kManyInstances, kVstAudioEffectClass,
           kPluginName, 0, "Fx", "1.0.0", kVstVersionString, AffineA::create_instance)
END_FACTORY
