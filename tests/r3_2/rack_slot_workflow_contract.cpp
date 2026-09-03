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

RackUiSnapshot base_snapshot()
{
    RackUiSnapshot s{};
    s.generation = 10;
    s.slot_count = 2;
    s.slots[0].slot_id = 41;
    s.slots[1].slot_id = 77;
    put(s.slots[0].plugin_name, "Alpha EQ");
    put(s.slots[1].plugin_name, "Beta Comp");
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

    PluginCatalogSnapshot bad_catalog = catalog;
    bad_catalog.entries[2].entry_id = 101;
    assert(!validate_plugin_catalog_snapshot(bad_catalog));
    bad_catalog = catalog;
    bad_catalog.entry_count = kRackCatalogMaxEntries + 1;
    assert(!validate_plugin_catalog_snapshot(bad_catalog));

    RackEditorModel model;
    const RackUiSnapshot initial = base_snapshot();
    assert(model.publish_snapshot(initial));

    const RackUiCommand move = model.request_move(41, 1);
    assert(move.command_id != 0 && move.type == RackUiCommandType::MoveSlot);
    assert(model.snapshot().slots[0].slot_id == 41); // no optimistic mutation
    RackUiCommandAck rejected{move.command_id, RackUiCommandResult::Rejected, initial.generation};
    assert(model.apply_ack(rejected));
    assert(!model.pending_command());
    assert(model.snapshot().slots[0].slot_id == 41);

    const RackUiCommand add = model.request_add(catalog.generation, 101, 1);
    assert(add.type == RackUiCommandType::AddSlot && add.catalog_entry_id == 101);
    assert(add.catalog_generation == catalog.generation && add.target_index == 1);
    RackUiCommandAck add_ack{add.command_id, RackUiCommandResult::Accepted, 11};
    assert(model.apply_ack(add_ack));
    RackUiSnapshot after_add = initial;
    after_add.generation = 11;
    after_add.slot_count = 3;
    after_add.slots[0] = initial.slots[0];
    after_add.slots[1].slot_id = 88;
    put(after_add.slots[1].plugin_name, "FabFilter Pro-Q 3");
    after_add.slots[2] = initial.slots[1];
    assert(model.publish_snapshot(after_add));
    assert(!model.pending_command());

    const RackUiCommand replace = model.request_replace(88, catalog.generation, 102);
    assert(replace.type == RackUiCommandType::ReplaceSlot && replace.slot_id == 88);
    assert(replace.catalog_entry_id == 102);
    assert(model.apply_ack({replace.command_id, RackUiCommandResult::Failed, 11}));

    const RackUiCommand bypass = model.request_bypass(88, true);
    assert(bypass.type == RackUiCommandType::SetBypass && bypass.slot_id == 88 && bypass.bypass);
    assert(model.apply_ack({bypass.command_id, RackUiCommandResult::Rejected, 11}));

    const RackUiCommand remove = model.request_remove(88);
    assert(remove.type == RackUiCommandType::RemoveSlot && remove.slot_id == 88);
    assert(model.apply_ack({remove.command_id, RackUiCommandResult::Rejected, 11}));

    RackUiCommandReplayGuard replay;
    RackUiCommandAck cached{};
    assert(!replay.lookup(remove, cached));
    const RackUiCommandAck replay_ack{remove.command_id, RackUiCommandResult::Accepted, 12};
    replay.remember(remove, replay_ack);
    assert(replay.lookup(remove, cached));
    assert(cached.command_id == replay_ack.command_id && cached.committed_generation == 12);

    assert(rack_ui_can_replace(RackUiSlotHealth::Missing));
    assert(rack_ui_can_remove(RackUiSlotHealth::Missing));
    assert(!rack_ui_can_bypass(RackUiSlotHealth::Missing));
    assert(rack_ui_can_replace(RackUiSlotHealth::Quarantined));
    assert(rack_ui_can_remove(RackUiSlotHealth::Quarantined));
    assert(!rack_ui_can_bypass(RackUiSlotHealth::Quarantined));
    assert(rack_ui_can_bypass(RackUiSlotHealth::Ready));

    return 0;
}
