# VST3 Rack v2 — Tracer-Bullet Ticket Map

**Execution style:** research → spec → vertical ticket → fresh implementation context → TDD → fixed-point review → exact-head qualification  
**Rule:** take one unblocked ticket at a time. Do not implement the next ticket in the same thread.

---

## How to use this file

Each ticket is intentionally a **vertical behavior**, not a horizontal subsystem rewrite.

For every ticket:

1. create/assign a GitHub issue from the ticket body;
2. record `Parent:` and `Blocked by:` explicitly;
3. start a fresh thread/context;
4. read the authoritative Rack documents;
5. establish exact `main` base SHA and baseline tests;
6. implement only this ticket;
7. review diff from the fixed point;
8. exact-head qualify before merge;
9. update the parent issue with evidence.

Do not batch R0/R1/R2 into one PR.

---

# REG-0 — Rack Entry Gate

## REG-0 — Prove and authorize the Rack extraction baseline

**Parent:** VST3 Rack v2.0  
**Blocked by:** none  
**Type:** research + evidence + ADR, no Rack production feature

### Behavior / decision

Resolve the current sequencing conflict: existing North Star requires Single v1.0 lock before Rack, while Rack is now the next major product target.

### Required work

- pin exact `main` SHA;
- inventory current Single runtime/lifecycle/state/recovery tests;
- identify exact code seam R0 would change;
- run required baseline CI/compatibility;
- map current stable capabilities against the **minimum extraction prerequisites**, not unrelated future breadth;
- identify true blockers vs deferred Single features;
- produce a short entry report;
- amend/clarify the execution contract by ADR if R0 is intentionally unlocked before historical full S6 breadth.

### Must answer

- Can `Vst3Engine` be made protocol-neutral without changing Single observable behavior?
- Is current component/controller state capture/restore deterministic enough to protect extraction?
- Are helper/realtime ownership boundaries sufficiently covered?
- Does current fixed point have exact tests that will detect Single regression?
- Does current protocol remain untouched by R0?

### Acceptance

One explicit result:

- **GO R0** with exact evidence and accepted phase-order clarification; or
- **BLOCKED** with one or more minimal prerequisite tickets.

### Non-goals

- no Rack protocol;
- no Rack helper;
- no multi-plugin processing;
- no UI.

---

# R0 — Reusable HostedPlugin seam

## R0-1 — Introduce protocol-neutral ProcessBlockView without changing Single behavior

**Blocked by:** REG-0 GO

### Vertical proof

The existing Single Host processes the same deterministic audio through the same VST3 engine behavior, but vendor processing no longer fundamentally depends on the `AudioSlot` transport type.

### Test first

Characterize current Single processing at the engine seam.

Add a failing/compile-contract test for the new protocol-neutral view before migrating production calls.

### Implementation

- introduce bounded non-owning `ProcessBlockView` or equivalent;
- add protocol-neutral `Vst3Engine::process(view)`;
- keep `process(AudioSlot&)` as adapter;
- no Single protocol layout/version change;
- no OBS-facing behavior change.

### Acceptance

- deterministic audio result unchanged;
- Single protocol layout tests unchanged/green;
- state/latency/parameter tests unchanged/green;
- Windows helper build green;
- supported OBS compatibility gate green.

### Non-goals

- no rename to HostedPlugin yet if it causes unnecessary diff;
- no Rack code.

---

## R0-2 — Extract one deep HostedPlugin lifecycle/state seam

**Blocked by:** R0-1

### Vertical proof

A helper-side object that owns one VST3 can be instantiated and exercised independently of Single transport semantics while the existing Single Host still behaves identically.

### Test first

Add/extend characterization around:

- open/close;
- process;
- latency;
- component/controller state round-trip;
- restart/lifecycle behavior already supported;
- editor accessor ownership boundary.

### Implementation

- converge proven `Vst3Engine` responsibilities into `HostedPlugin` seam;
- keep transport adapter outside the deep seam;
- keep Single helper orchestration behavior unchanged;
- do not move OBS-side state/recovery into HostedPlugin.

### Acceptance

All Single regression + compatibility gates remain green on exact head.

### Non-goals

- no generic plugin format abstraction;
- no MIDI;
- no graph ports;
- no scanner redesign.

---

# R1 — Rack runtime tracer bullet

## R1-1 — Separate Rack protocol/helper processes two deterministic VST3 effects in serial

**Blocked by:** R0-2

### Vertical proof

Through the real Rack transport and separate Rack helper:

```text
input -> Gain A -> Gain B -> output
```

produces the exact expected output.

### Test first

Deterministic two-effect integration test.

Use simple fixture VST3 processors with known math.

### Implementation

- new Rack protocol namespace/layout/version;
- new `obs-safe-vst3-rack-host.exe` target;
- minimal Rack runtime with two fixed slots in test harness;
- one Rack DSP worker;
- preallocated ping-pong buffers;
- no UI.

### Acceptance

- correct A→B processing;
- Rack helper is a separate process/binary;
- no Rack change to Single protocol;
- no DSP allocation in normal block path;
- Single regressions green.

---

## R1-2 — Rack bypass + total latency + whole-block fail-dry

**Blocked by:** R1-1

### Vertical proof

A two-slot Rack correctly handles bypass and latency, and a process failure in one active slot returns the original dry block instead of partial Rack output.

### Test first

Cases:

- both active;
- slot A bypassed;
- slot B bypassed;
- fixed latency A+B;
- B process error after A succeeds -> original dry output.

### Acceptance

- total latency equals sum of active processing slots;
- bypass contributes zero processing latency when vendor process is skipped;
- no partial wet block escapes;
- failure result is bounded.

---

## R1-3 — Add/remove/reorder via immutable chain generations

**Blocked by:** R1-2

### Vertical proof

While audio is active, topology changes build a new coherent chain generation off the DSP path and swap at a safe block frontier.

### Test first

- A→B then reorder B→A produces expected output;
- remove A while processing;
- add C while processing;
- stable slot IDs survive reorder;
- old generation resources are not destroyed while reachable.

### Implementation

No in-place mutation of the live DSP ordered container.

### Acceptance

Repeated topology mutations do not crash, leak stale slots or produce mixed generation metadata.

---

## R1-4 — Crash breadcrumb + bounded Rack helper restart

**Blocked by:** R1-3

### Vertical proof

Killing/crashing the Rack helper does not crash the outer host path; the latest processing breadcrumb is available diagnostically; helper restart returns to a coherent chain.

### Test first

- crash fixture in slot A;
- crash fixture in slot B;
- arbitrary helper kill with ambiguous attribution;
- restart from known topology.

### Acceptance

- outer path dry/pass-through during outage;
- breadcrumb reports generation/block/slot/phase when known;
- ambiguous death is not falsely classified as proven slot guilt;
- restart/backoff is bounded.

---

# R2 — Persistence, recovery, quarantine

## R2-1 — Versioned Rack Session Snapshot round-trip

**Blocked by:** R1-4

### Vertical proof

A Rack with multiple slots saves and restores exact order, stable slot IDs, plugin identity, bypass and complete per-slot component/controller state.

### Test first

Encode/decode + full destroy/recreate round-trip.

### Implementation

- versioned Rack manifest;
- bounded per-slot state blobs;
- validation/checksum;
- atomic persistence;
- previous/LKG recovery strategy.

### Acceptance

Close/recreate harness produces equivalent Rack state without user Save.

---

## R2-2 — Missing slot + correlated failure quarantine + good-slot recovery

**Blocked by:** R2-1

### Vertical proof

A missing or repeatedly failing slot remains visible/pass-through while every other available slot restores and processes normally.

### Test first

- missing middle slot;
- repeated deterministic crash in one slot;
- ambiguous helper death;
- helper restart after quarantine.

### Acceptance

- missing/quarantined topology is preserved;
- good slots retain state/order;
- no restart storm;
- ambiguous crash does not automatically quarantine innocent slot.

---

## R2-3 — Preset Library foundation: Save + Load into independent Rack

**Blocked by:** R2-1

### Vertical proof

```text
Rack A -> Save as Preset "Broadcast Vocal"
-> create independent Rack B
-> Load "Broadcast Vocal"
-> equivalent order/state/bypass restored
```

### Test first

Independent source/filter identity reuse test.

### Implementation

- stable preset UUID;
- name separate from identity;
- user-level library location;
- atomic versioned persistence;
- load creates working Rack state, not a live link to preset.

### Acceptance

Edits to Rack B after load do not mutate the saved preset.

---

# R3 — User workflow

## R3-1 — Register native `VST3 Rack` OBS filter with stock Properties slot lane

**Blocked by:** R2-1, R1-3

### Vertical proof

A real OBS user can add **VST3 Rack**, see current slots/status, add one effect and open its vendor UI using public OBS Properties only.

### Implementation rules

- separate filter internal ID;
- do not alter Single filter internal ID;
- no internal Qt injection;
- background runtime/scanner must not rebuild OBS-owned Properties unsafely.

### Acceptance

Real OBS Properties open/close/reopen is stable at minimum/current supported OBS versions.

---

## R3-2 — Complete slot editing workflow

**Blocked by:** R3-1

### Vertical proof

User can:

- add;
- insert where supported by the chosen stock-Properties layout;
- replace;
- bypass;
- Move Up;
- Move Down;
- remove;
- open each vendor UI.

### Test first

Pure command/topology tests plus OBS Properties ownership contract tests.

### Acceptance

Every user action maps to a control-plane topology transaction; no vendor lifecycle work occurs from realtime callback.

---

## R3-3 — Complete Preset management UX

**Blocked by:** R2-3, R3-1

### Vertical proof

In the real Rack filter user can:

- Save as Preset;
- browse/select/load;
- Rename;
- Delete;
- explicit Update Preset;
- load a preset with Missing plugin placeholder.

### Acceptance

- post-load edits do not mutate saved preset;
- Rename preserves identity/content;
- Delete affects only selected preset;
- Update is explicit;
- corrupt preset does not destroy current Rack.

---

# R4 — Stress + compatibility + release candidate

## R4-1 — Deterministic Rack torture/stress matrix

**Blocked by:** R2-2, R3-2, R3-3

### Vertical proof

Automated suite covers live mutation and failure under 1/2/4/8 slots.

### Required matrix

- 1/2/4/8 deterministic effects;
- reorder loop;
- add/remove loop;
- bypass loop;
- one slow slot;
- one crashing slot;
- one hanging slot;
- repeated Rack helper kill;
- editor open/close across slots;
- state capture while active;
- multiple independent Racks;
- corrupt Session Snapshot;
- corrupt preset candidate;
- missing plugin restore.

### Acceptance

No required test is merely registered; CI evidence must show it executed and passed.

---

## R4-2 — Windows package + OBS compatibility + representative commercial Rack qualification

**Blocked by:** R4-1

### Vertical proof

One exact candidate source head builds/installs both Single and Rack products and passes minimum/current OBS compatibility plus representative real-machine chains.

### Required package checks

- Rack helper included exactly once in correct root;
- Single helper still correct;
- installer upgrade/uninstall ownership correct;
- portable package correct;
- OBS compatibility floor loader probe;
- current supported OBS lane;
- artifact provenance names actual source SHA.

### Real-machine representative chains

Use known-good categories such as:

- iZotope RX/Ozone;
- FabFilter Pro-Q / Pro-C / Pro-R / Saturn;
- Waves channel strip;
- Klevgrand;
- Process Audio.

Test state restore, reorder/bypass and vendor UI, not only initial load.

---

# R5 — v2.0 lock

## R5-1 — VST3 Rack v2.0 exact-head release lock

**Blocked by:** R4-2

### Required evidence

Use the normative milestone closure template.

Must prove on the unchanged final source head:

- full Single regression contract green;
- full Rack runtime/stress suite green;
- automatic Session Snapshot close/reopen restoration;
- helper recovery restoration;
- slot quarantine/missing behavior;
- complete Preset Library workflow;
- independent preset reuse;
- package/installer/OBS compatibility;
- representative real-machine Rack chain;
- no unresolved review finding;
- artifact provenance exact.

### Release

`v2.0.0`

No feature additions after RC except release blockers with permanent regression tests.

---

# Post-v2 — explicitly NOT part of these tickets

Create new architecture phases after Rack v2 lock for:

- sidechain / multiple buses;
- routing matrix / parallel paths;
- MIDI event transport;
- VST3 instruments;
- performance controls;
- scene-aware snapshots;
- nested racks;
- optional maximum per-slot process isolation.

Do not append these to R1–R5 tickets.
