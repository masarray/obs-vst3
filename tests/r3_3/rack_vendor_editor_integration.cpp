#include "host/process_block_view.hpp"
#include "rack/rack_hosted_plugin.hpp"
#undef HostedPlugin
#include "rack/rack_vendor_editor_manager.hpp"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr wchar_t kNativeEditorClass[] = L"ObsSafeVst3NativeEditorWindow";
constexpr std::uint32_t kFrames = 16;
constexpr Steinberg::Vst::ParamID kGainParameterId = 100;

bool require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

HWND find_editor(const wchar_t* title)
{
    return FindWindowW(kNativeEditorClass, title);
}

bool process_oracle(safevst3::RackHostedPlugin& plugin,
                    std::uint64_t sequence,
                    float expected_gain)
{
    std::array<std::array<float, kFrames>, 2> input{};
    std::array<std::array<float, kFrames>, 2> output{};
    float* in[2] = {input[0].data(), input[1].data()};
    float* out[2] = {output[0].data(), output[1].data()};
    for (std::uint32_t ch = 0; ch < 2; ++ch) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame)
            input[ch][frame] = static_cast<float>(1 + ch + frame) / 32.0f;
    }
    safevst3::ProcessBlockView block{in, out, 2, kFrames, sequence};
    if (!plugin.process(block))
        return false;
    for (std::uint32_t ch = 0; ch < 2; ++ch) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            if (std::fabs(output[ch][frame] - input[ch][frame] * expected_gain) > 1.0e-6f)
                return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: r3-3-vendor-editor-integration <fixture.vst3>\n";
        return EXIT_FAILURE;
    }

    const std::string plugin_path = argv[1];
    safevst3::RackHostedPlugin plugin_a;
    safevst3::RackHostedPlugin plugin_b;
    std::string error;

    if (!require(plugin_a.open(plugin_path, "", 48000, 2, nullptr, error), error.c_str()))
        return EXIT_FAILURE;
    error.clear();
    if (!require(plugin_b.open(plugin_path, "", 48000, 2, nullptr, error), error.c_str()))
        return EXIT_FAILURE;

    safevst3::rack::ui::RackVendorEditorManager manager;
    if (!require(manager.open_count() == 0, "vendor UI must not auto-open on plug-in load") ||
        !require(find_editor(L"R3-3 Slot A") == nullptr, "Slot A must start with no HWND") ||
        !require(find_editor(L"R3-3 Slot B") == nullptr, "Slot B must start with no HWND"))
        return EXIT_FAILURE;

    error.clear();
    if (!require(manager.open(0x101, plugin_a, "R3-3 Slot A", error), error.c_str()) ||
        !require(manager.open_count() == 1, "Slot A open must create one managed window") ||
        !require(manager.created(0x101) && manager.visible(0x101), "Slot A must be visible"))
        return EXIT_FAILURE;
    HWND slot_a = find_editor(L"R3-3 Slot A");
    if (!require(slot_a != nullptr && IsWindow(slot_a), "Slot A native HWND must exist"))
        return EXIT_FAILURE;

    // Regression for real split-component vendors (FabFilter-class behavior):
    // a native editor changes the controller and calls IComponentHandler::performEdit.
    // The Rack must forward that edit to the processor on the DSP thread. A no-op
    // component handler would leave audio/component state at the 0.5 default.
    auto* controller_a = plugin_a.edit_controller();
    if (!require(controller_a != nullptr, "Slot A controller must exist") ||
        !require(controller_a->setParamNormalized(kGainParameterId, 0.8) ==
                     Steinberg::kResultTrue,
                 "simulated native GUI edit must be accepted") ||
        !require(process_oracle(plugin_a, 1, 0.8f),
                 "native GUI edit must reach Rack processor inputParameterChanges"))
        return EXIT_FAILURE;

    // The persisted component blob must contain the edited DSP value, not only
    // controller-private UI state. Restore it into an independent instance and
    // prove the processor still runs at 0.8 after the round trip.
    safevst3::PluginStateSnapshot edited_state{};
    error.clear();
    if (!require(plugin_a.capture_state(edited_state, error), error.c_str()))
        return EXIT_FAILURE;
    error.clear();
    if (!require(plugin_b.restore_state(edited_state, error), error.c_str()) ||
        !require(process_oracle(plugin_b, 2, 0.8f),
                 "captured component state must preserve native GUI edit"))
        return EXIT_FAILURE;

    error.clear();
    if (!require(manager.open(0x202, plugin_b, "R3-3 Slot B", error), error.c_str()) ||
        !require(manager.open_count() == 2, "Slot B open must preserve independent Slot A") ||
        !require(manager.created(0x202) && manager.visible(0x202), "Slot B must be visible"))
        return EXIT_FAILURE;
    HWND slot_b = find_editor(L"R3-3 Slot B");
    if (!require(slot_b != nullptr && IsWindow(slot_b) && slot_b != slot_a,
                 "two Rack slots must own distinct floating HWNDs"))
        return EXIT_FAILURE;

    // User-close policy is hide, not unload. Pumping happens on the same Rack
    // control owner that created the vendor windows.
    PostMessageW(slot_a, WM_CLOSE, 0, 0);
    manager.pump_messages();
    if (!require(manager.created(0x101) && !manager.visible(0x101),
                 "WM_CLOSE must hide Slot A while preserving its view") ||
        !require(process_oracle(plugin_a, 3, 0.8f),
                 "hiding vendor UI must not unload or destabilize edited DSP"))
        return EXIT_FAILURE;

    error.clear();
    if (!require(manager.open(0x101, plugin_a, "R3-3 Slot A", error), error.c_str()) ||
        !require(manager.visible(0x101), "explicit reopen must foreground/show hidden Slot A") ||
        !require(find_editor(L"R3-3 Slot A") == slot_a,
                 "reopen must reuse the existing slot-owned native editor"))
        return EXIT_FAILURE;

    // Closing one slot is independent and does not disturb the other slot/runtime.
    manager.close(0x202);
    manager.pump_messages();
    if (!require(!manager.created(0x202) && find_editor(L"R3-3 Slot B") == nullptr,
                 "explicit Slot B close must destroy only Slot B editor") ||
        !require(manager.created(0x101) && manager.visible(0x101),
                 "Slot A must remain open when Slot B closes") ||
        !require(process_oracle(plugin_b, 4, 0.8f),
                 "closing vendor UI must not unload restored plug-in state"))
        return EXIT_FAILURE;

    manager.close_all();
    manager.pump_messages();
    if (!require(manager.open_count() == 0, "helper shutdown close_all must drain all vendor editors") ||
        !require(find_editor(L"R3-3 Slot A") == nullptr,
                 "Slot A HWND must be gone before plug-in destruction"))
        return EXIT_FAILURE;

    plugin_b.close();
    plugin_a.close();
    return EXIT_SUCCESS;
}
