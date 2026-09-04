#ifdef _WIN32

#include "rack/rack_vendor_editor_manager.hpp"

#include "host/hosted_plugin.hpp"
#include "host/native_editor.hpp"

#include <array>
#include <utility>

#include <windows.h>

namespace safevst3::rack::ui {

struct RackVendorEditorManager::Impl {
    struct Entry {
        RackSlotId slot_id = 0;
        std::unique_ptr<NativeEditorWindow> window;
    };

    std::array<Entry, kRackMaxSlots> entries{};

    Entry* find(RackSlotId slot_id) noexcept
    {
        for (auto& entry : entries) {
            if (entry.slot_id == slot_id)
                return &entry;
        }
        return nullptr;
    }

    const Entry* find(RackSlotId slot_id) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.slot_id == slot_id)
                return &entry;
        }
        return nullptr;
    }

    Entry* free_entry() noexcept
    {
        for (auto& entry : entries) {
            if (entry.slot_id == 0)
                return &entry;
        }
        return nullptr;
    }
};

RackVendorEditorManager::RackVendorEditorManager()
    : impl_(std::make_unique<Impl>())
{
}

RackVendorEditorManager::~RackVendorEditorManager()
{
    close_all();
}

bool RackVendorEditorManager::open(RackSlotId slot_id,
                                   HostedPlugin& plugin,
                                   const std::string& title,
                                   std::string& error) noexcept
{
    if (!impl_ || slot_id == 0 || !plugin.edit_controller()) {
        error = "VST3 has no edit controller";
        return false;
    }

    if (Impl::Entry* existing = impl_->find(slot_id)) {
        if (!existing->window) {
            error = "Rack vendor editor entry is invalid";
            return false;
        }
        return existing->window->open(plugin.edit_controller(), title, error);
    }

    Impl::Entry* entry = impl_->free_entry();
    if (!entry) {
        error = "Rack vendor editor limit reached";
        return false;
    }

    auto window = std::make_unique<NativeEditorWindow>();
    if (!window->open(plugin.edit_controller(), title, error))
        return false;

    entry->slot_id = slot_id;
    entry->window = std::move(window);
    return true;
}

void RackVendorEditorManager::close(RackSlotId slot_id) noexcept
{
    if (!impl_ || slot_id == 0)
        return;
    Impl::Entry* entry = impl_->find(slot_id);
    if (!entry)
        return;
    if (entry->window)
        entry->window->close();
    entry->window.reset();
    entry->slot_id = 0;
}

void RackVendorEditorManager::close_all() noexcept
{
    if (!impl_)
        return;
    for (auto& entry : impl_->entries) {
        if (entry.window)
            entry.window->close();
        entry.window.reset();
        entry.slot_id = 0;
    }
}

void RackVendorEditorManager::pump_messages() noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

std::size_t RackVendorEditorManager::open_count() const noexcept
{
    if (!impl_)
        return 0;
    std::size_t count = 0;
    for (const auto& entry : impl_->entries) {
        if (entry.slot_id != 0 && entry.window && entry.window->created())
            ++count;
    }
    return count;
}

bool RackVendorEditorManager::created(RackSlotId slot_id) const noexcept
{
    if (!impl_ || slot_id == 0)
        return false;
    const Impl::Entry* entry = impl_->find(slot_id);
    return entry && entry->window && entry->window->created();
}

bool RackVendorEditorManager::visible(RackSlotId slot_id) const noexcept
{
    if (!impl_ || slot_id == 0)
        return false;
    const Impl::Entry* entry = impl_->find(slot_id);
    return entry && entry->window && entry->window->visible();
}

} // namespace safevst3::rack::ui

#endif
