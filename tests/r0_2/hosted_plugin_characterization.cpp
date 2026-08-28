#include "host/hosted_plugin.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#ifdef _WIN32

namespace {

using safevst3::HostedPlugin;
using safevst3::IoLayout;
using safevst3::PluginStateSnapshot;
using safevst3::ProcessBlockView;

constexpr std::uint32_t kGainId = 7001;
constexpr std::uint32_t kExpectedLatency = 37;

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool expect_sample(float actual, float expected, const char* message)
{
    if (std::fabs(actual - expected) <= 1.0e-6f)
        return true;
    std::cerr << "FAIL: " << message << " expected=" << expected
              << " actual=" << actual << '\n';
    return false;
}

bool open_plugin(HostedPlugin& plugin, const char* module_path)
{
    std::string error;
    if (plugin.open(module_path, "", 48000, 2, nullptr, error))
        return true;
    std::cerr << "FAIL: HostedPlugin open: " << error << '\n';
    return false;
}

bool process_stereo(HostedPlugin& plugin,
                    float left,
                    float right,
                    float expected_left,
                    float expected_right,
                    std::uint64_t sequence)
{
    float left_input[1] = {left};
    float right_input[1] = {right};
    float left_output[1] = {};
    float right_output[1] = {};
    float* inputs[2] = {left_input, right_input};
    float* outputs[2] = {left_output, right_output};
    const ProcessBlockView block{inputs, outputs, 2, 1, sequence};

    bool ok = true;
    ok &= expect(plugin.process(block), "protocol-neutral HostedPlugin process must succeed");
    ok &= expect_sample(left_output[0], expected_left, "left processed sample");
    ok &= expect_sample(right_output[0], expected_right, "right processed sample");
    return ok;
}

bool characterize_hosted_plugin(const char* module_path)
{
    HostedPlugin original;
    if (!open_plugin(original, module_path))
        return false;

    bool ok = true;
    ok &= expect(original.edit_controller() != nullptr,
                 "HostedPlugin must own/expose the loaded controller for helper-owned editor access");
    ok &= expect(original.latency_samples() == kExpectedLatency,
                 "initial latency must come from the hosted processor");
    ok &= expect(!original.parameters().empty(),
                 "controller parameter catalog must remain available through HostedPlugin");

    ok &= process_stereo(original, 2.0f, 4.0f, 3.0f, 6.0f, 1);

    // Drive a real processor-side change through the deep parameter seam. The
    // fixture records the resulting normalized gain in component state.
    ok &= expect(original.queue_parameter(kGainId, 0.75),
                 "HostedPlugin must accept a known controller/processor parameter");
    ok &= process_stereo(original, 2.0f, 4.0f, 3.5f, 7.0f, 2);

    PluginStateSnapshot snapshot;
    std::string error;
    ok &= expect(original.capture_state(snapshot, error),
                 "HostedPlugin component/controller state capture must succeed");
    if (!error.empty())
        std::cerr << "state capture detail: " << error << '\n';
    ok &= expect(!snapshot.component.empty(), "component state blob must be present");
    ok &= expect(!snapshot.controller.empty(), "controller-private state blob must be present");

    HostedPlugin restored;
    if (!open_plugin(restored, module_path))
        return false;
    ok &= expect(restored.restore_state(snapshot, error),
                 "HostedPlugin complete state restore must succeed");
    if (!error.empty())
        std::cerr << "state restore detail: " << error << '\n';
    ok &= process_stereo(restored, 2.0f, 4.0f, 3.5f, 7.0f, 3);

    PluginStateSnapshot round_trip;
    ok &= expect(restored.capture_state(round_trip, error),
                 "restored HostedPlugin state recapture must succeed");
    ok &= expect(round_trip.component == snapshot.component,
                 "component state must round-trip byte-for-byte");
    ok &= expect(round_trip.controller == snapshot.controller,
                 "controller-private state must round-trip byte-for-byte");

    ok &= expect(restored.refresh_latency_after_restart(error),
                 "HostedPlugin latency restart transaction must remain supported");
    ok &= expect(restored.latency_samples() == kExpectedLatency,
                 "latency restart must commit the processor latency");

    IoLayout layout{};
    std::uint32_t latency = 0;
    ok &= expect(restored.reconfigure_io_after_restart(layout, latency, error),
                 "HostedPlugin I/O restart lifecycle must remain supported");
    ok &= expect(layout.input_channels == 2 && layout.output_channels == 2,
                 "I/O restart must preserve the supported stereo layout");
    ok &= expect(latency == kExpectedLatency,
                 "I/O restart must return the current processor latency");

    restored.close();
    ok &= expect(restored.edit_controller() == nullptr,
                 "controller accessor must be cleared by HostedPlugin close");

    float input[1] = {1.0f};
    float output[1] = {};
    float* inputs[2] = {input, input};
    float* outputs[2] = {output, output};
    const ProcessBlockView after_close{inputs, outputs, 2, 1, 4};
    ok &= expect(!restored.process(after_close),
                 "closed HostedPlugin must reject process calls");

    ok &= expect(open_plugin(restored, module_path),
                 "HostedPlugin must support a clean reopen after close");
    restored.close();
    original.close();
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: r0-2-hosted-plugin-test <stateful-fixture.vst3>\n";
        return 2;
    }

    if (!characterize_hosted_plugin(argv[1]))
        return 1;

    std::cout << "R0-2 HostedPlugin lifecycle/state/process seam characterized successfully\n";
    return 0;
}

#else

int main() { return 0; }

#endif
