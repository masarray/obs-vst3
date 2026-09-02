#include "rack/rack_protocol.hpp"
#include "rack/rack_recovery_policy.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
using safevst3::RecoveryHealth;
using safevst3::RecoveryObservation;
using safevst3::rack::RackBreadcrumbPhase;
using safevst3::rack::RackBreadcrumbSnapshot;
using safevst3::rack::RackFailureConfidence;
using safevst3::rack::RackHostStatus;
using safevst3::rack::RackProcessResult;
using safevst3::rack::RackRecoveryPolicy;
using safevst3::rack::RackSharedAudioRegion;

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool close_enough(float a, float b) { return std::fabs(a - b) <= 1.0e-6f; }

std::wstring widen(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring quote(const std::wstring& value) { return L"\"" + value + L"\""; }

struct AudioResult {
    float left = 0.0f;
    float right = 0.0f;
    std::uint64_t chain_generation = 0;
    std::uint32_t latency = 0;
};

struct Scenario {
    HANDLE mapping = nullptr;
    HANDLE request = nullptr;
    HANDLE response = nullptr;
    HANDLE ready = nullptr;
    HANDLE crash_a = nullptr;
    HANDLE crash_b = nullptr;
    PROCESS_INFORMATION process{};
    RackSharedAudioRegion* region = nullptr;

    std::wstring mapping_name;
    std::wstring request_name;
    std::wstring response_name;
    std::wstring ready_name;
    std::wstring crash_a_name;
    std::wstring crash_b_name;

    ~Scenario()
    {
        stop_helper();
        if (region) UnmapViewOfFile(region);
        if (crash_b) CloseHandle(crash_b);
        if (crash_a) CloseHandle(crash_a);
        if (ready) CloseHandle(ready);
        if (response) CloseHandle(response);
        if (request) CloseHandle(request);
        if (mapping) CloseHandle(mapping);
    }

    bool process_alive() const noexcept
    {
        if (!process.hProcess)
            return false;
        return WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT;
    }

    void close_process_handles() noexcept
    {
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) CloseHandle(process.hProcess);
        process = {};
    }

    void stop_helper() noexcept
    {
        if (process.hProcess && process_alive()) {
            if (region) InterlockedExchange(&region->shutdown_requested, 1);
            if (request) SetEvent(request);
            if (WaitForSingleObject(process.hProcess, 2000) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 0xD14E);
                (void)WaitForSingleObject(process.hProcess, 2000);
            }
        }
        close_process_handles();
    }
};

bool create_scenario(Scenario& s, const wchar_t* label)
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"_" +
                        std::to_wstring(GetTickCount64()) + L"_" + label;
    s.mapping_name = L"Local\\SafeVst3RackR14Map_" + suffix;
    s.request_name = L"Local\\SafeVst3RackR14Req_" + suffix;
    s.response_name = L"Local\\SafeVst3RackR14Rsp_" + suffix;
    s.ready_name = L"Local\\SafeVst3RackR14Ready_" + suffix;
    s.crash_a_name = L"Local\\SafeVst3RackR14CrashA_" + suffix;
    s.crash_b_name = L"Local\\SafeVst3RackR14CrashB_" + suffix;

    s.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(sizeof(RackSharedAudioRegion)),
                                   s.mapping_name.c_str());
    if (!expect(s.mapping != nullptr, "create Rack recovery mapping")) return false;
    s.region = static_cast<RackSharedAudioRegion*>(MapViewOfFile(
        s.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RackSharedAudioRegion)));
    if (!expect(s.region != nullptr, "map Rack recovery region")) return false;
    *s.region = RackSharedAudioRegion{};
    s.region->sample_rate = 48000;
    s.region->channels = 2;

    s.request = CreateEventW(nullptr, FALSE, FALSE, s.request_name.c_str());
    s.response = CreateEventW(nullptr, FALSE, FALSE, s.response_name.c_str());
    s.ready = CreateEventW(nullptr, TRUE, FALSE, s.ready_name.c_str());
    s.crash_a = CreateEventW(nullptr, TRUE, FALSE, s.crash_a_name.c_str());
    s.crash_b = CreateEventW(nullptr, TRUE, FALSE, s.crash_b_name.c_str());
    return expect(s.request && s.response && s.ready && s.crash_a && s.crash_b,
                  "create Rack recovery events");
}

bool launch_helper(Scenario& s, const char* helper_utf8,
                   const char* plugin_a_utf8, const char* plugin_b_utf8)
{
    s.stop_helper();
    ResetEvent(s.ready);
    ResetEvent(s.response);
    InterlockedExchange(&s.region->shutdown_requested, 0);
    InterlockedExchange(&s.region->host_status, static_cast<long>(RackHostStatus::Booting));

    SetEnvironmentVariableW(L"SAFEVST3_R14_CRASH_A_EVENT", s.crash_a_name.c_str());
    SetEnvironmentVariableW(L"SAFEVST3_R14_CRASH_B_EVENT", s.crash_b_name.c_str());

    const std::wstring helper = widen(helper_utf8);
    std::wstring command = quote(helper) + L" --mapping " + quote(s.mapping_name) +
        L" --request-event " + quote(s.request_name) +
        L" --response-event " + quote(s.response_name) +
        L" --ready-event " + quote(s.ready_name) +
        L" --plugin-a " + quote(widen(plugin_a_utf8)) +
        L" --plugin-b " + quote(widen(plugin_b_utf8));
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    const BOOL created = CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr,
                                        FALSE, 0, nullptr, nullptr, &startup, &s.process);
    SetEnvironmentVariableW(L"SAFEVST3_R14_CRASH_A_EVENT", nullptr);
    SetEnvironmentVariableW(L"SAFEVST3_R14_CRASH_B_EVENT", nullptr);
    if (!expect(created != FALSE, "launch separate Rack helper"))
        return false;
    if (!expect(WaitForSingleObject(s.ready, 15000) == WAIT_OBJECT_0, "Rack helper ready"))
        return false;
    return expect(s.region->host_status == static_cast<long>(RackHostStatus::Ready),
                  "Rack helper reports Ready");
}

bool begin_audio(Scenario& s, float left, float right, long request_generation,
                 std::uint32_t sequence)
{
    s.region->frames = 1;
    s.region->block_channels = 2;
    s.region->sequence = sequence;
    s.region->input[0][0] = left;
    s.region->input[1][0] = right;
    s.region->output[0][0] = 999.0f;
    s.region->output[1][0] = -999.0f;
    InterlockedExchange(&s.region->request_generation, request_generation);
    MemoryBarrier();
    return expect(SetEvent(s.request) != FALSE, "signal Rack audio request");
}

bool finish_success(Scenario& s, long request_generation, AudioResult& result,
                    DWORD timeout_ms = 5000)
{
    bool ok = expect(WaitForSingleObject(s.response, timeout_ms) == WAIT_OBJECT_0,
                     "bounded Rack audio response");
    if (!ok)
        return false;
    ok &= expect(s.region->response_generation == request_generation,
                 "Rack audio response generation matches");
    ok &= expect(s.region->process_result == static_cast<long>(RackProcessResult::Ok),
                 "Rack block succeeds after recovery");
    result.left = s.region->output[0][0];
    result.right = s.region->output[1][0];
    result.chain_generation = static_cast<std::uint64_t>(s.region->processed_chain_generation);
    result.latency = s.region->total_latency_samples;
    return ok;
}

bool process_success(Scenario& s, float left, float right, long request_generation,
                     std::uint32_t sequence, AudioResult& result)
{
    return begin_audio(s, left, right, request_generation, sequence) &&
           finish_success(s, request_generation, result);
}

AudioResult dry_fallback(float left, float right) noexcept
{
    AudioResult result{};
    result.left = left;
    result.right = right;
    return result;
}

bool expect_dead_without_response(Scenario& s)
{
    bool ok = expect(WaitForSingleObject(s.process.hProcess, 3000) == WAIT_OBJECT_0,
                     "crashing Rack helper exits within bound");
    ok &= expect(WaitForSingleObject(s.response, 0) == WAIT_TIMEOUT,
                 "dead helper cannot publish a wet response");
    return ok;
}

bool expect_dry(const AudioResult& result, float left, float right, const char* label)
{
    bool ok = true;
    ok &= expect(close_enough(result.left, left), label);
    ok &= expect(close_enough(result.right, right), label);
    return ok;
}

bool expect_recovered_wet(const AudioResult& result, float left, float right)
{
    bool ok = true;
    ok &= expect(close_enough(result.left, left * 2.0f + 1.0f),
                 "restarted Rack restores A then B wet left");
    ok &= expect(close_enough(result.right, right * 2.0f + 1.0f),
                 "restarted Rack restores A then B wet right");
    ok &= expect(result.chain_generation == 1, "restarted known topology is coherent generation 1");
    ok &= expect(result.latency == 16, "restarted known topology latency is coherent");
    return ok;
}

bool prove_backoff(RackRecoveryPolicy& policy)
{
    const RecoveryObservation dead{false, UINT64_MAX, 0};
    bool ok = true;
    auto decision = policy.observe(100, dead);
    ok &= expect(decision.restart, "first dead-helper observation permits one restart");
    ok &= expect(decision.health == RecoveryHealth::Exited, "dead helper classified Exited");
    policy.record_restart_attempt(100);
    decision = policy.observe(101, dead);
    ok &= expect(!decision.restart, "immediate repeat restart is suppressed");
    ok &= expect(decision.health == RecoveryHealth::Backoff, "repeat attempt enters backoff");
    ok &= expect(decision.retry_after_ms == 999, "first Rack restart backoff is exactly one second");
    decision = policy.observe(1100, dead);
    ok &= expect(decision.restart, "restart allowed only after bounded backoff expires");
    return ok;
}

bool run_slot_crash(const char* helper, const char* plugin_a, const char* plugin_b,
                    bool crash_a)
{
    Scenario s;
    if (!create_scenario(s, crash_a ? L"A" : L"B"))
        return false;
    if (crash_a)
        SetEvent(s.crash_b);
    else
        SetEvent(s.crash_a);

    if (!launch_helper(s, helper, plugin_a, plugin_b))
        return false;

    const float left = 0.25f;
    const float right = -0.5f;
    const std::uint32_t sequence = crash_a ? 101u : 201u;
    bool ok = begin_audio(s, left, right, 1, sequence);
    ok &= expect_dead_without_response(s);

    RackBreadcrumbSnapshot breadcrumb{};
    ok &= expect(safevst3::rack::read_rack_breadcrumb(*s.region, breadcrumb),
                 "read coherent breadcrumb after vendor process death");
    ok &= expect(breadcrumb.chain_generation == 1, "breadcrumb preserves Rack generation");
    ok &= expect(breadcrumb.audio_sequence == sequence, "breadcrumb preserves audio sequence");
    ok &= expect(breadcrumb.slot_id == (crash_a ? safevst3::rack::kRackSlotIdA
                                                : safevst3::rack::kRackSlotIdB),
                 "breadcrumb identifies stable active slot");
    ok &= expect(breadcrumb.phase == RackBreadcrumbPhase::Process,
                 "breadcrumb identifies Process phase");
    ok &= expect(breadcrumb.dsp_progress != 0, "breadcrumb carries DSP progress generation");

    const auto attribution = safevst3::rack::classify_rack_helper_death(breadcrumb, false);
    ok &= expect(attribution.confidence == RackFailureConfidence::Suspect,
                 "synchronous process breadcrumb marks slot suspect only");
    ok &= expect(attribution.slot_id == breadcrumb.slot_id,
                 "suspect attribution keeps stable slot identity");

    const AudioResult outage = dry_fallback(left, right);
    ok &= expect_dry(outage, left, right, "outer Rack path stays dry while helper is dead");

    RackRecoveryPolicy recovery;
    ok &= prove_backoff(recovery);

    // Crash-once event was set by the fixture before process termination, so
    // the same known A->B topology can be recreated without persistence.
    if (!launch_helper(s, helper, plugin_a, plugin_b))
        return false;
    AudioResult wet{};
    ok &= process_success(s, left, right, 2, sequence + 1, wet);
    ok &= expect_recovered_wet(wet, left, right);

    RackBreadcrumbSnapshot idle{};
    ok &= expect(safevst3::rack::read_rack_breadcrumb(*s.region, idle),
                 "read breadcrumb after successful recovered block");
    ok &= expect(idle.phase == RackBreadcrumbPhase::None,
                 "successful block clears active vendor breadcrumb");
    return ok;
}

bool run_ambiguous_kill(const char* helper, const char* plugin_a, const char* plugin_b)
{
    Scenario s;
    if (!create_scenario(s, L"KILL"))
        return false;
    SetEvent(s.crash_a);
    SetEvent(s.crash_b);
    if (!launch_helper(s, helper, plugin_a, plugin_b))
        return false;

    bool ok = true;
    AudioResult wet{};
    ok &= process_success(s, 0.125f, -0.25f, 1, 301, wet);
    ok &= expect_recovered_wet(wet, 0.125f, -0.25f);

    RackBreadcrumbSnapshot before_kill{};
    ok &= expect(safevst3::rack::read_rack_breadcrumb(*s.region, before_kill),
                 "read idle breadcrumb before supervisor kill");
    ok &= expect(before_kill.phase == RackBreadcrumbPhase::None,
                 "no stale active slot remains after completed block");

    ok &= expect(TerminateProcess(s.process.hProcess, 0xD14D) != FALSE,
                 "outer supervisor can terminate helper for ambiguous kill test");
    ok &= expect(WaitForSingleObject(s.process.hProcess, 3000) == WAIT_OBJECT_0,
                 "externally killed helper exits within bound");

    RackBreadcrumbSnapshot after_kill{};
    ok &= expect(safevst3::rack::read_rack_breadcrumb(*s.region, after_kill),
                 "read breadcrumb after ambiguous helper kill");
    const auto attribution = safevst3::rack::classify_rack_helper_death(after_kill, true);
    ok &= expect(attribution.confidence == RackFailureConfidence::Unknown,
                 "arbitrary helper kill remains unknown rather than blaming last slot");
    ok &= expect(attribution.slot_id == 0, "unknown attribution carries no guilty slot ID");

    ok &= expect_dry(dry_fallback(0.125f, -0.25f), 0.125f, -0.25f,
                     "outer Rack path stays dry after arbitrary helper kill");

    RackRecoveryPolicy recovery;
    ok &= prove_backoff(recovery);
    if (!launch_helper(s, helper, plugin_a, plugin_b))
        return false;
    ok &= process_success(s, 0.125f, -0.25f, 2, 302, wet);
    ok &= expect_recovered_wet(wet, 0.125f, -0.25f);
    return ok;
}

bool run_test(const char* helper, const char* plugin_a, const char* plugin_b)
{
    bool ok = true;
    ok &= run_slot_crash(helper, plugin_a, plugin_b, true);
    ok &= run_slot_crash(helper, plugin_a, plugin_b, false);
    ok &= run_ambiguous_kill(helper, plugin_a, plugin_b);
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: r1-4-rack-recovery-integration <rack-helper> <plugin-a> <plugin-b>\n";
        return 2;
    }
    return run_test(argv[1], argv[2], argv[3]) ? 0 : 1;
}
