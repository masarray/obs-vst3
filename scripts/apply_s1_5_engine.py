from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(p): return (ROOT/p).read_text(encoding='utf-8')
def write(p,s): (ROOT/p).write_text(s,encoding='utf-8',newline='\n')
def repl(p,a,b):
    s=read(p)
    if s.count(a)!=1: raise RuntimeError(f'{p}: expected 1 match, got {s.count(a)}')
    write(p,s.replace(a,b,1))

# CMake portable seam + test
repl('CMakeLists.txt', '''add_library(safevst3_parameter_refresh_transaction STATIC
    src/common/parameter_refresh_transaction.cpp
)
target_include_directories(safevst3_parameter_refresh_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
''', '''add_library(safevst3_parameter_refresh_transaction STATIC
    src/common/parameter_refresh_transaction.cpp
)
target_include_directories(safevst3_parameter_refresh_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(safevst3_io_restart_transaction STATIC
    src/common/io_restart_transaction.cpp
)
target_include_directories(safevst3_io_restart_transaction PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
''')
repl('CMakeLists.txt', '''        safevst3_state_restore_policy safevst3_latency_restart_transaction
        safevst3_parameter_refresh_transaction Avrt)''', '''        safevst3_state_restore_policy safevst3_latency_restart_transaction
        safevst3_parameter_refresh_transaction safevst3_io_restart_transaction Avrt)''')
repl('CMakeLists.txt', '''    add_executable(parameter-refresh-transaction-test tests/parameter_refresh_transaction.cpp)
    target_link_libraries(parameter-refresh-transaction-test PRIVATE safevst3_parameter_refresh_transaction)
    add_test(NAME parameter-refresh-transaction COMMAND parameter-refresh-transaction-test)
''', '''    add_executable(parameter-refresh-transaction-test tests/parameter_refresh_transaction.cpp)
    target_link_libraries(parameter-refresh-transaction-test PRIVATE safevst3_parameter_refresh_transaction)
    add_test(NAME parameter-refresh-transaction COMMAND parameter-refresh-transaction-test)

    add_executable(io-restart-transaction-test tests/io_restart_transaction.cpp)
    target_link_libraries(io-restart-transaction-test PRIVATE safevst3_io_restart_transaction)
    add_test(NAME io-restart-transaction COMMAND io-restart-transaction-test)
''')
repl('CMakeLists.txt', '''        latency-restart-transaction-test parameter-refresh-transaction-test
        parameter-control-test state-snapshot-test recovery-policy-test spsc-ring-test)''', '''        latency-restart-transaction-test parameter-refresh-transaction-test io-restart-transaction-test
        parameter-control-test state-snapshot-test recovery-policy-test spsc-ring-test)''')

# Engine header
p='src/host/vst3_engine.hpp'
s=read(p)
s=s.replace('#include "common/latency_restart_transaction.hpp"', '#include "common/latency_restart_transaction.hpp"\n#include "common/io_restart_transaction.hpp"')
s=s.replace('class Vst3Engine final : public LatencyRestartTarget {', 'class Vst3Engine final : public LatencyRestartTarget, private IoRestartTarget {')
s=s.replace('    bool refresh_latency_after_restart(std::string& error);', '    bool refresh_latency_after_restart(std::string& error);\n    bool refresh_io_after_restart(std::string& error);')
s=s.replace('    std::uint32_t get_latency_samples() noexcept override;\n', '''    std::uint32_t get_latency_samples() noexcept override;

    bool inspect_requested_io(IoLayout& layout) noexcept override;
    bool confirm_requested_io(const IoLayout& requested) noexcept override;
    bool inspect_confirmed_io(IoLayout& layout) noexcept override;
    bool rebuild_process_data(const IoLayout& layout) noexcept override;
    std::uint32_t query_latency() noexcept override;
    bool commit_io(const IoLayout& layout, std::uint32_t latency_samples) noexcept override;
    void request_recovery() noexcept override;
''')
s=s.replace('    bool configure_buses(std::uint32_t channels, std::string& error);', '    bool configure_buses(std::uint32_t channels, std::string& error);\n    bool inspect_io_topology(IoLayout& layout, bool capture_arrangements) noexcept;')
s=s.replace('    Steinberg::int32 main_output_bus_ = -1;\n', '''    Steinberg::int32 main_output_bus_ = -1;
    Steinberg::int32 candidate_main_input_bus_ = -1;
    Steinberg::int32 candidate_main_output_bus_ = -1;
    std::vector<Steinberg::Vst::SpeakerArrangement> candidate_input_arrangements_;
    std::vector<Steinberg::Vst::SpeakerArrangement> candidate_output_arrangements_;
''')
s=s.replace('    std::uint32_t channels_ = 0;\n', '''    std::uint32_t channels_ = 0;
    std::uint32_t plugin_input_channels_ = 0;
    std::uint32_t plugin_output_channels_ = 0;
    std::array<std::array<float, kMaxFrames>, kMaxChannels> input_adapter_{};
    std::array<std::array<float, kMaxFrames>, kMaxChannels> output_adapter_{};
    bool io_recovery_requested_ = false;
''')
write(p,s)

# Engine cpp includes
repl('src/host/vst3_engine.cpp', '#include "common/parameter_utils.hpp"\n', '#include "common/parameter_utils.hpp"\n#include "common/channel_adapter.hpp"\n')

# initialize dynamic channel counts after initial bus config
repl('src/host/vst3_engine.cpp', '''    if (!configure_buses(channels, error))
        return false;

    sample_rate_ = sample_rate;
    channels_ = channels;
''', '''    if (!configure_buses(channels, error))
        return false;

    sample_rate_ = sample_rate;
    channels_ = channels;
    plugin_input_channels_ = channels;
    plugin_output_channels_ = channels;
''')

# insert S1.5 engine methods before latency refresh
marker='''bool Vst3Engine::refresh_latency_after_restart(std::string& error)
{'''
insert=r'''bool Vst3Engine::inspect_io_topology(IoLayout& layout, bool capture_arrangements) noexcept
{
    layout = {};
    if (!component_ || !processor_)
        return false;

    const int32 input_count = component_->getBusCount(kAudio, kInput);
    const int32 output_count = component_->getBusCount(kAudio, kOutput);
    if (input_count <= 0 || output_count <= 0)
        return false;

    if (capture_arrangements) {
        candidate_input_arrangements_.assign(static_cast<std::size_t>(input_count), SpeakerArr::kEmpty);
        candidate_output_arrangements_.assign(static_cast<std::size_t>(output_count), SpeakerArr::kEmpty);
    } else if (candidate_input_arrangements_.size() != static_cast<std::size_t>(input_count) ||
               candidate_output_arrangements_.size() != static_cast<std::size_t>(output_count)) {
        return false;
    }

    candidate_main_input_bus_ = -1;
    candidate_main_output_bus_ = -1;
    unsigned main_inputs = 0;
    unsigned main_outputs = 0;

    for (int32 i = 0; i < input_count; ++i) {
        BusInfo info{};
        if (component_->getBusInfo(kAudio, kInput, i, info) != kResultTrue)
            return false;
        SpeakerArrangement arrangement = SpeakerArr::kEmpty;
        if (processor_->getBusArrangement(kInput, i, arrangement) != kResultTrue)
            return false;
        if (capture_arrangements)
            candidate_input_arrangements_[static_cast<std::size_t>(i)] = arrangement;
        if (info.busType == BusTypes::kMain) {
            ++main_inputs;
            candidate_main_input_bus_ = i;
            layout.input_channels = static_cast<std::uint32_t>(SpeakerArr::getChannelCount(arrangement));
        } else {
            (void)component_->activateBus(kAudio, kInput, i, false);
        }
    }

    for (int32 i = 0; i < output_count; ++i) {
        BusInfo info{};
        if (component_->getBusInfo(kAudio, kOutput, i, info) != kResultTrue)
            return false;
        SpeakerArrangement arrangement = SpeakerArr::kEmpty;
        if (processor_->getBusArrangement(kOutput, i, arrangement) != kResultTrue)
            return false;
        if (capture_arrangements)
            candidate_output_arrangements_[static_cast<std::size_t>(i)] = arrangement;
        if (info.busType == BusTypes::kMain) {
            ++main_outputs;
            candidate_main_output_bus_ = i;
            layout.output_channels = static_cast<std::uint32_t>(SpeakerArr::getChannelCount(arrangement));
        } else {
            (void)component_->activateBus(kAudio, kOutput, i, false);
        }
    }

    return main_inputs == 1 && main_outputs == 1 &&
           candidate_main_input_bus_ >= 0 && candidate_main_output_bus_ >= 0 &&
           layout.supported();
}

bool Vst3Engine::inspect_requested_io(IoLayout& layout) noexcept
{
    return inspect_io_topology(layout, true);
}

bool Vst3Engine::confirm_requested_io(const IoLayout&) noexcept
{
    if (!processor_ || candidate_input_arrangements_.empty() || candidate_output_arrangements_.empty())
        return false;
    return processor_->setBusArrangements(
               candidate_input_arrangements_.data(), static_cast<int32>(candidate_input_arrangements_.size()),
               candidate_output_arrangements_.data(), static_cast<int32>(candidate_output_arrangements_.size())) == kResultTrue;
}

bool Vst3Engine::inspect_confirmed_io(IoLayout& layout) noexcept
{
    return inspect_io_topology(layout, false);
}

bool Vst3Engine::rebuild_process_data(const IoLayout&) noexcept
{
    if (!component_ || candidate_main_input_bus_ < 0 || candidate_main_output_bus_ < 0)
        return false;

    process_data_.unprepare();
    for (int32 i = 0; i < component_->getBusCount(kAudio, kInput); ++i)
        (void)component_->activateBus(kAudio, kInput, i, i == candidate_main_input_bus_);
    for (int32 i = 0; i < component_->getBusCount(kAudio, kOutput); ++i)
        (void)component_->activateBus(kAudio, kOutput, i, i == candidate_main_output_bus_);

    if (!process_data_.prepare(*component_, 0, kSample32))
        return false;
    process_data_.processContext = &process_context_;
    return true;
}

std::uint32_t Vst3Engine::query_latency() noexcept
{
    return processor_ ? processor_->getLatencySamples() : 0;
}

bool Vst3Engine::commit_io(const IoLayout& layout, std::uint32_t latency_samples) noexcept
{
    if (!layout.supported() || candidate_main_input_bus_ < 0 || candidate_main_output_bus_ < 0)
        return false;
    main_input_bus_ = candidate_main_input_bus_;
    main_output_bus_ = candidate_main_output_bus_;
    plugin_input_channels_ = layout.input_channels;
    plugin_output_channels_ = layout.output_channels;
    latency_samples_ = latency_samples;
    return true;
}

void Vst3Engine::request_recovery() noexcept
{
    io_recovery_requested_ = true;
}

bool Vst3Engine::refresh_io_after_restart(std::string& error)
{
    error.clear();
    io_recovery_requested_ = false;
    const auto result = run_io_restart_transaction(*this);
    if (!result.committed) {
        error = "VST3 dynamic I/O restart failed at step " + std::to_string(static_cast<int>(result.failed_step));
        return false;
    }
    return true;
}

'''
s=read('src/host/vst3_engine.cpp')
if s.count(marker)!=1: raise RuntimeError('latency marker mismatch')
write('src/host/vst3_engine.cpp', s.replace(marker, insert+marker,1))

# reset dynamic I/O state in close
repl('src/host/vst3_engine.cpp', '''    main_input_bus_ = -1;
    main_output_bus_ = -1;
    plugin_name_.clear();''', '''    main_input_bus_ = -1;
    main_output_bus_ = -1;
    candidate_main_input_bus_ = -1;
    candidate_main_output_bus_ = -1;
    candidate_input_arrangements_.clear();
    candidate_output_arrangements_.clear();
    plugin_input_channels_ = 0;
    plugin_output_channels_ = 0;
    io_recovery_requested_ = false;
    plugin_name_.clear();''')

# replace realtime process mapping with fixed scratch adaptation
old=r'''    Sample32* in[kMaxChannels]{};
    Sample32* out[kMaxChannels]{};
    for (std::uint32_t ch = 0; ch < channels_; ++ch) {
        in[ch] = slot.input[ch];
        out[ch] = slot.output[ch];
    }

    if (!process_data_.setChannelBuffers(kInput, main_input_bus_, in, static_cast<int32>(channels_)) ||
        !process_data_.setChannelBuffers(kOutput, main_output_bus_, out, static_cast<int32>(channels_)))
        return false;
'''
new=r'''    Sample32* in[kMaxChannels]{};
    Sample32* out[kMaxChannels]{};
    const float* transport_in1 = channels_ == 2 ? slot.input[1] : nullptr;
    float* transport_out1 = channels_ == 2 ? slot.output[1] : nullptr;
    if (!prepare_input_channels(slot.input[0], transport_in1, channels_, plugin_input_channels_,
                                slot.frames, input_adapter_[0].data(), input_adapter_[1].data(), in) ||
        !prepare_output_channels(slot.output[0], transport_out1, channels_, plugin_output_channels_,
                                 output_adapter_[0].data(), output_adapter_[1].data(), out))
        return false;

    if (!process_data_.setChannelBuffers(kInput, main_input_bus_, in, static_cast<int32>(plugin_input_channels_)) ||
        !process_data_.setChannelBuffers(kOutput, main_output_bus_, out, static_cast<int32>(plugin_output_channels_)))
        return false;
'''
repl('src/host/vst3_engine.cpp',old,new)
repl('src/host/vst3_engine.cpp', '''    const tresult result = processor_->process(process_data_);
    finish_parameter_changes();
    capture_output_parameter_changes();
    return result == kResultOk;
''', '''    const tresult result = processor_->process(process_data_);
    finish_parameter_changes();
    capture_output_parameter_changes();
    if (result != kResultOk)
        return false;
    return finalize_output_channels(slot.output[0], transport_out1, channels_, plugin_output_channels_,
                                    slot.frames, out[0], out[1]);
''')

# helper integration in main.cpp
repl('src/host/main.cpp', 'constexpr long kParameterRefreshFailedError = 18;\n', 'constexpr long kParameterRefreshFailedError = 18;\nconstexpr long kIoRestartFailedError = 19;\n')

marker='''class HelperLatencyRestartTarget final : public safevst3::LatencyRestartCoordinatorTarget {'''
insert=r'''class HelperIoRestartTarget {
public:
    HelperIoRestartTarget(DspWorker& dsp,
                          safevst3::Vst3Engine& engine,
                          safevst3::SharedAudioRegion* region,
                          ParameterQueue& dsp_to_control,
                          NativeOverrideBuffer& native_overrides,
                          std::atomic<bool>& feedback_resync_required)
        : dsp_(dsp), engine_(engine), region_(region), dsp_to_control_(dsp_to_control),
          native_overrides_(native_overrides), feedback_resync_required_(feedback_resync_required) {}

    bool run() noexcept
    {
        if (!dsp_.pause(2000))
            return fail();
        if (!reconcile_parameter_refresh_feedback_checked(
                region_, engine_, dsp_to_control_, feedback_resync_required_, native_overrides_) ||
            !catch_up_pending_host_parameters_after_pause(region_, engine_))
            return fail();

        std::string error;
        if (!engine_.refresh_io_after_restart(error)) {
            if (!error.empty()) std::cerr << error << '\n';
            return fail();
        }
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&region_->latency_samples),
                            static_cast<LONG>(engine_.latency_samples()));
        if (!dsp_.resume(2000))
            return fail();
        return true;
    }

private:
    bool fail() noexcept
    {
        if (region_ && InterlockedCompareExchange(&region_->shutdown_requested, 0, 0) == 0)
            request_consistency_recovery(region_, kIoRestartFailedError);
        return false;
    }
    DspWorker& dsp_;
    safevst3::Vst3Engine& engine_;
    safevst3::SharedAudioRegion* region_ = nullptr;
    ParameterQueue& dsp_to_control_;
    NativeOverrideBuffer& native_overrides_;
    std::atomic<bool>& feedback_resync_required_;
};

'''
s=read('src/host/main.cpp')
if s.count(marker)!=1: raise RuntimeError('main helper marker mismatch')
write('src/host/main.cpp',s.replace(marker,insert+marker,1))

old=r'''        if (restart_plan.refresh_parameter_values || restart_plan.refresh_parameter_metadata) {
            HelperParameterRefreshTarget parameter_target(
                dsp, engine, region, dsp_to_control, native_overrides,
                feedback_resync_required, restart_plan.refresh_parameter_metadata);
            const ParameterRefreshRequest refresh_request{
                restart_plan.refresh_parameter_values,
                restart_plan.refresh_parameter_metadata};
            if (!coordinate_parameter_refresh(parameter_target, refresh_request).completed)
                break;
        }
        if (restart_plan.refresh_latency) {
            HelperLatencyRestartTarget latency_target(dsp, engine, region);
            if (!coordinate_latency_restart(latency_target).completed)
                break;
        }
        // actions remain deliberately unsupported until their individual S1
        // tracer bullets implement them at a quiesced lifecycle frontier.
        if (restart_plan.reload_component || restart_plan.reconfigure_io ||
            restart_plan.unknown_flags != 0)
            InterlockedExchange(&region->last_error, 3);
'''
new=r'''        if (restart_plan.refresh_parameter_values || restart_plan.refresh_parameter_metadata) {
            HelperParameterRefreshTarget parameter_target(
                dsp, engine, region, dsp_to_control, native_overrides,
                feedback_resync_required, restart_plan.refresh_parameter_metadata);
            const ParameterRefreshRequest refresh_request{
                restart_plan.refresh_parameter_values,
                restart_plan.refresh_parameter_metadata};
            if (!coordinate_parameter_refresh(parameter_target, refresh_request).completed)
                break;
        }
        bool io_satisfied_latency = false;
        if (restart_plan.reconfigure_io) {
            HelperIoRestartTarget io_target(
                dsp, engine, region, dsp_to_control, native_overrides, feedback_resync_required);
            if (!io_target.run())
                break;
            io_satisfied_latency = true;
        }
        if (restart_plan.refresh_latency && !io_satisfied_latency) {
            HelperLatencyRestartTarget latency_target(dsp, engine, region);
            if (!coordinate_latency_restart(latency_target).completed)
                break;
        }
        // Reload and unknown actions remain deliberately unsupported until their
        // individual S1 tracer bullets implement them at a quiesced frontier.
        if (restart_plan.reload_component || restart_plan.unknown_flags != 0)
            InterlockedExchange(&region->last_error, 3);
'''
repl('src/host/main.cpp',old,new)
