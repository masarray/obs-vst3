#include "common/process_context_policy.hpp"

namespace safevst3 {

namespace {

constexpr std::uint32_t kTruthfullySupportedOptionalRequirements =
    kProcessNeedContinuousTimeSamples;

} // namespace

ProcessContextPolicy plan_process_context(std::uint32_t requested_requirements) noexcept
{
    ProcessContextPolicy policy{};
    policy.requested_requirements = requested_requirements;
    policy.provide_continuous_time =
        (requested_requirements & kProcessNeedContinuousTimeSamples) != 0;
    policy.unsupported_requirements =
        requested_requirements & ~kTruthfullySupportedOptionalRequirements;
    return policy;
}

ProcessContextFrame make_process_context_frame(
    double sample_rate,
    std::int64_t sample_position,
    const ProcessContextPolicy& policy) noexcept
{
    ProcessContextFrame frame{};
    frame.sample_rate = sample_rate;
    frame.project_time_samples = sample_position;
    if (policy.provide_continuous_time) {
        frame.continuous_time_samples = sample_position;
        frame.state |= kProcessContextContinuousTimeValid;
    }
    return frame;
}

} // namespace safevst3
