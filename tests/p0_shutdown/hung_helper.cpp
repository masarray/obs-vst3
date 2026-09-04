#ifdef _WIN32

#include "rack/rack_protocol.hpp"

#include <windows.h>

#include <string>

namespace {

std::wstring arg_value(int argc, wchar_t** argv, const wchar_t* key)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == key)
            return argv[i + 1];
    }
    return {};
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const std::wstring mapping_name = arg_value(argc, argv, L"--mapping");
    const std::wstring ready_name = arg_value(argc, argv, L"--ready-event");
    if (mapping_name.empty() || ready_name.empty())
        return 2;

    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping_name.c_str());
    if (!mapping)
        return 3;

    auto* region = static_cast<safevst3::rack::RackSharedAudioRegion*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0,
        sizeof(safevst3::rack::RackSharedAudioRegion)));
    if (!region) {
        CloseHandle(mapping);
        return 4;
    }

    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, ready_name.c_str());
    if (!ready) {
        UnmapViewOfFile(region);
        CloseHandle(mapping);
        return 5;
    }

    if (region->magic != safevst3::rack::kRackProtocolMagic ||
        region->version != safevst3::rack::kRackProtocolVersion) {
        CloseHandle(ready);
        UnmapViewOfFile(region);
        CloseHandle(mapping);
        return 6;
    }

    InterlockedExchange(&region->host_status,
                        static_cast<long>(safevst3::rack::RackHostStatus::Ready));
    SetEvent(ready);

    // Deliberately ignore shutdown_requested forever. The parent bridge must
    // enforce its bounded grace period and terminate this isolated helper.
    for (;;)
        Sleep(1000);
}

#else
int main() { return 0; }
#endif
