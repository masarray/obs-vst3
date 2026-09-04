#ifdef _WIN32

#include "rack/rack_editor_window.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
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
    if (message == WM_DESTROY)
        return 0;
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

std::array<char, 33> preset_id_text(const RackPresetId& id) noexcept
{
    static constexpr char digits[] = "0123456789abcdef";
    std::array<char, 33> result{};
    for (std::size_t index = 0; index < id.size(); ++index) {
        result[index * 2] = digits[(id[index] >> 4u) & 0x0fu];
        result[index * 2 + 1] = digits[id[index] & 0x0fu];
    }
    return result;
}

} // namespace

struct RackEditorWindow::Impl {
    enum class BrowserMode {
        None,
        Add,
        Replace,
    };

    explicit Impl(RackUiCommandHandler handler,
                  RackPresetUiCommandHandler preset_handler)
        : command_handler(std::move(handler)),
          preset_command_handler(std::move(preset_handler))
    {
    }

    ~Impl() { shutdown(); }

    bool publish_snapshot(const RackUiSnapshot& snapshot) noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.publish_snapshot(snapshot);
    }

    bool publish_catalog(const PluginCatalogSnapshot& snapshot) noexcept
    {
        if (!validate_plugin_catalog_snapshot(snapshot))
            return false;
        std::lock_guard lock(model_mutex);
        if (has_catalog && snapshot.generation <= catalog.generation)
            return false;
        catalog = snapshot;
        has_catalog = true;
        return true;
    }

    bool publish_presets(const RackPresetUiSnapshot& snapshot) noexcept
    {
        std::lock_guard lock(model_mutex);
        return preset_model.publish_snapshot(snapshot);
    }

    bool apply_ack(const RackUiCommandAck& ack) noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.apply_ack(ack);
    }

    bool apply_preset_ack(const RackPresetUiAck& ack) noexcept
    {
        std::lock_guard lock(model_mutex);
        return preset_model.apply_ack(ack);
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
        if (!thread_running) {
            stop_requested.store(false, std::memory_order_release);
            open_requested = true;
            creation_attempted = false;
            creation_succeeded = false;
            thread_running = true;
            try {
                window_thread = std::thread([this] { window_main(); });
            } catch (...) {
                thread_running = false;
                open_requested = false;
                creation_attempted = true;
                creation_succeeded = false;
                return false;
            }
        } else {
            creation_attempted = false;
            creation_succeeded = false;
            open_requested = true;
            lifecycle_cv.notify_all();
        }

        const bool ready = lifecycle_cv.wait_for(
            lifecycle_lock, std::chrono::seconds(5), [&] {
                return creation_attempted || !thread_running;
            });
        return ready && creation_succeeded;
    }

    void shutdown() noexcept
    {
        stop_requested.store(true, std::memory_order_release);
        {
            std::lock_guard lock(lifecycle_mutex);
            open_requested = false;
            lifecycle_cv.notify_all();
        }

        if (HWND current = hwnd.load(std::memory_order_acquire);
            current && IsWindow(current))
            PostMessageW(current, WM_CLOSE, 0, 0);

        if (window_thread.joinable())
            window_thread.join();

        std::lock_guard lock(lifecycle_mutex);
        thread_running = false;
        open_requested = false;
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

    PluginCatalogSnapshot catalog_copy() const noexcept
    {
        std::lock_guard lock(model_mutex);
        return has_catalog ? catalog : PluginCatalogSnapshot{};
    }

    RackPresetUiSnapshot preset_snapshot_copy() const noexcept
    {
        std::lock_guard lock(model_mutex);
        return preset_model.has_snapshot() ? preset_model.snapshot() : RackPresetUiSnapshot{};
    }

    bool pending_copy() const noexcept
    {
        std::lock_guard lock(model_mutex);
        return model.pending_command();
    }

    bool preset_pending_copy() const noexcept
    {
        std::lock_guard lock(model_mutex);
        return preset_model.pending_command();
    }

    void dispatch(RackUiCommand command) noexcept
    {
        if (command.command_id == 0 || !command_handler)
            return;
        const RackUiCommandAck ack = command_handler(command);
        if (ack.command_id != 0)
            apply_ack(ack);
    }

    void dispatch_preset(RackPresetUiCommand command) noexcept
    {
        if (command.command_id == 0 || !preset_command_handler)
            return;
        const RackPresetUiAck ack = preset_command_handler(command);
        if (ack.command_id != 0)
            apply_preset_ack(ack);
    }

    void emit_move(RackUiSlotId slot_id, std::uint32_t target_index) noexcept
    {
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = model.request_move(slot_id, target_index);
        }
        dispatch(command);
    }

    void emit_bypass(RackUiSlotId slot_id, bool bypass) noexcept
    {
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = model.request_bypass(slot_id, bypass);
        }
        dispatch(command);
    }

    void emit_remove(RackUiSlotId slot_id) noexcept
    {
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = model.request_remove(slot_id);
        }
        dispatch(command);
    }

    void emit_open_vendor_editor(RackUiSlotId slot_id) noexcept
    {
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = model.request_open_vendor_editor(slot_id);
        }
        dispatch(command);
    }

    void emit_catalog_action(RackCatalogEntryId entry_id) noexcept
    {
        const PluginCatalogSnapshot current_catalog = catalog_copy();
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            if (browser_mode == BrowserMode::Replace)
                command = model.request_replace(browser_slot_id, current_catalog.generation, entry_id);
            else
                command = model.request_add(current_catalog.generation, entry_id,
                                            browser_target_index);
        }
        if (command.command_id != 0) {
            browser_mode = BrowserMode::None;
            browser_slot_id = 0;
            ImGui::CloseCurrentPopup();
        }
        dispatch(command);
    }

    void emit_refresh() noexcept
    {
        RackUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = model.request_refresh_catalog();
        }
        dispatch(command);
    }

    bool emit_save_as(std::string_view name) noexcept
    {
        RackPresetUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = preset_model.request_save_as(name);
        }
        if (command.command_id == 0)
            return false;
        dispatch_preset(command);
        return true;
    }

    bool emit_load(const RackPresetId& preset_id) noexcept
    {
        RackPresetUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = preset_model.request_load(preset_id);
        }
        if (command.command_id == 0)
            return false;
        dispatch_preset(command);
        return true;
    }

    bool emit_rename(const RackPresetId& preset_id, std::string_view name) noexcept
    {
        RackPresetUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = preset_model.request_rename(preset_id, name);
        }
        if (command.command_id == 0)
            return false;
        dispatch_preset(command);
        return true;
    }

    bool emit_delete(const RackPresetId& preset_id) noexcept
    {
        RackPresetUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = preset_model.request_delete(preset_id);
        }
        if (command.command_id == 0)
            return false;
        dispatch_preset(command);
        return true;
    }

    bool emit_update(const RackPresetId& preset_id) noexcept
    {
        RackPresetUiCommand command{};
        {
            std::lock_guard lock(model_mutex);
            command = preset_model.request_update(preset_id);
        }
        if (command.command_id == 0)
            return false;
        dispatch_preset(command);
        return true;
    }

    void open_add_browser(std::uint32_t target_index) noexcept
    {
        browser_mode = BrowserMode::Add;
        browser_target_index = target_index;
        browser_slot_id = 0;
        browser_open_requested = true;
    }

    void open_replace_browser(RackUiSlotId slot_id) noexcept
    {
        browser_mode = BrowserMode::Replace;
        browser_slot_id = slot_id;
        browser_target_index = 0;
        browser_open_requested = true;
    }

    void render_browser(const PluginCatalogSnapshot& current_catalog, bool pending)
    {
        if (browser_open_requested) {
            ImGui::OpenPopup("plug-in-browser");
            browser_open_requested = false;
        }

        if (!ImGui::BeginPopup("plug-in-browser"))
            return;

        ImGui::TextUnformatted(browser_mode == BrowserMode::Replace ? "Replace Effect" : "Add Effect");
        ImGui::Separator();
        ImGui::InputTextWithHint("##browser-search", "Search plug-ins...",
                                 search.data(), search.size());

        if (current_catalog.generation == 0) {
            ImGui::TextDisabled("No installed plug-in catalog yet.");
        } else {
            std::array<std::uint32_t, kRackCatalogMaxEntries> matches{};
            const std::uint32_t match_count = filter_plugin_catalog(
                current_catalog, std::string_view(search.data()), matches);
            if (current_catalog.scanning)
                ImGui::TextDisabled("Refreshing catalog... existing results remain available");
            if (match_count == 0)
                ImGui::TextDisabled("No matching VST3 effects");
            for (std::uint32_t match = 0; match < match_count; ++match) {
                const auto& entry = current_catalog.entries[matches[match]];
                const std::string name = bounded_text(entry.name.data(), entry.name.size());
                const std::string vendor = bounded_text(entry.vendor.data(), entry.vendor.size());
                const std::string category = bounded_text(entry.category.data(), entry.category.size());
                ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.entry_id)));
                ImGui::BeginDisabled(pending);
                if (ImGui::Selectable(name.empty() ? "VST3 Effect" : name.c_str()))
                    emit_catalog_action(entry.entry_id);
                ImGui::EndDisabled();
                if (!vendor.empty() || !category.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s%s%s", vendor.c_str(),
                                        (!vendor.empty() && !category.empty()) ? " · " : "",
                                        category.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndPopup();
    }

    void render_preset_dialogs(const RackPresetUiSnapshot& presets, bool pending)
    {
        if (save_as_open_requested) {
            preset_name.fill('\0');
            ImGui::OpenPopup("Save as Rack Preset");
            save_as_open_requested = false;
        }
        if (ImGui::BeginPopupModal("Save as Rack Preset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save the current Rack as a reusable preset.");
            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputTextWithHint("##save-preset-name", "Preset name",
                                     preset_name.data(), preset_name.size());
            const std::string_view name(preset_name.data());
            ImGui::BeginDisabled(pending || !rack_preset_ui_name_valid(name));
            if (ImGui::Button("Save", ImVec2(110.0f, 0.0f)) && emit_save_as(name))
                ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (rename_open_requested) {
            preset_name.fill('\0');
            const std::string_view active = rack_preset_ui_name_view(presets.active_preset_name);
            std::copy(active.begin(), active.end(), preset_name.begin());
            ImGui::OpenPopup("Rename Rack Preset");
            rename_open_requested = false;
        }
        if (ImGui::BeginPopupModal("Rename Rack Preset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Rename this preset without changing its identity or sound.");
            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputTextWithHint("##rename-preset-name", "Preset name",
                                     preset_name.data(), preset_name.size());
            const std::string_view name(preset_name.data());
            ImGui::BeginDisabled(pending || !rack_preset_id_nonzero(presets.active_preset_id) ||
                                 !rack_preset_ui_name_valid(name));
            if (ImGui::Button("Rename", ImVec2(110.0f, 0.0f)) &&
                emit_rename(presets.active_preset_id, name))
                ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (delete_open_requested) {
            ImGui::OpenPopup("Delete Rack Preset?");
            delete_open_requested = false;
        }
        if (ImGui::BeginPopupModal("Delete Rack Preset?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string active(rack_preset_ui_name_view(presets.active_preset_name));
            ImGui::Text("Delete '%s'?", active.empty() ? "selected preset" : active.c_str());
            ImGui::TextDisabled("The current working Rack will remain available.");
            ImGui::BeginDisabled(pending || !rack_preset_id_nonzero(presets.active_preset_id));
            if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f)) &&
                emit_delete(presets.active_preset_id))
                ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void render_preset_bar(const RackPresetUiSnapshot& presets, bool pending)
    {
        ImGui::TextUnformatted("Preset");
        ImGui::SameLine();
        const std::string active_name(rack_preset_ui_name_view(presets.active_preset_name));
        const char* preview = active_name.empty() ? "No preset selected" : active_name.c_str();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::BeginDisabled(pending || presets.generation == 0);
        if (ImGui::BeginCombo("##rack-preset-select", preview)) {
            if (presets.entry_count == 0)
                ImGui::TextDisabled("No saved presets yet");
            for (std::uint32_t index = 0; index < presets.entry_count; ++index) {
                const RackPresetUiEntry& entry = presets.entries[index];
                const auto id_text = preset_id_text(entry.preset_id);
                const std::string name(rack_preset_ui_name_view(entry.name));
                const bool selected = entry.preset_id == presets.active_preset_id;
                ImGui::PushID(id_text.data());
                if (ImGui::Selectable(name.c_str(), selected))
                    (void)emit_load(entry.preset_id);
                if (selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(pending || presets.generation == 0);
        if (ImGui::Button("Save As"))
            save_as_open_requested = true;
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(pending || !rack_preset_id_nonzero(presets.active_preset_id));
        if (ImGui::Button("Update"))
            (void)emit_update(presets.active_preset_id);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(pending || !rack_preset_id_nonzero(presets.active_preset_id));
        if (ImGui::Button("Preset ..."))
            ImGui::OpenPopup("preset-actions");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("preset-actions")) {
            if (ImGui::MenuItem("Rename"))
                rename_open_requested = true;
            if (ImGui::MenuItem("Delete"))
                delete_open_requested = true;
            ImGui::EndPopup();
        }

        render_preset_dialogs(presets, pending);
    }

    void render_ui()
    {
        const RackUiSnapshot snapshot = snapshot_copy();
        const PluginCatalogSnapshot current_catalog = catalog_copy();
        const RackPresetUiSnapshot presets = preset_snapshot_copy();
        const bool rack_pending = pending_copy();
        const bool preset_pending = preset_pending_copy();
        const bool pending = rack_pending || preset_pending;

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

        render_preset_bar(presets, pending);
        ImGui::Separator();

        ImGui::SetNextItemWidth(-210.0f);
        ImGui::InputTextWithHint("##rack-search", "Search plug-ins...", search.data(), search.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(pending || snapshot.slot_count >= kRackUiMaxSlots || current_catalog.generation == 0);
        if (ImGui::Button("Add Effect"))
            open_add_browser(snapshot.slot_count);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(pending || current_catalog.scanning);
        if (ImGui::Button("Refresh"))
            emit_refresh();
        ImGui::EndDisabled();
        if (current_catalog.scanning) {
            ImGui::SameLine();
            ImGui::TextDisabled("Scanning...");
        }

        ImGui::TextUnformatted("INPUT");
        for (std::uint32_t index = 0; index < snapshot.slot_count; ++index) {
            const RackUiSlotSnapshot& slot = snapshot.slots[index];
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot.slot_id)));
            ImGui::BeginChild("rack-slot-card", ImVec2(0.0f, 126.0f), ImGuiChildFlags_Borders);

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

            ImGui::BeginDisabled(pending || !rack_ui_can_bypass(slot.health));
            if (ImGui::Button(slot.bypass ? "Enable" : "Bypass"))
                emit_bypass(slot.slot_id, !slot.bypass);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(pending || !rack_ui_can_open_vendor_editor(slot));
            if (ImGui::Button("Open UI"))
                emit_open_vendor_editor(slot.slot_id);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("..."))
                ImGui::OpenPopup("slot-actions");
            if (ImGui::BeginPopup("slot-actions")) {
                ImGui::BeginDisabled(pending || index == 0);
                if (ImGui::MenuItem("Move Up"))
                    emit_move(slot.slot_id, index - 1);
                ImGui::EndDisabled();
                ImGui::BeginDisabled(pending || index + 1 >= snapshot.slot_count);
                if (ImGui::MenuItem("Move Down"))
                    emit_move(slot.slot_id, index + 1);
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::BeginDisabled(pending || !rack_ui_can_replace(slot.health) || current_catalog.generation == 0);
                if (ImGui::MenuItem("Replace"))
                    open_replace_browser(slot.slot_id);
                ImGui::EndDisabled();
                ImGui::BeginDisabled(pending || snapshot.slot_count >= kRackUiMaxSlots || current_catalog.generation == 0);
                if (ImGui::MenuItem("Insert Before"))
                    open_add_browser(index);
                if (ImGui::MenuItem("Insert After"))
                    open_add_browser(index + 1);
                ImGui::EndDisabled();
                ImGui::BeginDisabled(pending || !rack_ui_can_remove(slot.health));
                if (ImGui::MenuItem("Remove"))
                    emit_remove(slot.slot_id);
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }

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
        if (snapshot.slot_count == 0)
            ImGui::TextDisabled("Rack is empty. Search plug-ins and choose Add Effect.");
        ImGui::TextUnformatted("OUTPUT TO OBS");

        render_browser(current_catalog, pending);
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

    bool run_window_session() noexcept
    {
        WindowRuntime runtime{};
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND window = CreateWindowExW(
            0, kRackEditorWindowClassName, L"OBS Safe VST3 Rack",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
            nullptr, nullptr, instance, &runtime);
        if (!window) {
            mark_creation(false);
            return false;
        }

        if (!create_d3d(window, runtime.d3d, D3D_DRIVER_TYPE_HARDWARE) &&
            !create_d3d(window, runtime.d3d, D3D_DRIVER_TYPE_WARP)) {
            DestroyWindow(window);
            mark_creation(false);
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        const bool imgui_win32 = ImGui_ImplWin32_Init(window);
        const bool imgui_dx11 = imgui_win32 &&
                                ImGui_ImplDX11_Init(runtime.d3d.device, runtime.d3d.context);
        if (!imgui_dx11) {
            if (imgui_win32)
                ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            release_d3d(runtime.d3d);
            DestroyWindow(window);
            mark_creation(false);
            return false;
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
                    stop_requested.store(true, std::memory_order_release);
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
        return true;
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

        for (;;) {
            {
                std::unique_lock lifecycle_lock(lifecycle_mutex);
                lifecycle_cv.wait(lifecycle_lock, [&] {
                    return stop_requested.load(std::memory_order_acquire) || open_requested;
                });
                if (stop_requested.load(std::memory_order_acquire))
                    break;
                open_requested = false;
            }
            run_window_session();
        }

        hwnd.store(nullptr, std::memory_order_release);
        UnregisterClassW(kRackEditorWindowClassName, instance);
        mark_stopped();
    }

    RackUiCommandHandler command_handler;
    RackPresetUiCommandHandler preset_command_handler;
    mutable std::mutex model_mutex;
    RackEditorModel model;
    RackPresetEditorModel preset_model;
    PluginCatalogSnapshot catalog{};
    bool has_catalog = false;

    std::array<char, 160> search{};
    BrowserMode browser_mode = BrowserMode::None;
    RackUiSlotId browser_slot_id = 0;
    std::uint32_t browser_target_index = 0;
    bool browser_open_requested = false;

    std::array<char, kRackPresetUiNameBytes> preset_name{};
    bool save_as_open_requested = false;
    bool rename_open_requested = false;
    bool delete_open_requested = false;

    std::mutex lifecycle_mutex;
    std::condition_variable lifecycle_cv;
    std::thread window_thread;
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> stop_requested{false};
    bool thread_running = false;
    bool open_requested = false;
    bool creation_attempted = false;
    bool creation_succeeded = false;
};

RackEditorWindow::RackEditorWindow(RackUiCommandHandler command_handler,
                                   RackPresetUiCommandHandler preset_command_handler)
    : impl_(std::make_unique<Impl>(std::move(command_handler),
                                   std::move(preset_command_handler)))
{
}

RackEditorWindow::~RackEditorWindow() = default;

bool RackEditorWindow::publish_snapshot(const RackUiSnapshot& snapshot) noexcept
{
    return impl_->publish_snapshot(snapshot);
}

bool RackEditorWindow::publish_catalog(const PluginCatalogSnapshot& snapshot) noexcept
{
    return impl_->publish_catalog(snapshot);
}

bool RackEditorWindow::publish_presets(const RackPresetUiSnapshot& snapshot) noexcept
{
    return impl_->publish_presets(snapshot);
}

bool RackEditorWindow::apply_ack(const RackUiCommandAck& ack) noexcept
{
    return impl_->apply_ack(ack);
}

bool RackEditorWindow::apply_preset_ack(const RackPresetUiAck& ack) noexcept
{
    return impl_->apply_preset_ack(ack);
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
