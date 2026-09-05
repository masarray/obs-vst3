#pragma once

#ifdef _WIN32

#include "rack/rack_session_snapshot.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace safevst3::rack {

inline constexpr wchar_t kRackSessionFileEnv[] = L"OBS_SAFE_VST3_RACK_SESSION_FILE";
inline constexpr wchar_t kRackSessionSaveEventEnv[] = L"OBS_SAFE_VST3_RACK_SESSION_SAVE_EVENT";
inline constexpr wchar_t kRackSessionSavedEventEnv[] = L"OBS_SAFE_VST3_RACK_SESSION_SAVED_EVENT";

inline std::array<std::uint8_t, 16> rack_session_id_for_path(
    const std::filesystem::path& path) noexcept
{
    const std::wstring native = path.native();
    std::uint64_t first = 1469598103934665603ull;
    std::uint64_t second = 1099511628211ull ^ 0x9e3779b97f4a7c15ull;
    for (wchar_t ch : native) {
        const std::uint32_t value = static_cast<std::uint32_t>(ch);
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const std::uint8_t byte = static_cast<std::uint8_t>((value >> shift) & 0xffu);
            first ^= byte;
            first *= 1099511628211ull;
            second ^= static_cast<std::uint8_t>(byte + 0x5bu);
            second *= 1099511628211ull;
        }
    }

    std::array<std::uint8_t, 16> id{};
    std::memcpy(id.data(), &first, sizeof(first));
    std::memcpy(id.data() + sizeof(first), &second, sizeof(second));
    bool nonzero = false;
    for (const auto byte : id)
        nonzero = nonzero || byte != 0;
    if (!nonzero)
        id[0] = 1;
    return id;
}

inline std::wstring rack_session_environment_value(const wchar_t* name)
{
    if (!name || !*name)
        return {};
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required));
    const DWORD written = GetEnvironmentVariableW(
        name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size())
        return {};
    return std::wstring(buffer.data(), written);
}

class RackSessionRuntime {
public:
    RackSessionRuntime() = default;
    ~RackSessionRuntime() { close(); }

    RackSessionRuntime(const RackSessionRuntime&) = delete;
    RackSessionRuntime& operator=(const RackSessionRuntime&) = delete;

    bool open(const std::filesystem::path& path,
              std::wstring_view save_event_name,
              std::wstring_view saved_event_name,
              std::string& error) noexcept
    {
        close();
        error.clear();
        if (path.empty())
            return true;
        if (save_event_name.empty() || saved_event_name.empty()) {
            error = "Rack session event names are incomplete";
            return false;
        }

        const std::wstring save_name(save_event_name);
        const std::wstring saved_name(saved_event_name);
        save_request_ = OpenEventW(SYNCHRONIZE, FALSE, save_name.c_str());
        save_complete_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, saved_name.c_str());
        if (!save_request_ || !save_complete_) {
            error = "Rack session save handshake events are unavailable";
            close();
            return false;
        }

        path_ = path;
        rack_id_ = rack_session_id_for_path(path_);
        next_generation_ = 1;
        return true;
    }

    bool open_from_environment(std::string& error) noexcept
    {
        const std::wstring path = rack_session_environment_value(kRackSessionFileEnv);
        const std::wstring save = rack_session_environment_value(kRackSessionSaveEventEnv);
        const std::wstring saved = rack_session_environment_value(kRackSessionSavedEventEnv);
        if (path.empty() && save.empty() && saved.empty()) {
            close();
            error.clear();
            return true;
        }
        if (path.empty() || save.empty() || saved.empty()) {
            close();
            error = "Rack session environment is incomplete";
            return false;
        }
        return open(std::filesystem::path(path), save, saved, error);
    }

    void close() noexcept
    {
        if (save_complete_)
            CloseHandle(save_complete_);
        if (save_request_)
            CloseHandle(save_request_);
        save_complete_ = nullptr;
        save_request_ = nullptr;
        path_.clear();
        rack_id_ = {};
        next_generation_ = 1;
    }

    bool enabled() const noexcept
    {
        return !path_.empty() && save_request_ && save_complete_;
    }

    const std::filesystem::path& path() const noexcept { return path_; }
    const std::array<std::uint8_t, 16>& rack_id() const noexcept { return rack_id_; }

    void adopt_loaded(const RackSessionSnapshot& snapshot) noexcept
    {
        rack_id_ = snapshot.rack_id;
        if (snapshot.generation == std::numeric_limits<std::uint64_t>::max())
            next_generation_ = 1;
        else
            next_generation_ = snapshot.generation + 1;
        if (next_generation_ == 0)
            next_generation_ = 1;
    }

    std::uint64_t allocate_generation() noexcept
    {
        if (next_generation_ == 0)
            next_generation_ = 1;
        const std::uint64_t result = next_generation_;
        if (next_generation_ == std::numeric_limits<std::uint64_t>::max())
            next_generation_ = 1;
        else
            ++next_generation_;
        return result;
    }

    bool take_save_request() noexcept
    {
        return enabled() && WaitForSingleObject(save_request_, 0) == WAIT_OBJECT_0;
    }

    void signal_save_complete() noexcept
    {
        if (save_complete_)
            SetEvent(save_complete_);
    }

private:
    std::filesystem::path path_;
    HANDLE save_request_ = nullptr;
    HANDLE save_complete_ = nullptr;
    std::array<std::uint8_t, 16> rack_id_{};
    std::uint64_t next_generation_ = 1;
};

} // namespace safevst3::rack

#endif
