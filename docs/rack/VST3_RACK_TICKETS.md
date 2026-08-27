# VST3 Rack v2 — Tracer-Bullet Ticket Map

**Execution style:** research → spec → vertical ticket → fresh implementation context → TDD → fixed-point review → exact-head qualification  
**Rule:** take one unblocked ticket at a time. Do not implement the next ticket in the same thread.

---

## How to use this file

Each ticket is intentionally a **vertical behavior**, not a horizontal subsystem rewrite.

For every ticket:

1. create/assign a GitHub issue from the ticket body only when it is approaching unblocked state;
2. record `Parent:` and `Blocked by:` explicitly;
3. start a fresh thread/context;
4. read the authoritative Rack documents in `THREAD_HANDOFF.md` order;
5. establish exact `main` base SHA and baseline tests;
6. implement only this ticket;
7. review diff from fixed point: Standards + Spec;
8. exact-head qualify before merge;
9. update parent issue #56 with evidence;
10. stop.

Do not batch REG-0/UI-0/R0/R1 into one PR.

---

# REG-0 — Rack Entry Gate

## REG-0 — Prove and authorize the Rack extraction baseline

**Parent:** #56 VST3 Rack v2.0  
**Blocked by:** none  
**Type:** research + evidence + ADR, no Rack production feature

### Decision

Resolve the sequencing conflict: historical contract requires Single v1.0 lock before Rack, while Rack is now the next major product target.

### Required work

- pin exact `main` SHA;
- inventory current Single runtime/lifecycle/state/recovery tests;
- identify exact code seam R0 would change;
- run/inspect required baseline CI/compatibility;
- map current stable capabilities against **minimum extraction prerequisites**;
- identify true blockers vs deferred Single breadth;
- produce concise entry report;
- amend/clarify execution order by accepted ADR/doc if R0 is intentionally unlocked before historical full S6 breadth.

### Must answer

- Can `Vst3Engine` become protocol-neutral without changing Single behavior?
- Is component/controller state capture/restore deterministic enough for extraction?
- Are helper/realtime ownership boundaries covered?
- Which exact tests form mandatory Single regression during Rack work?
- Does current Single protocol remain untouched by R0?

### Acceptance

Exactly one:

- **GO** with exact evidence + phase-order clarification; next unblocked ticket = UI-0; or
- **BLOCKED** with minimum prerequisite ticket(s).

### Non-goals

- no GUI toolkit work;
- no Rack protocol/helper;
- no HostedPlugin refactor;
- no multi-plugin runtime.

---

# UI-0 — Graphical helper dependency gate

## UI-0 — Prove helper-only Dear ImGui Rack editor shell without touching OBS module dependencies

**Parent:** #56  
**Blocked by:** REG-0 GO  
**Type:** timeboxed architecture/dependency proof, no VST3 loading

### Vertical proof

A helper-only/non-shipping smoke executable can host the chosen graphical stack while the existing OBS module and Single Host compatibility floor remain unchanged.

### Mandatory read

- ADR-0003
- `RACK_EDITOR_SPEC.md`
- this ticket

### Default stack

- Dear ImGui pinned exact upstream version/commit;
- Win32 platform backend;
- DirectX 11 renderer backend.

### Test/proof first

Create a minimal smoke target that renders:

- three dummy serial Rack cards;
- search text input;
- status text/badge;
- drag reorder that records/emits a command event only.

No VST3, no Rack protocol, no OBS filter.

### Required evidence

- exact dependency pin + licence evidence recorded;
- open/close/reopen loop clean;
- message pump/D3D teardown clean;
- no vendor load;
- OBS module PE dependencies unchanged;
- Single helper/module tests green;
- minimum/current OBS loader compatibility green;
- package/binary size impact recorded.

### Acceptance

Exactly one:

- **GO IMGUI** — pin the accepted stack and unlock R0-1; or
- **BLOCKED** — document exact reason and bounded fallback ADR/ticket.

### Non-goals

- no production Rack window;
- no visual-design polish;
- no VST3 hosting;
- no plugin browser integration;
- no Rack protocol;
- no R0 refactor.

### Thread rule

Stop after GO/BLOCKED. R0-1 starts in a fresh thread.

---

# R0 — Reusable HostedPlugin seam

## R0-1 — Introduce protocol-neutral ProcessBlockView without changing Single behavior

**Blocked by:** UI-0 GO + REG-0 GO

### Vertical proof

The existing Single Host processes the same deterministic audio through the same VST3 engine behavior, but vendor processing no longer fundamentally depends on Single `AudioSlot` transport.

### Test first

- characterize current Single processing at engine seam;
- add compile/behavior test for protocol-neutral view before migrating calls.

### Implementation

- introduce bounded non-owning `ProcessBlockView` or equivalent;
- add protocol-neutral engine process entry;
- keep `process(AudioSlot&)` as adapter;
- no Single protocol layout/version change;
- no OBS-facing behavior change;
- do not mix UI-0 smoke code into Single runtime.

### Acceptance

- deterministic audio unchanged;
- Single protocol layout tests unchanged/green;
- state/latency/parameter tests green;
- Windows helper/module green;
- supported OBS compatibility green.

### Non-goals

- no HostedPlugin rename if unnecessary;
- no Rack code.

---

## R0-2 — Extract one deep HostedPlugin lifecycle/state seam

**Blocked by:** R0-1

### Vertical proof

One helper-side object owning one VST3 can be exercised independently of Single transport semantics while Single Host remains behaviorally equivalent.

### Test first

Characterize:

- open/close;
- process;
- latency;
- component/controller state round-trip;
- supported restart/lifecycle behavior;
- editor accessor ownership.

### Implementation

- converge proven `Vst3Engine` responsibilities into deep `HostedPlugin` seam;
- transport adapter remains outside;
- Single helper orchestration stays equivalent;
- no OBS-side state/recovery moved into HostedPlugin.

### Acceptance

All mandatory Single regression + compatibility gates green on exact head.

### Non-goals

- no generic plugin-format abstraction;
- no MIDI;
- no graph ports;
- no scanner redesign;
- no graphical Rack implementation.

---

# R1 — Rack runtime tracer bullets

## R1-1 — Separate Rack protocol/helper processes two deterministic VST3 effects in serial

**Blocked by:** R0-2

### Vertical proof

Through real Rack transport and separate Rack helper:

```text
input -> Gain A -> Gain B -> output
```

produces exact expected output.

### Test first

Deterministic two-effect integration test with simple fixture VST3 processors.

### Implementation

- new Rack protocol namespace/layout/version;
- new `obs-safe-vst3-rack-host.exe` target;
- minimal Rack runtime with two fixed test slots;
- one Rack DSP worker;
- preallocated ping-pong buffers;
- no production Rack editor yet.

### Acceptance

- correct A→B processing;
- Rack helper separate binary/process;
- no Single protocol change;
- no normal DSP allocation;
- Single regressions green.

---

## R1-2 — Rack bypass + total latency + whole-block fail-dry

**Blocked by:** R1-1

### Vertical proof

Two-slot Rack correctly handles bypass/latency; a process failure in an active slot returns **original dry input**, not partial Rack output.

### Test cases

- both active;
- A bypassed;
- B bypassed;
- fixed latency A+B;
- B process error after A succeeds -> original dry.

### Acceptance

- total latency = sum of active processing slots;
- bypass contributes zero processing latency when vendor process skipped;
- no partial wet escapes;
- failure bounded.

---

## R1-3 — Add/remove/reorder via immutable chain generations

**Blocked by:** R1-2

### Vertical proof

While audio is active, topology changes build a coherent generation off DSP and swap at a safe block frontier.

### Test first

- A→B then B→A;
- remove A while processing;
- add C while processing;
- stable slot IDs survive reorder;
- old generation resources stay alive while reachable.

### Acceptance

Repeated topology mutations do not crash, leak stale slots or produce mixed-generation metadata.

---

## R1-4 — Crash breadcrumb + bounded Rack helper restart

**Blocked by:** R1-3

### Vertical proof

Killing/crashing Rack helper does not crash outer host; latest breadcrumb is diagnostic; bounded restart returns to coherent chain.

### Test first

- crash fixture slot A;
- crash fixture slot B;
- arbitrary helper kill with ambiguous attribution;
- restart known topology.

### Acceptance

- outer path dry/pass-through during outage;
- breadcrumb reports generation/block/slot/phase where known;
- ambiguous death not falsely classified as proven slot guilt;
- restart/backoff bounded.

---

# R2 — Persistence, recovery, quarantine

## R2-1 — Versioned Rack Session Snapshot round-trip

**Blocked by:** R1-4

### Vertical proof

Multi-slot Rack saves/restores exact order, stable IDs, plug-in identity, bypass and complete component/controller state automatically.

### Test first

Encode/decode + full destroy/recreate round-trip.

### Implementation

- versioned Rack manifest;
- bounded per-slot state blobs;
- validation/checksum;
- atomic persistence;
- previous/LKG recovery.

### Acceptance

Close/recreate harness produces equivalent Rack state without user Save.

---

## R2-2 — Missing slot + correlated failure quarantine + good-slot recovery

**Blocked by:** R2-1

### Vertical proof

Missing/repeatedly failing slot remains visible/pass-through while other slots restore/process normally.

### Test first

- missing middle slot;
- repeated deterministic crash one slot;
- ambiguous helper death;
- restart after quarantine.

### Acceptance

- missing/quarantined topology preserved;
- good slots retain state/order;
- no restart storm;
- ambiguous crash does not automatically quarantine innocent slot.

---

## R2-3 — Preset Library foundation: Save + Load into independent Rack

**Blocked by:** R2-1

### Vertical proof

```text
Rack A -> Save as Preset "Broadcast Vocal"
-> independent Rack B
-> Load preset
-> equivalent order/state/bypass restored
```

### Test first

Independent source/filter identity reuse.

### Implementation

- stable preset UUID;
- name separate from identity;
- user-level library;
- atomic versioned persistence;
- load creates working Rack state, not live link.

### Acceptance

Edits to Rack B after load do not mutate saved preset.

---

# R3 — Graphical user workflow

## R3-0 — Promote graphical Rack Editor shell into Rack helper with command/snapshot bridge

**Blocked by:** UI-0 GO, R2-1, R1-3

### Vertical proof

The real Rack helper starts with editor hidden; a control command opens a graphical Rack window showing authoritative Rack state; closing/reopening the editor does not alter DSP or topology.

### Test first

Use deterministic fake `RackUiSnapshot` + command sink before wiring full product state.

Required cases:

- editor hidden on helper/session restore;
- OpenRack shows/foregrounds editor;
- editor renders ordered cards by stable slot IDs;
- close editor while DSP/harness processing continues;
- reopen editor receives current state;
- dummy Move command is emitted/correlated, not applied optimistically;
- repeated window open/close teardown.

### Implementation rules

- GUI stack remains Rack-helper-only;
- no OBS Qt dependency;
- no direct mutable Rack runtime pointers in UI;
- no UI lock required by DSP.

### Acceptance

Editor lifetime is independent of audio lifetime and exact-head Single regression/loader compatibility remains green.

---

## R3-1 — Register native `VST3 Rack` OBS filter as thin launcher/status surface

**Blocked by:** R3-0, R2-1

### Vertical proof

A real OBS user can add **VST3 Rack**, see concise Rack health/effect count/latency and press **Open Rack** to foreground the helper-owned graphical editor.

### OBS Properties scope

- Rack/preset summary;
- status;
- effect count;
- total latency;
- `Open Rack`;
- actionable recovery text only when needed.

### Rules

- separate filter internal ID;
- Single filter ID unchanged;
- public `obs_properties` only;
- no private Qt/widget injection;
- scanner/recovery threads never rebuild open Properties unsafely;
- closing Properties does not close/reset Rack.

### Acceptance

Properties open/close/reopen stable at minimum/current supported OBS versions; Open Rack reliably brings the helper window forward.

---

## R3-2 — Complete graphical slot editing + plug-in browser

**Blocked by:** R3-1, R1-3

### Vertical proof

In Rack Editor user can:

- search installed catalog;
- add/insert;
- drag reorder;
- Move Up/Down fallback;
- replace;
- bypass;
- remove;
- see health/latency;
- see Missing/Quarantined placeholders.

### Test first

Pure command/snapshot tests:

- search filtering;
- stable slot UI identity;
- move command + ack;
- rejected move restores authoritative order;
- add/replace/remove correlation;
- command replay/idempotency policy;
- malformed snapshot rejection.

### Scanner rule

Refresh requests existing isolated scanner asynchronously. Existing catalog remains usable during scan. No vendor scan fallback in OBS/Rack UI process path.

### Acceptance

Every UI action maps to a control-plane transaction; normal DSP never performs vendor lifecycle/UI/scan work.

---

## R3-3 — Vendor editor orchestration across Rack slots

**Blocked by:** R3-2

### Vertical proof

Each loaded slot can open/close/reopen its floating native vendor UI from the Rack Editor without embedding vendor UI or destabilizing DSP.

### Implementation

Reuse proven helper-side native VST3 IPlugView/HWND ownership concepts behind `HostedPlugin`/Rack editor coordination.

### Required tests/evidence

- editor open slot A/B;
- close/reopen;
- foreground behavior;
- Rack Editor can close while vendor window open according to defined policy;
- helper shutdown closes vendor/editor windows safely;
- helper kill with vendor/Rack editor open -> OBS survives/dry/recovery;
- vendor windows do not auto-open on restore/preset load.

### Acceptance

Representative known-good vendor editors function without changing Single editor behavior.

---

## R3-4 — Complete Preset management UX in Rack Editor

**Blocked by:** R2-3, R3-2

### Vertical proof

User can:

- Save as Preset;
- browse/select/load;
- Rename;
- Delete;
- explicit Update Preset;
- load preset with Missing placeholder.

### Acceptance

- post-load edits do not mutate saved preset;
- Rename preserves identity/content;
- Delete affects selected preset only;
- Update explicit;
- corrupt preset does not destroy current Rack;
- preset/library UI renders authoritative snapshots rather than a second mutable model.

---

# R4 — Stress + compatibility + release candidate

## R4-1 — Deterministic Rack torture/stress matrix

**Blocked by:** R2-2, R3-3, R3-4

### Vertical proof

Automated suite covers runtime, UI/control and failure under 1/2/4/8 slots.

### Required matrix

- 1/2/4/8 deterministic effects;
- reorder loop;
- add/remove loop;
- bypass loop;
- one slow slot;
- one crashing slot;
- one hanging slot;
- repeated Rack helper kill;
- Rack editor open/close loop;
- editor hidden while DSP runs;
- vendor editor open/close across slots;
- state capture while active;
- multiple independent Racks/editor windows;
- corrupt Session Snapshot;
- corrupt preset candidate;
- missing plug-in restore;
- helper kill while Rack Editor/vendor editor open;
- UI snapshot/command malformed/overflow negative tests.

### Performance evidence

Record:

- Rack helper CPU 1/2/4/8 slots;
- DSP deadline misses;
- memory;
- editor hidden cost;
- editor CPU/GPU 1/4/8 slots;
- open/close resource stability.

### Acceptance

No required test is merely registered; CI evidence must show execution/pass.

---

## R4-2 — Windows package + OBS compatibility + representative commercial Rack qualification

**Blocked by:** R4-1

### Vertical proof

One exact candidate source head installs Single + Rack, passes supported OBS compatibility and representative real-machine chains.

### Package checks

- Rack helper exactly once in correct root;
- pinned graphical dependency source/licence notices correct;
- no unexpected GUI DLL dependency in OBS module;
- Single helper still correct;
- installer upgrade/uninstall ownership correct;
- portable correct;
- minimum OBS loader probe;
- current OBS lane;
- artifact provenance names actual source SHA.

### Real-machine chains

Use known-good categories such as:

- iZotope RX/Ozone;
- FabFilter Pro-Q / Pro-C / Pro-R / Saturn;
- Waves channel strip;
- Klevgrand;
- Process Audio.

Test:

- add/search/reorder/bypass;
- Rack Editor close/reopen;
- vendor UI;
- zero-action restore;
- preset independent reuse;
- missing/quarantine behavior.

---

# R5 — v2.0 lock

## R5-1 — VST3 Rack v2.0 exact-head release lock

**Blocked by:** R4-2

### Required evidence

On one unchanged final source head prove:

- full Single regression contract green;
- full Rack runtime/UI/stress suite green;
- automatic Session Snapshot close/reopen restoration;
- helper recovery restoration;
- slot quarantine/missing behavior;
- complete Preset Library workflow;
- independent preset reuse;
- Rack Editor helper-only dependency boundary;
- package/installer/OBS compatibility;
- representative real-machine Rack chain;
- no unresolved review finding;
- artifact provenance exact.

### Release

`v2.0.0`

No feature additions after RC except release blockers with permanent regression tests.

---

# Post-v2 — explicitly NOT part of these tickets

Create later architecture phases for:

- sidechain / multiple buses;
- routing matrix / parallel paths;
- visual patchbay/routing canvas;
- MIDI event transport;
- VST3 instruments;
- live performance/macros;
- scene-aware snapshots;
- nested racks;
- embedded vendor editors if ever justified;
- optional per-slot process isolation;
- cross-platform Rack UI.

The dedicated Rack Editor is intentionally future-ready, but these features must not leak into v2 runtime tickets.
