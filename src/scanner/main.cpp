#ifdef _WIN32

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

enum class ProbeOutcome {
    AudioEffects,
    NoAudioEffects,
    Failed,
};

ProbeOutcome classify_probe_exit(DWORD exit_code) noexcept
{
    if (exit_code == 0)
        return ProbeOutcome::AudioEffects;
    if (exit_code == 5)
        return ProbeOutcome::NoAudioEffects;
    return ProbeOutcome::Failed;
}

std::wstring quote(const std::wstring& value)
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

std::string utf8(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string sanitize(std::string value)
{
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

std::wstring env_w(const wchar_t* name)
{
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (!needed)
        return {};
    std::wstring value(needed, L'\0');
    GetEnvironmentVariableW(name, value.data(), needed);
    if (!value.empty() && value.back() == L'\0')
        value.pop_back();
    return value;
}

fs::path known_folder(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw)
        return {};
    fs::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

void append_unique_root(std::vector<fs::path>& roots, const fs::path& root)
{
    if (root.empty())
        return;
    std::error_code ec;
    if (!fs::exists(root, ec) || ec)
        return;
    const auto canonical = fs::weakly_canonical(root, ec);
    const auto candidate = ec ? root : canonical;
    if (std::find(roots.begin(), roots.end(), candidate) == roots.end())
        roots.push_back(candidate);
}

std::vector<fs::path> override_scan_roots()
{
    const std::wstring raw = env_w(L"OBS_SAFE_VST3_SCAN_ROOTS");
    if (raw.empty())
        return {};

    std::vector<fs::path> roots;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const auto end = raw.find(L';', start);
        const auto length = end == std::wstring::npos ? raw.size() - start : end - start;
        if (length != 0)
            append_unique_root(roots, fs::path(raw.substr(start, length)));
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return roots;
}

std::vector<fs::path> scan_roots()
{
    if (auto overrides = override_scan_roots(); !overrides.empty())
        return overrides;

    std::vector<fs::path> roots;

    // Use the Windows Known Folder API first. These match Steinberg's official
    // VST3 locations and do not depend on environment-variable redirection.
    const auto user_common = known_folder(FOLDERID_UserProgramFilesCommon);
    const auto global_common = known_folder(FOLDERID_ProgramFilesCommon);
    if (!user_common.empty())
        append_unique_root(roots, user_common / L"VST3");
    if (!global_common.empty())
        append_unique_root(roots, global_common / L"VST3");

    // Environment fallbacks keep discovery working on unusual Windows images
    // where Known Folder resolution is unavailable.
    const auto common = env_w(L"CommonProgramFiles");
    const auto local = env_w(L"LOCALAPPDATA");
    if (!common.empty())
        append_unique_root(roots, fs::path(common) / L"VST3");
    if (!local.empty())
        append_unique_root(roots, fs::path(local) / L"Programs" / L"Common" / L"VST3");

    return roots;
}

std::vector<fs::path> discover_bundles_from_roots(const std::vector<fs::path>& roots)
{
    std::vector<fs::path> bundles;
    std::set<std::wstring> seen;

    for (const auto& root : roots) {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        while (it != end) {
            if (ec) {
                ec.clear();
                it.increment(ec);
                continue;
            }

            const auto path = it->path();
            const bool vst3_path = _wcsicmp(path.extension().c_str(), L".vst3") == 0;
            const bool is_directory = it->is_directory(ec);
            if (ec)
                ec.clear();
            const bool is_regular_file = it->is_regular_file(ec);
            if (ec)
                ec.clear();

            if (vst3_path && (is_directory || is_regular_file)) {
                const auto key = path.wstring();
                if (seen.insert(key).second)
                    bundles.push_back(path);
                if (is_directory)
                    it.disable_recursion_pending();
            }
            it.increment(ec);
        }
    }

    std::sort(bundles.begin(), bundles.end());
    return bundles;
}

std::wstring normalized_path_key(const fs::path& input)
{
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(input, ec);
    std::wstring key = (ec ? input : canonical).wstring();
    std::replace(key.begin(), key.end(), L'/', L'\\');
    if (!key.empty())
        CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    return key;
}

using CachedLinesByPath = std::map<std::wstring, std::vector<std::string>>;

CachedLinesByPath load_cached_lines_by_path(const fs::path& cache)
{
    CachedLinesByPath entries;
    std::ifstream in(cache, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            continue;
        const std::string path_utf8 = line.substr(first + 1, second - first - 1);
        if (path_utf8.empty())
            continue;
        entries[normalized_path_key(fs::u8path(path_utf8))].push_back(line);
    }
    return entries;
}

int probe(const fs::path& plugin, const fs::path& output)
{
    std::string error;
    const std::string plugin_utf8 = utf8(plugin.wstring());
    auto module = VST3::Hosting::Module::create(plugin_utf8, error);
    if (!module) {
        std::cerr << error << '\n';
        return 3;
    }

    auto factory = module->getFactory();
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        return 4;

    bool found = false;
    for (const auto& info : factory.classInfos()) {
        if (info.category() != kVstAudioEffectClass)
            continue;
        found = true;
        out << sanitize(info.name()) << '\t'
            << sanitize(plugin_utf8) << '\t'
            << sanitize(info.ID().toString()) << '\n';
    }
    // Exit 5 is a successful module/factory probe with no audio-effect class.
    // The parent must distinguish it from crashes/timeouts/launch failures so
    // an obsolete cache can correctly become empty when effects are removed.
    return found ? 0 : 5;
}

bool start_parent_watchdog(DWORD parent_pid, std::jthread& watchdog)
{
    // Probe children are always scanner-owned. If ownership cannot be proven,
    // fail closed before third-party VST3 code is loaded.
    if (!parent_pid)
        return false;

    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
    if (!parent)
        return false;

    const DWORD initial = WaitForSingleObject(parent, 0);
    if (initial != WAIT_TIMEOUT) {
        CloseHandle(parent);
        return false;
    }

    watchdog = std::jthread([parent](std::stop_token stop) {
        while (!stop.stop_requested()) {
            const DWORD wait = WaitForSingleObject(parent, 100);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) {
                CloseHandle(parent);
                TerminateProcess(GetCurrentProcess(),
                                 wait == WAIT_OBJECT_0 ? ERROR_CANCELLED : ERROR_INVALID_HANDLE);
                return;
            }
        }
        CloseHandle(parent);
    });
    return true;
}

ProbeOutcome run_probe_child(const fs::path& self, const fs::path& plugin, const fs::path& output)
{
    std::wstring command = quote(self.wstring()) +
                           L" --probe " + quote(plugin.wstring()) +
                           L" --out " + quote(output.wstring()) +
                           L" --parent-pid " + std::to_wstring(GetCurrentProcessId());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_command = command;

    // Avoid PROC_THREAD_ATTRIBUTE_JOB_LIST here. OBS, Steam and enterprise
    // launchers can already place processes inside restrictive Job Objects;
    // nested job attachment then makes every probe child fail to start. Each
    // probe still has a hard timeout, and the child fails closed unless it can
    // open and watch the scanner parent before loading vendor code.
    if (!CreateProcessW(self.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, self.parent_path().c_str(), &si, &pi)) {
        std::wcerr << L"probe launch failed for " << plugin.wstring()
                   << L" (Win32 error " << GetLastError() << L")\n";
        return ProbeOutcome::Failed;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, 15000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0xDEAD);
        (void)WaitForSingleObject(pi.hProcess, 2000);
    }

    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return wait == WAIT_OBJECT_0 ? classify_probe_exit(exit_code) : ProbeOutcome::Failed;
}

int scan_all(const fs::path& self, const fs::path& cache)
{
    const auto roots = scan_roots();
    const auto bundles = discover_bundles_from_roots(roots);
    const auto cached = load_cached_lines_by_path(cache);
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);

    const auto staging = cache.wstring() + L".tmp";
    std::ofstream combined(fs::path(staging), std::ios::binary | std::ios::trunc);
    if (!combined)
        return 6;

    unsigned found = 0;
    unsigned retained = 0;
    unsigned failed = 0;
    unsigned valid_without_effects = 0;
    unsigned serial = 0;
    const auto temp_dir = fs::temp_directory_path(ec);

    for (const auto& bundle : bundles) {
        const auto probe_out = temp_dir / (L"obs-safe-vst3-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(serial++) + L".tsv");
        DeleteFileW(probe_out.c_str());

        const ProbeOutcome outcome = run_probe_child(self, bundle, probe_out);
        if (outcome == ProbeOutcome::AudioEffects) {
            std::ifstream in(probe_out, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty()) {
                    combined << line << '\n';
                    ++found;
                }
            }
        } else if (outcome == ProbeOutcome::NoAudioEffects) {
            ++valid_without_effects;
        } else {
            ++failed;
            const auto previous = cached.find(normalized_path_key(bundle));
            if (previous != cached.end()) {
                for (const auto& line : previous->second) {
                    combined << line << '\n';
                    ++retained;
                }
            }
        }
        DeleteFileW(probe_out.c_str());
    }

    combined.close();

    // If every discovered bundle failed, retain the previous cache byte-for-byte.
    // For a partial failure, only the failed bundle's previous entries are carried
    // forward; successful probes remain authoritative and removed bundles vanish.
    if (!bundles.empty() && failed == bundles.size()) {
        DeleteFileW(staging.c_str());
        std::cerr << "roots=" << roots.size() << " bundles=" << bundles.size()
                  << " plugins=0 retained=" << retained
                  << " no_audio_effects=0 failed_bundles=" << failed
                  << " previous_cache=kept\n";
        return 9;
    }

    if (!MoveFileExW(staging.c_str(), cache.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(staging.c_str());
        return 7;
    }

    std::cout << "roots=" << roots.size() << " bundles=" << bundles.size()
              << " plugins=" << found
              << " retained=" << retained
              << " no_audio_effects=" << valid_without_effects
              << " failed_bundles=" << failed << '\n';
    return 0;
}

int self_test_discovery()
{
    std::error_code ec;
    const auto root = fs::temp_directory_path(ec) /
        (L"obs-safe-vst3-scanner-selftest-" + std::to_wstring(GetCurrentProcessId()));
    const auto cache = root / L"plugins.tsv";
    fs::remove_all(root, ec);
    fs::create_directories(root / L"VendorA.vst3" / L"Contents", ec);
    fs::create_directories(root / L"Nested" / L"VendorB.vst3" / L"Contents", ec);
    fs::create_directories(root / L"VendorA.vst3" / L"ShouldNotRecurse.vst3", ec);
    std::ofstream(root / L"not-a-plugin.txt") << "test";

    const auto bundles = discover_bundles_from_roots({root});
    bool saw_a = false;
    bool saw_b = false;
    bool saw_nested_inside_bundle = false;
    for (const auto& bundle : bundles) {
        const auto name = bundle.filename().wstring();
        saw_a |= _wcsicmp(name.c_str(), L"VendorA.vst3") == 0;
        saw_b |= _wcsicmp(name.c_str(), L"VendorB.vst3") == 0;
        saw_nested_inside_bundle |= _wcsicmp(name.c_str(), L"ShouldNotRecurse.vst3") == 0;
    }

    const bool probe_classification_ok =
        classify_probe_exit(0) == ProbeOutcome::AudioEffects &&
        classify_probe_exit(5) == ProbeOutcome::NoAudioEffects &&
        classify_probe_exit(3) == ProbeOutcome::Failed;

    const std::string cached_path = utf8((root / L"VendorA.vst3").wstring());
    {
        std::ofstream out(cache, std::ios::binary | std::ios::trunc);
        out << "Cached A\t" << cached_path << "\tclass-a\n";
    }
    const auto cached_entries = load_cached_lines_by_path(cache);
    const auto cached_it = cached_entries.find(normalized_path_key(root / L"VendorA.vst3"));
    const bool partial_cache_lookup_ok =
        cached_it != cached_entries.end() && cached_it->second.size() == 1;

    std::jthread no_watchdog;
    const bool watchdog_fail_closed = !start_parent_watchdog(0, no_watchdog);

    fs::remove_all(root, ec);
    if (bundles.size() != 2 || !saw_a || !saw_b || saw_nested_inside_bundle ||
        !probe_classification_ok || !partial_cache_lookup_ok || !watchdog_fail_closed) {
        std::cerr << "scanner discovery self-test failed\n";
        return 10;
    }
    std::cout << "scanner discovery self-test passed\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    fs::path probe_path;
    fs::path out_path;
    fs::path cache_path;
    DWORD parent_pid = 0;
    bool self_test = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--probe" && i + 1 < argc)
            probe_path = argv[++i];
        else if (arg == L"--out" && i + 1 < argc)
            out_path = argv[++i];
        else if (arg == L"--scan-to" && i + 1 < argc)
            cache_path = argv[++i];
        else if (arg == L"--parent-pid" && i + 1 < argc)
            parent_pid = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 10));
        else if (arg == L"--self-test")
            self_test = true;
    }

    if (self_test)
        return self_test_discovery();

    if (!probe_path.empty() && !out_path.empty()) {
        std::jthread parent_watchdog;
        if (!start_parent_watchdog(parent_pid, parent_watchdog)) {
            std::cerr << "probe parent watchdog unavailable; refusing to load vendor code\n";
            return 11;
        }
        return probe(probe_path, out_path);
    }

    if (!cache_path.empty()) {
        wchar_t self_buffer[32768]{};
        const DWORD len = GetModuleFileNameW(nullptr, self_buffer, static_cast<DWORD>(std::size(self_buffer)));
        if (!len)
            return 2;
        return scan_all(fs::path(std::wstring(self_buffer, len)), cache_path);
    }

    std::cerr << "usage: obs-safe-vst3-scanner --scan-to <cache.tsv> | --self-test\n";
    return 1;
}

#else
int main() { return 0; }
#endif
