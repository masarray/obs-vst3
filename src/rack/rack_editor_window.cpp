#ifdef _WIN32

#include "rack/rack_editor_window.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace safevst3::rack::ui {
namespace {

constexpr UINT kForegroundMessage = WM_APP + 0x310;

struct D3dState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
};

struct WindowRuntime {
    D3dState d3d{};
};

void release_render_target(D3dState& d3d) noexcept
{
    if (d3d.render_target) {
        d3d.render_target->Release();
        d3d.render_target = nullptr;
    }
}

bool create_render_target(D3dState& d3d) noexcept
{
    if (!d3d.swap_chain || !d3d.device)
        return false;
    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(d3d.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || !back_buffer)
        return false;
    const HRESULT result = d3d.device->CreateRenderTargetView(
        back_buffer, nullptr, &d3d.render_target);
    back_buffer->Release();
    return SUCCEEDED(result) && d3d.render_target;
}

void release_d3d(D3dState& d3d) noexcept
{
    release_render_target(d3d);
    if (d3d.swap_chain) {
        d3d.swap_chain->Release();
        d3d.swap_chain = nullptr;
    }
    if (d3d.context) {
        d3d.context->Release();
        d3d.context = nullptr;
    }
    if (d3d.device) {
        d3d.device->Release();
        d3d.device = nullptr;
    }
}

bool create_d3d(HWND window, D3dState& d3d, D3D_DRIVER_TYPE driver) noexcept
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained{};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, driver, nullptr, 0, requested,
        static_cast<UINT>(std::size(requested)), D3D11_SDK_VERSION,
        &desc, &d3d.swap_chain, &d3d.device, &obtained, &d3d.context);
    if (FAILED(result)) {
        release_d3d(d3d);
        return false;
    }
    if (!create_render_target(d3d)) {
        release_d3d(d3d);
        return false;
    }
    return true;
}

LRESULT CALLBACK rack_editor_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
        return 1;

    auto* runtime = reinterpret_cast<WindowRuntime*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_SIZE && runtime && runtime->d3d.swap_chain &&
        wparam != SIZE_MINIMIZED) {
        release_render_target(runtime->d3d);
        const UINT width = static_cast<UINT>(LOWORD(lparam));
        const UINT height = static_cast<UINT>(HIWORD(lparam));
        if (FAILED(runtime->d3d.swap_chain->ResizeBuffers(
                0, width, height, DXGI_FORMAT_UNKNOWN, 0)) ||
            !create_render_target(runtime->d3d))
            return 0;
        return 0;
    }
    if (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_KEYMENU)
        return 0;
    if (message == kForegroundMessage) {
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

const char* health_text(RackUiSlotHealth health) noexcept
{
    switch (health) {
    case RackUiSlotHealth::Ready: return "Ready";
    case RackUiSlotHealth::Bypassed: return "Bypassed";
    case RackUiSlotHealth::Loading: return "Loading";
    case RackUiSlotHealth::Missing: return "Missing";
    case RackUiSlotHealth::Recovering: return "Recovering";
    case RackUiSlotHealth::NeedsAttention: return "Needs Attention";
    case RackUiSlotHealth::Quarantined: return "Quarantined";
    }
    return "Unknown";
}

std::string bounded_text(const char* data, std::size_t capacity)
{
    std::size_t length = 0;
    while (length < capacity && data[length] != '\0')
        ++length;
    return std::string(data, length);
}

} // namespace

struct RackEditorWindow::Impl {
    explicit Impl(RackUiCommandHandler handler)
        : command_handler(std::move(handler))
    {
    }

    ~Impl() { shutdown(); }

    bool publish_snapshot(const RackUiSnapshot& snapshot) noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.publish_snapshot(snapshot);
    }

    bool apply_ack(const RackUiCommandAck& ack) noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.apply_ack(ack);
    }

    bool visible() const noexcept
    {
        const HWND current = hwnd.load(std::memory_order_acquire);
        return current && IsWindow(current) != FALSE;
    }

    bool open_or_foreground() noexcept
    {
        if (HWND current = hwnd.load(std::memory_order_acquire);
            current && IsWindow(current)) {
            PostMessageW(current, kForegroundMessage, 0, 0);
            return true;
        }

        std::unique_lock lifecycle_lock(lifecycle_mutex);
        if (thread_running) {
            const bool stopped_or_visible = lifecycle_cv.wait_for(
                lifecycle_lock, std::chrono::seconds(5), [&] {
                    const HWND current = hwnd.load(std::memory_order_acquire);
                    return !thread_running || (current && IsWindow(current));
                });
            if (!stopped_or_visible)
                return false;
            if (HWND current = hwnd.load(std::memory_order_acquire);
                current && IsWindow(current)) {
                PostMessageW(current, kForegroundMessage, 0, 0);
                return true;
            }
        }

        if (window_thread.joinable()) {
            lifecycle_lock.unlock();
            window_thread.join();
            lifecycle_lock.lock();
        }

        stop_requested.store(false, std::memory_order_release);
        creation_attempted = false;
        creation_succeeded = false;
        thread_running = true;
        window_thread = std::thread([this] { window_main(); });

        const bool ready = lifecycle_cv.wait_for(
            lifecycle_lock, std::chrono::seconds(5), [&] {
                return creation_attempted || !thread_running;
            });
        return ready && creation_succeeded;
    }

    void shutdown() noexcept
    {
        stop_requested.store(true, std::memory_order_release);
        if (HWND current = hwnd.load(std::memory_order_acquire);
            current && IsWindow(current))
            PostMessageW(current, WM_CLOSE, 0, 0);

        if (window_thread.joinable())
            window_thread.join();

        std::lock_guard lock(lifecycle_mutex);
        thread_running = false;
        creation_attempted = true;
        creation_succeeded = false;
        hwnd.store(nullptr, std::memory_order_release);
        lifecycle_cv.notify_all();
    }

    RackUiSnapshot snapshot_copy() const noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.has_snapshot() ? model.snapshot() : RackUiSnapshot{};
    }

    bool pending_copy() const noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.pending_command();
    }

    void emit_move(RackUiSlotId slot_id, std::uint32_t target_index) noexcept
    {
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = model.request_move(slot_id, target_index);
        }
        if (command.command_id == 0 || !command_handler)
            return;

        const RackUiCommandAck ack = command_handler(command);
        if (ack.command_id != 0)
            apply_ack(ack);
    }

    void render_ui()
    {
        const RackUiSnapshot snapshot = snapshot_copy();
        const bool pending = pending_copy();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("OBS Safe VST3 Rack", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        const std::string rack_name = bounded_text(snapshot.rack_name.data(), snapshot.rack_name.size());
        if (rack_name.empty())
            ImGui::TextUnformatted("OBS Safe VST3 Rack");
        else
            ImGui::Text("OBS Safe VST3 Rack — %s", rack_name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%u effects · %u samples", snapshot.slot_count,
                            snapshot.total_latency_samples);
        if (pending) {
            ImGui::SameLine();
            ImGui::TextUnformatted("Pending...");
        }
        ImGui::Separator();

        ImGui::TextUnformatted("INPUT");
        for (std::uint32_t index = 0; index < snapshot.slot_count; ++index) {
            const RackUiSlotSnapshot& slot = snapshot.slots[index];
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot.slot_id)));
            ImGui::BeginChild("rack-slot-card", ImVec2(0.0f, 94.0f), ImGuiChildFlags_Borders);

            const std::string plugin_name = bounded_text(slot.plugin_name.data(), slot.plugin_name.size());
            const std::string vendor = bounded_text(slot.vendor.data(), slot.vendor.size());
            ImGui::Text("%u  %s", index + 1,
                        plugin_name.empty() ? "VST3 Effect" : plugin_name.c_str());
            if (!vendor.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", vendor.c_str());
            }
            ImGui::Text("%s · %u samples%s", health_text(slot.health), slot.latency_samples,
                        slot.bypass ? " · Bypassed" : "");

            if (!pending && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const RackUiSlotId payload = slot.slot_id;
                ImGui::SetDragDropPayload("SAFEVST3_RACK_SLOT", &payload, sizeof(payload));
                ImGui::Text("Move %s", plugin_name.empty() ? "effect" : plugin_name.c_str());
                ImGui::EndDragDropSource();
            }
            if (!pending && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SAFEVST3_RACK_SLOT")) {
                    if (payload->DataSize == sizeof(RackUiSlotId)) {
                        const auto moved = *static_cast<const RackUiSlotId*>(payload->Data);
                        if (moved != slot.slot_id)
                            emit_move(moved, index);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::TextUnformatted("OUTPUT TO OBS");
        ImGui::End();
    }

    void mark_creation(bool succeeded) noexcept
    {
        std::lock_guard lock(lifecycle_mutex);
        creation_attempted = true;
        creation_succeeded = succeeded;
        lifecycle_cv.notify_all();
    }

    void mark_stopped() noexcept
    {
        std::lock_guard lock(lifecycle_mutex);
        thread_running = false;
        lifecycle_cv.notify_all();
    }

    void window_main() noexcept
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = rack_editor_window_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kRackEditorWindowClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            mark_creation(false);
            mark_stopped();
            return;
        }

        WindowRuntime runtime{};
        HWND window = CreateWindowExW(
            0, kRackEditorWindowClassName, L"OBS Safe VST3 Rack",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 620,
            nullptr, nullptr, instance, &runtime);
        if (!window) {
            UnregisterClassW(kRackEditorWindowClassName, instance);
            mark_creation(false);
            mark_stopped();
            return;
        }

        if (!create_d3d(window, runtime.d3d, D3D_DRIVER_TYPE_HARDWARE) &&
            !create_d3d(window, runtime.d3d, D3D_DRIVER_TYPE_WARP)) {
            DestroyWindow(window);
            UnregisterClassW(kRackEditorWindowClassName, instance);
            mark_creation(false);
            mark_stopped();
            return;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        bool imgui_win32 = ImGui_ImplWin32_Init(window);
        bool imgui_dx11 = imgui_win32 && ImGui_ImplDX11_Init(runtime.d3d.device, runtime.d3d.context);
        if (!imgui_dx11) {
            if (imgui_win32)
                ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            release_d3d(runtime.d3d);
            DestroyWindow(window);
            UnregisterClassW(kRackEditorWindowClassName, instance);
            mark_creation(false);
            mark_stopped();
            return;
        }

        hwnd.store(window, std::memory_order_release);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        SetForegroundWindow(window);
        mark_creation(true);

        bool running = true;
        while (running && !stop_requested.load(std::memory_order_acquire) && IsWindow(window)) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running || !IsWindow(window))
                break;

            if (IsIconic(window)) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                continue;
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            render_ui();
            ImGui::Render();

            constexpr float clear[4] = {0.08f, 0.08f, 0.09f, 1.0f};
            runtime.d3d.context->OMSetRenderTargets(1, &runtime.d3d.render_target, nullptr);
            runtime.d3d.context->ClearRenderTargetView(runtime.d3d.render_target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (FAILED(runtime.d3d.swap_chain->Present(1, 0)))
                running = false;

            MsgWaitForMultipleObjectsEx(0, nullptr, 33, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

        if (IsWindow(window))
            DestroyWindow(window);
        hwnd.store(nullptr, std::memory_order_release);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        release_d3d(runtime.d3d);
        UnregisterClassW(kRackEditorWindowClassName, instance);
        mark_stopped();
    }

    RackUiCommandHandler command_handler;
    mutable std::mutex model_mutex;
    RackEditorModel model;

    std::mutex lifecycle_mutex;
    std::condition_variable lifecycle_cv;
    std::thread window_thread;
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> stop_requested{false};
    bool thread_running = false;
    bool creation_attempted = false;
    bool creation_succeeded = false;
};

RackEditorWindow::RackEditorWindow(RackUiCommandHandler command_handler)
    : impl_(std::make_unique<Impl>(std::move(command_handler)))
{
}

RackEditorWindow::~RackEditorWindow() = default;

bool RackEditorWindow::publish_snapshot(const RackUiSnapshot& snapshot) noexcept
{
    return impl_->publish_snapshot(snapshot);
}

bool RackEditorWindow::apply_ack(const RackUiCommandAck& ack) noexcept
{
    return impl_->apply_ack(ack);
}

bool RackEditorWindow::open_or_foreground() noexcept
{
    return impl_->open_or_foreground();
}

void RackEditorWindow::shutdown() noexcept
{
    impl_->shutdown();
}

bool RackEditorWindow::visible() const noexcept
{
    return impl_->visible();
}

} // namespace safevst3::rack::ui

#endif
