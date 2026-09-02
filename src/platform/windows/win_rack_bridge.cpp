#ifdef _WIN32

#include "platform/windows/win_rack_bridge.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <sstream>

namespace safevst3 {
namespace {
constexpr ULONGLONG kRackHelperStartupTimeoutMs = 5000;

std::string win_error(const char* what)
{
    const DWORD code = GetLastError();
    std::ostringstream os;
    os << what << " failed (Win32 error " << code << ')';
    return os.str();
}
}

WinRackBridge::~WinRackBridge() { stop(); }

std::wstring WinRackBridge::quote(const std::wstring& value)
{
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        if (c == L'\"')
            out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

WinRackBridge::Names WinRackBridge::make_names()
{
    static std::atomic<unsigned long long> counter{0};
    const auto pid = static_cast<unsigned long long>(GetCurrentProcessId());
    const auto tick = static_cast<unsigned long long>(GetTickCount64());
    const auto serial = counter.fetch_add(1, std::memory_order_relaxed);
    std::wstringstream ss;
    ss << L"Local\\obs-safe-vst3-rack-" << pid << L'-' << tick << L'-' << serial;
    const std::wstring base = ss.str();
    return {base + L"-map", base + L"-req", base + L"-rsp",
            base + L"-ready", base + L"-ui-open"};
}

std::uint64_t WinRackBridge::qpc_now() noexcept
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint64_t WinRackBridge::qpc_frequency() noexcept
{
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::uint64_t>(value.QuadPart);
    }();
    return frequency;
}

bool WinRackBridge::process_alive() const noexcept
{
    if (!process_.hProcess)
        return false;
    DWORD code = 0;
    return GetExitCodeProcess(process_.hProcess, &code) && code == STILL_ACTIVE;
}

bool WinRackBridge::start(const std::filesystem::path& helper,
                          std::uint32_t sample_rate,
                          std::uint32_t channels,
                          std::string& error,
                          std::stop_token cancel)
{
    stop();
    if (cancel.stop_requested()) {
        error = "Rack helper startup cancelled";
        return false;
    }
    if (helper.empty() || !std::filesystem::exists(helper)) {
        error = "Rack helper executable not found";
        return false;
    }
    if (sample_rate == 0 || channels == 0 || channels > rack::kMaxChannels) {
        error = "Rack supports mono/stereo audio only";
        return false;
    }

    names_ = make_names();
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(sizeof(rack::RackSharedAudioRegion)),
                                  names_.mapping.c_str());
    if (!mapping_) {
        error = win_error("CreateFileMappingW(Rack)");
        stop();
        return false;
    }

    region_ = static_cast<rack::RackSharedAudioRegion*>(MapViewOfFile(
        mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rack::RackSharedAudioRegion)));
    if (!region_) {
        error = win_error("MapViewOfFile(Rack)");
        stop();
        return false;
    }
    std::memset(region_, 0, sizeof(*region_));
    region_->magic = rack::kRackProtocolMagic;
    region_->version = rack::kRackProtocolVersion;
    region_->sample_rate = sample_rate;
    region_->channels = channels;
    region_->max_frames = rack::kMaxFrames;
    region_->host_status = static_cast<long>(rack::RackHostStatus::Booting);
    region_->process_result = static_cast<long>(rack::RackProcessResult::Ok);

    request_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.request_event.c_str());
    response_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.response_event.c_str());
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, names_.ready_event.c_str());
    ui_open_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.ui_open_event.c_str());
    if (!request_event_ || !response_event_ || !ready_event_ || !ui_open_event_) {
        error = win_error("CreateEventW(Rack)");
        stop();
        return false;
    }

    std::wstring command = quote(helper.wstring()) +
        L" --mapping " + quote(names_.mapping) +
        L" --request-event " + quote(names_.request_event) +
        L" --response-event " + quote(names_.response_event) +
        L" --ready-event " + quote(names_.ready_event) +
        L" --ui-open-event " + quote(names_.ui_open_event);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring mutable_command = command;
    if (!CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        helper.parent_path().c_str(), &startup, &process_)) {
        error = win_error("CreateProcessW(Rack)");
        stop();
        return false;
    }
    SetPriorityClass(process_.hProcess, HIGH_PRIORITY_CLASS);
    ResumeThread(process_.hThread);

    const ULONGLONG deadline = GetTickCount64() + kRackHelperStartupTimeoutMs;
    HANDLE waits[] = {ready_event_, process_.hProcess};
    bool ready = false;
    while (!cancel.stop_requested()) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        const DWORD slice = static_cast<DWORD>(std::min<ULONGLONG>(50, deadline - now));
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, slice);
        if (wait == WAIT_OBJECT_0) {
            ready = true;
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            DWORD code = 0xFFFFFFFFu;
            (void)GetExitCodeProcess(process_.hProcess, &code);
            std::ostringstream os;
            os << "Rack helper exited during startup (exit code " << code << ')';
            error = os.str();
            stop();
            return false;
        }
        if (wait == WAIT_FAILED) {
            error = win_error("WaitForMultipleObjects(Rack)");
            stop();
            return false;
        }
    }

    if (cancel.stop_requested()) {
        error = "Rack helper startup cancelled";
        abort();
        return false;
    }
    if (!ready) {
        error = "Rack helper startup timed out";
        abort();
        return false;
    }
    if (InterlockedCompareExchange(&region_->host_status, 0, 0) !=
        static_cast<long>(rack::RackHostStatus::Ready)) {
        error = "Rack helper did not become ready";
        stop();
        return false;
    }

    next_sequence_ = 1;
    next_request_generation_ = 1;
    pending_request_generation_ = 0;
    deadline_misses_.store(0, std::memory_order_relaxed);
    return true;
}

void WinRackBridge::stop() noexcept
{
    if (region_) {
        InterlockedExchange(&region_->shutdown_requested, 1);
        if (request_event_)
            SetEvent(request_event_);
        if (ui_open_event_)
            SetEvent(ui_open_event_);
    }
    if (process_.hProcess) {
        const DWORD wait = WaitForSingleObject(process_.hProcess, 2000);
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(process_.hProcess, 0xDEAD);
            (void)WaitForSingleObject(process_.hProcess, 1000);
        }
    }
    if (process_.hThread) CloseHandle(process_.hThread);
    if (process_.hProcess) CloseHandle(process_.hProcess);
    process_ = {};
    if (region_) UnmapViewOfFile(region_);
    region_ = nullptr;
    if (ui_open_event_) CloseHandle(ui_open_event_);
    if (ready_event_) CloseHandle(ready_event_);
    if (response_event_) CloseHandle(response_event_);
    if (request_event_) CloseHandle(request_event_);
    if (mapping_) CloseHandle(mapping_);
    ui_open_event_ = ready_event_ = response_event_ = request_event_ = mapping_ = nullptr;
    names_ = {};
    pending_request_generation_ = 0;
}

void WinRackBridge::abort() noexcept
{
    if (process_.hProcess)
        TerminateProcess(process_.hProcess, ERROR_CANCELLED);
    stop();
}

bool WinRackBridge::running() const noexcept
{
    return region_ && process_alive() &&
        InterlockedCompareExchange(&region_->host_status, 0, 0) ==
            static_cast<long>(rack::RackHostStatus::Ready);
}

bool WinRackBridge::retire_completed_request() noexcept
{
    if (!region_ || pending_request_generation_ == 0)
        return true;
    const long response = InterlockedCompareExchange(&region_->response_generation, 0, 0);
    if (response != pending_request_generation_)
        return false;
    (void)WaitForSingleObject(response_event_, 0);
    pending_request_generation_ = 0;
    return true;
}

bool WinRackBridge::process(float* const* channels,
                            std::uint32_t channel_count,
                            std::uint32_t frames,
                            double deadline_fraction) noexcept
{
    if (!running() || !channels || channel_count == 0 ||
        channel_count > rack::kMaxChannels || frames == 0 || frames > rack::kMaxFrames ||
        channel_count != region_->channels)
        return false;

    // Rack protocol v4 has one bounded request region. If a prior block missed
    // its realtime deadline, never overwrite it while the helper may still be
    // consuming that memory. Fail dry until the old response frontier arrives.
    if (!retire_completed_request()) {
        deadline_misses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    for (std::uint32_t ch = 0; ch < channel_count; ++ch) {
        if (!channels[ch])
            return false;
    }

    const std::uint32_t sequence = next_sequence_++;
    const long request_generation = next_request_generation_;
    if (next_request_generation_ >= LONG_MAX)
        next_request_generation_ = 1;
    else
        ++next_request_generation_;

    region_->frames = frames;
    region_->block_channels = channel_count;
    region_->sequence = sequence;
    for (std::uint32_t ch = 0; ch < channel_count; ++ch)
        std::memcpy(region_->input[ch], channels[ch], sizeof(float) * frames);

    pending_request_generation_ = request_generation;
    MemoryBarrier();
    InterlockedExchange(&region_->request_generation, request_generation);
    MemoryBarrier();
    SetEvent(request_event_);

    deadline_fraction = std::clamp(deadline_fraction, 0.10, 0.95);
    const double block_seconds = static_cast<double>(frames) /
                                 static_cast<double>(region_->sample_rate);
    const double budget_seconds = std::clamp(block_seconds * deadline_fraction, 0.0005, 0.010);
    const std::uint64_t frequency = qpc_frequency();
    const std::uint64_t started = qpc_now();
    const std::uint64_t deadline_ticks = static_cast<std::uint64_t>(
        budget_seconds * static_cast<double>(frequency));

    while (qpc_now() - started < deadline_ticks) {
        const long response = InterlockedCompareExchange(&region_->response_generation, 0, 0);
        if (response == request_generation) {
            MemoryBarrier();
            const bool valid = region_->process_result == static_cast<long>(rack::RackProcessResult::Ok);
            if (valid) {
                for (std::uint32_t ch = 0; ch < channel_count; ++ch)
                    std::memcpy(channels[ch], region_->output[ch], sizeof(float) * frames);
            }
            pending_request_generation_ = 0;
            (void)WaitForSingleObject(response_event_, 0);
            return valid;
        }

        const std::uint64_t elapsed = qpc_now() - started;
        if (elapsed >= deadline_ticks)
            break;
        const double remaining_ms =
            (static_cast<double>(deadline_ticks - elapsed) * 1000.0) /
            static_cast<double>(frequency);
        const DWORD wait_ms = static_cast<DWORD>(
            std::max(1.0, std::min(remaining_ms, 2.0)));
        (void)WaitForSingleObject(response_event_, wait_ms);
    }

    deadline_misses_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool WinRackBridge::open_editor() noexcept
{
    return running() && ui_open_event_ && SetEvent(ui_open_event_) != FALSE;
}

RackBridgeStatus WinRackBridge::status() const noexcept
{
    RackBridgeStatus snapshot{};
    if (!region_)
        return snapshot;

    snapshot.running = running();
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto before = static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(&region_->committed_chain_generation), 0, 0));
        MemoryBarrier();
        const std::uint32_t count = std::min(region_->committed_slot_count, rack::kRackMaxSlots);
        const std::uint32_t latency = region_->total_latency_samples;
        MemoryBarrier();
        const auto after = static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(&region_->committed_chain_generation), 0, 0));
        if (before == after) {
            snapshot.chain_generation = after;
            snapshot.effect_count = count;
            snapshot.total_latency_samples = latency;
            return snapshot;
        }
        SwitchToThread();
    }
    return snapshot;
}

} // namespace safevst3

#endif
