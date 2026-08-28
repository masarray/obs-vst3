#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <cstdint>

namespace safevst3::r0_2_fixture {

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUid(0x5A36F1B2, 0x2C834761, 0x9E1507C4, 0xB1A2D301);
static const FUID kControllerUid(0xA861D24C, 0xDF0C4E32, 0x83B1F705, 0x26C9E402);
constexpr ParamID kGainId = 7001;
constexpr std::uint32_t kLatencySamples = 37;
constexpr std::uint32_t kComponentMagic = 0x52303243u; // R02C
constexpr std::uint32_t kControllerMagic = 0x52303255u; // R02U
constexpr std::int32_t kControllerCookie = 0x2468;

namespace {

template <typename T>
bool write_exact(IBStream* stream, const T& value)
{
    if (!stream)
        return false;
    T copy = value;
    int32 written = 0;
    return stream->write(&copy, static_cast<int32>(sizeof(copy)), &written) == kResultOk &&
           written == static_cast<int32>(sizeof(copy));
}

template <typename T>
bool read_exact(IBStream* stream, T& value)
{
    if (!stream)
        return false;
    int32 read = 0;
    return stream->read(&value, static_cast<int32>(sizeof(value)), &read) == kResultOk &&
           read == static_cast<int32>(sizeof(value));
}

} // namespace

class StatefulProcessor final : public AudioEffect {
public:
    StatefulProcessor()
    {
        setControllerClass(kControllerUid);
    }

    static FUnknown* create_instance(void*)
    {
        return static_cast<IAudioProcessor*>(new StatefulProcessor());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult result = AudioEffect::initialize(context);
        if (result != kResultOk)
            return result;
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs,
                                           int32 num_inputs,
                                           SpeakerArrangement* outputs,
                                           int32 num_outputs) override
    {
        if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1)
            return kResultFalse;
        return inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo
            ? kResultTrue
            : kResultFalse;
    }

    tresult PLUGIN_API setProcessing(TBool) override
    {
        return kResultTrue;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolic_sample_size) override
    {
        return symbolic_sample_size == kSample32 ? kResultTrue : kResultFalse;
    }

    uint32 PLUGIN_API getLatencySamples() override
    {
        return kLatencySamples;
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.numSamples < 0 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs)
            return kResultFalse;

        if (data.inputParameterChanges) {
            const int32 parameter_count = data.inputParameterChanges->getParameterCount();
            for (int32 index = 0; index < parameter_count; ++index) {
                auto* queue = data.inputParameterChanges->getParameterData(index);
                if (!queue || queue->getParameterId() != kGainId || queue->getPointCount() <= 0)
                    continue;
                int32 sample_offset = 0;
                ParamValue value = gain_normalized_;
                if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == kResultTrue)
                    gain_normalized_ = value;
            }
        }

        if (data.numSamples == 0)
            return kResultOk;
        if (data.inputs[0].numChannels != 2 || data.outputs[0].numChannels != 2 ||
            !data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32)
            return kResultFalse;

        const float gain = 1.0f + static_cast<float>(gain_normalized_);
        for (int32 channel = 0; channel < 2; ++channel) {
            auto* input = data.inputs[0].channelBuffers32[channel];
            auto* output = data.outputs[0].channelBuffers32[channel];
            if (!input || !output)
                return kResultFalse;
            for (int32 frame = 0; frame < data.numSamples; ++frame)
                output[frame] = input[frame] * gain;
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        return write_exact(state, kComponentMagic) && write_exact(state, gain_normalized_)
            ? kResultTrue
            : kResultFalse;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        std::uint32_t magic = 0;
        ParamValue gain = 0.0;
        if (!read_exact(state, magic) || magic != kComponentMagic || !read_exact(state, gain))
            return kResultFalse;
        gain_normalized_ = gain;
        return kResultTrue;
    }

private:
    ParamValue gain_normalized_ = 0.5;
};

class StatefulController final : public EditController {
public:
    static FUnknown* create_instance(void*)
    {
        return static_cast<IEditController*>(new StatefulController());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult result = EditController::initialize(context);
        if (result != kResultOk)
            return result;
        parameters.addParameter(
            STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kGainId);
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        std::uint32_t magic = 0;
        ParamValue gain = 0.0;
        if (!read_exact(state, magic) || magic != kComponentMagic || !read_exact(state, gain))
            return kResultFalse;
        return setParamNormalized(kGainId, gain);
    }

    tresult PLUGIN_API getState(IBStream* state) override
    {
        return write_exact(state, kControllerMagic) && write_exact(state, controller_cookie_)
            ? kResultTrue
            : kResultFalse;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        std::uint32_t magic = 0;
        std::int32_t cookie = 0;
        if (!read_exact(state, magic) || magic != kControllerMagic || !read_exact(state, cookie))
            return kResultFalse;
        controller_cookie_ = cookie;
        return kResultTrue;
    }

private:
    std::int32_t controller_cookie_ = kControllerCookie;
};

} // namespace safevst3::r0_2_fixture

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::r0_2_fixture::StatefulController;
using safevst3::r0_2_fixture::StatefulProcessor;
using safevst3::r0_2_fixture::kControllerUid;
using safevst3::r0_2_fixture::kProcessorUid;

BEGIN_FACTORY_DEF("OBS Safe VST3 Tests", "https://github.com/masarray/obs-vst3", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "SafeVST3 R0-2 Stateful Fixture",
           0,
           "Fx",
           "1.0.0",
           kVstVersionString,
           StatefulProcessor::create_instance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUid),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           "SafeVST3 R0-2 Stateful Fixture Controller",
           0,
           "",
           "1.0.0",
           kVstVersionString,
           StatefulController::create_instance)

END_FACTORY
