#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace safevst3::r3_3_fixture {

using namespace Steinberg;
using namespace Steinberg::Vst;

static const FUID kProcessorUid(0x13C07981, 0xA3E34AB4, 0xB86D9380, 0x17E35D42);
static const FUID kControllerUid(0x5227BC17, 0x44E04C01, 0x8B94CF4B, 0xF0B9D2A1);
constexpr ParamID kGainParameterId = 100;
constexpr ParamID kPresetTriggerParameterId = 101;
constexpr auto kPluginName = "SafeVST3 R3-3 Vendor Editor Fixture";
constexpr SpeakerArrangement kArrangement = SpeakerArr::kStereo;
constexpr uint32 kLatencySamples = 32;

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

class FixturePlugView final : public IPlugView {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
    {
        if (!object)
            return kInvalidArgument;
        *object = nullptr;
        if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual(iid, IPlugView::iid)) {
            *object = static_cast<IPlugView*>(this);
            addRef();
            return kResultTrue;
        }
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override
    {
        return refs_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    uint32 PLUGIN_API release() override
    {
        const uint32 previous = refs_.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 1) {
            delete this;
            return 0;
        }
        return previous - 1;
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        if (!parent || isPlatformTypeSupported(type) != kResultTrue || child_)
            return kResultFalse;
        const HWND host = static_cast<HWND>(parent);
        child_ = CreateWindowExW(0, L"STATIC", L"R3-3 deterministic vendor UI",
                                 WS_CHILD | WS_VISIBLE | SS_CENTER,
                                 0, 0, rect_.getWidth(), rect_.getHeight(),
                                 host, nullptr, GetModuleHandleW(nullptr), nullptr);
        return child_ ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API removed() override
    {
        if (child_) {
            DestroyWindow(child_);
            child_ = nullptr;
        }
        return kResultTrue;
    }

    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }

    tresult PLUGIN_API getSize(ViewRect* size) override
    {
        if (!size)
            return kInvalidArgument;
        *size = rect_;
        return kResultTrue;
    }

    tresult PLUGIN_API onSize(ViewRect* size) override
    {
        if (!size || size->getWidth() <= 0 || size->getHeight() <= 0)
            return kInvalidArgument;
        rect_ = *size;
        if (child_)
            SetWindowPos(child_, nullptr, 0, 0, rect_.getWidth(), rect_.getHeight(),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return kResultTrue;
    }

    tresult PLUGIN_API onFocus(TBool state) override
    {
        if (state && child_)
            SetFocus(child_);
        return kResultTrue;
    }

    tresult PLUGIN_API setFrame(IPlugFrame* frame) override
    {
        frame_ = frame;
        return kResultTrue;
    }

    tresult PLUGIN_API canResize() override { return kResultTrue; }

    tresult PLUGIN_API checkSizeConstraint(ViewRect* size) override
    {
        if (!size)
            return kInvalidArgument;
        if (size->getWidth() < 160)
            size->right = size->left + 160;
        if (size->getHeight() < 90)
            size->bottom = size->top + 90;
        return kResultTrue;
    }

private:
    ~FixturePlugView() { removed(); }

    std::atomic<uint32> refs_{1};
    HWND child_ = nullptr;
    IPlugFrame* frame_ = nullptr;
    ViewRect rect_{0, 0, 320, 180};
};

class Processor final : public AudioEffect {
public:
    static FUnknown* create_instance(void*)
    {
        return static_cast<IAudioProcessor*>(new Processor());
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

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 num_inputs,
                                           SpeakerArrangement* outputs, int32 num_outputs) override
    {
        if (!inputs || !outputs || num_inputs != 1 || num_outputs != 1)
            return kResultFalse;
        return inputs[0] == kArrangement && outputs[0] == kArrangement ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setProcessing(TBool) override { return kResultTrue; }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultTrue : kResultFalse;
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
        // A split-component VST3 receives editor/automation changes through the
        // host's inputParameterChanges bridge. This fixture intentionally keeps
        // controller and processor state separate so R3-3 can catch hosts that
        // update only the GUI/controller while leaving DSP state at defaults.
        if (data.inputParameterChanges) {
            const int32 queue_count = data.inputParameterChanges->getParameterCount();
            for (int32 queue_index = 0; queue_index < queue_count; ++queue_index) {
                IParamValueQueue* queue =
                    data.inputParameterChanges->getParameterData(queue_index);
                if (!queue || queue->getParameterId() != kGainParameterId ||
                    queue->getPointCount() <= 0)
                    continue;
                int32 sample_offset = 0;
                ParamValue value = gain_;
                if (queue->getPoint(queue->getPointCount() - 1,
                                    sample_offset, value) == kResultTrue)
                    gain_ = std::clamp(value, 0.0, 1.0);
            }
        }

        if (data.numSamples == 0)
            return kResultOk;
        if (data.numSamples < 0 || data.numInputs != 1 || data.numOutputs != 1 ||
            !data.inputs || !data.outputs || data.inputs[0].numChannels != 2 ||
            data.outputs[0].numChannels != 2 || !data.inputs[0].channelBuffers32 ||
            !data.outputs[0].channelBuffers32)
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

class Controller final : public EditController {
public:
    static FUnknown* create_instance(void*)
    {
        return static_cast<IEditController*>(new Controller());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult result = EditController::initialize(context);
        if (result != kResultOk)
            return result;
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5,
                                ParameterInfo::kCanAutomate, kGainParameterId);
        parameters.addParameter(STR16("Preset Trigger"), nullptr, 0, 0.5,
                                ParameterInfo::kIsHidden, kPresetTriggerParameterId);
        return kResultOk;
    }

    tresult PLUGIN_API setComponentHandler(IComponentHandler* handler) override
    {
        component_handler_ = handler;
        return EditController::setComponentHandler(handler);
    }

    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override
    {
        const tresult result = EditController::setParamNormalized(tag, value);
        if (result != kResultTrue || !native_view_created_ || !component_handler_)
            return result;

        if (tag == kPresetTriggerParameterId) {
            // Model an internal vendor preset load. The controller updates many
            // values privately, then asks the host to resynchronize all parameter
            // values with the processor rather than emitting performEdit per knob.
            (void)EditController::setParamNormalized(kGainParameterId, value);
            (void)component_handler_->restartComponent(kParamValuesChanged);
            return result;
        }

        if (tag == kGainParameterId) {
            // Model a normal native GUI edit: the controller changes locally then
            // asks the host to forward that value to the processor component.
            (void)component_handler_->beginEdit(tag);
            (void)component_handler_->performEdit(tag, value);
            (void)component_handler_->endEdit(tag);
        }
        return result;
    }

    IPlugView* PLUGIN_API createView(FIDString name) override
    {
        if (name && std::strcmp(name, ViewType::kEditor) == 0) {
            native_view_created_ = true;
            return new FixturePlugView();
        }
        return nullptr;
    }

    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        double value = 0.0;
        if (!read_double(state, value))
            return kResultFalse;
        return setParamNormalized(kGainParameterId, std::clamp(value, 0.0, 1.0));
    }

private:
    IComponentHandler* component_handler_ = nullptr;
    bool native_view_created_ = false;
};

} // namespace safevst3::r3_3_fixture

using namespace Steinberg;
using namespace Steinberg::Vst;
using safevst3::r3_3_fixture::Controller;
using safevst3::r3_3_fixture::Processor;
using safevst3::r3_3_fixture::kControllerUid;
using safevst3::r3_3_fixture::kPluginName;
using safevst3::r3_3_fixture::kProcessorUid;

BEGIN_FACTORY_DEF("OBS Safe VST3 Tests", "https://github.com/masarray/obs-vst3", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUid), PClassInfo::kManyInstances,
           kVstAudioEffectClass, kPluginName, 0, "Fx", "1.0.0", kVstVersionString,
           Processor::create_instance)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUid), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "SafeVST3 R3-3 Fixture Controller", 0, "",
           "1.0.0", kVstVersionString, Controller::create_instance)

END_FACTORY
