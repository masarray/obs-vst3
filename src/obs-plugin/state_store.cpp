#ifdef _WIN32

#include "obs-plugin/state_store.hpp"

#include "obs-plugin/parameter_controls.hpp"

#include <windows.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace safevst3::obsstate {
namespace {

std::filesystem::path state_root_path()
{
    wchar_t buffer[32768]{};
    const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (!len || len >= std::size(buffer))
        return {};
    return std::filesystem::path(std::wstring(buffer, len)) / L"OBS Safe VST3 Host" / L"states";
}

std::string sanitize_id(const char* value)
{
    std::string out;
    if (!value)
        return out;
    for (const unsigned char c : std::string(value)) {
        if (std::isalnum(c) || c == '-' || c == '_')
            out.push_back(static_cast<char>(c));
    }
    return out;
}

std::filesystem::path state_file_path(obs_source_t* source,
                                      const std::string& vst_path,
                                      const std::string& class_id)
{
    if (!source || vst_path.empty())
        return {};
    const auto root = state_root_path();
    const std::string source_id = sanitize_id(obs_source_get_uuid(source));
    if (root.empty() || source_id.empty())
        return {};
    const std::string scope = obsparam::parameter_scope(vst_path, class_id);
    return root / std::filesystem::u8path(source_id) / std::filesystem::u8path(scope + ".sv3state");
}

} // namespace

LoadResult load(obs_source_t* source,
                const std::string& vst_path,
                const std::string& class_id,
                PluginStateSnapshot& snapshot,
                std::string& error)
{
    snapshot = {};
    error.clear();
    const auto path = state_file_path(source, vst_path, class_id);
    if (path.empty() || !std::filesystem::exists(path))
        return LoadResult::Missing;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "could not open saved VST3 state";
        return LoadResult::Invalid;
    }

    const std::streamoff raw_size = in.tellg();
    if (raw_size < 0 || static_cast<std::uint64_t>(raw_size) > kStateBlobHeaderBytes + kMaxStateBytes) {
        error = "saved VST3 state file is oversized";
        return LoadResult::Invalid;
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(raw_size));
    if (!bytes.empty())
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in && !bytes.empty()) {
        error = "saved VST3 state file is truncated";
        return LoadResult::Invalid;
    }

    if (!decode_state_blob(bytes, snapshot, error))
        return LoadResult::Invalid;
    return LoadResult::Loaded;
}

bool save(obs_source_t* source,
          const std::string& vst_path,
          const std::string& class_id,
          const PluginStateSnapshot& snapshot,
          std::string& error)
{
    error.clear();
    const auto path = state_file_path(source, vst_path, class_id);
    if (path.empty()) {
        error = "VST3 state storage path is unavailable";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!encode_state_blob(snapshot, bytes, error))
        return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "could not create VST3 state directory";
        return false;
    }

    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "could not create temporary VST3 state file";
            return false;
        }
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            error = "could not write complete VST3 state snapshot";
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "atomic VST3 state replace failed";
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

} // namespace safevst3::obsstate

#endif
