# VST3 Rack v2 — Engineering Execution Spec

**Status:** Authoritative Rack v2 implementation strategy  
**Target:** Safe VST3 Rack v2.0, Windows x64 first  
**Important:** every implementation ticket establishes a fresh exact `main` fixed point before coding.  
**UI authority:** `ADR-0003-ISOLATED-RACK-EDITOR.md` + `RACK_EDITOR_SPEC.md`

---

## 1. Mission

Build a separate OBS filter named **VST3 Rack** that lets a normal OBS user create a safe ordered chain of modern VST3 audio effects with a purpose-built graphical Rack Editor:

```text
Add Filter
-> VST3 Rack
-> Open Rack
-> search/add effects
-> drag/reorder / bypass / open vendor UI
-> stream
-> close Rack window if desired
-> DSP keeps running
-> close OBS
-> reopen
-> complete Rack returns automatically
```

The Rack succeeds when multiple VST3 effects are almost as easy to use as one effect **without weakening the existing crash-isolation contract**.

The product is not a DAW in v2. The editor is graphical, but the engine topology is a serial chain.

---

## 2. Product contract for v2

### User-visible Rack capabilities

A Rack filter supports up to 8 serial effect slots for qualification.

For each slot:

- stable slot identity;
- selected VST3 effect;
- Ready / Loading / Missing / Failed / Suspect / Quarantined health;
- bypass;
- latency display;
- Open Plug-in Interface;
- Replace;
- reorder by drag plus Move Up/Down fallback;
- Remove.

Rack-level:

- dedicated graphical Rack Editor;
- searchable installed plug-in browser;
- Add Effect;
- total latency;
- clear Rack health/recovery state;
- automatic Session Snapshot;
- named Rack Preset Library;
- Save as Preset;
- Load;
- Rename;
- Delete;
- explicit Update Preset.

### OBS Properties contract

OBS Properties is deliberately small:

```text
VST3 Rack
Broadcast Vocal
Ready · 4 effects · 128 samples latency
[ Open Rack ]
```

It is not the primary Rack editor.

### Safety contract

- third-party VST3 code never runs in `obs64.exe`;
- graphical Rack editor code/dependencies do not link into the OBS module;
- Rack has its own helper process and protocol;
- OBS realtime callback has bounded work and bounded waiting only;
- no vendor lifecycle/state/editor/filesystem/process work in `filter_audio`;
- no project-owned allocation in normal Rack audio processing;
- invalid/late/unavailable wet Rack result fails open to the original dry block;
- helper recovery is bounded;
- repeated slot-correlated failure can quarantine a slot;
- missing/quarantined slot remains visible and passes through;
- Session Snapshot last-known-good is promoted only as a coherent Rack generation;
- UI/control code never owns a lock required by normal DSP progress.

---

## 3. Mandatory execution order

Do not skip or batch these gates:

```text
REG-0  prove extraction baseline / authorize Rack work
  -> UI-0  prove helper-only graphical window dependency
  -> R0-1  protocol-neutral ProcessBlockView
  -> R0-2  HostedPlugin extraction
  -> R1-1  two-plugin separate Rack helper
  -> R1-2  bypass + latency + whole-block fail-dry
  -> R1-3  immutable topology generations
  -> R1-4  crash breadcrumb + bounded restart
  -> R2-1  Rack Session Snapshot
       |-> R2-2 missing/quarantine recovery
       `-> R2-3 Preset Save/Load independent reuse
  -> R3-0  production Rack Editor shell + snapshot/command bridge
  -> R3-1  native OBS VST3 Rack thin launcher/filter
  -> R3-2  graphical slot workflow + plugin browser
  -> R3-3  vendor editor orchestration
  -> R3-4  complete preset management UX
  -> R4-1  deterministic stress matrix
  -> R4-2  package + OBS + representative commercial compatibility
  -> R5-1  v2.0 exact-head lock
```

One fresh implementation thread per ticket. Stop after each ticket.

---

## 4. Entry condition — REG-0

The existing execution contract historically places Rack after Single Host v1.0 lock while current product direction makes Rack the next major target.

Do not resolve that contradiction informally.

REG-0 must answer with exact evidence:

1. What exact `main` SHA is the extraction baseline?
2. Which Single Host invariants required by Rack are already deterministic and green?
3. Which Single tests protect the VST3 lifecycle/state/process seam R0 will touch?
4. Does current public stable behavior prove state restore/recovery sufficiently for safe extraction?
5. Is any missing Single feature a true blocker to serial Rack extraction, or merely deferred breadth?
6. Does R0 require a contract/ADR clarification before historical full S6 breadth is complete?

**GO to UI-0/R0 path** only if:

- process/lifecycle/state ownership is understood;
- extraction can be behavior-preserving;
- relevant Single regression tests exist or are added first;
- Rack work can keep Single protocol untouched;
- milestone ordering is explicitly clarified.

Otherwise create the minimum prerequisite ticket and remain out of Rack production code.

---

## 5. UI-0 — graphical helper feasibility gate

UI-0 exists because the graphical Rack Editor is now a foundation, not late polish.

It is a short architecture/dependency proof, not production Rack implementation.

### Default candidate

- Dear ImGui, pinned exact upstream source version/commit;
- Win32 platform backend;
- DirectX 11 renderer backend;
- helper/smoke executable only;
- no Qt dependency;
- no JUCE dependency by default.

### Why not lock JUCE

JUCE is a useful host/UI reference, but current JUCE licensing is AGPLv3 or commercial. This repository is GPL-3.0. Do not introduce a licensing transition accidentally.

### UI-0 proof

Build a non-shipping or helper-only smoke target that:

- opens a top-level Rack-like window;
- renders three dummy slot cards;
- supports a search text input;
- supports drag reorder that emits a command event only;
- closes/reopens repeatedly cleanly;
- tears down D3D/message-pump resources cleanly;
- never loads a VST3;
- never links Dear ImGui/D3D editor code into the OBS module.

Then prove:

- Single Host CI remains green;
- minimum/current OBS loader compatibility remains green;
- OBS module PE dependencies are unchanged by Rack editor dependency;
- package size/extra files are recorded.

Result is exactly one:

- `GO IMGUI`, with exact pinned dependency/proof; or
- `BLOCKED`, with exact reason and bounded fallback decision.

Fallback order if Dear ImGui is blocked:

1. native Win32 + Direct2D/DirectWrite;
2. another permissive helper-only toolkit through a new ADR;
3. JUCE only after explicit licensing/dependency approval.

Stop after UI-0. Do not begin R0 in the same thread.

---

## 6. Target internal seams

Names may evolve, but responsibilities may not blur.

### 6.1 `HostedPlugin`

Owns exactly one VST3 class instance in a helper process.

Responsibilities:

- module/factory/component/controller lifecycle;
- bus configuration;
- `setActive` / `setProcessing` ordering;
- proven `restartComponent()` transaction behavior;
- parameter queues/catalog;
- process context;
- plug-in processing;
- latency;
- capture/restore complete component/controller state;
- native editor access through helper-owned path;
- VST3 compatibility policy.

Does not own:

- OBS source/filter objects;
- Single/Rack shared-memory layouts;
- Rack slot order;
- Rack presets;
- Rack recovery policy;
- graphical Rack widgets.

### 6.2 `ProcessBlockView`

Protocol-neutral non-owning view used by `HostedPlugin::process`.

Conceptually:

```cpp
struct ProcessBlockView {
    float* const* input;
    float* const* output;
    uint32_t channels;
    uint32_t frames;
    uint64_t sequence;
};
```

Must remain:

- non-owning;
- allocation-free;
- bounded;
- independent of Single `AudioSlot` and Rack protocol structs.

### 6.3 `RackSlotRuntime`

One logical slot:

```text
slot_id
logical_plugin_identity
bypass
health
latency
HostedPlugin? when loaded
last-known-good plugin state
failure counters / attribution metadata
```

### 6.4 `RackChainGeneration`

Immutable ordered processing plan published to Rack DSP.

Contains only normal block needs:

- generation number;
- ordered slot runtime references/handles;
- active/bypass state;
- coherent total latency;
- supported channel layout;
- preallocated working-buffer plan.

### 6.5 `RackControlPlane`

Owns slow/mutating work:

- add/insert/remove/replace/reorder;
- slot load/unload/lifecycle;
- state capture/restore;
- vendor editor commands;
- topology validation;
- building next generation;
- persistence coordination;
- UI command validation/acknowledgement;
- publishing immutable UI/catalog/preset snapshots.

### 6.6 `RackDspWorker`

Owns only normal serial block execution.

No filesystem, UI, D3D, scanner, preset or unbounded lifecycle work.

### 6.7 `RackRecoveryPolicy`

Pure/deterministic where practical.

Input:

- helper status;
- heartbeat/progress;
- last active slot breadcrumb;
- failure history;
- elapsed time;
- prior slot health.

Output:

- retry helper;
- backoff;
- mark suspect;
- quarantine;
- restore Rack LKG;
- surface terminal/manual action state.

### 6.8 UI command/snapshot seams

The Rack Editor is a view/controller, not another model.

```text
user input
-> RackCommand
-> RackControlPlane transaction
-> coherent state/generation
-> RackUiSnapshot
-> editor render
```

Use stable IDs and command IDs. Do not send raw pointers across UI/control/protocol boundaries.

Recommended snapshots:

- `RackUiSnapshot`;
- `PluginCatalogSnapshot`;
- `PresetLibrarySnapshot`;
- `DiagnosticsSnapshot`.

---

## 7. R0 — safe extraction strategy

R0 is the highest regression-risk phase because it touches proven Single code.

### R0 rule

No Rack product feature yet:

- no Rack OBS filter;
- no Rack protocol;
- no multi-plugin processing;
- no production Rack editor;
- no presets.

UI-0 smoke code must remain isolated from the Single runtime seam.

### Expand → migrate → contract

**A. Characterize current Single seam**

Before refactor, prove:

- deterministic processing behavior;
- latency query;
- component/controller state round-trip;
- parameter transfer behavior;
- lifecycle cleanup where testable.

**B. Introduce `ProcessBlockView`**

```text
Single AudioSlot adapter
-> protocol-neutral process()
-> existing VST3 processing
```

Single public behavior/protocol bytes remain unchanged.

**C. Move deep behavior behind `HostedPlugin`**

Move only what Rack needs. Do not redesign scanner, OBS recovery, Single Properties, installer or Single state file naming.

**D. Prove Single equivalence**

All mandatory Single tests + compatibility/build gates green on exact head.

If unintended observable Single behavior changes, R0 fails.

---

## 8. R1 — serial Rack runtime

### R1-1 deterministic tracer

Use two deterministic fixture effects:

```text
Gain A = x2
Gain B = x0.5
```

Prove through the real Rack transport:

```text
Rack harness
-> Rack protocol
-> separate rack helper
-> HostedPlugin A
-> HostedPlugin B
-> output
```

Do not use commercial plug-ins as first correctness oracle.

### R1-2 bypass/latency/fail-dry

Prove:

- both active;
- A bypassed;
- B bypassed;
- total active latency sum;
- B process error after A succeeds -> **original dry input**, not partial wet.

### R1-3 immutable topology generations

Topology mutation is a transaction:

```text
generation N
-> build/validate N+1 off DSP
-> publish at block frontier
-> retire old generation only after unreachable
```

Stable slot IDs survive reorder/replace semantics.

### R1-4 crash/restart

Publish breadcrumb before vendor work:

- Rack generation;
- audio sequence;
- stable slot ID;
- phase;
- DSP progress.

Kill/crash helper -> outer path remains dry/pass-through -> bounded restart -> coherent chain restore.

Ambiguous helper death is not proof that last-named slot is guilty.

---

## 9. Serial audio algorithm

Per block:

1. Validate frames/channels against Rack bounds.
2. Preserve original dry source for fail-open.
3. Acquire one coherent immutable chain generation.
4. If no active processing slots, return dry/pass-through.
5. Use preallocated working buffer A/B.
6. For each ordered slot:
   - bypass/missing/quarantined -> logical pass-through;
   - publish breadcrumb;
   - call `HostedPlugin::process` into opposite buffer;
   - validate result;
   - on failure mark whole Rack block invalid and stop.
7. If every active slot succeeds, publish wet block.
8. Otherwise return original dry block.
9. Update bounded DSP heartbeat/progress.

No project-owned allocation or UI/control lock on the normal Rack DSP path.

---

## 10. Topology mutation algorithm

### Add/insert

```text
command
-> resolve plugin identity
-> create/load HostedPlugin off DSP
-> restore initial state if supplied
-> validate layout
-> build next ordered generation
-> calculate coherent latency
-> publish at block frontier
-> publish updated UI snapshot
-> persist Session Snapshot after coherent state frontier
```

### Remove

Never destroy an object still reachable from a current DSP generation.

### Reorder

Order changes; stable slot identity/state does not.

### Replace

Recommended v2 semantics:

- slot ID survives Replace;
- old unrelated plugin state is not applied to new plug-in;
- failed replacement leaves/recoverably restores known-good current Rack;
- UI waits for transaction acknowledgement/snapshot before treating replacement as committed.

---

## 11. Failure attribution and quarantine

Breadcrumb phases include at minimum:

- None;
- Load;
- RestoreState;
- Process;
- CaptureState;
- OpenEditor;
- CloseEditor;
- LifecycleRestart;
- RackUiControl when useful for helper-level diagnosis.

Conceptual confidence:

- Unknown;
- Suspect;
- Correlated;
- Quarantined.

Do not quarantine from one ambiguous helper death.

Correlated repeated failure recovery:

```text
mark suspect/quarantined
-> restart Rack helper
-> rebuild from Rack LKG
-> keep suspect slot visible/pass-through
-> restore other good slots
-> resume wet only after coherent generation
```

OBS stays dry while helper unavailable/recovering.

UI/rendering failure alone must not fabricate plug-in guilt.

---

## 12. Persistence formats

### Rack Session Snapshot

Versioned manifest contains at least:

- stable Rack ID;
- generation/version;
- exact ordered slots;
- stable slot IDs;
- logical plug-in identity;
- bypass;
- health placeholder semantics;
- complete component/controller state references/blobs;
- persistent Rack audio controls.

Must be:

- bounded;
- validated/checksummed as appropriate;
- migration aware;
- atomic;
- tolerant of missing plug-ins;
- independent of transient editor-open state.

Preferred durability:

```text
serialize candidate
-> validate
-> write temp
-> flush
-> preserve/rotate previous LKG
-> atomic replace
-> read-back/validate where practical
```

### Preset Library

`preset_uuid != display_name`.

Preset stores ordered topology + complete slot states/bypass + required metadata.

Load semantics:

- creates new working Rack state;
- does not live-link autosave to preset file;
- later edits update Session Snapshot only;
- explicit Update Preset changes saved preset.

### Editor preferences

Window position/size and other harmless UI-only preferences are separate from Rack Session Snapshot and Preset state.

Do not auto-persist/reopen:

- Rack editor open state;
- vendor window open state;
- popups/drag state;
- pending command state.

---

## 13. R3 graphical user workflow

### R3-0 production editor shell

Blocked by R2-1 and UI-0 GO.

Promote the validated GUI stack into `obs-safe-vst3-rack-host.exe`.

Prove:

- helper starts with editor hidden;
- `OpenRack` command creates/shows foreground editor;
- close/reopen does not alter DSP generation;
- editor renders authoritative `RackUiSnapshot`;
- editor emits `RackCommand` and correlates ack/pending state;
- D3D/editor teardown does not block DSP/helper shutdown.

### R3-1 OBS thin launcher/filter

Register separate `VST3 Rack` OBS filter.

Properties only exposes:

- Rack/preset summary;
- overall status/effect count/latency;
- `Open Rack`;
- actionable recovery text when needed.

No private Qt injection.

### R3-2 slot workflow/browser

Graphical editor provides:

- search/add from cached catalog snapshot;
- drag reorder + Move Up/Down fallback;
- replace;
- bypass;
- remove;
- health/latency;
- Missing/Quarantined visible placeholders.

Scanner refresh remains isolated and asynchronous. Existing catalog stays usable during scan.

### R3-3 vendor editor orchestration

Use existing proven helper-side native VST3 editor ownership concepts.

Vendor editors are floating windows in v2, not embedded cards.

Prove open/close/reopen/focus across multiple Rack slots and helper recovery.

### R3-4 preset management UX

Complete:

- Save as Preset;
- browse/select/load;
- Rename;
- Delete;
- explicit Update;
- Missing plug-in presentation;
- corrupt preset rejection without current-Rack loss.

---

## 14. UI architecture rules

`RACK_EDITOR_SPEC.md` is authoritative for detailed controls.

Core rules:

- editor uses immutable snapshots;
- editor does not directly touch mutable DSP/runtime objects;
- topology changes are commands/transactions;
- no permanent optimistic model divergence;
- stable slot ID is UI identity, list index is presentation only;
- plug-in search filters an already published catalog snapshot;
- no per-frame filesystem work;
- UI thread never owns a DSP-required lock;
- editor can be closed while Rack continues processing;
- render only when visible; idle refresh is capped/efficient;
- D3D device loss/recreation is control/UI work only;
- vendor windows never auto-open on restore/preset load.

---

## 15. Test architecture

### Pure/unit seams

Test without OBS/commercial VST3s:

- slot IDs/order;
- immutable generation builder;
- latency sum;
- bypass/missing semantics;
- recovery/quarantine policy;
- snapshot encode/decode/validation;
- preset identity/CRUD semantics;
- atomic persistence decisions;
- RackCommand generation/correlation;
- UI snapshot ordering;
- failed move/add/replace keeps authoritative state;
- catalog search filtering;
- malformed snapshot rejection.

### Deterministic VST3 fixtures

Need modes for:

- Gain;
- fixed latency;
- process error after N blocks;
- crash on process;
- hang on process;
- large state;
- state reject/corruption;
- latency change.

### Rack integration before RC

- 0-slot dry;
- 1-slot equivalence;
- 2-slot order;
- 4/8-slot processing;
- bypass combinations;
- add/insert/remove/reorder loops;
- active-slot process error -> whole-block dry;
- helper kill/hang;
- correlated crash quarantine;
- missing slot restore;
- editor open/close/reopen;
- vendor editor open/close across slots;
- helper kill with Rack Editor open;
- state capture while active;
- helper recovery restores good slots;
- corrupt snapshot fallback;
- multiple independent Racks/editor windows;
- scene collection save/reload.

### Preset mandatory tests

- Save as Preset;
- list/select/load;
- independent Rack reuse;
- Rename;
- Delete;
- explicit Update;
- post-load edits do not mutate saved preset;
- missing placeholder;
- corrupt/interrupted write preserves previous preset;
- Session Snapshot remains independent.

### Single regression

Every shared-runtime Rack PR must run the established Single regression contract.

---

## 16. Performance gates

Measure at least:

- Rack helper CPU at 1/2/4/8 deterministic slots;
- processing time distribution/deadline misses;
- helper restart time;
- topology rebuild time;
- state capture time/size;
- editor/control stall impact on DSP;
- memory per Rack/slot;
- Rack Editor CPU/GPU at 1/4/8 slots;
- editor hidden idle cost;
- editor open/close/reopen resource stability.

Performance work is evidence-driven. Do not add lock-free/multithreaded UI complexity without measured need.

---

## 17. Real-machine qualification sequence

### R1 alpha

Deterministic fixtures + at most one known-good simple commercial effect if needed.

### R2 beta

Known-good representative chain, for example:

```text
Brusfri / RX
-> FabFilter Pro-Q 3
-> FabFilter Pro-C 2
```

Verify state/recovery.

### R3 beta UX

Test:

- `Open Rack` from OBS;
- add/search/reorder/bypass/remove;
- vendor UI per slot;
- Rack Editor close while audio continues;
- close/reopen OBS zero-action restore;
- preset reuse on another source;
- Missing/quarantine presentation.

### R4 RC

Representative categories:

- iZotope Ozone/RX;
- FabFilter EQ/dynamics/reverb/saturation;
- Waves channel strip;
- Klevgrand;
- Process Audio;
- other currently known-good effects;
- deterministic bad-plugin fixtures.

Commercial qualification supplements deterministic fault tests; it never replaces them.

---

## 18. Stop conditions

Stop and return to research/spec if:

- Rack requires changing Single protocol layout;
- any Rack/vendor GUI code loads into `obs64.exe`;
- realtime path requires unbounded mutex/wait/allocation;
- topology mutation requires unsafe in-place live DSP modification;
- persistence cannot distinguish coherent generation from mixed states;
- GUI dependency reaches the OBS module unexpectedly;
- Rack Editor requires private OBS Qt/widget integration;
- UI/control thread must hold a lock required by DSP;
- crash attribution confuses evidence with guess;
- required Single regression changes unexpectedly;
- compatibility floor fails after shared-code extraction;
- a toolkit/license dependency cannot be redistributed under an explicitly accepted project policy.

Do not solve a stop condition with a local workaround. Re-open the relevant ADR.

---

## 19. Definition of done for one ticket

A ticket is complete only when applicable evidence exists:

```text
Fixed-point base SHA
Behavior/spec linked
Failing test first where deterministic seam exists
Minimum implementation
Focused test green
Surrounding Single/Rack regression green
Standards review from fixed point
Spec review from fixed point
All findings resolved
Exact final head identified
Required exact-head CI/compatibility green
Manual evidence if required
No known invariant violation
Parent evidence ledger updated
```

If source head changes after final review/qualification, old evidence no longer authorizes merge.

---

## 20. v2 release non-goals

Do not pull these into v2:

- sidechain;
- parallel routing;
- arbitrary patchbay;
- MIDI;
- VST3 instruments;
- live MIDI mapping;
- macro/performance dashboard;
- nested racks;
- scene-aware performance switching;
- embedded vendor editors;
- per-slot bridge process;
- direct audio-device routing;
- Float64;
- arbitrary multichannel;
- cross-platform Rack.

The dedicated editor is future-ready, but v2 engine scope remains serial effects.

---

## 21. First action from a fresh thread

The current first ticket remains **REG-0**.

A fresh thread must not start UI-0, R0 or Rack production code until #57 produces an accepted evidence-backed GO decision.

After REG-0 GO, the next fresh thread executes **UI-0 only**, records `GO IMGUI` or `BLOCKED`, and stops.
