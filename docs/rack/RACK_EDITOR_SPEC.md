# VST3 Rack Editor — Implementation Spec

**Status:** Authoritative UI/control-plane spec for Rack v2 after ADR-0003  
**Target:** Windows x64, helper-owned graphical serial Rack  
**Toolkit baseline:** Dear ImGui + Win32 + DirectX 11 after UI-0 GO

---

## 1. Purpose

This document prevents a fresh implementation thread from inventing a new UI architecture while coding.

The Rack Editor is a **view/controller over RackControlPlane state**. It is not the DSP engine, not a second persistence model, and not an OBS Qt extension.

The editor must make the common live-stream workflow obvious:

```text
Open Rack
-> search/add effect
-> drag/reorder
-> bypass/open vendor UI/tune
-> save named preset if desired
-> close Rack window
-> Rack keeps processing
```

---

## 2. Hard boundaries

### Editor may

- render Rack/preset/catalog snapshots;
- collect keyboard/mouse input;
- emit validated high-level commands;
- display command pending/success/failure state;
- request vendor editor open/close;
- request scanner refresh;
- remember editor-only window preferences.

### Editor may not

- call VST3 processor/controller objects directly;
- mutate `RackChainGeneration` directly;
- block the DSP thread;
- scan/load vendor bundles itself;
- write Session Snapshot or Preset files directly;
- own OBS source/filter pointers;
- link into or modify OBS Qt widgets;
- assume a command succeeded before control-plane acknowledgement.

---

## 3. Window shell

Default window title:

`OBS Safe VST3 Rack — <Rack/Filter Name>`

Minimum layout:

```text
┌──────────────────────────────────────────────────────────────┐
│ OBS Safe VST3 Rack                        ● Ready · 128 smp  │
│ Preset [Broadcast Vocal ▼] [Save As] [Update] [•••]         │
├──────────────────────────────────────────────────────────────┤
│ [ Search plug-ins...                                  ] [+] │
├──────────────────────────────────────────────────────────────┤
│ INPUT                                                        │
│   │                                                          │
│   ▼                                                          │
│ ┌──────────────────────────────────────────────────────────┐ │
│ │ 1  RX Spectral De-noise        iZotope                  │ │
│ │    ● Ready · 0 samples                                   │ │
│ │    [Bypass] [Open UI]                            [•••]    │ │
│ └──────────────────────────────────────────────────────────┘ │
│   │                                                          │
│ ┌──────────────────────────────────────────────────────────┐ │
│ │ 2  FabFilter Pro-Q 3           FabFilter                │ │
│ │    ● Ready · 0 samples                                   │ │
│ │    [Bypass] [Open UI]                            [•••]    │ │
│ └──────────────────────────────────────────────────────────┘ │
│   │                                                          │
│ [+ Add Effect]                                               │
│   │                                                          │
│ OUTPUT TO OBS                                                │
├──────────────────────────────────────────────────────────────┤
│ Rack Ready · 2 effects · 0 samples            CPU/diag opt. │
└──────────────────────────────────────────────────────────────┘
```

The exact visual style may evolve, but hierarchy and behavior remain stable.

---

## 4. Rack card contract

Each visible slot card must have a stable UI key derived from `slot_id`, never list index.

Required display:

- order number (presentation only);
- plug-in display name;
- vendor when available;
- health;
- latency;
- bypass state;
- pending transaction state when applicable.

Required primary actions:

- Bypass/Enable;
- Open UI.

Secondary menu/actions:

- Replace;
- Remove;
- Move Up;
- Move Down;
- diagnostics/repair when needed.

### Health presentation

Normal user wording:

- Ready
- Bypassed
- Loading
- Missing
- Recovering
- Needs Attention
- Quarantined

Internal states such as Suspect/Correlated may be translated into user wording while diagnostics preserve exact technical classification.

Never show shared-memory names, helper PID, ClassID or protocol generation on the normal card.

---

## 5. Drag reorder semantics

Drag reorder is a command, not an in-place model mutation.

```text
user drags slot A from index 0 to 2
-> UI emits MoveSlot(A, 2)
-> card may show Pending
-> control plane builds/validates generation N+1
-> publication succeeds
-> new RackUiSnapshot order arrives
-> UI renders authoritative order
```

If transaction fails:

- authoritative old order remains;
- Pending clears;
- user receives concise error;
- no fabricated topology remains in UI.

Keyboard/menu Move Up/Down is mandatory fallback for accessibility and testing.

---

## 6. Add Effect browser

The browser operates on a cached/published `PluginCatalogSnapshot`.

Required v2 filtering:

- search by plug-in name;
- vendor;
- category/subcategory when scanner metadata supports it;
- optional Favorites if persistence is trivial and independent from Rack state.

Initial browser should not require a graph canvas.

Example:

```text
Add Effect
[ Search plug-ins... ]

Recent / Favorites
  FabFilter Pro-Q 3
  iZotope Ozone 11

EQ
  FabFilter Pro-Q 3

Dynamics
  FabFilter Pro-C 2
  Waves SSL Channel

Restoration
  RX Spectral De-noise
```

### Rescan

`Refresh Plug-in List` requests the existing isolated scanner path asynchronously.

While scan is active:

- existing catalog remains usable;
- UI shows scanning state;
- no vendor code loads in Rack UI or OBS process;
- updated catalog is published as a new immutable snapshot.

---

## 7. Preset workflow

Preset controls are first-class in the Rack Editor, not hidden in OBS Properties.

Required actions:

- Save as Preset;
- browse/select/load;
- Rename;
- Delete with intentional confirmation;
- explicit Update Preset.

Rules:

- preset UUID is identity; display name is not;
- load creates working Rack state, not a live link;
- normal edits after load update Session Snapshot only;
- only explicit Update changes saved preset;
- corrupt preset cannot replace current working Rack;
- Missing plug-ins remain visible placeholders.

Editor rendering must derive from preset library snapshot + current Rack snapshot. Do not maintain a second mutable preset list in UI code.

---

## 8. Vendor editor orchestration

`Open UI` emits `OpenVendorEditor(slot_id)`.

Control/UI owner resolves the slot and uses the existing helper-side native VST3 editor mechanism.

v2 rules:

- vendor editor is floating, not embedded in slot card;
- only valid Ready/Bypassed loaded slots may open a vendor editor;
- opening an editor does not change bypass/order;
- close/hide does not unload the plug-in;
- editor should come to front when explicitly opened;
- helper/vendor window failure must not crash OBS;
- vendor editor windows do not auto-open on restore/preset load.

---

## 9. Command protocol

The exact binary structs are defined by the Rack protocol ticket, but UI behavior assumes request/ack semantics.

Every topology/preset command has:

```text
command_id
command_type
rack_id
target slot/preset identity when applicable
bounded payload
```

Acknowledgement has:

```text
command_id
result = accepted / rejected / failed
optional user-safe message code
committed rack/preset generation when applicable
```

The UI must be able to correlate pending command state without comparing display strings.

### Idempotency

Commands that may be retried after IPC/control disruption require explicit idempotency semantics or command IDs so replay does not accidentally duplicate a slot/preset operation.

Do not blindly retry a mutating command after an ambiguous disconnect.

---

## 10. Snapshot publication

UI snapshots are immutable/versioned projections.

Recommended conceptual split:

```text
RackUiSnapshot
PluginCatalogSnapshot
PresetLibrarySnapshot
DiagnosticsSnapshot
```

This prevents one giant constantly changing object and makes update frequencies independent.

Publication may use shared memory or bounded control messages depending on size/frequency, but rules are fixed:

- no UI thread reads mutable DSP containers;
- no DSP thread serializes UI snapshots;
- snapshots have generation/version;
- bounded strings/counts;
- invalid/truncated snapshot is rejected and previous valid snapshot remains displayed.

---

## 11. Threading

### UI/control thread

- Win32 message pump;
- ImGui frame lifecycle;
- D3D11 device/swapchain;
- command dispatch/ack handling;
- vendor editor window messages;
- slow control transactions delegated appropriately.

### DSP thread

No editor locks, rendering calls or catalog/preset work.

### State/persistence work

May run on control/worker context, never DSP. Completion is surfaced through coherent snapshots/acks.

### Lock rule

No mutex held by UI may be required by normal Rack DSP progress.

---

## 12. Rendering/performance

- Window hidden: no continuous rendering loop required.
- Window visible but idle: use a capped/idle-friendly refresh policy.
- High-rate meters are optional v2 polish; if present, publish bounded atomics/snapshots and sample in UI.
- D3D device lost/recreate is isolated to editor control path.
- Search filtering must not touch filesystem per frame.
- Avoid unbounded text/event history.
- Measure editor CPU/GPU at 1/4/8 slots.

Do not optimize through speculative multi-threaded rendering.

---

## 13. DPI/accessibility/usability baseline

v2 Windows editor must handle:

- per-monitor DPI awareness appropriate to the helper executable;
- readable scaling at 100/125/150/200%;
- keyboard focus for search and buttons;
- keyboard/menu reorder fallback;
- sufficiently large click targets;
- high-contrast health state not dependent only on color;
- scrollable 8-slot Rack at minimum supported window size.

Do not block v2 on a full accessibility framework, but do not design mouse-only controls with no fallback.

---

## 14. Persistence separation

Do not put transient editor state inside Rack audio snapshots/presets except where explicitly useful.

May persist separately as editor preferences:

- Rack Editor position/size;
- last browser search/category only if harmless;
- optional UI density/theme preference.

Must not persist as automatic audio state:

- editor open/closed;
- vendor window open/closed;
- temporary popup/menu state;
- drag state;
- pending command state.

---

## 15. OBS Properties launcher contract

The OBS filter Properties is not a fallback full editor.

Required v2 surface:

- filter/Rack display name or current preset summary;
- overall health;
- effect count;
- total latency when known;
- `Open Rack` button;
- actionable recovery text only when necessary.

If Rack helper is down:

- `Open Rack` may initiate bounded normal helper recovery/open flow on control path;
- OBS Properties must remain responsive;
- audio remains dry/pass-through until coherent wet Rack returns.

---

## 16. UI deterministic test seams

Do not rely only on screenshot/manual testing.

Required pure/controller tests:

- snapshot -> card ordering by stable slot ID/order;
- MoveSlot command generation;
- failed move restores authoritative order;
- add/replace/remove command correlation;
- Missing/Quarantined actions enabled/disabled correctly;
- preset action semantics;
- search filtering over catalog snapshot;
- bounded malformed snapshot rejection;
- command replay/idempotency policy.

Required smoke/integration tests:

- open/close/reopen editor repeatedly;
- helper DSP remains active when editor closes;
- editor window open while vendor window opens/closes;
- helper kill with editor open -> OBS survives/dry/recovery;
- D3D/editor teardown on helper shutdown;
- two independent Rack filters can open separate correctly identified editor windows.

Manual UX acceptance remains required before RC, but it supplements deterministic tests.

---

## 17. Visual scope guard

The following are explicitly **not required for v2**:

- cable routing canvas;
- parallel graph;
- animated signal cables;
- embedded vendor GUIs;
- spectrum visualizer;
- custom themes marketplace;
- live macro/performance dashboard;
- MIDI keyboard.

The product should look professional through typography, spacing, hierarchy, health state and responsive interaction—not through unnecessary animation.

---

## 18. UI-0 implementation checklist

A fresh UI-0 thread should do only this:

```text
1. Establish exact main SHA.
2. Read ADR-0003 + this file.
3. Pin Dear ImGui source version/commit and licence evidence.
4. Add non-shipping smoke target or isolated helper-shell target.
5. Win32 + D3D11 window opens.
6. Render 3 dummy Rack cards.
7. Search input works.
8. Drag reorder emits/records command event only.
9. Open/close/reopen loop passes.
10. Clean teardown passes.
11. Verify OBS module dependency list unchanged.
12. Run Single CI/OBS loader compatibility.
13. Record GO IMGUI or BLOCKED.
14. STOP. Do not begin R0 in same thread.
```

No VST3 loading and no production Rack engine in UI-0.
