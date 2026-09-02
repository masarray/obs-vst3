#include "host/hosted_plugin.hpp"
#include "host/process_block_view.hpp"
#include "rack/rack_protocol.hpp"
#include "rack/rack_session_snapshot.hpp"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <windows.h>

#include <algorithm>
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
using safevst3::rack::RackSessionLoadSource;
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

bool process_gain(HostedPlugin& plugin, float input_value, float expected, std::uint64_t sequence)
{
    float left_in[1] = {input_value};
    float right_in[1] = {input_value};
    float left_out[1] = {};
    float right_out[1] = {};
    float* inputs[2] = {left_in, right_in};
    float* outputs[2] = {left_out, right_out};
    ProcessBlockView block{inputs, outputs, 2, 1, sequence};
    if (!expect(plugin.process(block), "stateful fixture process must succeed"))
        return false;
    return expect(close_enough(left_out[0], expected) && close_enough(right_out[0], expected),
                  "stateful fixture output must match saved gain");
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

bool same_snapshot(const RackSessionSnapshot& a, const RackSessionSnapshot& b)
{
    if (a.rack_id != b.rack_id || a.generation != b.generation || a.slots.size() != b.slots.size())
        return false;
    for (std::size_t i = 0; i < a.slots.size(); ++i) {
        if (!same_slot(a.slots[i], b.slots[i]))
            return false;
    }
    return true;
}

std::filesystem::path test_snapshot_path()
{
    auto path = std::filesystem::temp_directory_path();
    path /= "safevst3-r2-1-" + std::to_string(GetCurrentProcessId()) + ".rack-session";
    return path;
}

void cleanup_snapshot_files(const std::filesystem::path& path)
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + ".tmp", ignored);
    std::filesystem::remove(path.string() + ".previous", ignored);
    std::filesystem::remove(path.string() + ".previous.tmp", ignored);
}

void corrupt_current_file(const std::filesystem::path& path, const char* marker)
{
    std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
    corrupt << marker;
}

bool test_codec_negatives(const RackSessionSnapshot& good)
{
    std::string error;
    std::vector<std::uint8_t> encoded;
    if (!expect(safevst3::rack::encode_rack_session_snapshot(good, encoded, error),
                "valid snapshot must encode"))
        return false;

    bool ok = true;
    RackSessionSnapshot sentinel{};
    sentinel.generation = 999;

    auto truncated = encoded;
    truncated.pop_back();
    ok &= expect(!safevst3::rack::decode_rack_session_snapshot(truncated, sentinel, error),
                 "truncated snapshot must be rejected");
    ok &= expect(sentinel.generation == 999,
                 "failed decode must not mutate caller's known-good snapshot");

    auto corrupt_body = encoded;
    corrupt_body.back() ^= 0x5a;
    ok &= expect(!safevst3::rack::decode_rack_session_snapshot(corrupt_body, sentinel, error),
                 "checksum-corrupt slot payload must be rejected");
    ok &= expect(sentinel.generation == 999,
                 "payload checksum failure must not mutate known-good snapshot");

    auto corrupt_identity = encoded;
    corrupt_identity[16] ^= 0x01;
    ok &= expect(!safevst3::rack::decode_rack_session_snapshot(corrupt_identity, sentinel, error),
                 "checksum must protect stable Rack identity metadata");
    ok &= expect(sentinel.generation == 999,
                 "header checksum failure must not mutate known-good snapshot");

    auto corrupt_generation = encoded;
    corrupt_generation[32] ^= 0x01;
    ok &= expect(!safevst3::rack::decode_rack_session_snapshot(corrupt_generation, sentinel, error),
                 "checksum must protect Rack generation metadata");

    RackSessionSnapshot too_many = good;
    while (too_many.slots.size() <= safevst3::rack::kRackMaxSlots)
        too_many.slots.push_back(good.slots.front());
    std::vector<std::uint8_t> rejected;
    ok &= expect(!safevst3::rack::encode_rack_session_snapshot(too_many, rejected, error),
                 "more than eight slots must be rejected");

    RackSessionSnapshot long_path = good;
    long_path.slots.front().plugin_path.assign(
        safevst3::rack::kRackSessionMaxPluginPathBytes + 1, 'x');
    ok &= expect(!safevst3::rack::encode_rack_session_snapshot(long_path, rejected, error),
                 "oversized plugin identity path must be rejected");
    return ok;
}

bool run_round_trip(const char* module_a, const char* module_b)
{
    TestComponentHandler handler;
    HostedPlugin plugin_a;
    HostedPlugin plugin_b;
    bool ok = true;

    if (!open_plugin(plugin_a, module_a, "", handler) || !open_plugin(plugin_b, module_b, "", handler))
        return false;

    ok &= set_gain_and_apply(plugin_a, 0.25, 10);
    ok &= set_gain_and_apply(plugin_b, 0.80, 11);

    RackSessionSlotSnapshot slot_a{};
    RackSessionSlotSnapshot slot_b{};
    std::string error;
    ok &= expect(safevst3::rack::capture_rack_session_slot(
                     plugin_a, safevst3::rack::kRackSlotIdA, module_a, false,
                     RackPersistedSlotHealth::Ready, slot_a, error),
                 "slot A state capture must succeed");
    ok &= expect(safevst3::rack::capture_rack_session_slot(
                     plugin_b, safevst3::rack::kRackSlotIdB, module_b, true,
                     RackPersistedSlotHealth::Suspect, slot_b, error),
                 "slot B state capture must preserve non-default health placeholder metadata");
    ok &= expect(!slot_a.class_id.empty() && !slot_b.class_id.empty(),
                 "logical class identity must come from the opened plugin");
    ok &= expect(slot_a.plugin_path != slot_b.plugin_path,
                 "two independently persisted slot identities must retain distinct module paths");

    RackSessionSnapshot first{};
    first.rack_id = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                     0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f};
    first.generation = 42;
    first.slots = {slot_b, slot_a};

    std::vector<std::uint8_t> bytes;
    ok &= expect(safevst3::rack::encode_rack_session_snapshot(first, bytes, error),
                 "multi-slot Rack snapshot must encode");
    RackSessionSnapshot decoded{};
    ok &= expect(safevst3::rack::decode_rack_session_snapshot(bytes, decoded, error),
                 "encoded Rack snapshot must decode");
    ok &= expect(same_snapshot(first, decoded),
                 "encode/decode must preserve Rack ID, generation, order, stable IDs, identity, bypass, health and state");
    ok &= test_codec_negatives(first);

    const auto path = test_snapshot_path();
    cleanup_snapshot_files(path);
    ok &= expect(safevst3::rack::write_rack_session_snapshot_atomic(path, first, error),
                 "first Session Snapshot atomic write must succeed");

    plugin_b.close();
    plugin_a.close();

    RackSessionSnapshot loaded{};
    RackSessionLoadSource source = RackSessionLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_session_snapshot_lkg(path, loaded, source, error),
                 "current Session Snapshot must load after full plugin destroy");
    ok &= expect(source == RackSessionLoadSource::Current && same_snapshot(first, loaded),
                 "current snapshot must be authoritative before corruption");

    HostedPlugin restored_b;
    HostedPlugin restored_a;
    const auto& loaded_b = loaded.slots[0];
    const auto& loaded_a = loaded.slots[1];
    ok &= expect(loaded_b.slot_id == safevst3::rack::kRackSlotIdB &&
                 loaded_a.slot_id == safevst3::rack::kRackSlotIdA,
                 "saved order must survive full destroy/recreate");
    if (!open_plugin(restored_b, loaded_b.plugin_path, loaded_b.class_id, handler) ||
        !open_plugin(restored_a, loaded_a.plugin_path, loaded_a.class_id, handler)) {
        cleanup_snapshot_files(path);
        return false;
    }
    ok &= expect(safevst3::rack::restore_rack_session_slot_state(restored_b, loaded_b, error),
                 "slot B component/controller state restore must succeed");
    ok &= expect(safevst3::rack::restore_rack_session_slot_state(restored_a, loaded_a, error),
                 "slot A component/controller state restore must succeed");
    ok &= process_gain(restored_b, 2.0f, 1.60f, 20);
    ok &= process_gain(restored_a, 2.0f, 0.50f, 21);
    restored_b.close();
    restored_a.close();

    RackSessionSnapshot second = first;
    second.generation = 43;
    std::reverse(second.slots.begin(), second.slots.end());
    second.slots[0].bypass = true;
    second.slots[1].bypass = false;
    ok &= expect(safevst3::rack::write_rack_session_snapshot_atomic(path, second, error),
                 "second valid save must atomically replace current and preserve previous LKG");

    RackSessionSnapshot current{};
    source = RackSessionLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_session_snapshot_lkg(path, current, source, error),
                 "second current snapshot must load");
    ok &= expect(source == RackSessionLoadSource::Current && same_snapshot(second, current),
                 "second coherent snapshot must become current");

    corrupt_current_file(path, "corrupt-current");
    RackSessionSnapshot recovered{};
    source = RackSessionLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_session_snapshot_lkg(path, recovered, source, error),
                 "corrupt current snapshot must recover from previous LKG");
    ok &= expect(source == RackSessionLoadSource::Previous && same_snapshot(first, recovered),
                 "previous coherent LKG must survive corrupt current replacement");

    RackSessionSnapshot third = second;
    third.generation = 44;
    ok &= expect(safevst3::rack::write_rack_session_snapshot_atomic(path, third, error),
                 "new coherent save must replace corrupt current without rotating corruption over previous LKG");
    corrupt_current_file(path, "corrupt-third");
    RackSessionSnapshot recovered_again{};
    source = RackSessionLoadSource::None;
    ok &= expect(safevst3::rack::load_rack_session_snapshot_lkg(path, recovered_again, source, error),
                 "LKG must remain recoverable after saving over a corrupt current file");
    ok &= expect(source == RackSessionLoadSource::Previous && same_snapshot(first, recovered_again),
                 "corrupt current must never poison the previous coherent LKG");

    cleanup_snapshot_files(path);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: r2-1-rack-session-snapshot-integration <stateful-a.vst3> <stateful-b.vst3>\n";
        return 2;
    }
    if (!run_round_trip(argv[1], argv[2]))
        return 1;
    std::cout << "R2-1 Rack Session Snapshot round-trip and LKG recovery passed\n";
    return 0;
}
