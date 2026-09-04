#include "rack/rack_ui_contract.hpp"
#include "rack/rack_vendor_editor_manager.hpp"

#include <cstdlib>

using namespace safevst3::rack::ui;

namespace {

RackUiSnapshot make_snapshot()
{
    RackUiSnapshot snapshot{};
    snapshot.generation = 7;
    snapshot.slot_count = 2;

    snapshot.slots[0].slot_id = 0x101;
    snapshot.slots[0].health = RackUiSlotHealth::Ready;
    snapshot.slots[0].editor_available = true;

    snapshot.slots[1].slot_id = 0x202;
    snapshot.slots[1].health = RackUiSlotHealth::Bypassed;
    snapshot.slots[1].bypass = true;
    snapshot.slots[1].editor_available = true;
    return snapshot;
}

bool require(bool condition)
{
    return condition;
}

} // namespace

int main()
{
    static_assert(sizeof(RackVendorEditorManager) > 0);

    RackEditorModel model;
    const RackUiSnapshot snapshot = make_snapshot();
    if (!require(model.publish_snapshot(snapshot)))
        return EXIT_FAILURE;

    const RackUiCommand open_a = model.request_open_vendor_editor(0x101);
    if (!require(open_a.command_id != 0 &&
                 open_a.type == RackUiCommandType::OpenVendorEditor &&
                 open_a.slot_id == 0x101))
        return EXIT_FAILURE;

    RackUiCommandAck accepted{};
    accepted.command_id = open_a.command_id;
    accepted.result = RackUiCommandResult::Accepted;
    accepted.committed_generation = snapshot.generation;
    if (!require(model.apply_ack(accepted) && !model.pending_command()))
        return EXIT_FAILURE;

    // Opening a transient vendor window must not mutate the authoritative Rack.
    if (!require(model.snapshot().generation == 7 &&
                 model.snapshot().slot_count == 2 &&
                 model.snapshot().slots[0].slot_id == 0x101 &&
                 model.snapshot().slots[1].slot_id == 0x202 &&
                 !model.snapshot().slots[0].bypass &&
                 model.snapshot().slots[1].bypass))
        return EXIT_FAILURE;

    const RackUiCommand open_b = model.request_open_vendor_editor(0x202);
    if (!require(open_b.command_id != 0 && open_b.slot_id == 0x202))
        return EXIT_FAILURE;
    RackUiCommandAck rejected{};
    rejected.command_id = open_b.command_id;
    rejected.result = RackUiCommandResult::Rejected;
    rejected.committed_generation = snapshot.generation;
    if (!require(model.apply_ack(rejected) && !model.pending_command()))
        return EXIT_FAILURE;

    RackUiSnapshot blocked = snapshot;
    blocked.generation = 8;
    blocked.slots[0].health = RackUiSlotHealth::Missing;
    if (!require(model.publish_snapshot(blocked)))
        return EXIT_FAILURE;
    if (!require(model.request_open_vendor_editor(0x101).command_id == 0))
        return EXIT_FAILURE;

    RackUiSnapshot no_editor = blocked;
    no_editor.generation = 9;
    no_editor.slots[0].health = RackUiSlotHealth::Ready;
    no_editor.slots[0].editor_available = false;
    if (!require(model.publish_snapshot(no_editor)))
        return EXIT_FAILURE;
    if (!require(model.request_open_vendor_editor(0x101).command_id == 0))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
