#ifdef _WIN32

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

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

std::vector<fs::path> scan_roots()
{
    std::vector<fs::path> roots;
    const auto common = env_w(L"CommonProgramFiles");
    const auto local = env_w(L"LOCALAPPDATA");

    if (!common.empty())
        roots.emplace_back(fs::path(common) / L"VST3");
    if (!local.empty())
        roots.emplace_back(fs::path(local) / L"Programs" / L"Common" / L"VST3");

    std::vector<fs::path> unique;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec))
            continue;
        const auto canonical = fs::weakly_canonical(root, ec);
        const auto candidate = ec ? root : canonical;
        if (std::find(unique.begin(), unique.end(), candidate) == unique.end())
            unique.push_back(candidate);
    }
    return unique;
}

std::vector<fs::path> discover_bundles()
{
    std::vector<fs::path> bundles;
    std::set<std::wstring> seen;

    for (const auto& root : scan_roots()) {
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
    return found ? 0 : 5;
}

HANDLE make_probe_job()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return nullptr;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

bool run_probe_child(HANDLE probe_job, const fs::path& self, const fs::path& plugin, const fs::path& output)
{
    if (!probe_job)
        return false;

    std::wstring command = quote(self.wstring()) + L" --probe " + quote(plugin.wstring()) + L" --out " + quote(output.wstring());

    // Attach the child to the kill-on-close job at process creation time. If
    // OBS terminates the scanner because the whole scan exceeded its ceiling,
    // the scanner's job handle closes and Windows terminates any active probe
    // even though third-party VST3 code is running inside that descendant.
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    if (attribute_bytes == 0)
        return false;

    std::vector<unsigned char> attribute_storage(attribute_bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes))
        return false;

    HANDLE jobs[] = {probe_job};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                   jobs, sizeof(jobs), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes);
        return false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attributes;
    PROCESS_INFORMATION pi{};

    const BOOL created = CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                        nullptr, self.parent_path().c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attributes);
    if (!created)
        return false;

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
    return wait == WAIT_OBJECT_0 && exit_code == 0;
}

int scan_all(const fs::path& self, const fs::path& cache)
{
    const auto bundles = discover_bundles();
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);

    HANDLE probe_job = make_probe_job();
    if (!probe_job)
        return 8;

    const auto staging = cache.wstring() + L".tmp";
    std::ofstream combined(fs::path(staging), std::ios::binary | std::ios::trunc);
    if (!combined) {
        CloseHandle(probe_job);
        return 6;
    }

    unsigned found = 0;
    unsigned failed = 0;
    unsigned serial = 0;
    const auto temp_dir = fs::temp_directory_path(ec);

    for (const auto& bundle : bundles) {
        const auto probe_out = temp_dir / (L"obs-safe-vst3-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(serial++) + L".tsv");
        DeleteFileW(probe_out.c_str());

        if (run_probe_child(probe_job, self, bundle, probe_out)) {
            std::ifstream in(probe_out, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty()) {
                    combined << line << '\n';
                    ++found;
                }
            }
        } else {
            ++failed;
        }
        DeleteFileW(probe_out.c_str());
    }

    combined.close();
    if (!MoveFileExW(staging.c_str(), cache.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(staging.c_str());
        CloseHandle(probe_job);
        return 7;
    }

    CloseHandle(probe_job); // Kill-on-close also guarantees no probe survives scan completion.
    std::cout << "plugins=" << found << " failed_bundles=" << failed << '\n';
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    fs::path probe_path;
    fs::path out_path;
    fs::path cache_path;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--probe" && i + 1 < argc)
            probe_path = argv[++i];
        else if (arg == L"--out" && i + 1 < argc)
            out_path = argv[++i];
        else if (arg == L"--scan-to" && i + 1 < argc)
            cache_path = argv[++i];
    }

    if (!probe_path.empty() && !out_path.empty())
        return probe(probe_path, out_path);

    if (!cache_path.empty()) {
        wchar_t self_buffer[32768]{};
        const DWORD len = GetModuleFileNameW(nullptr, self_buffer, static_cast<DWORD>(std::size(self_buffer)));
        if (!len)
            return 2;
        return scan_all(fs::path(std::wstring(self_buffer, len)), cache_path);
    }

    std::cerr << "usage: obs-safe-vst3-scanner --scan-to <cache.tsv>\n";
    return 1;
}

#else
int main() { return 0; }
#endif
