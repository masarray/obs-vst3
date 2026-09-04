#include "rack/rack_preset_ui_contract.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>

using namespace safevst3::rack;
using namespace safevst3::rack::ui;

namespace {

RackPresetId make_id(unsigned seed)
{
    RackPresetId id{};
    for (std::size_t i = 0; i < id.size(); ++i)
        id[i] = static_cast<std::uint8_t>(seed + i + 1u);
    return id;
}

void set_name(auto& destination, const char* text)
{
    std::fill(destination.begin(), destination.end(), '\0');
    std::strncpy(destination.data(), text, destination.size() - 1);
}

RackPresetUiSnapshot make_snapshot(std::uint64_t generation,
                                   const RackPresetId& a,
                                   const RackPresetId& b)
{
    RackPresetUiSnapshot snapshot{};
    snapshot.generation = generation;
    snapshot.entry_count = 2;
    snapshot.entries[0].preset_id = a;
    set_name(snapshot.entries[0].name, "Broadcast Vocal");
    snapshot.entries[1].preset_id = b;
    set_name(snapshot.entries[1].name, "Music Master");
    return snapshot;
}

} // namespace

int main()
{
    const RackPresetId a = make_id(10);
    const RackPresetId b = make_id(50);

    RackPresetEditorModel model;
    auto snapshot = make_snapshot(1, a, b);
    assert(model.publish_snapshot(snapshot));
    assert(model.has_snapshot());

    // Save As is explicit and bounded by name; accepted mutation waits for a
    // newer authoritative preset snapshot rather than optimistically adding.
    RackPresetUiCommand save = model.request_save_as("Dialogue Clean");
    assert(save.command_id != 0);
    assert(save.type == RackPresetUiCommandType::SaveAs);
    assert(!rack_preset_id_nonzero(save.preset_id));
    RackPresetUiAck save_ack{};
    save_ack.command_id = save.command_id;
    save_ack.result = RackPresetUiCommandResult::Accepted;
    save_ack.committed_generation = 2;
    save_ack.preset_id = make_id(90);
    assert(model.apply_ack(save_ack));
    assert(model.pending_command());
    snapshot = make_snapshot(2, a, b);
    snapshot.entry_count = 3;
    snapshot.entries[2].preset_id = save_ack.preset_id;
    set_name(snapshot.entries[2].name, "Dialogue Clean");
    snapshot.active_preset_id = save_ack.preset_id;
    set_name(snapshot.active_preset_name, "Dialogue Clean");
    assert(model.publish_snapshot(snapshot));
    assert(!model.pending_command());

    // Load materializes detached working state and then publishes the selected
    // preset as authoritative library/UI context. It never mutates the preset.
    RackPresetUiCommand load = model.request_load(a);
    assert(load.type == RackPresetUiCommandType::Load);
    RackPresetUiAck load_ack{};
    load_ack.command_id = load.command_id;
    load_ack.result = RackPresetUiCommandResult::Accepted;
    load_ack.committed_generation = 3;
    load_ack.preset_id = a;
    assert(model.apply_ack(load_ack));
    assert(model.pending_command());
    snapshot.generation = 3;
    snapshot.active_preset_id = a;
    set_name(snapshot.active_preset_name, "Broadcast Vocal");
    assert(model.publish_snapshot(snapshot));
    assert(!model.pending_command());

    // Rename preserves preset identity; Update is explicit; Delete is scoped
    // only to the selected stable preset ID.
    RackPresetUiCommand rename = model.request_rename(a, "Broadcast Vocal v2");
    assert(rename.type == RackPresetUiCommandType::Rename);
    assert(rename.preset_id == a);
    RackPresetUiAck reject{};
    reject.command_id = rename.command_id;
    reject.result = RackPresetUiCommandResult::Rejected;
    assert(model.apply_ack(reject));
    assert(!model.pending_command());

    RackPresetUiCommand update = model.request_update(a);
    assert(update.type == RackPresetUiCommandType::Update);
    RackPresetUiAck failed{};
    failed.command_id = update.command_id;
    failed.result = RackPresetUiCommandResult::Failed;
    assert(model.apply_ack(failed));

    RackPresetUiCommand remove = model.request_delete(b);
    assert(remove.type == RackPresetUiCommandType::Delete);
    assert(remove.preset_id == b);

    // Duplicate IDs and stale snapshots are invalid; known-good remains.
    RackPresetUiSnapshot malformed = snapshot;
    malformed.generation = 4;
    malformed.entries[1].preset_id = malformed.entries[0].preset_id;
    assert(!model.publish_snapshot(malformed));
    assert(model.snapshot().generation == 3);

    return 0;
}
