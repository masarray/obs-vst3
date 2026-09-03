#include "rack/rack_slot_workflow.hpp"
#include "rack/rack_ui_contract.hpp"

#include <array>
#include <cassert>
#include <cstring>

using namespace safevst3::rack::ui;

namespace {
template <std::size_t N>
void put(std::array<char, N>& dst, const char* text)
{
    dst.fill('\0');
    std::strncpy(dst.data(), text, N - 1);
}

RackUiSnapshot base_snapshot(std::uint64_t generation = 10)
{
    RackUiSnapshot s{};
    s.generation = generation;
    s.slot_count = 2;
    s.slots[0].slot_id = 41;
    s.slots[1].slot_id = 77;
    put(s.slots[0].plugin_name, "Alpha EQ");
    put(s.slots[1].plugin_name, "Beta Comp");
    return s;
}

RackUiSnapshot full_snapshot()
{
    RackUiSnapshot s{};
    s.generation = 50;
    s.slot_count = kRackUiMaxSlots;
    for (std::uint32_t i = 0; i < s.slot_count; ++i)
        s.slots[i].slot_id = 1000 + i;
    return s;
}
}

int main()
{
    PluginCatalogSnapshot catalog{};
    catalog.generation = 3;
    catalog.entry_count = 3;
    catalog.entries[0].entry_id = 101;
    put(catalog.entries[0].name, "FabFilter Pro-Q 3");
    put(catalog.entries[0].vendor, "FabFilter");
    put(catalog.entries[0].category, "EQ");
    catalog.entries[1].entry_id = 102;
    put(catalog.entries[1].name, "RX Spectral De-noise");
    put(catalog.entries[1].vendor, "iZotope");
    put(catalog.entries[1].category, "Restoration");
    catalog.entries[2].entry_id = 103;
    put(catalog.entries[2].name, "Pro-C 2");
    put(catalog.entries[2].vendor, "FabFilter");
    put(catalog.entries[2].category, "Dynamics");
    assert(validate_plugin_catalog_snapshot(catalog));

    std::array<std::uint32_t, kRackCatalogMaxEntries> matches{};
    assert(filter_plugin_catalog(catalog, "fab", matches) == 2);
    assert(matches[0] == 0 && matches[1] == 2);
    assert(filter_plugin_catalog(catalog, "restoration", matches) == 1);
    assert(matches[0] == 1);
    assert(filter_plugin_catalog(catalog, "PRO-Q", matches) == 1);
    assert(matches[0] == 0);

    PluginCatalogSnapshot bad_catalog = catalog;
    bad_catalog.entries[2].entry_id = 101;
    assert(!validate_plugin_catalog_snapshot(bad_catalog));
    bad_catalog = catalog;
    bad_catalog.entry_count = kRackCatalogMaxEntries + 1;
    assert(!validate_plugin_catalog_snapshot(bad_catalog));
    bad_catalog = catalog;
    bad_catalog.entries[0].name.fill('x');
    assert(!validate_plugin_catalog_snapshot(bad_catalog));

    RackEditorModel model;
    const RackUiSnapshot initial = base_snapshot();
    assert(model.publish_snapshot(initial));

    // Accepted move is correlated but must not optimistically change card order.
    const RackUiCommand accepted_move = model.request_move(41, 1);
    assert(accepted_move.command_id != 0 && accepted_move.type == RackUiCommandType::MoveSlot);
    assert(accepted_move.slot_id == 41 && accepted_move.target_index == 1);
    assert(model.snapshot().slots[0].slot_id == 41);
    assert(model.apply_ack({accepted_move.command_id, RackUiCommandResult::Accepted, 11}));
    assert(model.pending_command());
    assert(model.snapshot().slots[0].slot_id == 41);

    RackUiSnapshot moved = initial;
    moved.generation = 11;
    moved.slots[0] = initial.slots[1];
    moved.slots[1] = initial.slots[0];
    assert(model.publish_snapshot(moved));
    assert(!model.pending_command());
    assert(model.snapshot().slots[0].slot_id == 77 && model.snapshot().slots[1].slot_id == 41);

    // Rejected and failed moves preserve the authoritative order.
    const RackUiCommand rejected_move = model.request_move(41, 0);
    assert(rejected_move.command_id != 0);
    assert(model.apply_ack({rejected_move.command_id, RackUiCommandResult::Rejected, 11}));
    assert(!model.pending_command());
    assert(model.snapshot().slots[0].slot_id == 77);

    const RackUiCommand failed_move = model.request_move(41, 0);
    assert(failed_move.command_id != 0);
    assert(model.apply_ack({failed_move.command_id, RackUiCommandResult::Failed, 11}));
    assert(!model.pending_command());
    assert(model.snapshot().slots[0].slot_id == 77);

    // Malformed/truncated-equivalent Rack snapshots never replace last-known-good.
    RackUiSnapshot malformed = moved;
    malformed.generation = 12;
    malformed.slot_count = kRackUiMaxSlots + 1;
    assert(!model.publish_snapshot(malformed));
    assert(model.snapshot().generation == 11 && model.snapshot().slots[0].slot_id == 77);
    RackUiSnapshot duplicate = moved;
    duplicate.generation = 12;
    duplicate.slots[1].slot_id = duplicate.slots[0].slot_id;
    assert(!model.publish_snapshot(duplicate));
    assert(model.snapshot().generation == 11);

    // Insert position is bounded, and a full eight-slot Rack cannot accept Add.
    assert(model.request_add(catalog.generation, 101, model.snapshot().slot_count + 1).command_id == 0);
    RackEditorModel full_model;
    assert(full_model.publish_snapshot(full_snapshot()));
    assert(full_model.request_add(catalog.generation, 101, 0).command_id == 0);

    const RackUiCommand add = model.request_add(catalog.generation, 101, 1);
    assert(add.command_id != 0 && add.type == RackUiCommandType::AddSlot);
    assert(add.catalog_entry_id == 101 && add.catalog_generation == catalog.generation);
    assert(add.target_index == 1);
    assert(model.apply_ack({add.command_id, RackUiCommandResult::Accepted, 12}));
    RackUiSnapshot after_add{};
    after_add.generation = 12;
    after_add.slot_count = 3;
    after_add.slots[0] = moved.slots[0];
    after_add.slots[1].slot_id = 88;
    put(after_add.slots[1].plugin_name, "FabFilter Pro-Q 3");
    after_add.slots[2] = moved.slots[1];
    assert(model.publish_snapshot(after_add));
    assert(!model.pending_command());

    const RackUiCommand replace = model.request_replace(88, catalog.generation, 102);
    assert(replace.command_id != 0 && replace.type == RackUiCommandType::ReplaceSlot);
    assert(replace.slot_id == 88 && replace.catalog_entry_id == 102);
    assert(model.apply_ack({replace.command_id, RackUiCommandResult::Failed, 12}));

    const RackUiCommand bypass = model.request_bypass(88, true);
    assert(bypass.command_id != 0 && bypass.type == RackUiCommandType::SetBypass);
    assert(bypass.slot_id == 88 && bypass.bypass);
    assert(model.apply_ack({bypass.command_id, RackUiCommandResult::Rejected, 12}));

    const RackUiCommand remove = model.request_remove(88);
    assert(remove.command_id != 0 && remove.type == RackUiCommandType::RemoveSlot);
    assert(remove.slot_id == 88);
    assert(model.apply_ack({remove.command_id, RackUiCommandResult::Rejected, 12}));

    // Catalog refresh has its own immutable generation stream; accepting it must
    // clear Pending without inventing a Rack DSP generation.
    const RackUiCommand refresh = model.request_refresh_catalog();
    assert(refresh.command_id != 0 && refresh.type == RackUiCommandType::RefreshCatalog);
    assert(model.apply_ack({refresh.command_id, RackUiCommandResult::Accepted, 12}));
    assert(!model.pending_command());
    assert(model.snapshot().generation == 12);

    // Duplicate command IDs resolve from a bounded replay ledger rather than
    // executing a second topology mutation.
    RackUiCommandReplayGuard replay;
    RackUiCommandAck cached{};
    assert(!replay.lookup(remove, cached));
    const RackUiCommandAck replay_ack{remove.command_id, RackUiCommandResult::Accepted, 13};
    replay.remember(remove, replay_ack);
    assert(replay.lookup(remove, cached));
    assert(cached.command_id == replay_ack.command_id && cached.committed_generation == 13);
    const RackUiCommandAck replay_update{remove.command_id, RackUiCommandResult::Rejected, 12};
    replay.remember(remove, replay_update);
    assert(replay.lookup(remove, cached));
    assert(cached.result == RackUiCommandResult::Rejected && cached.committed_generation == 12);

    // Missing/Quarantined placeholders stay visible and removable/replaceable,
    // but cannot issue an active bypass command for a plug-in that is not live.
    assert(rack_ui_can_replace(RackUiSlotHealth::Missing));
    assert(rack_ui_can_remove(RackUiSlotHealth::Missing));
    assert(!rack_ui_can_bypass(RackUiSlotHealth::Missing));
    assert(rack_ui_can_replace(RackUiSlotHealth::Quarantined));
    assert(rack_ui_can_remove(RackUiSlotHealth::Quarantined));
    assert(!rack_ui_can_bypass(RackUiSlotHealth::Quarantined));
    assert(rack_ui_can_bypass(RackUiSlotHealth::Ready));

    return 0;
}
