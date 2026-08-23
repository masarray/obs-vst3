#ifdef _WIN32

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
            if (it->is_directory(ec) && _wcsicmp(path.extension().c_str(), L".vst3") == 0) {
                const auto key = path.wstring();
                if (seen.insert(key).second)
                    bundles.push_back(path);
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
    auto module = VST3::Hosting::Module::create(plugin.u8string(), error);
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
            << sanitize(plugin.u8string()) << '\t'
            << sanitize(info.ID().toString()) << '\n';
    }
    return found ? 0 : 5;
}

bool run_probe_child(const fs::path& self, const fs::path& plugin, const fs::path& output)
{
    std::wstring command = quote(self.wstring()) + L" --probe " + quote(plugin.wstring()) + L" --out " + quote(output.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, self.parent_path().c_str(), &si, &pi))
        return false;

    const DWORD wait = WaitForSingleObject(pi.hProcess, 15000);
    if (wait == WAIT_TIMEOUT)
        TerminateProcess(pi.hProcess, 0xDEAD);

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

    const auto staging = cache.wstring() + L".tmp";
    std::ofstream combined(fs::path(staging), std::ios::binary | std::ios::trunc);
    if (!combined)
        return 6;

    unsigned found = 0;
    unsigned failed = 0;
    unsigned serial = 0;
    const auto temp_dir = fs::temp_directory_path(ec);

    for (const auto& bundle : bundles) {
        const auto probe_out = temp_dir / (L"obs-safe-vst3-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(serial++) + L".tsv");
        DeleteFileW(probe_out.c_str());

        if (run_probe_child(self, bundle, probe_out)) {
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
        return 7;
    }

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
