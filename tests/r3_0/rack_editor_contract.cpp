#include "rack/rack_ui_contract.hpp"

#include <cstdint>
#include <iostream>

namespace {
using namespace safevst3::rack::ui;

bool expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

RackUiSnapshot make_snapshot(std::uint64_t generation)
{
    RackUiSnapshot snapshot{};
    snapshot.version = kRackUiSnapshotVersion;
    snapshot.generation = generation;
    snapshot.total_latency_samples = 16;
    snapshot.slot_count = 3;
    snapshot.slots[0].slot_id = 0xA001;
    snapshot.slots[0].latency_samples = 5;
    snapshot.slots[0].health = RackUiSlotHealth::Ready;
    snapshot.slots[1].slot_id = 0xB002;
    snapshot.slots[1].latency_samples = 11;
    snapshot.slots[1].health = RackUiSlotHealth::Ready;
    snapshot.slots[2].slot_id = 0xC003;
    snapshot.slots[2].latency_samples = 0;
    snapshot.slots[2].bypass = true;
    snapshot.slots[2].health = RackUiSlotHealth::Bypassed;
    return snapshot;
}

bool run_contract()
{
    bool ok = true;
    RackEditorModel model;

    ok &= expect(!model.has_snapshot(), "editor model must start without authoritative state");
    ok &= expect(!model.pending_command(), "editor model must start without pending command");

    const RackUiSnapshot initial = make_snapshot(7);
    ok &= expect(model.publish_snapshot(initial), "valid first snapshot must publish");
    ok &= expect(model.has_snapshot(), "first snapshot must become authoritative");
    ok &= expect(model.snapshot().generation == 7, "authoritative generation must be preserved");
    ok &= expect(model.snapshot().slots[0].slot_id == 0xA001 &&
                 model.snapshot().slots[1].slot_id == 0xB002 &&
                 model.snapshot().slots[2].slot_id == 0xC003,
                 "slot card order must follow immutable snapshot order using stable IDs");

    const RackUiCommand move = model.request_move(0xC003, 0);
    ok &= expect(move.command_id != 0, "MoveSlot command must have correlation ID");
    ok &= expect(move.type == RackUiCommandType::MoveSlot, "command type must be MoveSlot");
    ok &= expect(move.slot_id == 0xC003 && move.target_index == 0,
                 "MoveSlot command must carry stable slot identity and target index");
    ok &= expect(model.pending_command(), "MoveSlot must become pending until correlated acknowledgement");
    ok &= expect(model.snapshot().slots[0].slot_id == 0xA001,
                 "MoveSlot must not optimistically mutate authoritative order");

    RackUiCommandAck wrong{};
    wrong.command_id = move.command_id + 1;
    wrong.result = RackUiCommandResult::Accepted;
    wrong.committed_generation = 8;
    ok &= expect(!model.apply_ack(wrong), "uncorrelated acknowledgement must be rejected");
    ok &= expect(model.pending_command(), "uncorrelated acknowledgement must not clear pending state");

    RackUiCommandAck rejected{};
    rejected.command_id = move.command_id;
    rejected.result = RackUiCommandResult::Rejected;
    rejected.committed_generation = 7;
    ok &= expect(model.apply_ack(rejected), "correlated rejection must be consumed");
    ok &= expect(!model.pending_command(), "correlated rejection must clear pending state");
    ok &= expect(model.snapshot().slots[0].slot_id == 0xA001,
                 "rejected move must leave authoritative order unchanged");

    RackUiSnapshot malformed = make_snapshot(8);
    malformed.slot_count = kRackUiMaxSlots + 1;
    ok &= expect(!model.publish_snapshot(malformed), "malformed snapshot must be rejected");
    ok &= expect(model.snapshot().generation == 7,
                 "malformed snapshot rejection must preserve previous valid state");

    RackUiSnapshot duplicate = make_snapshot(8);
    duplicate.slots[2].slot_id = duplicate.slots[1].slot_id;
    ok &= expect(!model.publish_snapshot(duplicate), "duplicate stable slot IDs must be rejected");
    ok &= expect(model.snapshot().generation == 7,
                 "duplicate-ID snapshot must preserve prior state");

    RackUiSnapshot stale = make_snapshot(6);
    ok &= expect(!model.publish_snapshot(stale), "older snapshot generation must be rejected");

    RackUiSnapshot committed = make_snapshot(8);
    committed.slots[0] = initial.slots[2];
    committed.slots[1] = initial.slots[0];
    committed.slots[2] = initial.slots[1];
    ok &= expect(model.publish_snapshot(committed), "new authoritative snapshot must publish");
    ok &= expect(model.snapshot().slots[0].slot_id == 0xC003,
                 "authoritative committed snapshot may change visible order");

    return ok;
}
}

int main()
{
    if (!run_contract())
        return 1;
    std::cout << "R3-0 immutable Rack editor snapshot/command contract passed\n";
    return 0;
}
