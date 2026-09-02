#ifdef _WIN32

#include "host/hosted_plugin.hpp"
#include "host/process_block_view.hpp"
#include "rack/rack_protocol.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {
using safevst3::HostedPlugin;
using safevst3::ProcessBlockView;
using safevst3::kMaxChannels;
using safevst3::kMaxFrames;
using safevst3::rack::RackHostStatus;
using safevst3::rack::RackProcessResult;
using safevst3::rack::RackSharedAudioRegion;

struct Options {
    std::wstring mapping;
    std::wstring request_event;
    std::wstring response_event;
    std::wstring ready_event;
    std::string plugin_a;
    std::string plugin_b;
};

std::string narrow(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

bool parse_options(int argc, wchar_t** argv, Options& options)
{
    auto take = [&](int& index, std::wstring& destination) {
        if (index + 1 >= argc)
            return false;
        destination = argv[++index];
        return true;
    };

    std::wstring plugin_a;
    std::wstring plugin_b;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--mapping") {
            if (!take(i, options.mapping)) return false;
        } else if (arg == L"--request-event") {
            if (!take(i, options.request_event)) return false;
        } else if (arg == L"--response-event") {
            if (!take(i, options.response_event)) return false;
        } else if (arg == L"--ready-event") {
            if (!take(i, options.ready_event)) return false;
        } else if (arg == L"--plugin-a") {
            if (!take(i, plugin_a)) return false;
        } else if (arg == L"--plugin-b") {
            if (!take(i, plugin_b)) return false;
        } else {
            return false;
        }
    }
    if (options.mapping.empty() || options.request_event.empty() || options.response_event.empty() ||
        options.ready_event.empty() || plugin_a.empty() || plugin_b.empty())
        return false;
    options.plugin_a = narrow(plugin_a);
    options.plugin_b = narrow(plugin_b);
    return true;
}

struct Endpoint {
    HANDLE mapping = nullptr;
    HANDLE request = nullptr;
    HANDLE response = nullptr;
    HANDLE ready = nullptr;
    RackSharedAudioRegion* region = nullptr;

    ~Endpoint()
    {
        if (region) UnmapViewOfFile(region);
        if (ready) CloseHandle(ready);
        if (response) CloseHandle(response);
        if (request) CloseHandle(request);
        if (mapping) CloseHandle(mapping);
    }

    bool open(const Options& options)
    {
        mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, options.mapping.c_str());
        if (!mapping) return false;
        region = static_cast<RackSharedAudioRegion*>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RackSharedAudioRegion)));
        if (!region) return false;
        request = OpenEventW(SYNCHRONIZE, FALSE, options.request_event.c_str());
        response = OpenEventW(EVENT_MODIFY_STATE, FALSE, options.response_event.c_str());
        ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, options.ready_event.c_str());
        return request && response && ready;
    }
};

struct RackBuffers {
    alignas(64) std::array<std::array<float, kMaxFrames>, kMaxChannels> ping{};
    alignas(64) std::array<std::array<float, kMaxFrames>, kMaxChannels> pong{};
};

void publish_result(RackSharedAudioRegion& region, HANDLE response, long generation,
                    RackProcessResult result) noexcept
{
    InterlockedExchange(&region.process_result, static_cast<long>(result));
    MemoryBarrier();
    InterlockedExchange(&region.response_generation, generation);
    MemoryBarrier();
    SetEvent(response);
}

void dsp_loop(Endpoint& endpoint, HostedPlugin& plugin_a, HostedPlugin& plugin_b) noexcept
{
    RackBuffers buffers;
    float* rack_input[kMaxChannels]{};
    float* ping[kMaxChannels]{};
    float* pong[kMaxChannels]{};

    for (std::uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        rack_input[ch] = endpoint.region->input[ch];
        ping[ch] = buffers.ping[ch].data();
        pong[ch] = buffers.pong[ch].data();
    }

    for (;;) {
        const DWORD wait = WaitForSingleObject(endpoint.request, 100);
        if (InterlockedCompareExchange(&endpoint.region->shutdown_requested, 0, 0) != 0)
            break;
        if (wait != WAIT_OBJECT_0)
            continue;

        const long generation = InterlockedCompareExchange(&endpoint.region->request_generation, 0, 0);
        const std::uint32_t frames = endpoint.region->frames;
        const std::uint32_t channels = endpoint.region->block_channels;
        const std::uint64_t sequence = endpoint.region->sequence;
        if (frames == 0 || frames > kMaxFrames || channels == 0 || channels > kMaxChannels ||
            channels != endpoint.region->channels) {
            publish_result(*endpoint.region, endpoint.response, generation, RackProcessResult::InvalidBlock);
            continue;
        }

        ProcessBlockView block_a{rack_input, ping, channels, frames, sequence};
        if (!plugin_a.process(block_a)) {
            publish_result(*endpoint.region, endpoint.response, generation, RackProcessResult::PluginAError);
            continue;
        }

        ProcessBlockView block_b{ping, pong, channels, frames, sequence};
        if (!plugin_b.process(block_b)) {
            publish_result(*endpoint.region, endpoint.response, generation, RackProcessResult::PluginBError);
            continue;
        }

        for (std::uint32_t ch = 0; ch < channels; ++ch) {
            for (std::uint32_t frame = 0; frame < frames; ++frame)
                endpoint.region->output[ch][frame] = buffers.pong[ch][frame];
        }
        publish_result(*endpoint.region, endpoint.response, generation, RackProcessResult::Ok);
    }
}

int run(const Options& options)
{
    Endpoint endpoint;
    if (!endpoint.open(options)) {
        std::cerr << "R1-1 Rack helper could not open transport\n";
        return 3;
    }
    if (endpoint.region->magic != safevst3::rack::kRackProtocolMagic ||
        endpoint.region->version != safevst3::rack::kRackProtocolVersion ||
        endpoint.region->sample_rate == 0 || endpoint.region->channels == 0 ||
        endpoint.region->channels > kMaxChannels) {
        InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
        SetEvent(endpoint.ready);
        return 4;
    }

    HostedPlugin plugin_a;
    HostedPlugin plugin_b;
    std::string error;
    if (!plugin_a.open(options.plugin_a, "", endpoint.region->sample_rate,
                       endpoint.region->channels, nullptr, error)) {
        std::cerr << "Gain A open failed: " << error << '\n';
        InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
        SetEvent(endpoint.ready);
        return 5;
    }
    error.clear();
    if (!plugin_b.open(options.plugin_b, "", endpoint.region->sample_rate,
                       endpoint.region->channels, nullptr, error)) {
        std::cerr << "Gain B open failed: " << error << '\n';
        InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Error));
        SetEvent(endpoint.ready);
        return 6;
    }

    InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::Ready));
    MemoryBarrier();
    SetEvent(endpoint.ready);

    std::thread dsp([&] { dsp_loop(endpoint, plugin_a, plugin_b); });
    dsp.join();

    InterlockedExchange(&endpoint.region->host_status, static_cast<long>(RackHostStatus::ShuttingDown));
    plugin_b.close();
    plugin_a.close();
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "usage: obs-safe-vst3-rack-host --mapping <name> --request-event <name> "
                     "--response-event <name> --ready-event <name> --plugin-a <vst3> --plugin-b <vst3>\n";
        return 2;
    }
    return run(options);
}

#else
int main() { return 0; }
#endif
