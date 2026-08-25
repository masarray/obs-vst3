#include "common/process_context_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "process context policy failure: " << message << '\n';
    std::exit(1);
}

} // namespace

int main()
{
    using namespace safevst3;

    static_assert(std::is_trivially_copyable_v<ProcessContextPolicy>);
    static_assert(std::is_trivially_copyable_v<ProcessContextFrame>);

    const auto none = plan_process_context(0);
    require(!none.provide_continuous_time,
            "no requirements must not enable optional context");
    require(none.unsupported_requirements == 0,
            "no requirements must not report unsupported context");
    const auto base = make_process_context_frame(48000.0, 1024, none);
    require(base.sample_rate == 48000.0,
            "sampleRate must always reflect the processing setup");
    require(base.project_time_samples == 1024,
            "projectTimeSamples must always reflect the monotonic sample position");
    require(base.continuous_time_samples == 0 && base.state == 0,
            "no optional context validity may be fabricated");

    const auto continuous = plan_process_context(kProcessNeedContinuousTimeSamples);
    require(continuous.provide_continuous_time,
            "continuous-time request must enable deterministic no-loop time");
    require(continuous.unsupported_requirements == 0,
            "continuous-time request is supported by the OBS sample counter");
    const auto continuous_frame = make_process_context_frame(44100.0, 987654, continuous);
    require(continuous_frame.project_time_samples == 987654 &&
                continuous_frame.continuous_time_samples == 987654,
            "OBS no-loop continuous time must equal project sample time");
    require(continuous_frame.state == kProcessContextContinuousTimeValid,
            "continuous time must be the only optional validity bit emitted");

    constexpr std::uint32_t unavailable =
        kProcessNeedSystemTime | kProcessNeedProjectTimeMusic |
        kProcessNeedBarPositionMusic | kProcessNeedCycleMusic |
        kProcessNeedSamplesToNextClock | kProcessNeedTempo |
        kProcessNeedTimeSignature | kProcessNeedChord |
        kProcessNeedFrameRate | kProcessNeedTransportState;
    const auto unsupported = plan_process_context(unavailable);
    require(!unsupported.provide_continuous_time,
            "unsupported musical/transport requests must not enable fake context");
    require(unsupported.unsupported_requirements == unavailable,
            "every unavailable optional requirement must remain diagnosable");
    const auto unsupported_frame = make_process_context_frame(48000.0, 2048, unsupported);
    require(unsupported_frame.state == 0,
            "tempo/time-signature/transport/system-time validity must not be invented");

    const auto mixed = plan_process_context(unavailable | kProcessNeedContinuousTimeSamples);
    require(mixed.provide_continuous_time && mixed.unsupported_requirements == unavailable,
            "supported continuous time must survive alongside unavailable requests");
    const auto mixed_frame = make_process_context_frame(96000.0, 4096, mixed);
    require(mixed_frame.state == kProcessContextContinuousTimeValid &&
                mixed_frame.continuous_time_samples == 4096,
            "mixed requirements must expose only truthful continuous time");

    constexpr std::uint32_t unknown = 1u << 30;
    const auto future = plan_process_context(unknown);
    require(!future.provide_continuous_time && future.unsupported_requirements == unknown,
            "unknown future requirement bits must remain unsupported");
    const auto future_frame = make_process_context_frame(48000.0, 8192, future);
    require(future_frame.state == 0,
            "unknown future requirements must never fabricate validity flags");

    std::cout << "deterministic process context policy ok\n";
    return 0;
}
