#include "common/protocol.hpp"
#include "common/state_snapshot.hpp"
#include "host/hosted_plugin.hpp"
#include "host/process_block_view.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <atomic>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <string>

#ifdef _WIN32

namespace {

using safevst3::AudioSlot;
using safevst3::HostedPlugin;
using safevst3::PluginStateSnapshot;
using safevst3::ProcessBlockView;

constexpr std::uint32_t kGainParameterId = 100;

template <typename Plugin>
concept ProcessesBlockView = requires(Plugin& plugin, ProcessBlockView& block) {
    { plugin.process(block) } -> std::same_as<bool>;
};

template <typename Plugin>
concept ProcessesSingleSlot = requires(Plugin& plugin, AudioSlot& slot) {
    plugin.process(slot);
};

static_assert(ProcessesBlockView<HostedPlugin>);
static_assert(!ProcessesSingleSlot<HostedPlugin>);

class TestComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** object) override
    {
        if (!object)
            return Steinberg::kInvalidArgument;
        *object = nullptr;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid)) {
            *object = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            addRef();
            return Steinberg::kResultTrue;
        }
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return ++refs_; }
    Steinberg::uint32 PLUGIN_API release() override { return --refs_; }
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
    {
        restart_flags_.fetch_or(static_cast<std::uint32_t>(flags), std::memory_order_relaxed);
        return Steinberg::kResultTrue;
    }

private:
    std::atomic<Steinberg::uint32> refs_{1};
    std::atomic<std::uint32_t> restart_flags_{0};
};

bool close_enough(float actual, float expected)
{
    return std::fabs(actual - expected) <= 1.0e-6f;
}

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool expect_sample(float actual, float expected, const char* message)
{
    if (close_enough(actual, expected))
        return true;
    std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool open_plugin(HostedPlugin& plugin, const char* module_path, TestComponentHandler& handler)
{
    std::string error;
    if (plugin.open(module_path, "", 48000, 2, &handler, error))
        return true;
    std::cerr << "FAIL: HostedPlugin::open: " << error << '\n';
    return false;
}

bool process_gain_block(HostedPlugin& plugin, float input_value, float expected, std::uint64_t sequence)
{
    float left_in[1] = {input_value};
    float right_in[1] = {input_value};
    float left_out[1] = {};
    float right_out[1] = {};
    float* inputs[2] = {left_in, right_in};
    float* outputs[2] = {left_out, right_out};
    ProcessBlockView block{inputs, outputs, 2, 1, sequence};
    bool ok = expect(plugin.process(block), "HostedPlugin ProcessBlockView must process");
    ok &= expect_sample(left_out[0], expected, "HostedPlugin left output");
    ok &= expect_sample(right_out[0], expected, "HostedPlugin right output");
    return ok;
}

bool characterize_deep_hosted_plugin(const char* module_path)
{
    TestComponentHandler handler;
    HostedPlugin plugin;
    bool ok = true;

    float unopened_in = 1.0f;
    float unopened_out = 0.0f;
    float* unopened_inputs[1] = {&unopened_in};
    float* unopened_outputs[1] = {&unopened_out};
    ProcessBlockView unopened_block{unopened_inputs, unopened_outputs, 1, 1, 1};
    ok &= expect(!plugin.process(unopened_block), "closed HostedPlugin must reject process");

    if (!open_plugin(plugin, module_path, handler))
        return false;

    ok &= expect(plugin.latency_samples() == 64, "fixture latency must be exposed by HostedPlugin");
    auto* first_editor_accessor = plugin.edit_controller();
    ok &= expect(first_editor_accessor != nullptr, "helper-owned editor accessor must expose fixture controller");
    ok &= expect(plugin.edit_controller() == first_editor_accessor,
                 "editor accessor ownership must remain stable while plugin is open");

    ok &= expect(plugin.queue_parameter(kGainParameterId, 0.75), "queue gain=0.75 must succeed");
    ok &= process_gain_block(plugin, 2.0f, 1.5f, 10);

    PluginStateSnapshot snapshot{};
    std::string error;
    ok &= expect(plugin.capture_state(snapshot, error), "component/controller state capture must succeed");
    ok &= expect(!snapshot.component.empty(), "component state must be captured");
    ok &= expect(!snapshot.controller.empty(), "controller state must be captured");

    ok &= expect(plugin.queue_parameter(kGainParameterId, 0.25), "queue gain=0.25 must succeed");
    ok &= process_gain_block(plugin, 2.0f, 0.5f, 11);

    error.clear();
    ok &= expect(plugin.restore_state(snapshot, error), "component/controller state restore must succeed");
    ok &= process_gain_block(plugin, 2.0f, 1.5f, 12);

    error.clear();
    ok &= expect(plugin.refresh_latency_after_restart(error), "latency restart transaction must succeed");
    ok &= expect(plugin.latency_samples() == 64, "latency must remain coherent after restart transaction");

    plugin.close();
    ok &= expect(plugin.edit_controller() == nullptr, "editor accessor must clear after close");
    ok &= expect(!plugin.process(unopened_block), "closed HostedPlugin must reject process after explicit close");

    ok &= expect(open_plugin(plugin, module_path, handler), "HostedPlugin must reopen after close");
    plugin.close();
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: r0-2-hosted-plugin-characterization <fixture.vst3>\n";
        return 2;
    }
    if (!characterize_deep_hosted_plugin(argv[1]))
        return 1;
    std::cout << "R0-2 HostedPlugin lifecycle/state/process seam characterized successfully\n";
    return 0;
}

#else
int main() { return 0; }
#endif
