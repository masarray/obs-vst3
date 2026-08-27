#ifdef _WIN32

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

struct DummySlot {
    std::uint64_t id;
    const char* name;
    const char* status;
};

struct MoveCommand {
    std::uint64_t moved_slot_id = 0;
    std::uint64_t before_slot_id = 0;
};

class Ui0Model {
public:
    const std::array<DummySlot, 3>& slots() const noexcept { return slots_; }
    const std::vector<MoveCommand>& commands() const noexcept { return commands_; }

    void set_search(std::string value) { search_ = std::move(value); }
    const std::string& search() const noexcept { return search_; }

    bool visible(const DummySlot& slot) const
    {
        if (search_.empty())
            return true;
        return contains_case_insensitive(slot.name, search_) ||
               contains_case_insensitive(slot.status, search_);
    }

    void request_move(std::uint64_t moved_slot_id, std::uint64_t before_slot_id)
    {
        if (moved_slot_id == 0 || before_slot_id == 0 || moved_slot_id == before_slot_id)
            return;
        commands_.push_back({moved_slot_id, before_slot_id});
        // UI-0 law: emit intent only. The authoritative order is immutable until
        // a future control-plane snapshot publishes a new generation.
    }

private:
    static bool contains_case_insensitive(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
            return true;
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        std::string h(haystack);
        std::string n(needle);
        std::transform(h.begin(), h.end(), h.begin(), lower);
        std::transform(n.begin(), n.end(), n.begin(), lower);
        return h.find(n) != std::string::npos;
    }

    std::array<DummySlot, 3> slots_{{
        {101, "Warm EQ", "Ready"},
        {202, "Glue Compressor", "Ready"},
        {303, "Air Enhancer", "Bypassed"},
    }};
    std::string search_;
    std::vector<MoveCommand> commands_;
};

struct D3dState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
};

D3dState* g_d3d = nullptr;

void release_render_target(D3dState& d3d) noexcept
{
    if (d3d.render_target) {
        d3d.render_target->Release();
        d3d.render_target = nullptr;
    }
}

bool create_render_target(D3dState& d3d) noexcept
{
    ID3D11Texture2D* back_buffer = nullptr;
    if (!d3d.swap_chain ||
        FAILED(d3d.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || !back_buffer)
        return false;
    const HRESULT result = d3d.device->CreateRenderTargetView(back_buffer, nullptr, &d3d.render_target);
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

bool create_d3d(HWND window, D3dState& d3d, const D3D_DRIVER_TYPE driver) noexcept
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained{};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, driver, nullptr, 0, requested, static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION, &desc, &d3d.swap_chain, &d3d.device, &obtained, &d3d.context);
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

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
        return 1;

    if (message == WM_SIZE && g_d3d && g_d3d->swap_chain && wparam != SIZE_MINIMIZED) {
        release_render_target(*g_d3d);
        const UINT width = static_cast<UINT>(LOWORD(lparam));
        const UINT height = static_cast<UINT>(HIWORD(lparam));
        if (FAILED(g_d3d->swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)) ||
            !create_render_target(*g_d3d))
            return 0;
        return 0;
    }
    if (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_KEYMENU)
        return 0;
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void render_proof_ui(Ui0Model& model)
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Safe VST3 Rack UI-0", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    char search[96]{};
    std::snprintf(search, sizeof(search), "%s", model.search().c_str());
    if (ImGui::InputTextWithHint("##search", "Search plug-ins...", search, sizeof(search)))
        model.set_search(search);
    ImGui::SameLine();
    ImGui::TextUnformatted("UI-0 PROOF");
    ImGui::Separator();

    for (const DummySlot& slot : model.slots()) {
        if (!model.visible(slot))
            continue;

        ImGui::PushID(static_cast<int>(slot.id));
        ImGui::BeginChild("slot-card", ImVec2(0.0f, 82.0f), ImGuiChildFlags_Borders);
        ImGui::Text("%s", slot.name);
        ImGui::Text("Status: %s", slot.status);
        ImGui::TextDisabled("Slot ID: %llu", static_cast<unsigned long long>(slot.id));

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::uint64_t payload_id = slot.id;
            ImGui::SetDragDropPayload("UI0_SLOT_ID", &payload_id, sizeof(payload_id));
            ImGui::Text("Move %s", slot.name);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI0_SLOT_ID")) {
                if (payload->DataSize == sizeof(std::uint64_t)) {
                    const auto moved = *static_cast<const std::uint64_t*>(payload->Data);
                    model.request_move(moved, slot.id);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    ImGui::End();
}

bool pump_messages(HWND window) noexcept
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return IsWindow(window) != FALSE;
}

bool render_frame(HWND window, D3dState& d3d, Ui0Model& model)
{
    if (!pump_messages(window) || !d3d.render_target)
        return false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    render_proof_ui(model);
    ImGui::Render();

    constexpr float clear[4]{0.08f, 0.08f, 0.09f, 1.0f};
    d3d.context->OMSetRenderTargets(1, &d3d.render_target, nullptr);
    d3d.context->ClearRenderTargetView(d3d.render_target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return SUCCEEDED(d3d.swap_chain->Present(0, 0));
}

bool verify_model_contract(Ui0Model& model)
{
    const auto original = model.slots();
    if (original.size() != 3)
        return false;

    model.set_search("compress");
    std::size_t visible_count = 0;
    for (const auto& slot : model.slots())
        visible_count += model.visible(slot) ? 1u : 0u;
    if (visible_count != 1 || !model.visible(model.slots()[1]))
        return false;

    model.set_search({});
    model.request_move(303, 101);
    if (model.commands().size() != 1 || model.commands().front().moved_slot_id != 303 ||
        model.commands().front().before_slot_id != 101)
        return false;

    // The command is only intent; authoritative order must not change.
    for (std::size_t i = 0; i < original.size(); ++i) {
        if (model.slots()[i].id != original[i].id)
            return false;
    }
    return true;
}

bool run_window_cycle(HINSTANCE instance, const wchar_t* class_name, Ui0Model& model, int cycle)
{
    HWND window = CreateWindowExW(
        0, class_name, L"Safe VST3 Rack UI-0 Proof", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 560, nullptr, nullptr, instance, nullptr);
    if (!window)
        return false;

    D3dState d3d{};
    g_d3d = &d3d;
    if (!create_d3d(window, d3d, D3D_DRIVER_TYPE_HARDWARE) &&
        !create_d3d(window, d3d, D3D_DRIVER_TYPE_WARP)) {
        g_d3d = nullptr;
        DestroyWindow(window);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(d3d.device, d3d.context)) {
        if (ImGui::GetCurrentContext()) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        release_d3d(d3d);
        g_d3d = nullptr;
        DestroyWindow(window);
        return false;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    bool ok = true;
    for (int frame = 0; frame < 4 && ok; ++frame)
        ok = render_frame(window, d3d, model);

    if (ok) {
        const int width = 700 + cycle * 24;
        const int height = 500 + cycle * 18;
        SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (int frame = 0; frame < 3 && ok; ++frame)
            ok = render_frame(window, d3d, model);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_d3d(d3d);
    g_d3d = nullptr;
    if (IsWindow(window))
        DestroyWindow(window);
    return ok;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Ui0Model model;
    if (!verify_model_contract(model))
        return 10;

    constexpr wchar_t class_name[] = L"SafeVst3RackUi0SmokeWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = class_name;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 11;

    bool ok = true;
    for (int cycle = 0; cycle < 3 && ok; ++cycle)
        ok = run_window_cycle(instance, class_name, model, cycle);

    UnregisterClassW(class_name, instance);
    if (!ok)
        return 12;

    const auto& slots = model.slots();
    if (slots[0].id != 101 || slots[1].id != 202 || slots[2].id != 303)
        return 13;

    std::printf("UI-0 ImGui Win32/D3D11 create-render-resize-close-reopen proof passed; commands=%zu\n",
                model.commands().size());
    return 0;
}

#endif
