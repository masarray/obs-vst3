#ifdef _WIN32

#include "platform/windows/win_ipc.hpp"

#include "common/coherent_generation.hpp"
#include "common/parameter_utils.hpp"
#include "common/startup_error.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sstream>

namespace safevst3 {

namespace {
constexpr ULONGLONG kHelperStartupTimeoutMs = 5000;

std::string win_error(const char* what)
{
    const DWORD code = GetLastError();
    std::ostringstream os;
    os << what << " failed (Win32 error " << code << ")";
    return os.str();
}

bool process_alive(const PROCESS_INFORMATION& pi) noexcept
{
    if (!pi.hProcess)
        return false;
    DWORD code = 0;
    return GetExitCodeProcess(pi.hProcess, &code) && code == STILL_ACTIVE;
}

std::int64_t double_to_bits(double value) noexcept
{
    std::int64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double bits_to_double(std::int64_t bits) noexcept
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string bounded_string(const char* value, std::size_t capacity)
{
    if (!value || capacity == 0)
        return {};
    std::size_t length = 0;
    while (length < capacity && value[length] != '\0')
        ++length;
    return std::string(value, length);
}
} // namespace

WinObsBridge::~WinObsBridge() { stop(); }

std::wstring WinObsBridge::widen(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring WinObsBridge::quote(const std::wstring& value)
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

BridgeNames WinObsBridge::make_names()
{
    static std::atomic<unsigned long long> counter{0};
    const auto pid = static_cast<unsigned long long>(GetCurrentProcessId());
    const auto tick = static_cast<unsigned long long>(GetTickCount64());
    const auto serial = counter.fetch_add(1, std::memory_order_relaxed);
    std::wstringstream ss;
    ss << L"Local\\obs-safe-vst3-" << pid << L'-' << tick << L'-' << serial;
    const auto base = ss.str();
    return {base + L"-map", base + L"-state-map", base + L"-req", base + L"-dsp",
            base + L"-rsp", base + L"-ready", base + L"-state-done"};
}

bool WinObsBridge::start(const std::filesystem::path& helper,
                         const std::filesystem::path& vst_path,
                         const std::string& class_id,
                         std::uint32_t sample_rate,
                         std::uint32_t channels,
                         std::string& error,
                         std::stop_token cancel)
{
    stop();
    if (cancel.stop_requested()) {
        error = "VST3 helper startup cancelled";
        return false;
    }
    if (channels == 0 || channels > kMaxChannels) {
        error = "Public preview supports mono/stereo only";
        return false;
    }

    names_ = make_names();
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(sizeof(SharedAudioRegion)), names_.mapping.c_str());
    if (!mapping_) {
        error = win_error("CreateFileMappingW");
        stop();
        return false;
    }

    region_ = static_cast<SharedAudioRegion*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedAudioRegion)));
    if (!region_) {
        error = win_error("MapViewOfFile");
        stop();
        return false;
    }
    std::memset(region_, 0, sizeof(*region_));
    region_->magic = kProtocolMagic;
    region_->version = kProtocolVersion;
    region_->sample_rate = sample_rate;
    region_->channels = channels;
    region_->max_frames = kMaxFrames;
    region_->slot_count = kSlotCount;
    region_->parameter_count = 0;
    region_->parameter_total_count = 0;
    region_->host_status = static_cast<long>(HostStatus::Booting);
    region_->last_error = static_cast<long>(StartupErrorCode::None);
    region_->state_command = static_cast<long>(StateCommand::None);
    region_->state_status = static_cast<long>(StateStatus::Idle);
    for (auto& slot : region_->slots)
        slot.state = static_cast<long>(SlotState::Free);

    state_mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(sizeof(StateTransferRegion)), names_.state_mapping.c_str());
    if (!state_mapping_) {
        error = win_error("CreateFileMappingW(state)");
        stop();
        return false;
    }
    state_region_ = static_cast<StateTransferRegion*>(
        MapViewOfFile(state_mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(StateTransferRegion)));
    if (!state_region_) {
        error = win_error("MapViewOfFile(state)");
        stop();
        return false;
    }
    state_region_->magic = kStateTransferMagic;
    state_region_->version = kStateTransferVersion;
    state_region_->capacity = static_cast<std::uint32_t>(kMaxStateBytes);
    state_region_->reserved = 0;

    request_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.request_event.c_str());
    dsp_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.dsp_event.c_str());
    response_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.response_event.c_str());
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, names_.ready_event.c_str());
    state_event_ = CreateEventW(nullptr, FALSE, FALSE, names_.state_event.c_str());
    if (!request_event_ || !dsp_event_ || !response_event_ || !ready_event_ || !state_event_) {
        error = win_error("CreateEventW");
        stop();
        return false;
    }

    if (cancel.stop_requested()) {
        error = "VST3 helper startup cancelled";
        stop();
        return false;
    }

    std::wstring command = quote(helper.wstring()) +
        L" --mapping " + quote(names_.mapping) +
        L" --state-mapping " + quote(names_.state_mapping) +
        L" --request-event " + quote(names_.request_event) +
        L" --dsp-event " + quote(names_.dsp_event) +
        L" --response-event " + quote(names_.response_event) +
        L" --ready-event " + quote(names_.ready_event) +
        L" --state-event " + quote(names_.state_event) +
        L" --vst " + quote(vst_path.wstring());
    if (!class_id.empty())
        command += L" --class-id " + quote(widen(class_id));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring mutable_command = command;
    if (!CreateProcessW(helper.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, helper.parent_path().c_str(), &si, &process_)) {
        error = win_error("CreateProcessW");
        stop();
        return false;
    }
    SetPriorityClass(process_.hProcess, HIGH_PRIORITY_CLASS);
    const ULONGLONG startup_started = GetTickCount64();
    ResumeThread(process_.hThread);

    const ULONGLONG deadline = startup_started + kHelperStartupTimeoutMs;
    HANDLE wait_handles[] = {ready_event_, process_.hProcess};
    bool ready_signaled = false;
    while (true) {
        if (cancel.stop_requested()) {
            error = "VST3 helper startup cancelled";
            abort();
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 50));
        const DWORD wait = WaitForMultipleObjects(2, wait_handles, FALSE, remaining);
        if (wait == WAIT_OBJECT_0) {
            ready_signaled = true;
            break;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            const long phase = region_
                ? InterlockedCompareExchange(&region_->last_error, 0, 0)
                : static_cast<long>(StartupErrorCode::None);
            DWORD exit_code = 0xFFFFFFFFu;
            if (process_.hProcess)
                (void)GetExitCodeProcess(process_.hProcess, &exit_code);
            error = format_startup_process_exit(phase, exit_code);
            stop();
            return false;
        }
        if (wait == WAIT_FAILED) {
            error = win_error("WaitForMultipleObjects");
            stop();
            return false;
        }
    }

    if (cancel.stop_requested()) {
        error = "VST3 helper startup cancelled";
        abort();
        return false;
    }
    if (!ready_signaled) {
        const long phase = region_
            ? InterlockedCompareExchange(&region_->last_error, 0, 0)
            : static_cast<long>(StartupErrorCode::None);
        const ULONGLONG now = GetTickCount64();
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            now >= startup_started ? now - startup_started : kHelperStartupTimeoutMs);
        error = format_startup_timeout(phase, elapsed);
        stop();
        return false;
    }
    if (region_->host_status != static_cast<long>(HostStatus::Ready)) {
        std::ostringstream os;
        os << "VST3 helper did not become ready";
        if (region_) {
            const long host_error = InterlockedCompareExchange(&region_->last_error, 0, 0);
            const char* phase = startup_error_phase(static_cast<StartupErrorCode>(host_error));
            if (phase)
                os << " (phase=" << phase << ", host error " << host_error << ')';
            else
                os << " (host error " << host_error << ')';
        }
        error = os.str();
        stop();
        return false;
    }

    // During Booting, last_error doubles as a crash-safe startup breadcrumb.
    // Clear it only after Ready so runtime watchdog/control errors retain the
    // existing meaning and a successful helper never exposes a stale phase.
    InterlockedExchange(&region_->last_error, static_cast<long>(StartupErrorCode::None));
    return true;
}

void WinObsBridge::stop() noexcept
{
    if (region_) {
        InterlockedExchange(&region_->shutdown_requested, 1);
        if (request_event_)
            SetEvent(request_event_);
        if (dsp_event_)
            SetEvent(dsp_event_);
    }
    if (process_.hProcess) {
        const DWORD wait = WaitForSingleObject(process_.hProcess, 1500);
        if (wait == WAIT_TIMEOUT)
            TerminateProcess(process_.hProcess, 0xDEAD);
    }
    if (process_.hThread) CloseHandle(process_.hThread);
    if (process_.hProcess) CloseHandle(process_.hProcess);
    process_ = {};
    if (state_region_) UnmapViewOfFile(state_region_);
    state_region_ = nullptr;
    if (region_) UnmapViewOfFile(region_);
    region_ = nullptr;
    if (state_event_) CloseHandle(state_event_);
    if (ready_event_) CloseHandle(ready_event_);
    if (response_event_) CloseHandle(response_event_);
    if (dsp_event_) CloseHandle(dsp_event_);
    if (request_event_) CloseHandle(request_event_);
    if (state_mapping_) CloseHandle(state_mapping_);
    if (mapping_) CloseHandle(mapping_);
    state_event_ = ready_event_ = response_event_ = dsp_event_ = request_event_ = nullptr;
    state_mapping_ = mapping_ = nullptr;
}

void WinObsBridge::abort() noexcept
{
    if (process_.hProcess)
        TerminateProcess(process_.hProcess, ERROR_CANCELLED);
    stop();
}

bool WinObsBridge::running() const noexcept
{
    return region_ && region_->host_status == static_cast<long>(HostStatus::Ready) && process_alive(process_);
}

std::vector<ParameterSnapshot> WinObsBridge::parameters() const
{
    std::vector<ParameterSnapshot> result;
    if (!running() || !region_)
        return result;

    const auto read_generation = [&]() noexcept {
        return static_cast<std::uint32_t>(InterlockedCompareExchange(
            &region_->parameter_catalog_generation, 0, 0));
    };
    const bool coherent = read_coherent_generation(
        result, read_generation,
        [&](std::vector<ParameterSnapshot>& candidate) {
            const std::uint32_t count = std::min(region_->parameter_count, kMaxParameters);
            candidate.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                auto& descriptor = region_->parameters[i];
                const auto bits = InterlockedCompareExchange64(
                    reinterpret_cast<volatile LONG64*>(&descriptor.current_value_bits), 0, 0);

                ParameterSnapshot snapshot{};
                snapshot.id = descriptor.id;
                snapshot.step_count = descriptor.step_count;
                snapshot.flags = descriptor.flags;
                snapshot.default_normalized = descriptor.default_normalized;
                snapshot.current_normalized = bits_to_double(static_cast<std::int64_t>(bits));
                snapshot.title = bounded_string(descriptor.title, kParameterTitleBytes);
                snapshot.units = bounded_string(descriptor.units, kParameterUnitsBytes);
                candidate.push_back(std::move(snapshot));
            }
            MemoryBarrier();
            return true;
        },
        3);
    if (!coherent)
        result.clear();
    return result;
}

bool WinObsBridge::set_parameter(std::uint32_t id, double normalized) noexcept
{
    if (!running() || !region_ || !request_event_)
        return false;

    for (std::size_t attempt = 0; attempt < 3; ++attempt) {
        const long stable = InterlockedCompareExchange(
            &region_->parameter_catalog_generation, 0, 0);
        if ((stable & 1L) != 0) {
            SwitchToThread();
            continue;
        }

        // Claim the same odd generation used by helper-side catalog publication.
        // Once claimed, metadata cannot be replaced until this bounded edit has
        // either been queued or rejected, so a stale edit cannot leak into a new
        // descriptor topology.
        if (InterlockedCompareExchange(
                &region_->parameter_catalog_generation, stable + 1, stable) != stable) {
            SwitchToThread();
            continue;
        }

        bool accepted = false;
        const std::uint32_t count = std::min(region_->parameter_count, kMaxParameters);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& descriptor = region_->parameters[i];
            if (descriptor.id != id)
                continue;
            if ((descriptor.flags & (ParameterReadOnly | ParameterHidden)) == 0) {
                const double value = normalize_parameter_value(normalized, descriptor.step_count);
                const auto bits = static_cast<LONG64>(double_to_bits(value));
                InterlockedExchange64(
                    reinterpret_cast<volatile LONG64*>(&descriptor.pending_value_bits), bits);
                MemoryBarrier();
                InterlockedIncrement(&descriptor.pending_generation);
                accepted = true;
            }
            break;
        }

        MemoryBarrier();
        InterlockedExchange(&region_->parameter_catalog_generation, stable + 2);
        if (accepted)
            SetEvent(request_event_);
        return accepted;
    }
    return false;
}

std::uint32_t WinObsBridge::parameter_total_count() const noexcept
{
    if (!region_)
        return 0;
    std::uint32_t result = 0;
    const auto read_generation = [&]() noexcept {
        return static_cast<std::uint32_t>(InterlockedCompareExchange(
            &region_->parameter_catalog_generation, 0, 0));
    };
    if (!read_coherent_generation(
            result, read_generation,
            [&](std::uint32_t& candidate) {
                candidate = region_->parameter_total_count;
                MemoryBarrier();
                return true;
            },
            3))
        return 0;
    return result;
}

AudioSlot* WinObsBridge::acquire_slot() noexcept
{
    if (!region_)
        return nullptr;
    for (std::uint32_t i = 0; i < kSlotCount; ++i) {
        auto& slot = region_->slots[i];
        const long state = slot.state;
        if ((state == static_cast<long>(SlotState::Free) || state == static_cast<long>(SlotState::Done)) &&
            InterlockedCompareExchange(&slot.state, static_cast<long>(SlotState::Claimed), state) == state)
            return &slot;
    }
    return nullptr;
}

std::uint64_t WinObsBridge::qpc_now() noexcept
{
    LARGE_INTEGER v{};
    QueryPerformanceCounter(&v);
    return static_cast<std::uint64_t>(v.QuadPart);
}

std::uint64_t WinObsBridge::qpc_frequency() noexcept
{
    static const std::uint64_t f = [] {
        LARGE_INTEGER v{};
        QueryPerformanceFrequency(&v);
        return static_cast<std::uint64_t>(v.QuadPart);
    }();
    return f;
}

bool WinObsBridge::process(float* const* channels,
                           std::uint32_t channel_count,
                           std::uint32_t frames,
                           double deadline_fraction) noexcept
{
    if (!running() || !channels || channel_count == 0 || channel_count > kMaxChannels || frames == 0 || frames > kMaxFrames)
        return false;

    AudioSlot* slot = acquire_slot();
    if (!slot) {
        ++deadline_misses_;
        return false;
    }

    const std::uint32_t seq = next_sequence_++;
    slot->sequence = seq;
    slot->frames = frames;
    slot->channels = channel_count;
    slot->result = static_cast<long>(ProcessResult::Ok);
    for (std::uint32_t ch = 0; ch < channel_count; ++ch)
        std::memcpy(slot->input[ch], channels[ch], sizeof(float) * frames);

    MemoryBarrier();
    InterlockedExchange(&slot->state, static_cast<long>(SlotState::Ready));
    SetEvent(dsp_event_);

    deadline_fraction = std::clamp(deadline_fraction, 0.10, 0.95);
    const double block_seconds = static_cast<double>(frames) / static_cast<double>(region_->sample_rate);
    const double budget_seconds = std::clamp(block_seconds * deadline_fraction, 0.0005, 0.010);
    const std::uint64_t freq = qpc_frequency();
    const std::uint64_t start = qpc_now();
    const std::uint64_t deadline_ticks = static_cast<std::uint64_t>(budget_seconds * static_cast<double>(freq));

    while (true) {
        const long state = slot->state;
        if (state == static_cast<long>(SlotState::Done)) {
            MemoryBarrier();
            if (slot->sequence == seq && slot->result == static_cast<long>(ProcessResult::Ok)) {
                for (std::uint32_t ch = 0; ch < channel_count; ++ch)
                    std::memcpy(channels[ch], slot->output[ch], sizeof(float) * frames);
                InterlockedExchange(&slot->state, static_cast<long>(SlotState::Free));
                return true;
            }
            InterlockedExchange(&slot->state, static_cast<long>(SlotState::Free));
            return false;
        }

        const std::uint64_t elapsed = qpc_now() - start;
        if (elapsed >= deadline_ticks)
            break;

        const double remaining_ms = (static_cast<double>(deadline_ticks - elapsed) * 1000.0) / static_cast<double>(freq);
        DWORD wait_ms = static_cast<DWORD>(std::max(1.0, std::min(remaining_ms, 2.0)));
        WaitForSingleObject(response_event_, wait_ms);
    }

    ++deadline_misses_;
    InterlockedCompareExchange(&slot->state, static_cast<long>(SlotState::Free), static_cast<long>(SlotState::Ready));
    return false;
}

WinHostEndpoint::~WinHostEndpoint() { close(); }

bool WinHostEndpoint::open(const BridgeNames& names, std::string& error)
{
    close();
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, names.mapping.c_str());
    if (!mapping_) { error = win_error("OpenFileMappingW"); return false; }
    region_ = static_cast<SharedAudioRegion*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedAudioRegion)));
    if (!region_) { error = win_error("MapViewOfFile"); close(); return false; }

    state_mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, names.state_mapping.c_str());
    if (!state_mapping_) { error = win_error("OpenFileMappingW(state)"); close(); return false; }
    state_region_ = static_cast<StateTransferRegion*>(
        MapViewOfFile(state_mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(StateTransferRegion)));
    if (!state_region_) { error = win_error("MapViewOfFile(state)"); close(); return false; }

    request_event_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, names.request_event.c_str());
    dsp_event_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, names.dsp_event.c_str());
    response_event_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, names.response_event.c_str());
    ready_event_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, names.ready_event.c_str());
    state_event_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, names.state_event.c_str());
    if (!request_event_ || !dsp_event_ || !response_event_ || !ready_event_ || !state_event_) {
        error = win_error("OpenEventW");
        close();
        return false;
    }
    if (region_->magic != kProtocolMagic || region_->version != kProtocolVersion) {
        error = "Shared-memory protocol mismatch";
        close();
        return false;
    }
    if (state_region_->magic != kStateTransferMagic ||
        state_region_->version != kStateTransferVersion ||
        state_region_->capacity != kMaxStateBytes) {
        error = "VST3 state-transfer protocol mismatch";
        close();
        return false;
    }
    return true;
}

void WinHostEndpoint::close() noexcept
{
    if (state_region_) UnmapViewOfFile(state_region_);
    state_region_ = nullptr;
    if (region_) UnmapViewOfFile(region_);
    region_ = nullptr;
    if (state_event_) CloseHandle(state_event_);
    if (ready_event_) CloseHandle(ready_event_);
    if (response_event_) CloseHandle(response_event_);
    if (dsp_event_) CloseHandle(dsp_event_);
    if (request_event_) CloseHandle(request_event_);
    if (state_mapping_) CloseHandle(state_mapping_);
    if (mapping_) CloseHandle(mapping_);
    state_event_ = ready_event_ = response_event_ = dsp_event_ = request_event_ = nullptr;
    state_mapping_ = mapping_ = nullptr;
}

} // namespace safevst3

#endif
