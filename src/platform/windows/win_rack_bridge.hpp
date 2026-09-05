#pragma once

#ifdef _WIN32

#include "rack/rack_protocol.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace safevst3 {

struct RackBridgeStatus {
    bool running = false;
    std::uint64_t chain_generation = 0;
    std::uint32_t effect_count = 0;
    std::uint32_t total_latency_samples = 0;
};

class WinRackBridge {
public:
    WinRackBridge() = default;
    ~WinRackBridge();

    WinRackBridge(const WinRackBridge&) = delete;
    WinRackBridge& operator=(const WinRackBridge&) = delete;

    bool start(const std::filesystem::path& helper,
               std::uint32_t sample_rate,
               std::uint32_t channels,
               std::string& error,
               std::stop_token cancel = {});

    bool start(const std::filesystem::path& helper,
               std::uint32_t sample_rate,
               std::uint32_t channels,
               const std::filesystem::path& session_file,
               std::string& error,
               std::stop_token cancel = {});

    void stop() noexcept;
    void abort() noexcept;
    bool running() const noexcept;

    bool process(float* const* channels,
                 std::uint32_t channel_count,
                 std::uint32_t frames,
                 double deadline_fraction) noexcept;

    bool open_editor() noexcept;
    bool save_session(DWORD timeout_ms = 750) noexcept;
    RackBridgeStatus status() const noexcept;

    std::uint64_t deadline_misses() const noexcept
    {
        return deadline_misses_.load(std::memory_order_relaxed);
    }

private:
    struct Names {
        std::wstring mapping;
        std::wstring request_event;
        std::wstring response_event;
        std::wstring ready_event;
        std::wstring ui_open_event;
        std::wstring session_save_event;
        std::wstring session_saved_event;
    };

    static std::wstring quote(const std::wstring& value);
    static Names make_names();
    static std::uint64_t qpc_now() noexcept;
    static std::uint64_t qpc_frequency() noexcept;
    bool process_alive() const noexcept;
    bool retire_completed_request() noexcept;

    HANDLE mapping_ = nullptr;
    HANDLE request_event_ = nullptr;
    HANDLE response_event_ = nullptr;
    HANDLE ready_event_ = nullptr;
    HANDLE ui_open_event_ = nullptr;
    HANDLE session_save_event_ = nullptr;
    HANDLE session_saved_event_ = nullptr;
    PROCESS_INFORMATION process_{};
    rack::RackSharedAudioRegion* region_ = nullptr;
    Names names_{};
    std::uint32_t next_sequence_ = 1;
    long next_request_generation_ = 1;
    long pending_request_generation_ = 0;
    std::atomic<std::uint64_t> deadline_misses_{0};
};

} // namespace safevst3

#endif
