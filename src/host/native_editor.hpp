#pragma once

#ifdef _WIN32

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <windows.h>

#include <string>

namespace safevst3 {

class NativeEditorWindow final : public Steinberg::IPlugFrame {
public:
    NativeEditorWindow() = default;
    ~NativeEditorWindow();

    NativeEditorWindow(const NativeEditorWindow&) = delete;
    NativeEditorWindow& operator=(const NativeEditorWindow&) = delete;

    bool open(Steinberg::Vst::IEditController* controller,
              const std::string& title,
              std::string& error);
    void hide() noexcept;
    void close() noexcept;

    bool visible() const noexcept;
    bool created() const noexcept { return window_ != nullptr && view_ != nullptr; }

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
                                             Steinberg::ViewRect* new_size) override;
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static bool ensure_window_class(std::string& error);
    static std::wstring widen(const std::string& value);

    void resize_client(int width, int height) noexcept;
    void notify_view_size(int width, int height) noexcept;

    HWND window_ = nullptr;
    Steinberg::IPlugView* view_ = nullptr;
    bool attached_ = false;
    bool resize_guard_ = false;
};

} // namespace safevst3

#endif
