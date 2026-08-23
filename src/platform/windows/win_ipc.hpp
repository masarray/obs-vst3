#pragma once

#ifdef _WIN32

#include "common/protocol.hpp"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace safevst3 {

struct BridgeNames {
    std::wstring mapping;
    std::wstring request_event;
    std::wstring response_event;
    std::wstring ready_event;
};

struct ParameterSnapshot {
    std::uint32_t id = 0;
    std::int32_t step_count = 0;
    std::uint32_t flags = 0;
    double default_normalized = 0.0;
    double current_normalized = 0.0;
    std::string title;
    std::string units;
};

class WinObsBridge {
public:
    WinObsBridge() = default;
    ~WinObsBridge();

    WinObsBridge(const WinObsBridge&) = delete;
    WinObsBridge& operator=(const WinObsBridge&) = delete;

    bool start(const std::filesystem::path& helper,
               const std::filesystem::path& vst_path,
               const std::string& class_id,
               std::uint32_t sample_rate,
               std::uint32_t channels,
               std::string& error,
               std::stop_token cancel = {});

    void stop() noexcept;
    void abort() noexcept;
    bool running() const noexcept;

    bool process(float* const* channels,
                 std::uint32_t channel_count,
                 std::uint32_t frames,
                 double deadline_fraction) noexcept;

    std::vector<ParameterSnapshot> parameters() const;
    bool set_parameter(std::uint32_t id, double normalized) noexcept;
    std::uint32_t parameter_total_count() const noexcept;

    // Read-only status snapshot for the normal OBS properties UI. The helper
    // owns these fields and publishes them before HostStatus::Ready.
    std::string plugin_name() const
    {
        return region_ ? std::string(region_->plugin_name) : std::string{};
    }
    std::uint32_t latency_samples() const noexcept
    {
        return region_ ? region_->latency_samples : 0;
    }

    // Non-realtime native-editor seam. The vendor UI remains entirely inside
    // the helper process; OBS only publishes an asynchronous command.
    bool open_editor() noexcept;
    bool hide_editor() noexcept;
    EditorStatus editor_status() const noexcept;

    std::uint64_t deadline_misses() const noexcept { return deadline_misses_; }

private:
    static std::wstring widen(const std::string& value);
    static std::wstring quote(const std::wstring& value);
    static BridgeNames make_names();
    AudioSlot* acquire_slot() noexcept;
    bool send_editor_command(EditorCommand command) noexcept;
    static std::uint64_t qpc_now() noexcept;
    static std::uint64_t qpc_frequency() noexcept;

    HANDLE mapping_ = nullptr;
    HANDLE request_event_ = nullptr;
    HANDLE response_event_ = nullptr;
    HANDLE ready_event_ = nullptr;
    PROCESS_INFORMATION process_{};
    SharedAudioRegion* region_ = nullptr;
    BridgeNames names_{};
    std::uint32_t next_sequence_ = 1;
    std::uint64_t deadline_misses_ = 0;
};

class WinHostEndpoint {
public:
    WinHostEndpoint() = default;
    ~WinHostEndpoint();

    WinHostEndpoint(const WinHostEndpoint&) = delete;
    WinHostEndpoint& operator=(const WinHostEndpoint&) = delete;

    bool open(const BridgeNames& names, std::string& error);
    void close() noexcept;

    SharedAudioRegion* region() const noexcept { return region_; }
    HANDLE request_event() const noexcept { return request_event_; }
    HANDLE response_event() const noexcept { return response_event_; }
    HANDLE ready_event() const noexcept { return ready_event_; }

private:
    HANDLE mapping_ = nullptr;
    HANDLE request_event_ = nullptr;
    HANDLE response_event_ = nullptr;
    HANDLE ready_event_ = nullptr;
    SharedAudioRegion* region_ = nullptr;
};

} // namespace safevst3

#endif
