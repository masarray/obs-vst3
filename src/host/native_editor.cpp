#ifdef _WIN32

#include "host/native_editor.hpp"

#include "pluginterfaces/base/funknown.h"

#include <algorithm>
#include <sstream>

namespace safevst3 {

namespace {
constexpr wchar_t kWindowClassName[] = L"ObsSafeVst3NativeEditorWindow";

std::string win_error(const char* what)
{
    std::ostringstream stream;
    stream << what << " failed (Win32 error " << GetLastError() << ')';
    return stream.str();
}

void activate_editor_window(HWND window) noexcept
{
    if (!window || !IsWindow(window))
        return;

    // The editor lives in the isolated helper process while the user's click
    // originates in OBS. SetForegroundWindow alone is therefore allowed to be
    // rejected by Windows foreground-lock rules. Temporarily join the current
    // foreground input queue, then promote the editor to the top of the normal
    // z-order. The TOPMOST transition is immediately undone: this is an
    // activation boost, not a persistent always-on-top policy.
    const HWND foreground = GetForegroundWindow();
    const DWORD current_thread = GetCurrentThreadId();
    const DWORD foreground_thread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr)
        : 0;
    const bool attached_input = foreground_thread != 0 &&
                                foreground_thread != current_thread &&
                                AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE;

    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);

    constexpr UINT position_flags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW;
    (void)SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, position_flags);
    (void)SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, position_flags);
    (void)BringWindowToTop(window);
    (void)SetForegroundWindow(window);
    (void)SetActiveWindow(window);
    (void)SetFocus(window);

    if (attached_input)
        (void)AttachThreadInput(current_thread, foreground_thread, FALSE);
}
} // namespace

NativeEditorWindow::~NativeEditorWindow()
{
    close();
}

std::wstring NativeEditorWindow::widen(const std::string& value)
{
    if (value.empty())
        return L"VST3 Plug-in";
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return L"VST3 Plug-in";
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

bool NativeEditorWindow::ensure_window_class(std::string& error)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &NativeEditorWindow::window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&wc) != 0)
        return true;
    if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        return true;

    error = win_error("RegisterClassExW");
    return false;
}

bool NativeEditorWindow::open(Steinberg::Vst::IEditController* controller,
                              const std::string& title,
                              std::string& error)
{
    if (window_ && view_) {
        activate_editor_window(window_);
        return true;
    }

    if (!controller) {
        error = "VST3 has no edit controller";
        return false;
    }

    if (!ensure_window_class(error))
        return false;

    view_ = controller->createView(Steinberg::Vst::ViewType::kEditor);
    if (!view_) {
        error = "VST3 does not provide a native editor";
        return false;
    }

    if (view_->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
        error = "VST3 native editor does not support HWND";
        view_->release();
        view_ = nullptr;
        return false;
    }

    Steinberg::ViewRect view_size{};
    if (view_->getSize(&view_size) != Steinberg::kResultTrue ||
        view_size.getWidth() <= 0 || view_size.getHeight() <= 0) {
        error = "VST3 native editor did not provide a valid size";
        view_->release();
        view_ = nullptr;
        return false;
    }

    const int client_width = std::max<int>(1, view_size.getWidth());
    const int client_height = std::max<int>(1, view_size.getHeight());
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (view_->canResize() == Steinberg::kResultTrue)
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;

    RECT outer{0, 0, client_width, client_height};
    AdjustWindowRectEx(&outer, style, FALSE, 0);

    window_ = CreateWindowExW(0,
                              kWindowClassName,
                              widen(title).c_str(),
                              style,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              outer.right - outer.left,
                              outer.bottom - outer.top,
                              nullptr,
                              nullptr,
                              GetModuleHandleW(nullptr),
                              this);
    if (!window_) {
        error = win_error("CreateWindowExW");
        view_->release();
        view_ = nullptr;
        return false;
    }

    if (view_->setFrame(this) != Steinberg::kResultTrue) {
        error = "VST3 native editor rejected IPlugFrame";
        DestroyWindow(window_);
        window_ = nullptr;
        view_->release();
        view_ = nullptr;
        return false;
    }

    if (view_->attached(window_, Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
        error = "Attaching VST3 native editor to helper HWND failed";
        view_->setFrame(nullptr);
        DestroyWindow(window_);
        window_ = nullptr;
        view_->release();
        view_ = nullptr;
        return false;
    }
    attached_ = true;

    UpdateWindow(window_);
    activate_editor_window(window_);
    return true;
}

void NativeEditorWindow::hide() noexcept
{
    if (window_)
        ShowWindow(window_, SW_HIDE);
}

void NativeEditorWindow::close() noexcept
{
    if (view_) {
        if (attached_)
            (void)view_->removed();
        attached_ = false;
        view_->setFrame(nullptr);
    }

    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }

    if (view_) {
        view_->release();
        view_ = nullptr;
    }
}

bool NativeEditorWindow::visible() const noexcept
{
    return window_ && IsWindowVisible(window_) != FALSE;
}

void NativeEditorWindow::resize_client(int width, int height) noexcept
{
    if (!window_)
        return;

    RECT window_rect{};
    RECT client_rect{};
    if (!GetWindowRect(window_, &window_rect) || !GetClientRect(window_, &client_rect))
        return;

    const int frame_width = (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
    const int frame_height = (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);

    resize_guard_ = true;
    SetWindowPos(window_, nullptr, 0, 0,
                 std::max(1, width + frame_width),
                 std::max(1, height + frame_height),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    resize_guard_ = false;
}

void NativeEditorWindow::notify_view_size(int width, int height) noexcept
{
    if (!view_ || !attached_ || width <= 0 || height <= 0)
        return;
    Steinberg::ViewRect size{0, 0, width, height};
    (void)view_->onSize(&size);
}

Steinberg::tresult PLUGIN_API NativeEditorWindow::resizeView(Steinberg::IPlugView* view,
                                                             Steinberg::ViewRect* new_size)
{
    if (!view || !new_size || view != view_ || !window_)
        return Steinberg::kInvalidArgument;

    const int width = std::max<int>(1, new_size->getWidth());
    const int height = std::max<int>(1, new_size->getHeight());
    resize_client(width, height);
    notify_view_size(width, height);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API NativeEditorWindow::queryInterface(const Steinberg::TUID iid, void** obj)
{
    if (!obj)
        return Steinberg::kInvalidArgument;
    *obj = nullptr;
    if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
        *obj = static_cast<Steinberg::IPlugFrame*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    return Steinberg::kNoInterface;
}

LRESULT CALLBACK NativeEditorWindow::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NativeEditorWindow* self = reinterpret_cast<NativeEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<NativeEditorWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (!self)
        return DefWindowProcW(hwnd, message, wparam, lparam);

    switch (message) {
    case WM_CLOSE:
        self->hide();
        return 0;
    case WM_SIZE:
        if (!self->resize_guard_)
            self->notify_view_size(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_SETFOCUS:
        if (self->view_)
            (void)self->view_->onFocus(true);
        break;
    case WM_KILLFOCUS:
        if (self->view_)
            (void)self->view_->onFocus(false);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace safevst3

#endif
