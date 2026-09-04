#include "host/hosted_plugin.hpp"
#include "host/process_block_view.hpp"
#include "rack/rack_preset_library.hpp"
#include "rack/rack_session_snapshot.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using safevst3::HostedPlugin;
using safevst3::ProcessBlockView;
using safevst3::rack::RackPersistedSlotHealth;
using safevst3::rack::RackPreset;
using safevst3::rack::RackPresetId;
using safevst3::rack::RackPresetLoadSource;
using safevst3::rack::RackSessionSlotSnapshot;
using safevst3::rack::RackSessionSnapshot;

constexpr std::uint32_t kGainParameterId = 100;

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
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultTrue; }

private:
    std::atomic<Steinberg::uint32> refs_{1};
};

class LocalAppDataGuard {
public:
    explicit LocalAppDataGuard(const std::wstring& replacement)
    {
        wchar_t buffer[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
        if (length != 0 && length < std::size(buffer)) {
            old_.assign(buffer, length);
            had_old_ = true;
        }
        SetEnvironmentVariableW(L"LOCALAPPDATA", replacement.c_str());
    }

    ~LocalAppDataGuard()
    {
        SetEnvironmentVariableW(L"LOCALAPPDATA", had_old_ ? old_.c_str() : nullptr);
    }

private:
    std::wstring old_;
    bool had_old_ = false;
};

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float actual, float expected)
{
    return std::fabs(actual - expected) <= 1.0e-6f;
}

bool open_plugin(HostedPlugin& plugin,
                 const std::string& path,
                 const std::string& class_id,
                 TestComponentHandler& handler)
{
    std::string error;
    if (plugin.open(path, class_id, 48000, 2, &handler, error))
        return true;
    std::cerr << "FAIL: HostedPlugin::open(" << path << "): " << error << '\n';
    return false;
}

bool process_gain(HostedPlugin& plugin, float input, float expected, std::uint64_t sequence)
{
    float left_in[1] = {input};
    float right_in[1] = {input};
    float left_out[1] = {};
    float right_out[1] = {};
    float* inputs[2] = {left_in, right_in};
    float* outputs[2] = {left_out, right_out};
    ProcessBlockView block{inputs, outputs, 2, 1, sequence};
    if (!expect(plugin.process(block), "stateful fixture process must succeed"))
        return false;
    return expect(close_enough(left_out[0], expected) && close_enough(right_out[0], expected),
                  "restored preset state must reproduce the saved gain");
}

bool set_gain_and_apply(HostedPlugin& plugin, double normalized, std::uint64_t sequence)
{
    if (!expect(plugin.queue_parameter(kGainParameterId, normalized), "queue gain must succeed"))
        return false;
    return process_gain(plugin, 2.0f, static_cast<float>(2.0 * normalized), sequence);
}

bool same_slot(const RackSessionSlotSnapshot& a, const RackSessionSlotSnapshot& b)
{
    return a.slot_id == b.slot_id &&
           a.plugin_path == b.plugin_path &&
           a.class_id == b.class_id &&
           a.bypass == b.bypass &&
           a.health == b.health &&
           a.state.component == b.state.component &&
           a.state.controller == b.state.controller;
}

bool same_preset(const RackPreset& a, const RackPreset& b)
{
    if (a.preset_id != b.preset_id || a.name != b.name || a.slots.size() != b.slots.size())
        return false;
    for (std::size_t i = 0; i < a.slots.size(); ++i) {
        if (!same_slot(a.slots[i], b.slots[i]))
            return false;
    }
    return true;
}

bool same_working_content(const RackPreset& preset, const RackSessionSnapshot& working)
{
    if (preset.slots.size() != working.slots.size())
        return false;
    for (std::size_t i = 0; i < preset.slots.size(); ++i) {
        if (!same_slot(preset.slots[i], working.slots[i]))
            return false;
    }
    return true;
}

void corrupt_file(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "corrupt-preset";
}

void cleanup_tree(const std::filesystem::path& root)
{
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

bool run_test(const char* module_a, const char* module_b)
{
    bool ok = true;
    const auto temp_root = std::filesystem::temp_directory_path() /
        (L"safevst3-r2-3-" + std::to_wstring(GetCurrentProcessId()));
    cleanup_tree(temp_root);
    std::filesystem::create_directories(temp_root);
    LocalAppDataGuard appdata(temp_root.wstring());

    const auto library = safevst3::rack::rack_preset_library_path();
    ok &= expect(library == temp_root / L"OBS Safe VST3 Host" / L"Rack Presets",
                 "preset library must be user-level and separate from Session Snapshots");

    RackPresetId generated_a{};
    RackPresetId generated_b{};
    std::string error;
    ok &= expect(safevst3::rack::generate_rack_preset_id(generated_a, error),
                 "preset UUID generation must succeed");
    ok &= expect(safevst3::rack::generate_rack_preset_id(generated_b, error),
                 "second preset UUID generation must succeed");
    ok &= expect(generated_a != generated_b,
                 "independent preset UUIDs must not use display name as identity");

    TestComponentHandler handler;
    HostedPlugin plugin_a;
    HostedPlugin plugin_b;
    if (!open_plugin(plugin_a, module_a, "", handler) ||
        !open_plugin(plugin_b, module_b, "", handler)) {
        cleanup_tree(temp_root);
        return false;
    }

    ok &= set_gain_and_apply(plugin_a, 0.25, 10);
    ok &= set_gain_and_apply(plugin_b, 0.80, 11);

    RackSessionSlotSnapshot slot_a{};
    RackSessionSlotSnapshot slot_b{};
    ok &= expect(safevst3::rack::capture_rack_session_slot(
                     plugin_a, 0x1000000000000101ull, module_a, false,
                     RackPersistedSlotHealth::Ready, slot_a, error),
                 "Rack A slot A full state capture must succeed");
    ok &= expect(safevst3::rack::capture_rack_session_slot(
                     plugin_b, 0x1000000000000202ull, module_b, true,
                     RackPersistedSlotHealth::Ready, slot_b, error),
                 "Rack A slot B full state capture must succeed");
    plugin_b.close();
    plugin_a.close();

    RackPreset preset{};
    preset.preset_id = {0x62, 0x72, 0x6f, 0x61, 0x64, 0x63, 0x61, 0x73,
                        0x74, 0x2d, 0x76, 0x6f, 0x63, 0x61, 0x6c, 0x01};
    preset.name = "Broadcast Vocal";
    preset.slots = {slot_b, slot_a};

    std::vector<std::uint8_t> encoded;
    ok &= expect(safevst3::rack::encode_rack_preset(preset, encoded, error),
                 "valid preset must encode");
    RackPreset decoded{};
    ok &= expect(safevst3::rack::decode_rack_preset(encoded, decoded, error),
                 "valid preset must decode");
    ok &= expect(same_preset(preset, decoded),
                 "preset codec must preserve UUID/name/order/stable IDs/identity/bypass/full state");

    RackPreset sentinel{};
    sentinel.name = "known-good-sentinel";
    auto corrupt_bytes = encoded;
    corrupt_bytes.back() ^= 0x5a;
    ok &= expect(!safevst3::rack::decode_rack_preset(corrupt_bytes, sentinel, error),
                 "checksum-corrupt preset must be rejected");
    ok &= expect(sentinel.name == "known-good-sentinel",
                 "failed preset decode must not mutate caller known-good state");

    ok &= expect(safevst3::rack::write_rack_preset_atomic(library, preset, error),
                 "Save as Preset must atomically publish one user-level preset");
    const auto preset_path = safevst3::rack::rack_preset_file_path(library, preset.preset_id);
    ok &= expect(std::filesystem::exists(preset_path),
                 "preset path must be derived from stable UUID, not display name");
    ok &= expect(preset_path.filename().wstring().find(L"Broadcast Vocal") == std::wstring::npos,
                 "renamable display text must not define preset filename identity");

    RackPreset loaded{};
    RackPresetLoadSource source = RackPresetLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_preset_lkg(
                     library, preset.preset_id, loaded, source, error),
                 "saved preset must load by stable UUID");
    ok &= expect(source == RackPresetLoadSource::Current && same_preset(preset, loaded),
                 "current saved preset must round-trip exactly");

    const std::array<std::uint8_t, 16> rack_a_id = {
        0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
        0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0};
    const std::array<std::uint8_t, 16> rack_b_id = {
        0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8,
        0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0};
    RackSessionSnapshot working_b{};
    ok &= expect(safevst3::rack::make_working_rack_from_preset(
                     loaded, rack_b_id, 77, working_b, error),
                 "preset must materialize into independent Rack B working state");
    ok &= expect(working_b.rack_id == rack_b_id && working_b.rack_id != rack_a_id,
                 "loading preset must retain destination Rack B identity, never source Rack identity");
    ok &= expect(working_b.generation == 77 && same_working_content(loaded, working_b),
                 "Rack B must receive equivalent order/state/bypass with destination generation");

    HostedPlugin restored_b;
    HostedPlugin restored_a;
    if (!open_plugin(restored_b, working_b.slots[0].plugin_path,
                     working_b.slots[0].class_id, handler) ||
        !open_plugin(restored_a, working_b.slots[1].plugin_path,
                     working_b.slots[1].class_id, handler)) {
        cleanup_tree(temp_root);
        return false;
    }
    ok &= expect(safevst3::rack::restore_rack_session_slot_state(
                     restored_b, working_b.slots[0], error),
                 "Rack B slot B component/controller state restore must succeed");
    ok &= expect(safevst3::rack::restore_rack_session_slot_state(
                     restored_a, working_b.slots[1], error),
                 "Rack B slot A component/controller state restore must succeed");
    ok &= process_gain(restored_b, 2.0f, 1.60f, 20);
    ok &= process_gain(restored_a, 2.0f, 0.50f, 21);
    restored_b.close();
    restored_a.close();

    // Loaded working Rack is deliberately detached from the library artifact.
    working_b.slots[0].bypass = false;
    working_b.slots[0].plugin_path = "mutated-working-rack-only";
    working_b.slots[0].state.component.clear();

    RackPreset reloaded{};
    source = RackPresetLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_preset_lkg(
                     library, preset.preset_id, reloaded, source, error),
                 "saved preset must remain reloadable after Rack B edits");
    ok &= expect(same_preset(preset, reloaded),
                 "post-load edits to Rack B must not silently mutate the saved preset");

    // Same-ID replacement is the persistence primitive later used by explicit Update.
    RackPreset updated = preset;
    updated.slots[0].bypass = false;
    ok &= expect(safevst3::rack::write_rack_preset_atomic(library, updated, error),
                 "same preset UUID may atomically replace its content only on explicit save call");
    corrupt_file(preset_path);

    RackPreset recovered{};
    source = RackPresetLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_preset_lkg(
                     library, preset.preset_id, recovered, source, error),
                 "corrupt current preset must recover previous valid same-ID copy");
    ok &= expect(source == RackPresetLoadSource::Previous && same_preset(preset, recovered),
                 "corrupt current preset must never destroy previous valid preset");

    cleanup_tree(temp_root);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: r2-3-rack-preset-library-integration <stateful-a.vst3> <stateful-b.vst3>\n";
        return 2;
    }
    if (!run_test(argv[1], argv[2]))
        return 1;
    std::cout << "R2-3 independent Rack Preset Save/Load foundation passed\n";
    return 0;
}
