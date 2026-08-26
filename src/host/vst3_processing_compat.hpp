#pragma once

#ifdef _WIN32

#include "common/state_restore_policy.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <cstddef>

namespace safevst3 {

// Some shipping VST3 processors, including the iZotope family observed during
// S1.8c qualification, return kNotImplemented from setProcessing even though
// their process() implementation is usable. Keep that compatibility quirk
// isolated here: only kNotImplemented is normalized to success. kResultFalse
// and every other failure remain visible to the existing host diagnostics.
class CompatibleAudioProcessorPtr {
public:
    using Processor = Steinberg::Vst::IAudioProcessor;

    CompatibleAudioProcessorPtr() = default;

    CompatibleAudioProcessorPtr& operator=(Processor* processor) noexcept
    {
        processor_ = processor;
        return *this;
    }

    CompatibleAudioProcessorPtr& operator=(std::nullptr_t) noexcept
    {
        processor_ = static_cast<Processor*>(nullptr);
        return *this;
    }

    explicit operator bool() const noexcept { return processor_.get() != nullptr; }

    CompatibleAudioProcessorPtr* operator->() noexcept { return this; }
    const CompatibleAudioProcessorPtr* operator->() const noexcept { return this; }

    Steinberg::tresult setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                          Steinberg::int32 num_ins,
                                          Steinberg::Vst::SpeakerArrangement* outputs,
                                          Steinberg::int32 num_outs)
    {
        return processor_->setBusArrangements(inputs, num_ins, outputs, num_outs);
    }

    Steinberg::tresult getBusArrangement(Steinberg::Vst::BusDirection direction,
                                         Steinberg::int32 index,
                                         Steinberg::Vst::SpeakerArrangement& arrangement)
    {
        return processor_->getBusArrangement(direction, index, arrangement);
    }

    Steinberg::tresult canProcessSampleSize(Steinberg::int32 symbolic_sample_size)
    {
        return processor_->canProcessSampleSize(symbolic_sample_size);
    }

    Steinberg::uint32 getLatencySamples()
    {
        return processor_->getLatencySamples();
    }

    Steinberg::tresult setupProcessing(Steinberg::Vst::ProcessSetup& setup)
    {
        return processor_->setupProcessing(setup);
    }

    Steinberg::tresult setProcessing(Steinberg::TBool state)
    {
        const Steinberg::tresult result = processor_->setProcessing(state);
        const PluginCallResult classified =
            result == Steinberg::kResultTrue ? PluginCallResult::Success :
            result == Steinberg::kResultFalse ? PluginCallResult::ResultFalse :
            result == Steinberg::kNotImplemented ? PluginCallResult::NotImplemented :
                                                   PluginCallResult::UnexpectedFailure;
        return accepts_processing_state_result(classified) ? Steinberg::kResultTrue : result;
    }

    Steinberg::tresult process(Steinberg::Vst::ProcessData& data)
    {
        return processor_->process(data);
    }

    Steinberg::uint32 getTailSamples()
    {
        return processor_->getTailSamples();
    }

private:
    Steinberg::FUnknownPtr<Processor> processor_{};
};

} // namespace safevst3

#endif
