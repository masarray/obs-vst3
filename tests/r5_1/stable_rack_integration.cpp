#include "host/hosted_plugin.hpp"
#include "platform/windows/win_rack_bridge.hpp"
#include "rack/rack_recovery_policy.hpp"
#include "rack/rack_session_snapshot.hpp"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using safevst3::HostedPlugin;
using safevst3::RecoveryHealth;
using safevst3::RecoveryObservation;
using safevst3::WinRackBridge;
using safevst3::rack::RackPersistedSlotHealth;
using safevst3::rack::RackRecoveryPolicy;
using safevst3::rack::RackSessionLoadSource;
using safevst3::rack::RackSessionSlotSnapshot;
using safevst3::rack::RackSessionSnapshot;

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kFrames = 64;
constexpr std::uint64_t kSlotA = 0x510001u;
constexpr std::uint64_t kSlotB = 0x510002u;
constexpr char kRackIdText[] = "00112233445546778899aabbccddeeff";
constexpr std::array<std::uint8_t, 16> kRackId{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x46, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float left, float right) noexcept
{
    return std::fabs(left - right) <= 1.0e-5f;
}

std::filesystem::path suffixed(const std::filesystem::path& path, const wchar_t* suffix)
{
    auto native = path.native();
    native += suffix;
    return std::filesystem::path(std::move(native));
}

void cleanup_snapshot_files(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(suffixed(path, L".previous"), ignored);
    std::filesystem::remove(suffixed(path, L".tmp"), ignored);
    std::filesystem::remove(suffixed(path, L".previous.tmp"), ignored);
}

bool corrupt_current(const std::filesystem::path& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    static constexpr char bytes[] = "corrupt-r5-current";
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes, static_cast<DWORD>(sizeof(bytes) - 1),
                              &written, nullptr);
    if (ok)
        FlushFileBuffers(file);
    CloseHandle(file);
    return ok && written == sizeof(bytes) - 1;
}

bool process_expected_wet(WinRackBridge& bridge)
{
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    float* planes[kChannels]{left.data(), right.data()};

    for (int attempt = 0; attempt < 40; ++attempt) {
        left.fill(1.0f);
        right.fill(2.0f);
        if (bridge.process(planes, kChannels, kFrames, 0.90)) {
            bool ok = true;
            for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
                ok &= close_enough(left[frame], 0.25f);
                ok &= close_enough(right[frame], 0.50f);
            }
            if (!ok)
                std::cerr << "FAIL: restored Rack wet output is not two serial 0.5 gains\n";
            return ok;
        }
        Sleep(5);
    }
    std::cerr << "FAIL: restored Rack did not produce a bounded wet block\n";
    return false;
}

bool create_two_slot_snapshot(const std::filesystem::path& fixture,
                              const std::filesystem::path& snapshot_path)
{
    HostedPlugin plugin_a;
    HostedPlugin plugin_b;
    std::string error;
    const std::string fixture_path = fixture.string();

    if (!expect(plugin_a.open(fixture_path, "", kSampleRate, kChannels, nullptr, error),
                "open fixture A for Session Snapshot")) {
        std::cerr << error << '\n';
        return false;
    }
    error.clear();
    if (!expect(plugin_b.open(fixture_path, "", kSampleRate, kChannels, nullptr, error),
                "open fixture B for Session Snapshot")) {
        std::cerr << error << '\n';
        plugin_a.close();
        return false;
    }

    RackSessionSlotSnapshot slot_a{};
    RackSessionSlotSnapshot slot_b{};
    error.clear();
    bool ok = expect(safevst3::rack::capture_rack_session_slot(
                         plugin_a, kSlotA, fixture_path, false,
                         RackPersistedSlotHealth::Ready, slot_a, error),
                     "capture fixture A state");
    if (!ok && !error.empty())
        std::cerr << error << '\n';
    error.clear();
    ok &= expect(safevst3::rack::capture_rack_session_slot(
                     plugin_b, kSlotB, fixture_path, false,
                     RackPersistedSlotHealth::Ready, slot_b, error),
                 "capture fixture B state");
    if (!ok && !error.empty())
        std::cerr << error << '\n';

    RackSessionSnapshot snapshot{};
    snapshot.rack_id = kRackId;
    snapshot.generation = 7;
    snapshot.slots.push_back(slot_a);
    snapshot.slots.push_back(slot_b);

    if (ok) {
        error.clear();
        ok &= expect(safevst3::rack::write_rack_session_snapshot_atomic(
                         snapshot_path, snapshot, error),
                     "write first stable Rack Session Snapshot");
        if (!ok && !error.empty())
            std::cerr << error << '\n';
    }

    // A second coherent write creates the previous/LKG file. The test then
    // corrupts current so the shipping helper must recover from previous.
    if (ok) {
        snapshot.generation = 8;
        error.clear();
        ok &= expect(safevst3::rack::write_rack_session_snapshot_atomic(
                         snapshot_path, snapshot, error),
                     "write second stable Rack Session Snapshot");
        if (!ok && !error.empty())
            std::cerr << error << '\n';
    }

    plugin_b.close();
    plugin_a.close();
    return ok;
}

bool verify_current_repaired(const std::filesystem::path& snapshot_path)
{
    RackSessionSnapshot loaded{};
    RackSessionLoadSource source = RackSessionLoadSource::None;
    std::string error;
    const bool loaded_ok = safevst3::rack::load_rack_session_snapshot_lkg(
        snapshot_path, loaded, source, error);
    bool ok = expect(loaded_ok, "load helper-repaired Session Snapshot");
    if (!loaded_ok && !error.empty())
        std::cerr << error << '\n';
    ok &= expect(source == RackSessionLoadSource::Current,
                 "shipping helper promotes recovered LKG back to current");
    ok &= expect(loaded.rack_id == kRackId,
                 "repaired Session Snapshot preserves stable Rack identity");
    ok &= expect(loaded.slots.size() == 2,
                 "repaired Session Snapshot preserves both Rack slots");
    return ok;
}

bool prove_recovery_policy_bound()
{
    RackRecoveryPolicy policy;
    const RecoveryObservation dead{false, UINT64_MAX, 0};
    auto decision = policy.observe(100, dead);
    bool ok = expect(decision.restart, "dead Rack helper permits bounded restart");
    ok &= expect(decision.health == RecoveryHealth::Exited,
                 "dead Rack helper is classified exited");
    policy.record_restart_attempt(100);
    decision = policy.observe(101, dead);
    ok &= expect(!decision.restart, "immediate Rack restart loop is suppressed");
    ok &= expect(decision.health == RecoveryHealth::Backoff,
                 "repeated Rack recovery enters backoff");
    return ok;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        std::cerr << "usage: r5-1-stable-rack-integration <rack-helper.exe> <fixture.vst3>\n";
        return 2;
    }

    const std::filesystem::path helper(argv[1]);
    const std::filesystem::path fixture(argv[2]);

    wchar_t temp_root[MAX_PATH + 1]{};
    const DWORD temp_len = GetTempPathW(MAX_PATH, temp_root);
    if (!expect(temp_len != 0 && temp_len <= MAX_PATH, "resolve Windows temp directory"))
        return 3;

    const std::filesystem::path snapshot_path =
        std::filesystem::path(temp_root) /
        (L"safevst3-r5-1-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".rack-session");
    cleanup_snapshot_files(snapshot_path);

    bool ok = create_two_slot_snapshot(fixture, snapshot_path);
    ok &= expect(corrupt_current(snapshot_path),
                 "corrupt current Session Snapshot before production restore");

    WinRackBridge bridge;
    std::string error;
    if (ok) {
        ok &= expect(bridge.start(helper, kSampleRate, kChannels, error, {},
                                  snapshot_path, kRackIdText),
                     "shipping Rack helper starts from previous/LKG Session Snapshot");
        if (!ok && !error.empty())
            std::cerr << error << '\n';
    }

    if (ok) {
        const auto health = bridge.health_snapshot();
        ok &= expect(health.process_alive && health.ready,
                     "shipping Rack helper health reports ready");
        ok &= verify_current_repaired(snapshot_path);
        ok &= process_expected_wet(bridge);
        const auto status = bridge.status();
        ok &= expect(status.effect_count == 2,
                     "restored shipping Rack reports two committed effects");
        ok &= expect(status.total_latency_samples == 128,
                     "restored shipping Rack reports aggregate fixture latency");
    }

    bridge.abort();
    ok &= expect(!bridge.running(), "forced helper outage leaves bridge not running");
    ok &= prove_recovery_policy_bound();

    // Simulate another bad current checkpoint before supervisor restart. The
    // same persistent Rack identity/path must still recover without manual Save.
    ok &= expect(corrupt_current(snapshot_path),
                 "corrupt current Session Snapshot before restart");
    error.clear();
    if (ok) {
        ok &= expect(bridge.start(helper, kSampleRate, kChannels, error, {},
                                  snapshot_path, kRackIdText),
                     "Rack helper restarts from the same Session identity");
        if (!ok && !error.empty())
            std::cerr << error << '\n';
    }
    if (ok) {
        ok &= verify_current_repaired(snapshot_path);
        ok &= process_expected_wet(bridge);
    }

    bridge.stop();
    cleanup_snapshot_files(snapshot_path);

    if (!ok)
        return 1;
    std::cout << "R5-1 stable Rack Session restore/restart integration passed\n";
    return 0;
}
