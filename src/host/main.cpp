#ifdef _WIN32

#include "host/vst3_engine.hpp"
#include "platform/windows/win_ipc.hpp"

#include <avrt.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

#pragma comment(lib, "Avrt.lib")

namespace {
std::string narrow(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::map<std::wstring, std::wstring> parse_args(int argc, wchar_t** argv)
{
    std::map<std::wstring, std::wstring> values;
    for (int i = 1; i + 1 < argc; i += 2)
        values[argv[i]] = argv[i + 1];
    return values;
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

void copy_text(char* destination, std::size_t capacity, const std::string& value)
{
    if (!destination || capacity == 0)
        return;
    const std::size_t count = std::min(capacity - 1, value.size());
    std::memcpy(destination, value.data(), count);
    destination[count] = '\0';
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace safevst3;
    const auto args = parse_args(argc, argv);
    const auto get = [&](const wchar_t* key) -> std::wstring {
        auto it = args.find(key);
        return it == args.end() ? std::wstring{} : it->second;
    };

    BridgeNames names{get(L"--mapping"), get(L"--request-event"), get(L"--response-event"), get(L"--ready-event")};
    const std::wstring vst_path_w = get(L"--vst");
    const std::string class_id = narrow(get(L"--class-id"));

    WinHostEndpoint endpoint;
    std::string error;
    if (!endpoint.open(names, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    auto* region = endpoint.region();
    Vst3Engine engine;
    if (!engine.open(narrow(vst_path_w), class_id, region->sample_rate, region->channels, error)) {
        region->last_error = 1;
        InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Error));
        SetEvent(endpoint.ready_event());
        std::cerr << "VST3 init failed: " << error << '\n';
        return 3;
    }

    const auto& parameters = engine.parameters();
    region->parameter_total_count = static_cast<std::uint32_t>(parameters.size());
    region->parameter_count = static_cast<std::uint32_t>(std::min<std::size_t>(parameters.size(), kMaxParameters));
    for (std::uint32_t i = 0; i < region->parameter_count; ++i) {
        const auto& source = parameters[i];
        auto& destination = region->parameters[i];
        destination.id = source.id;
        destination.step_count = source.step_count;
        destination.flags = source.flags;
        destination.default_normalized = source.default_normalized;
        destination.current_value_bits = double_to_bits(source.current_normalized);
        destination.pending_value_bits = destination.current_value_bits;
        destination.pending_generation = 0;
        destination.applied_generation = 0;
        copy_text(destination.title, kParameterTitleBytes, source.title);
        copy_text(destination.units, kParameterUnitsBytes, source.units);
    }
    MemoryBarrier();

    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    if (mmcss)
        AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::Ready));
    SetEvent(endpoint.ready_event());

    while (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) == 0) {
        const DWORD wait = WaitForSingleObject(endpoint.request_event(), INFINITE);
        if (wait != WAIT_OBJECT_0)
            break;
        if (InterlockedCompareExchange(&region->shutdown_requested, 0, 0) != 0)
            break;

        bool parameter_edits = false;
        for (std::uint32_t i = 0; i < region->parameter_count; ++i) {
            auto& descriptor = region->parameters[i];
            const long generation = InterlockedCompareExchange(&descriptor.pending_generation, 0, 0);
            const long applied = InterlockedCompareExchange(&descriptor.applied_generation, 0, 0);
            if (generation == applied)
                continue;

            const auto raw_bits = InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64*>(&descriptor.pending_value_bits), 0, 0);
            const double value = bits_to_double(static_cast<std::int64_t>(raw_bits));
            if (engine.queue_parameter(descriptor.id, value)) {
                const auto normalized_bits = static_cast<LONG64>(double_to_bits(
                    normalize_parameter_value(value, descriptor.step_count)));
                InterlockedExchange64(
                    reinterpret_cast<volatile LONG64*>(&descriptor.current_value_bits), normalized_bits);
                parameter_edits = true;
            }
            InterlockedExchange(&descriptor.applied_generation, generation);
        }

        bool processed_any = false;
        for (std::uint32_t i = 0; i < kSlotCount; ++i) {
            auto& slot = region->slots[i];
            if (InterlockedCompareExchange(&slot.state,
                                           static_cast<long>(SlotState::Processing),
                                           static_cast<long>(SlotState::Ready)) != static_cast<long>(SlotState::Ready))
                continue;

            processed_any = true;
            const bool ok = engine.process(slot);
            InterlockedExchange(&slot.result, static_cast<long>(ok ? ProcessResult::Ok : ProcessResult::VstProcessError));
            MemoryBarrier();
            InterlockedExchange(&slot.state, static_cast<long>(SlotState::Done));
            SetEvent(endpoint.response_event());
        }

        // VST3 requires hosts to flush controller-to-processor parameter edits
        // even when no normal audio block is running. This zero-sample process
        // keeps generic controls synchronized while an OBS source is inactive.
        if (parameter_edits && !processed_any && !engine.flush_parameter_changes())
            region->last_error = 2;

        if (!processed_any && !parameter_edits)
            Sleep(0);
    }

    InterlockedExchange(&region->host_status, static_cast<long>(HostStatus::ShuttingDown));
    engine.close();
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    return 0;
}

#endif
