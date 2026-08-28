#ifdef _WIN32

#include "host/vst3_engine.hpp"

#include "common/audio_channel_adapter.hpp"
#include "common/parameter_utils.hpp"
#include "common/state_restore_policy.hpp"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <vector>

namespace safevst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

static_assert(kProcessNeedSystemTime == IProcessContextRequirements::kNeedSystemTime);
static_assert(kProcessNeedContinuousTimeSamples == IProcessContextRequirements::kNeedContinousTimeSamples);
static_assert(kProcessNeedProjectTimeMusic == IProcessContextRequirements::kNeedProjectTimeMusic);
static_assert(kProcessNeedBarPositionMusic == IProcessContextRequirements::kNeedBarPositionMusic);
static_assert(kProcessNeedCycleMusic == IProcessContextRequirements::kNeedCycleMusic);
static_assert(kProcessNeedSamplesToNextClock == IProcessContextRequirements::kNeedSamplesToNextClock);
static_assert(kProcessNeedTempo == IProcessContextRequirements::kNeedTempo);
static_assert(kProcessNeedTimeSignature == IProcessContextRequirements::kNeedTimeSignature);
static_assert(kProcessNeedChord == IProcessContextRequirements::kNeedChord);
static_assert(kProcessNeedFrameRate == IProcessContextRequirements::kNeedFrameRate);
static_assert(kProcessNeedTransportState == IProcessContextRequirements::kNeedTransportState);
static_assert(kProcessContextContinuousTimeValid == ProcessContext::kContTimeValid);

namespace {

bool copy_stream(MemoryStream& stream, std::vector<std::uint8_t>& destination,
                 const char* label, std::string& error)
{
    const auto raw_size = stream.getSize();
    if (raw_size < 0 || static_cast<std::uint64_t>(raw_size) > kMaxStateBytes) {
        error = std::string(label) + " state exceeds the supported snapshot size";
        return false;
    }

    const auto size = static_cast<std::size_t>(raw_size);
    if (size == 0) {
        destination.clear();
        return true;
    }

    const char* data = stream.getData();
    if (!data) {
        error = std::string(label) + " state stream returned no data";
        return false;
    }

    const auto* begin = reinterpret_cast<const std::uint8_t*>(data);
    destination.assign(begin, begin + static_cast<std::ptrdiff_t>(size));
    return true;
}

MemoryStream read_stream(const std::vector<std::uint8_t>& bytes)
{
    return MemoryStream(bytes.empty() ? nullptr : const_cast<std::uint8_t*>(bytes.data()),
                        static_cast<TSize>(bytes.size()));
}

PluginCallResult classify_plugin_call_result(tresult result) noexcept
{
    if (result == kResultTrue)
        return PluginCallResult::Success;
    if (result == kResultFalse)
        return PluginCallResult::ResultFalse;
    if (result == kNotImplemented)
        return PluginCallResult::NotImplemented;
    return PluginCallResult::UnexpectedFailure;
}

std::uint32_t supported_arrangement_channels(SpeakerArrangement arrangement) noexcept
{
    if (arrangement == SpeakerArr::kMono)
        return 1;
    if (arrangement == SpeakerArr::kStereo)
        return 2;
    return 0;
}

SpeakerArrangement fallback_arrangement_for_channels(int32 channels) noexcept
{
    if (channels == 0)
        return SpeakerArr::kEmpty;
    if (channels == 1)
        return SpeakerArr::kMono;
    if (channels == 2)
        return SpeakerArr::kStereo;
    return SpeakerArr::kEmpty;
}

const char* io_restart_step_name(IoRestartLifecycleStep step) noexcept
{
    switch (step) {
    case IoRestartLifecycleStep::StopProcessing: return "setProcessing(false)";
    case IoRestartLifecycleStep::Deactivate: return "setActive(false)";
    case IoRestartLifecycleStep::InspectRequested: return "inspect requested bus arrangements";
    case IoRestartLifecycleStep::ConfirmRequested: return "setBusArrangements";
    case IoRestartLifecycleStep::InspectConfirmed: return "inspect confirmed bus arrangements";
    case IoRestartLifecycleStep::RebuildProcessing: return "rebuild ProcessData";
    case IoRestartLifecycleStep::Activate: return "setActive(true)";
    case IoRestartLifecycleStep::QueryLatency: return "getLatencySamples()";
    case IoRestartLifecycleStep::StartProcessing: return "setProcessing(true)";
    case IoRestartLifecycleStep::Commit: return "commit I/O layout";
    case IoRestartLifecycleStep::None:
    default: return "unknown step";
    }
}

const char* latency_restart_step_name(LatencyRestartStep step) noexcept
{
    switch (step) {
    case LatencyRestartStep::StopProcessing:
        return "setProcessing(false)";
    case LatencyRestartStep::Deactivate:
        return "setActive(false)";
    case LatencyRestartStep::Activate:
        return "setActive(true)";
    case LatencyRestartStep::QueryLatency:
        return "getLatencySamples()";
    case LatencyRestartStep::StartProcessing:
        return "setProcessing(true)";
    case LatencyRestartStep::None:
    default:
        return "unknown step";
    }
}

} // namespace

Vst3Engine::~Vst3Engine() { close(); }

void Vst3Engine::report_startup_phase(StartupErrorCode phase) noexcept
{
    if (startup_phase_sink_)
        startup_phase_sink_->publish(phase);
}

bool Vst3Engine::configure_buses(std::uint32_t channels, std::string& error)
{
    main_input_bus_ = -1;
    main_output_bus_ = -1;
    plugin_input_channels_ = 0;
    plugin_output_channels_ = 0;

    report_startup_phase(StartupErrorCode::BusNegotiation);
    const int32 input_count = component_->getBusCount(kAudio, kInput);
    report_startup_phase(StartupErrorCode::BusNegotiation);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    if (input_count <= 0 || output_count <= 0) {
        error = "VST3 effect has no audio input/output bus";
        return false;
    }

    std::vector<SpeakerArrangement> input_arrangements(static_cast<std::size_t>(input_count), SpeakerArr::kEmpty);
    std::vector<SpeakerArrangement> output_arrangements(static_cast<std::size_t>(output_count), SpeakerArr::kEmpty);

    for (int32 i = 0; i < input_count; ++i) {
        BusInfo info{};
        report_startup_phase(StartupErrorCode::BusNegotiation);
        if (component_->getBusInfo(kAudio, kInput, i, info) != kResultTrue) {
            error = "VST3 input bus metadata unavailable";
            return false;
        }
        report_startup_phase(StartupErrorCode::BusNegotiation);
        if (processor_->getBusArrangement(kInput, i, input_arrangements[static_cast<std::size_t>(i)]) != kResultTrue) {
            const auto fallback = fallback_arrangement_for_channels(info.channelCount);
            if (info.channelCount > 2) {
                error = "VST3 input bus arrangement unavailable for multichannel bus";
                return false;
            }
            input_arrangements[static_cast<std::size_t>(i)] = fallback;
        }
        if (main_input_bus_ < 0 && info.busType == BusTypes::kMain)
            main_input_bus_ = i;
    }

    for (int32 i = 0; i < output_count; ++i) {
        BusInfo info{};
        report_startup_phase(StartupErrorCode::BusNegotiation);
        if (component_->getBusInfo(kAudio, kOutput, i, info) != kResultTrue) {
            error = "VST3 output bus metadata unavailable";
            return false;
        }
        report_startup_phase(StartupErrorCode::BusNegotiation);
        if (processor_->getBusArrangement(kOutput, i, output_arrangements[static_cast<std::size_t>(i)]) != kResultTrue) {
            const auto fallback = fallback_arrangement_for_channels(info.channelCount);
            if (info.channelCount > 2) {
                error = "VST3 output bus arrangement unavailable for multichannel bus";
                return false;
            }
            output_arrangements[static_cast<std::size_t>(i)] = fallback;
        }
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

    report_startup_phase(StartupErrorCode::BusNegotiation);
    const tresult set_result = processor_->setBusArrangements(
        input_arrangements.data(), input_count, output_arrangements.data(), output_count);
    if (set_result != kResultTrue && set_result != kResultFalse) {
        error = "VST3 setBusArrangements failed during initial negotiation";
        return false;
    }

    SpeakerArrangement actual_in = requested;
    SpeakerArrangement actual_out = requested;
    report_startup_phase(StartupErrorCode::BusNegotiation);
    const bool queried_in =
        processor_->getBusArrangement(kInput, main_input_bus_, actual_in) == kResultTrue;
    report_startup_phase(StartupErrorCode::BusNegotiation);
    const bool queried_out =
        processor_->getBusArrangement(kOutput, main_output_bus_, actual_out) == kResultTrue;
    const bool queried = queried_in && queried_out;
    if (set_result == kResultFalse && !queried) {
        error = "VST3 rejected requested I/O and did not expose a fallback arrangement";
        return false;
    }

    plugin_input_channels_ = supported_arrangement_channels(actual_in);
    plugin_output_channels_ = supported_arrangement_channels(actual_out);
    if (plugin_input_channels_ == 0 || plugin_output_channels_ == 0) {
        error = "VST3 main I/O is outside the supported mono/stereo scope";
        return false;
    }

    return true;
}

bool Vst3Engine::activate_configured_buses(std::string& error)
{
    if (!component_ || main_input_bus_ < 0 || main_output_bus_ < 0) {
        error = "VST3 init[bus-activation]: configured main buses unavailable";
        return false;
    }

    report_startup_phase(StartupErrorCode::BusActivation);
    const int32 input_count = component_->getBusCount(kAudio, kInput);
    report_startup_phase(StartupErrorCode::BusActivation);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    for (int32 i = 0; i < input_count; ++i) {
        report_startup_phase(StartupErrorCode::BusActivation);
        (void)component_->activateBus(kAudio, kInput, i, false);
    }
    for (int32 i = 0; i < output_count; ++i) {
        report_startup_phase(StartupErrorCode::BusActivation);
        (void)component_->activateBus(kAudio, kOutput, i, false);
    }

    // Preserve the broad S1 compatibility contract: some shipping plug-ins
    // return advisory/non-true results here even though the requested main bus
    // becomes usable. The compatibility fix is the Setup-Done ordering, not a
    // new fatal return-code requirement. Processing/setup failures remain hard.
    report_startup_phase(StartupErrorCode::BusActivation);
    (void)component_->activateBus(kAudio, kInput, main_input_bus_, true);
    report_startup_phase(StartupErrorCode::BusActivation);
    (void)component_->activateBus(kAudio, kOutput, main_output_bus_, true);
    return true;
}

bool Vst3Engine::enumerate_parameters(std::string& error)
{
    parameters_.clear();
    if (!controller_)
        return true;

    report_startup_phase(StartupErrorCode::ParameterCatalog);
    const int32 count = controller_->getParameterCount();
    if (count < 0) {
        error = "VST3 controller returned an invalid parameter count";
        return false;
    }

    parameters_.reserve(static_cast<std::size_t>(count));
    for (int32 index = 0; index < count; ++index) {
        ParameterInfo info{};
        report_startup_phase(StartupErrorCode::ParameterCatalog);
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
        report_startup_phase(StartupErrorCode::ParameterCatalog);
        parameter.current_normalized = normalize_parameter_value(controller_->getParamNormalized(info.id), info.stepCount);
        parameter.title = StringConvert::convert(info.title);
        parameter.units = StringConvert::convert(info.units);
        parameters_.push_back(std::move(parameter));
    }

    const auto queue_capacity = static_cast<int32>(parameters_.size());
    input_parameter_changes_.setMaxParameters(queue_capacity);
    output_parameter_changes_.setMaxParameters(queue_capacity);
    return true;
}

bool Vst3Engine::open(const std::string& path,
                      const std::string& class_id,
                      std::uint32_t sample_rate,
                      std::uint32_t channels,
                      IComponentHandler* component_handler,
                      StartupPhaseSink* startup_phase_sink,
                      std::string& error)
{
    close();
    startup_phase_sink_ = startup_phase_sink;
    if (channels == 0 || channels > kMaxChannels) {
        error = "Public preview supports only mono or stereo";
        return false;
    }

    host_ = owned(new HostApplication());
    // Preserve the S1 baseline SDK host-context contract. Strict lifecycle
    // ownership changes component/controller ordering only; it must not remove
    // the plugin context visible to SDK-backed objects during initialization.
    PluginContextFactory::instance().setPluginContext(host_.get());

    report_startup_phase(StartupErrorCode::ModuleLoad);
    module_ = VST3::Hosting::Module::create(path, error);
    if (!module_) {
        error = "VST3 init[module-load]: " + error;
        return false;
    }

    report_startup_phase(StartupErrorCode::ClassSelect);
    auto factory = module_->getFactory();
    report_startup_phase(StartupErrorCode::ClassSelect);
    factory.setHostContext(host_.get());
    const VST3::Hosting::ClassInfo* chosen = nullptr;
    report_startup_phase(StartupErrorCode::ClassSelect);
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
        error = class_id.empty()
            ? "VST3 init[class-select]: no VST3 audio-effect class found in module"
            : "VST3 init[class-select]: requested VST3 class ID not found";
        return false;
    }

    plugin_name_ = chosen->name();
    loaded_class_id_ = chosen->ID().toString();

    report_startup_phase(StartupErrorCode::ComponentCreate);
    component_ = factory.createInstance<IComponent>(chosen->ID());
    if (!component_) {
        error = "VST3 init[component-create]: component instance unavailable";
        return false;
    }
    report_startup_phase(StartupErrorCode::ComponentInitialize);
    if (component_->initialize(host_.get()) != kResultOk) {
        error = "VST3 init[component-initialize]: initialize failed";
        return false;
    }
    component_initialized_ = true;

    IEditController* single_controller = nullptr;
    report_startup_phase(StartupErrorCode::ControllerCreate);
    if (component_->queryInterface(IEditController::iid,
                                   reinterpret_cast<void**>(&single_controller)) == kResultTrue &&
        single_controller) {
        controller_ = owned(single_controller);
        controller_is_component_ = true;
    } else {
        TUID controller_cid{};
        report_startup_phase(StartupErrorCode::ControllerCreate);
        if (component_->getControllerClassId(controller_cid) == kResultTrue) {
            report_startup_phase(StartupErrorCode::ControllerCreate);
            controller_ = factory.createInstance<IEditController>(VST3::UID(controller_cid));
            if (!controller_) {
                error = "VST3 init[controller-create]: advertised controller could not be created";
                return false;
            }
            report_startup_phase(StartupErrorCode::ControllerInitialize);
            if (controller_->initialize(host_.get()) != kResultOk) {
                error = "VST3 init[controller-initialize]: initialize failed";
                return false;
            }
            controller_initialized_ = true;
        }
    }

    // Strict separated-component ordering: the host callback must exist before
    // controller/component connection. Some vendors tolerate a late handler;
    // iZotope-family processors are known to exercise this frontier strictly.
    if (controller_) {
        if (!component_handler) {
            error = "VST3 init[component-handler]: host handler unavailable";
            return false;
        }
        report_startup_phase(StartupErrorCode::ComponentHandler);
        if (controller_->setComponentHandler(component_handler) != kResultTrue) {
            error = "VST3 init[component-handler]: setComponentHandler failed";
            return false;
        }
    }

    if (controller_ && !controller_is_component_) {
        report_startup_phase(StartupErrorCode::ConnectionPoints);
        FUnknownPtr<IConnectionPoint> component_cp(component_);
        report_startup_phase(StartupErrorCode::ConnectionPoints);
        FUnknownPtr<IConnectionPoint> controller_cp(controller_);
        if (component_cp && controller_cp) {
            component_connection_ = owned(new ConnectionProxy(component_cp));
            controller_connection_ = owned(new ConnectionProxy(controller_cp));
            report_startup_phase(StartupErrorCode::ConnectComponentController);
            if (component_connection_->connect(controller_cp) != kResultTrue) {
                error = "VST3 init[connect-component-controller]: component connection failed";
                return false;
            }
            report_startup_phase(StartupErrorCode::ConnectControllerComponent);
            if (controller_connection_->connect(component_cp) != kResultTrue) {
                report_startup_phase(StartupErrorCode::ConnectControllerComponent);
                (void)component_connection_->disconnect();
                error = "VST3 init[connect-controller-component]: controller connection failed";
                return false;
            }
        } else if (component_cp || controller_cp) {
            error = "VST3 init[connection-points]: asymmetric separated connection support";
            return false;
        }
    }

    // VST3 split components must begin with the controller synchronized to the
    // component's current processor state. Steinberg's host contract places
    // this after handler/connection setup and before the host scans parameters.
    // The SDK examples intentionally treat the returned setComponentState code
    // as advisory; the compliance requirement here is that the synchronization
    // call happens whenever the component can provide an initial state.
    if (controller_ && !controller_is_component_) {
        MemoryStream initial_component_state;
        report_startup_phase(StartupErrorCode::InitialStateSync);
        if (component_->getState(&initial_component_state) == kResultTrue) {
            if (initial_component_state.seek(0, IBStream::kIBSeekSet, nullptr) != kResultTrue) {
                error = "VST3 init[initial-state-sync]: failed to rewind initial component state";
                return false;
            }
            report_startup_phase(StartupErrorCode::InitialStateSync);
            (void)controller_->setComponentState(&initial_component_state);
        }
    }

    report_startup_phase(StartupErrorCode::ProcessorInterface);
    processor_ = FUnknownPtr<IAudioProcessor>(component_).getInterface();
    if (!processor_) {
        error = "VST3 init[processor-interface]: component does not implement IAudioProcessor";
        return false;
    }

    report_startup_phase(StartupErrorCode::SampleFormat);
    if (processor_->canProcessSampleSize(kSample32) != kResultTrue) {
        error = "VST3 init[sample-format]: public preview requires float32-capable VST3 processing";
        return false;
    }

    if (!enumerate_parameters(error)) {
        error = "VST3 init[parameter-catalog]: " + error;
        return false;
    }
    if (!configure_buses(channels, error)) {
        error = "VST3 init[bus-negotiation]: " + error;
        return false;
    }

    sample_rate_ = sample_rate;
    channels_ = channels;
    process_setup_.processMode = kRealtime;
    process_setup_.symbolicSampleSize = kSample32;
    process_setup_.maxSamplesPerBlock = static_cast<int32>(kMaxFrames);
    process_setup_.sampleRate = static_cast<SampleRate>(sample_rate);

    report_startup_phase(StartupErrorCode::SetupProcessing);
    if (processor_->setupProcessing(process_setup_) != kResultOk) {
        error = "VST3 init[setup-processing]: setupProcessing failed";
        return false;
    }
    if (!activate_configured_buses(error))
        return false;

    // VST3 3.7+ asks the host to query this processor extension once during
    // setup, before activation. Older plug-ins may not expose it and remain
    // compatible with the always-valid sampleRate/projectTimeSamples fields.
    std::uint32_t requested_context = 0;
    report_startup_phase(StartupErrorCode::ProcessContext);
    FUnknownPtr<IProcessContextRequirements> context_requirements(component_);
    if (context_requirements) {
        report_startup_phase(StartupErrorCode::ProcessContext);
        requested_context = context_requirements->getProcessContextRequirements();
    }
    process_context_policy_ = plan_process_context(requested_context);

    report_startup_phase(StartupErrorCode::ProcessData);
    if (!process_data_.prepare(*component_, 0, kSample32)) {
        error = "VST3 init[process-data]: failed to prepare ProcessData bus containers";
        return false;
    }

    const auto initial_context = make_process_context_frame(
        static_cast<double>(sample_rate), sample_position_, process_context_policy_);
    process_context_.sampleRate = initial_context.sample_rate;
    process_context_.projectTimeSamples = initial_context.project_time_samples;
    process_context_.continousTimeSamples = initial_context.continuous_time_samples;
    process_context_.state = initial_context.state;
    process_data_.processContext = &process_context_;

    report_startup_phase(StartupErrorCode::SetActive);
    if (component_->setActive(true) != kResultTrue) {
        error = "VST3 init[set-active]: setActive(true) failed";
        return false;
    }

    // Steinberg's processing lifecycle queries initial latency after activation
    // but before entering the Processing state. Some plug-ins tolerate the
    // inverse order; strict hosts must not depend on that tolerance.
    report_startup_phase(StartupErrorCode::LatencyQuery);
    latency_samples_ = processor_->getLatencySamples();

    report_startup_phase(StartupErrorCode::SetProcessing);
    const tresult set_processing_result = processor_->setProcessing(true);
    if (set_processing_result != kResultTrue) {
        if (startup_phase_sink_)
            startup_phase_sink_->publish_vendor_result(
                static_cast<std::int32_t>(set_processing_result));
        component_->setActive(false);
        error = "VST3 init[set-processing]: setProcessing(true) failed (tresult=" +
                format_vst3_tresult(static_cast<std::int32_t>(set_processing_result)) + ')';
        return false;
    }

    startup_phase_sink_ = nullptr;
    return true;
}

bool Vst3Engine::capture_state(PluginStateSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    error.clear();
    if (!component_) {
        error = "VST3 component unavailable while capturing state";
        return false;
    }

    MemoryStream component_stream;
    if (component_->getState(&component_stream) != kResultTrue) {
        error = "VST3 component getState failed";
        return false;
    }
    if (!copy_stream(component_stream, snapshot.component, "VST3 component", error))
        return false;

    if (controller_) {
        MemoryStream controller_stream;
        if (controller_->getState(&controller_stream) == kResultTrue &&
            !copy_stream(controller_stream, snapshot.controller, "VST3 controller", error))
            return false;
    }

    if (snapshot.total_bytes() > kMaxStateBytes) {
        error = "Combined VST3 component/controller state exceeds the supported snapshot size";
        snapshot = {};
        return false;
    }
    return true;
}

bool Vst3Engine::restore_state(const PluginStateSnapshot& snapshot, std::string& error)
{
    error.clear();
    if (!component_ || !processor_) {
        error = "VST3 component unavailable while restoring state";
        return false;
    }
    if (snapshot.total_bytes() > kMaxStateBytes) {
        error = "VST3 state exceeds the supported snapshot size";
        return false;
    }

    // State restoration is a control-plane transaction. Suspend the processor,
    // restore in the ordering required by the VST3 specification, and always
    // try to resume so a corrupt vendor blob cannot leave the helper inactive.
    (void)processor_->setProcessing(false);
    (void)component_->setActive(false);
    input_parameter_changes_.clearQueue();
    output_parameter_changes_.clearQueue();
    parameter_changes_pending_ = false;

    bool restored = true;
    auto component_stream = read_stream(snapshot.component);
    if (!accepts_state_restore_result(
            StateRestoreCall::ComponentState,
            classify_plugin_call_result(component_->setState(&component_stream)))) {
        error = "VST3 component setState failed";
        restored = false;
    }

    if (restored && controller_) {
        auto controller_component_stream = read_stream(snapshot.component);
        if (!accepts_state_restore_result(
                StateRestoreCall::ControllerComponentState,
                classify_plugin_call_result(
                    controller_->setComponentState(&controller_component_stream)))) {
            error = "VST3 controller setComponentState returned an unexpected failure";
            restored = false;
        }
    }

    if (restored && controller_ && !snapshot.controller.empty()) {
        auto controller_stream = read_stream(snapshot.controller);
        if (!accepts_state_restore_result(
                StateRestoreCall::ControllerPrivateState,
                classify_plugin_call_result(controller_->setState(&controller_stream)))) {
            error = "VST3 controller setState failed";
            restored = false;
        }
    }

    const bool activated = component_->setActive(true) == kResultTrue;
    const bool processing = activated && processor_->setProcessing(true) == kResultTrue;
    if (!activated || !processing) {
        error = !activated ? "VST3 setActive(true) failed after state restore"
                           : "VST3 setProcessing(true) failed after state restore";
        return false;
    }

    if (!restored)
        return false;

    latency_samples_ = processor_->getLatencySamples();
    refresh_parameter_values();
    return true;
}

bool Vst3Engine::refresh_latency_after_restart(std::string& error)
{
    error.clear();
    if (!component_ || !processor_) {
        error = "VST3 component unavailable while refreshing latency";
        return false;
    }

    const auto result = run_latency_restart_transaction(*this);
    if (!result.committed) {
        error = std::string("VST3 latency restart failed at ") +
                latency_restart_step_name(result.failed_step);
        return false;
    }

    latency_samples_ = result.latency_samples;
    return true;
}

bool Vst3Engine::reconfigure_io_after_restart(IoLayout& layout,
                                                  std::uint32_t& latency_samples,
                                                  std::string& error)
{
    error.clear();
    layout = {};
    latency_samples = 0;
    if (!component_ || !processor_) {
        error = "VST3 component unavailable while reconfiguring I/O";
        return false;
    }

    const auto result = run_io_restart_lifecycle(*this);
    if (!result.committed) {
        error = std::string("VST3 I/O restart failed at ") + io_restart_step_name(result.failed_step);
        return false;
    }

    layout = result.layout;
    latency_samples = result.latency_samples;
    return true;
}

bool Vst3Engine::collect_io_layout_candidate(IoLayout& layout) noexcept
{
    layout = {};
    if (!component_ || !processor_)
        return false;

    const int32 input_count = component_->getBusCount(kAudio, kInput);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    if (input_count <= 0 || output_count <= 0 ||
        input_count > static_cast<int32>(kMaxDynamicAudioBuses) ||
        output_count > static_cast<int32>(kMaxDynamicAudioBuses))
        return false;

    io_candidate_inputs_.fill(SpeakerArr::kEmpty);
    io_candidate_outputs_.fill(SpeakerArr::kEmpty);
    io_candidate_input_count_ = input_count;
    io_candidate_output_count_ = output_count;
    io_candidate_main_input_bus_ = -1;
    io_candidate_main_output_bus_ = -1;
    std::uint32_t main_input_count = 0;
    std::uint32_t main_output_count = 0;

    for (int32 i = 0; i < input_count; ++i) {
        BusInfo info{};
        if (component_->getBusInfo(kAudio, kInput, i, info) != kResultTrue)
            return false;
        auto& arrangement = io_candidate_inputs_[static_cast<std::size_t>(i)];
        if (processor_->getBusArrangement(kInput, i, arrangement) != kResultTrue) {
            if (info.channelCount > 2)
                return false;
            arrangement = fallback_arrangement_for_channels(info.channelCount);
        }
        if (info.busType == BusTypes::kMain) {
            ++main_input_count;
            if (main_input_count == 1u)
                io_candidate_main_input_bus_ = i;
        }
    }

    for (int32 i = 0; i < output_count; ++i) {
        BusInfo info{};
        if (component_->getBusInfo(kAudio, kOutput, i, info) != kResultTrue)
            return false;
        auto& arrangement = io_candidate_outputs_[static_cast<std::size_t>(i)];
        if (processor_->getBusArrangement(kOutput, i, arrangement) != kResultTrue) {
            if (info.channelCount > 2)
                return false;
            arrangement = fallback_arrangement_for_channels(info.channelCount);
        }
        if (info.busType == BusTypes::kMain) {
            ++main_output_count;
            if (main_output_count == 1u)
                io_candidate_main_output_bus_ = i;
        }
    }

    if (!has_unambiguous_main_io(main_input_count, main_output_count) ||
        io_candidate_main_input_bus_ < 0 || io_candidate_main_output_bus_ < 0)
        return false;

    layout.main_input_bus = io_candidate_main_input_bus_;
    layout.main_output_bus = io_candidate_main_output_bus_;
    layout.input_channels = supported_arrangement_channels(
        io_candidate_inputs_[static_cast<std::size_t>(io_candidate_main_input_bus_)]);
    layout.output_channels = supported_arrangement_channels(
        io_candidate_outputs_[static_cast<std::size_t>(io_candidate_main_output_bus_)]);
    return is_supported_io_layout(layout);
}

bool Vst3Engine::io_stop_processing() noexcept
{
    return processor_ && processor_->setProcessing(false) == kResultTrue;
}

bool Vst3Engine::io_deactivate() noexcept
{
    return component_ && component_->setActive(false) == kResultTrue;
}

bool Vst3Engine::io_inspect_requested_layout(IoLayout& layout) noexcept
{
    return collect_io_layout_candidate(layout);
}

IoArrangementResult Vst3Engine::io_confirm_requested_layout(const IoLayout&) noexcept
{
    if (!processor_ || io_candidate_input_count_ <= 0 || io_candidate_output_count_ <= 0)
        return IoArrangementResult::FatalFailure;

    const tresult result = processor_->setBusArrangements(
        io_candidate_inputs_.data(), io_candidate_input_count_,
        io_candidate_outputs_.data(), io_candidate_output_count_);
    if (result == kResultTrue)
        return IoArrangementResult::Accepted;
    if (result == kResultFalse)
        return IoArrangementResult::AdvisoryRejected;
    return IoArrangementResult::FatalFailure;
}

bool Vst3Engine::io_inspect_confirmed_layout(IoLayout& layout) noexcept
{
    return collect_io_layout_candidate(layout);
}

bool Vst3Engine::io_rebuild_processing(const IoLayout& layout) noexcept
{
    if (!component_ || !is_supported_io_layout(layout))
        return false;

    const int32 input_count = component_->getBusCount(kAudio, kInput);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    if (input_count <= layout.main_input_bus || output_count <= layout.main_output_bus)
        return false;

    for (int32 i = 0; i < input_count; ++i)
        (void)component_->activateBus(kAudio, kInput, i, false);
    for (int32 i = 0; i < output_count; ++i)
        (void)component_->activateBus(kAudio, kOutput, i, false);
    (void)component_->activateBus(kAudio, kInput, layout.main_input_bus, true);
    (void)component_->activateBus(kAudio, kOutput, layout.main_output_bus, true);

    process_data_.unprepare();
    try {
        if (!process_data_.prepare(*component_, 0, kSample32))
            return false;
    } catch (...) {
        return false;
    }
    process_data_.processContext = &process_context_;
    return true;
}

bool Vst3Engine::io_activate() noexcept
{
    return component_ && component_->setActive(true) == kResultTrue;
}

bool Vst3Engine::io_query_latency(std::uint32_t& latency_samples) noexcept
{
    if (!processor_)
        return false;
    latency_samples = processor_->getLatencySamples();
    return true;
}

bool Vst3Engine::io_start_processing() noexcept
{
    return processor_ && processor_->setProcessing(true) == kResultTrue;
}

void Vst3Engine::io_commit_layout(const IoLayout& layout,
                                  std::uint32_t latency_samples) noexcept
{
    main_input_bus_ = layout.main_input_bus;
    main_output_bus_ = layout.main_output_bus;
    plugin_input_channels_ = layout.input_channels;
    plugin_output_channels_ = layout.output_channels;
    latency_samples_ = latency_samples;
}

bool Vst3Engine::set_processing(bool enabled) noexcept
{
    return processor_ && processor_->setProcessing(enabled) == kResultTrue;
}

bool Vst3Engine::set_active(bool enabled) noexcept
{
    return component_ && component_->setActive(enabled) == kResultTrue;
}

std::uint32_t Vst3Engine::get_latency_samples() noexcept
{
    return processor_ ? processor_->getLatencySamples() : 0;
}

void Vst3Engine::set_component_handler(IComponentHandler* handler) noexcept
{
    if (controller_)
        (void)controller_->setComponentHandler(handler);
}

void Vst3Engine::close() noexcept
{
    if (processor_)
        (void)processor_->setProcessing(false);
    if (component_)
        (void)component_->setActive(false);
    process_data_.unprepare();

    if (controller_)
        (void)controller_->setComponentHandler(nullptr);
    if (component_connection_)
        (void)component_connection_->disconnect();
    if (controller_connection_)
        (void)controller_connection_->disconnect();
    component_connection_ = nullptr;
    controller_connection_ = nullptr;

    processor_ = nullptr;
    // Match Steinberg PlugProvider teardown ordering after disconnect: the
    // component terminates first, followed by a separately initialized controller.
    if (component_ && component_initialized_)
        (void)component_->terminate();
    if (controller_ && controller_initialized_ && !controller_is_component_)
        (void)controller_->terminate();

    input_parameter_changes_.clearQueue();
    output_parameter_changes_.clearQueue();
    parameter_changes_pending_ = false;
    parameter_update_count_ = 0;
    parameters_.clear();
    controller_ = nullptr;
    component_ = nullptr;
    module_.reset();
    PluginContextFactory::instance().setPluginContext(nullptr);
    host_ = nullptr;
    main_input_bus_ = -1;
    main_output_bus_ = -1;
    plugin_input_channels_ = 0;
    plugin_output_channels_ = 0;
    io_candidate_inputs_.fill(SpeakerArr::kEmpty);
    io_candidate_outputs_.fill(SpeakerArr::kEmpty);
    io_candidate_input_count_ = 0;
    io_candidate_output_count_ = 0;
    io_candidate_main_input_bus_ = -1;
    io_candidate_main_output_bus_ = -1;
    plugin_name_.clear();
    loaded_class_id_.clear();
    process_context_policy_ = {};
    process_context_ = {};
    latency_samples_ = 0;
    sample_position_ = 0;
    component_initialized_ = false;
    controller_initialized_ = false;
    controller_is_component_ = false;
    startup_phase_sink_ = nullptr;
}

EngineParameter* Vst3Engine::find_parameter(std::uint32_t id) noexcept
{
    auto it = std::find_if(parameters_.begin(), parameters_.end(), [id](const EngineParameter& parameter) {
        return parameter.id == id;
    });
    return it == parameters_.end() ? nullptr : &*it;
}

bool Vst3Engine::queue_parameter_impl(std::uint32_t id, double normalized, bool update_controller) noexcept
{
    if (!processor_)
        return false;

    EngineParameter* parameter = find_parameter(id);
    if (!parameter)
        return false;

    // Hidden/read-only flags restrict host-authored fallback controls. A native
    // vendor editor is the plug-in's own controller UI; its performEdit() still
    // has to be transferred by the host to the processor, even for parameters
    // that are not meant to be exposed as generic host controls.
    if (update_controller && (parameter->flags & (ParameterReadOnly | ParameterHidden)) != 0)
        return false;

    normalized = normalize_parameter_value(normalized, parameter->step_count);
    if (update_controller) {
        if (!controller_ || controller_->setParamNormalized(static_cast<ParamID>(id), normalized) != kResultTrue)
            return false;
    }

    int32 queue_index = 0;
    IParamValueQueue* queue = input_parameter_changes_.addParameterData(static_cast<ParamID>(id), queue_index);
    if (!queue)
        return false;

    int32 point_index = 0;
    if (queue->addPoint(0, normalized, point_index) != kResultTrue)
        return false;

    parameter->current_normalized = normalized;
    record_parameter_update(id, normalized);
    parameter_changes_pending_ = true;
    return true;
}

bool Vst3Engine::queue_parameter(std::uint32_t id, double normalized) noexcept
{
    return queue_parameter_impl(id, normalized, true);
}

bool Vst3Engine::queue_parameter_from_controller(std::uint32_t id, double normalized) noexcept
{
    return queue_processor_parameter(id, normalized);
}

bool Vst3Engine::set_controller_parameter(std::uint32_t id, double normalized) noexcept
{
    if (!controller_)
        return false;
    EngineParameter* parameter = find_parameter(id);
    if (!parameter)
        return false;
    normalized = normalize_parameter_value(normalized, parameter->step_count);
    return controller_->setParamNormalized(static_cast<ParamID>(id), normalized) == kResultTrue;
}

bool Vst3Engine::queue_processor_parameter(std::uint32_t id, double normalized) noexcept
{
    return queue_parameter_impl(id, normalized, false);
}

void Vst3Engine::refresh_parameter_values() noexcept
{
    if (!controller_)
        return;
    for (auto& parameter : parameters_) {
        const double value = normalize_parameter_value(
            controller_->getParamNormalized(static_cast<ParamID>(parameter.id)), parameter.step_count);
        if (value == parameter.current_normalized)
            continue;
        parameter.current_normalized = value;
        record_parameter_update(parameter.id, value);
    }
}

bool Vst3Engine::refresh_parameter_metadata(std::string& error)
{
    error.clear();
    return enumerate_parameters(error);
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

void Vst3Engine::record_parameter_update(std::uint32_t id, double normalized) noexcept
{
    for (std::size_t i = 0; i < parameter_update_count_; ++i) {
        if (parameter_updates_[i].id == id) {
            parameter_updates_[i].normalized = normalized;
            return;
        }
    }
    if (parameter_update_count_ < parameter_updates_.size())
        parameter_updates_[parameter_update_count_++] = {id, normalized};
}

void Vst3Engine::capture_output_parameter_changes() noexcept
{
    const int32 count = output_parameter_changes_.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = output_parameter_changes_.getParameterData(i);
        if (!queue || queue->getPointCount() <= 0)
            continue;

        int32 sample_offset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) != kResultTrue)
            continue;

        const auto id = static_cast<std::uint32_t>(queue->getParameterId());
        EngineParameter* parameter = find_parameter(id);
        if (!parameter)
            continue;

        value = normalize_parameter_value(value, parameter->step_count);
        parameter->current_normalized = value;
        // Processor feedback is intentionally controller-free. S2.2 transfers
        // this update over DSP->control SPSC and the control thread applies it
        // to IEditController. This keeps vendor UI calls out of process().
        record_parameter_update(id, value);
    }
    output_parameter_changes_.clearQueue();
}

std::size_t Vst3Engine::take_parameter_updates(EngineParameterUpdate* destination, std::size_t capacity) noexcept
{
    if (!destination || capacity == 0)
        return 0;
    const std::size_t count = std::min(capacity, parameter_update_count_);
    std::copy_n(parameter_updates_.begin(), count, destination);
    if (count < parameter_update_count_)
        std::move(parameter_updates_.begin() + static_cast<std::ptrdiff_t>(count),
                  parameter_updates_.begin() + static_cast<std::ptrdiff_t>(parameter_update_count_),
                  parameter_updates_.begin());
    parameter_update_count_ -= count;
    return count;
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
    flush.outputParameterChanges = &output_parameter_changes_;
    flush.processContext = &process_context_;
    apply_pending_parameter_changes(flush);

    const tresult result = processor_->process(flush);
    finish_parameter_changes();
    capture_output_parameter_changes();
    return result == kResultOk;
}

bool Vst3Engine::process(AudioSlot& slot) noexcept
{
    float* input[kMaxChannels] = {slot.input[0], slot.input[1]};
    float* output[kMaxChannels] = {slot.output[0], slot.output[1]};
    const ProcessBlockView block{
        input,
        output,
        slot.channels,
        slot.frames,
        slot.sequence,
    };
    return process(block);
}

bool Vst3Engine::process(const ProcessBlockView& block) noexcept
{
    if (!processor_ || !block.input || !block.output ||
        block.frames == 0 || block.frames > kMaxFrames ||
        block.channels != channels_ ||
        (plugin_input_channels_ != 1 && plugin_input_channels_ != 2) ||
        (plugin_output_channels_ != 1 && plugin_output_channels_ != 2))
        return false;

    for (std::uint32_t ch = 0; ch < block.channels; ++ch) {
        if (!block.input[ch] || !block.output[ch])
            return false;
    }

    Sample32* in[kMaxChannels]{};
    Sample32* out[kMaxChannels]{};

    if (plugin_input_channels_ == channels_) {
        for (std::uint32_t ch = 0; ch < channels_; ++ch)
            in[ch] = block.input[ch];
    } else if (channels_ == 2 && plugin_input_channels_ == 1) {
        average_stereo_to_mono(
            block.input[0], block.input[1], input_adapter_[0].data(), block.frames);
        in[0] = input_adapter_[0].data();
    } else if (channels_ == 1 && plugin_input_channels_ == 2) {
        duplicate_mono_to_stereo(
            block.input[0], input_adapter_[0].data(), input_adapter_[1].data(), block.frames);
        in[0] = input_adapter_[0].data();
        in[1] = input_adapter_[1].data();
    } else {
        return false;
    }

    const bool output_direct = plugin_output_channels_ == channels_;
    if (output_direct) {
        for (std::uint32_t ch = 0; ch < channels_; ++ch)
            out[ch] = block.output[ch];
    } else {
        for (std::uint32_t ch = 0; ch < plugin_output_channels_; ++ch)
            out[ch] = output_adapter_[ch].data();
    }

    if (!process_data_.setChannelBuffers(
            kInput, main_input_bus_, in, static_cast<int32>(plugin_input_channels_)) ||
        !process_data_.setChannelBuffers(
            kOutput, main_output_bus_, out, static_cast<int32>(plugin_output_channels_)))
        return false;

    process_data_.numSamples = static_cast<int32>(block.frames);
    process_data_.inputEvents = nullptr;
    process_data_.outputEvents = nullptr;
    process_data_.outputParameterChanges = &output_parameter_changes_;
    apply_pending_parameter_changes(process_data_);

    const auto context_frame = make_process_context_frame(
        process_setup_.sampleRate, sample_position_, process_context_policy_);
    process_context_.sampleRate = context_frame.sample_rate;
    process_context_.projectTimeSamples = context_frame.project_time_samples;
    process_context_.continousTimeSamples = context_frame.continuous_time_samples;
    process_context_.state = context_frame.state;

    const tresult result = processor_->process(process_data_);
    finish_parameter_changes();
    capture_output_parameter_changes();
    if (result != kResultOk)
        return false;
    sample_position_ += block.frames;

    if (!output_direct) {
        if (channels_ == 2 && plugin_output_channels_ == 1) {
            duplicate_mono_to_stereo(
                output_adapter_[0].data(), block.output[0], block.output[1], block.frames);
        } else if (channels_ == 1 && plugin_output_channels_ == 2) {
            average_stereo_to_mono(
                output_adapter_[0].data(), output_adapter_[1].data(),
                block.output[0], block.frames);
        } else {
            return false;
        }
    }

    return true;
}

} // namespace safevst3

#endif