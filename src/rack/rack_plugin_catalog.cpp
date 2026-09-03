#ifdef _WIN32

#include "rack/rack_plugin_catalog.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <string_view>

namespace safevst3::rack::ui {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr ULONGLONG kScannerTimeoutMs = 180000;

std::uint64_t fnv1a_append(std::uint64_t hash, std::string_view text) noexcept
{
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

RackCatalogEntryId catalog_entry_id(std::string_view path, std::string_view class_id) noexcept
{
    std::uint64_t hash = fnv1a_append(kFnvOffset, path);
    hash ^= 0xffu;
    hash *= kFnvPrime;
    hash = fnv1a_append(hash, class_id);
    return hash == 0 ? 1 : hash;
}

template <std::size_t N>
void copy_text(std::array<char, N>& destination, std::string_view source) noexcept
{
    destination.fill('\0');
    if constexpr (N > 1) {
        const std::size_t count = std::min<std::size_t>(source.size(), N - 1);
        std::copy_n(source.data(), count, destination.data());
    }
}

bool split_tsv_line(const std::string& line, std::string& name,
                    std::string& path, std::string& class_id) noexcept
{
    const std::size_t first = line.find('\t');
    const std::size_t second = first == std::string::npos
                                   ? std::string::npos
                                   : line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos)
        return false;
    name = line.substr(0, first);
    path = line.substr(first + 1, second - first - 1);
    class_id = line.substr(second + 1);
    return !name.empty() && !path.empty();
}

std::wstring quote(const std::wstring& value)
{
    std::wstring out = L"\"";
    for (const wchar_t ch : value) {
        if (ch == L'\"')
            out += L'\\';
        out += ch;
    }
    out += L'\"';
    return out;
}

} // namespace

RackPluginCatalog::RackPluginCatalog() noexcept
{
    snapshot_.version = kRackCatalogSnapshotVersion;
    snapshot_.generation = 1;
}

const RackPluginCatalogRecord* RackPluginCatalog::resolve(
    std::uint64_t generation, RackCatalogEntryId entry_id) const noexcept
{
    if (generation == 0 || generation != snapshot_.generation || entry_id == 0)
        return nullptr;
    for (std::uint32_t i = 0; i < snapshot_.entry_count; ++i) {
        if (records_[i].entry_id == entry_id)
            return &records_[i];
    }
    return nullptr;
}

bool RackPluginCatalog::load_cache(const std::filesystem::path& cache_path) noexcept
{
    try {
        std::ifstream in(cache_path, std::ios::binary);
        if (!in)
            return false;

        std::array<RackPluginCatalogRecord, kRackCatalogMaxEntries> candidate_records{};
        PluginCatalogSnapshot candidate{};
        candidate.generation = snapshot_.generation + 1;
        if (candidate.generation == 0)
            candidate.generation = 1;
        candidate.scanning = snapshot_.scanning;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;
            if (candidate.entry_count >= kRackCatalogMaxEntries)
                break;

            std::string name;
            std::string path;
            std::string class_id;
            if (!split_tsv_line(line, name, path, class_id))
                continue;

            const RackCatalogEntryId id = catalog_entry_id(path, class_id);
            bool duplicate_identity = false;
            bool hash_collision = false;
            for (std::uint32_t i = 0; i < candidate.entry_count; ++i) {
                if (candidate_records[i].entry_id != id)
                    continue;
                duplicate_identity = candidate_records[i].path == path &&
                                     candidate_records[i].class_id == class_id;
                hash_collision = !duplicate_identity;
                break;
            }
            if (duplicate_identity || hash_collision)
                continue;

            auto& record = candidate_records[candidate.entry_count];
            record.entry_id = id;
            record.name = name;
            record.path = path;
            record.class_id = class_id;
            record.category = "Effect";

            auto& entry = candidate.entries[candidate.entry_count];
            entry.entry_id = id;
            copy_text(entry.name, record.name);
            copy_text(entry.vendor, record.vendor);
            copy_text(entry.category, record.category);
            ++candidate.entry_count;
        }

        if (!validate_plugin_catalog_snapshot(candidate))
            return false;
        records_ = std::move(candidate_records);
        snapshot_ = candidate;
        return true;
    } catch (...) {
        return false;
    }
}

void RackPluginCatalog::set_scanning(bool scanning) noexcept
{
    if (snapshot_.scanning == scanning)
        return;
    snapshot_.scanning = scanning;
    ++snapshot_.generation;
    if (snapshot_.generation == 0)
        snapshot_.generation = 1;
}

std::filesystem::path rack_catalog_cache_path()
{
    wchar_t buffer[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer))
        return {};
    return std::filesystem::path(std::wstring(buffer, length)) /
           L"OBS Safe VST3 Host" / L"plugins.tsv";
}

std::filesystem::path rack_scanner_path()
{
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer))
        return {};
    return std::filesystem::path(std::wstring(buffer, length)).parent_path() /
           L"obs-safe-vst3-scanner.exe";
}

bool run_rack_scanner(const std::filesystem::path& scanner,
                      const std::filesystem::path& cache,
                      std::stop_token stop) noexcept
{
    try {
        if (scanner.empty() || cache.empty() || !std::filesystem::exists(scanner))
            return false;
        std::error_code ec;
        std::filesystem::create_directories(cache.parent_path(), ec);

        std::wstring command = quote(scanner.wstring()) + L" --scan-to " + quote(cache.wstring());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(scanner.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, scanner.parent_path().c_str(),
                            &startup, &process))
            return false;

        const ULONGLONG deadline = GetTickCount64() + kScannerTimeoutMs;
        DWORD wait = WAIT_TIMEOUT;
        while (!stop.stop_requested()) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                break;
            const DWORD slice = static_cast<DWORD>(
                std::min<ULONGLONG>(250, deadline - now));
            wait = WaitForSingleObject(process.hProcess, slice);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED)
                break;
        }

        DWORD exit_code = 1;
        if (stop.stop_requested() || wait != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess,
                             stop.stop_requested() ? ERROR_CANCELLED : 0xDEAD);
            (void)WaitForSingleObject(process.hProcess, 2000);
        } else {
            GetExitCodeProcess(process.hProcess, &exit_code);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return !stop.stop_requested() && wait == WAIT_OBJECT_0 && exit_code == 0;
    } catch (...) {
        return false;
    }
}

} // namespace safevst3::rack::ui

#endif
