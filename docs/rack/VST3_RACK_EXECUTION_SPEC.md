# VST3 Rack v2 — Engineering Execution Spec

**Status:** Authoritative implementation strategy once merged  
**Target:** Safe VST3 Rack v2.0  
**Research fixed point:** `3f7b3f5423faa3b667bc0a2d15274ba0c68ee1d2`  
**Important:** every implementation ticket must establish a new exact `main` fixed point before coding.

---

## 1. Mission

Build a separate OBS filter named **VST3 Rack** that lets a normal OBS user create a safe ordered chain of VST3 audio effects:

```text
Add Filter
-> VST3 Rack
-> Add Effect
-> Add Effect
-> reorder / bypass / open vendor UI
-> stream
-> close OBS
-> reopen
-> complete Rack returns automatically
```

The Rack is successful when multiple modern VST3 effects become almost as easy to use as one effect **without weakening the existing crash-isolation contract**.

---

## 2. Product contract for v2

### User-visible capabilities

A Rack filter supports up to 8 serial effect slots.

For each slot:

- stable slot identity;
- selected VST3 effect;
- Ready / Missing / Failed / Suspect / Quarantined health;
- bypass;
- latency display;
- Open Plug-in Interface;
- Replace;
- Move Up;
- Move Down;
- Remove.

Rack-level:

- Add Effect;
- total latency;
- automatic Session Snapshot;
- named Rack Preset Library;
- Save as Preset;
- Load;
- Rename;
- Delete;
- explicit Update Preset;
- understandable recovery status.

### Safety contract

- third-party VST3 code never runs in `obs64.exe`;
- Rack has its own helper process and protocol;
- OBS realtime callback has bounded work and bounded waiting only;
- no vendor lifecycle/state/editor/filesystem/process work in `filter_audio`;
- no project-owned allocation in normal Rack audio processing;
- invalid/late/unavailable wet Rack result fails open to the original dry block;
- helper recovery is bounded;
- repeated slot-correlated failure can quarantine a slot;
- a missing/quarantined slot remains visible and passes through;
- Session Snapshot last-known-good is promoted only as a coherent Rack generation.

---

## 3. Entry condition — REG-0

### Why REG-0 exists

The existing execution contract says Rack starts after Single Host v1.0 lock. The current product direction wants Rack as the next major target.

Do not resolve that contradiction informally.

### REG-0 deliverable

One short evidence report + ADR decision must answer:

1. What exact `main` SHA is the extraction baseline?
2. Which Single Host invariants required by Rack are already deterministic and green?
3. Which Single tests protect the VST3 lifecycle/state/process seam that R0 will touch?
4. Does current public stable behavior prove state restore/recovery sufficiently for safe extraction?
5. Is any missing Single feature a true blocker to **serial Rack extraction**, or merely deferred breadth?
6. Does R0 require a contract amendment that unlocks extraction before full historical S6 breadth is complete?

### REG-0 stop/go

**GO to R0** only if:

- process/lifecycle/state ownership is understood;
- extraction can be made behavior-preserving;
- relevant Single regression tests exist or are added first;
- Rack work can keep Single protocol untouched;
- an explicit accepted doc/ADR resolves milestone ordering.

Otherwise create the minimum prerequisite ticket and stay out of Rack production code.

---

## 4. Target internal seams

Names may evolve, but responsibilities may not blur.

### 4.1 `HostedPlugin`

Owns exactly one VST3 class instance in a helper process.

Responsibilities:

- module/factory/component/controller lifecycle;
- bus configuration;
- `setActive` / `setProcessing` ordering;
- restartComponent transaction handling already proven in Single;
- parameter queues/catalog;
- process context;
- plugin processing;
- latency;
- capture/restore opaque component/controller state;
- native editor access through helper-owned UI path;
- VST3 compatibility policy.

Does **not** own:

- OBS source/filter objects;
- Single or Rack shared-memory layout;
- Rack slot order;
- Rack presets;
- Rack recovery policy.

### 4.2 `ProcessBlockView`

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

Exact type may include safe context fields, but must remain:

- non-owning;
- allocation-free;
- bounded;
- independent of Single `AudioSlot` and Rack protocol structs.

### 4.3 `RackSlotRuntime`

Owns one logical Rack slot.

Minimum conceptual state:

```text
slot_id
logical_plugin_identity
bypass
health
latency
HostedPlugin? (when loaded)
last-known-good plugin state
failure counters / attribution metadata
```

### 4.4 `RackChainGeneration`

Immutable ordered processing plan published to Rack DSP.

Contains only what normal block processing needs:

- generation number;
- ordered slot runtime references/handles;
- active/bypass state needed for the block frontier;
- coherent total latency;
- supported channel layout;
- preallocated working buffer plan.

### 4.5 `RackControlPlane`

Owns topology mutation and slow work:

- add/insert/remove/replace/reorder;
- slot load/unload/lifecycle;
- capture/restore state;
- helper/editor commands;
- topology validation;
- building next generation;
- preset/session persistence coordination.

### 4.6 `RackDspWorker`

Owns normal serial block execution.

It must never perform filesystem, UI, scan or unbounded lifecycle work.

### 4.7 `RackRecoveryPolicy`

Pure/deterministic where practical.

Input:

- helper status;
- heartbeat/progress;
- last active slot breadcrumb;
- failure history;
- elapsed time;
- slot prior health.

Output:

- retry helper;
- backoff;
- mark suspect;
- quarantine slot;
- restore Rack LKG;
- surface terminal/manual action state.

Keep policy testable without launching OBS or commercial plugins.

---

## 5. R0 — safe extraction strategy

R0 is the highest regression-risk phase because it touches proven Single code.

### 5.1 R0 rule: no Rack feature yet

R0 should not add:

- Rack OBS filter;
- Rack protocol;
- multiple plugin processing;
- Rack UI;
- presets.

It only makes the current proven single-plugin runtime reusable.

### 5.2 Expand → migrate → contract

#### Step A — characterize current Single seam

Before refactor, add tests proving at minimum:

- current deterministic processing behavior;
- bypass/identity behavior relevant to the engine seam;
- latency query behavior;
- component/controller state round-trip;
- parameter transfer behavior that will remain inside HostedPlugin;
- lifecycle close/reopen cleanup where testable.

#### Step B — introduce protocol-neutral block view

Add `ProcessBlockView` next to existing Single seam.

Keep:

```text
Single AudioSlot adapter -> protocol-neutral process() -> existing VST3 processing
```

Single public behavior and protocol bytes must not change.

#### Step C — move deep behavior behind `HostedPlugin`

Move only code Rack needs.

Do not simultaneously redesign:

- scanner;
- OBS-side recovery;
- Single Properties;
- installer;
- protocol;
- state file naming.

#### Step D — prove Single equivalence

Run all existing Single tests + compatibility/build gates on exact head.

If observable Single behavior changes unintentionally, R0 fails.

---

## 6. R1 — serial Rack tracer bullet

The first Rack runtime is deliberately tiny and end-to-end.

### 6.1 Deterministic fixture

Use two deterministic test VST3 effects or equivalent fixture mode:

```text
Gain A = x2
Gain B = x0.5
```

Expected serial output equals the known mathematical result.

Do not use Ozone/FabFilter as the first correctness oracle.

### 6.2 Real seam to prove

```text
Rack test/OBS adapter
-> Rack protocol
-> separate rack helper
-> Rack runtime
-> HostedPlugin A
-> HostedPlugin B
-> output
```

### 6.3 R1 initial behaviors

Prove:

- ordered A→B processing;
- B→A after reorder;
- per-slot bypass;
- total latency sum;
- Missing placeholder pass-through;
- whole-block fail-dry on one active slot process error;
- helper kill produces bounded dry on outer host side;
- helper restarts to a coherent chain.

### 6.4 No final UX in R1

A CLI/integration harness or minimal non-user-facing control seam is acceptable.

Do not build final Properties before runtime behavior exists.

---

## 7. Serial audio algorithm

For each block:

1. Validate frames/channels against Rack protocol bounds.
2. Preserve the original dry input available to the outer Rack result path.
3. Acquire one coherent immutable chain generation.
4. If no active processing slots, return dry/pass-through immediately.
5. Copy/input-adapt into working buffer A only as required by the architecture.
6. For each ordered slot:
   - if bypass/missing/quarantined: pass through logically;
   - publish breadcrumb `(generation, sequence, slot_id, Process)`;
   - call `HostedPlugin::process` into opposite ping-pong buffer;
   - validate process result;
   - on failure, mark Rack block invalid and stop serial processing.
7. If every active slot succeeded, publish completed wet block.
8. If not, outer Rack path returns original dry block.
9. Update bounded DSP heartbeat/progress.

### No partial wet

Never publish:

```text
Denoise succeeded -> EQ succeeded -> Compressor crashed
```

as `Denoise + EQ` output for that block.

The block contract is all-valid-wet or original-dry.

---

## 8. Topology mutation algorithm

Topology changes are transactions.

### Add/insert

```text
request
-> resolve plugin identity
-> create/load HostedPlugin off DSP path
-> restore initial state if supplied
-> validate layout
-> build next ordered generation
-> calculate coherent latency
-> publish generation at block frontier
-> persist new Session Snapshot after coherent state frontier
```

### Remove

Never destroy an object still reachable by the current DSP generation.

Use generation ownership/ref-count/retirement so old generation resources are destroyed only after no processing path can access them.

### Reorder

Reorder changes only topology/order. Stable slot IDs and per-slot plugin state remain unchanged.

### Replace

Replace keeps the slot ID unless product semantics explicitly require “new slot.”

Recommended v2 behavior:

- slot ID survives Replace;
- old plugin state is retained only in previous snapshot/history, not applied to unrelated plugin;
- new plugin starts from its own initial state or user-selected preset;
- current Rack remains recoverable if replacement load fails.

---

## 9. Failure attribution and quarantine

### 9.1 Breadcrumb phases

At minimum:

- None;
- Load;
- RestoreState;
- Process;
- CaptureState;
- OpenEditor;
- CloseEditor;
- LifecycleRestart.

### 9.2 Confidence policy

Do not quarantine from one ambiguous helper death.

Suggested conceptual levels:

- `Unknown`
- `Suspect`
- `Correlated`
- `Quarantined`

Evidence that increases confidence:

- deterministic vendor process returned error for slot;
- repeated helper death with same active slot + same phase after LKG restore;
- deterministic torture fixture reproduces same failure.

Evidence that should remain ambiguous:

- process dies long after vendor call returned;
- Windows/process termination without reliable active phase;
- unrelated control/editor thread fault while breadcrumb happens to name last DSP slot.

### 9.3 Recovery order

For correlated repeated failure:

```text
mark slot suspect/quarantined in recovery state
-> restart Rack helper
-> rebuild from last-known-good Rack snapshot
-> keep suspect slot visible as pass-through
-> restore all other available slots
-> resume Rack wet processing only after coherent generation ready
```

OBS stays dry while helper is unavailable/recovering.

---

## 10. Persistence formats

Do not use one opaque mega-blob with no topology metadata.

### 10.1 Rack Session Snapshot

Conceptual manifest:

```json
{
  "format": "obs-safe-vst3-rack-session",
  "version": 1,
  "rack_id": "stable-filter-rack-id",
  "generation": 42,
  "slots": [
    {
      "slot_id": "uuid",
      "plugin": {
        "class_id": "...",
        "path_hint": "...",
        "display_name": "..."
      },
      "bypass": false,
      "health_persisted": "normal-or-placeholder-semantics",
      "component_state_ref": "...",
      "controller_state_ref": "..."
    }
  ],
  "rack_controls": {}
}
```

Exact encoding may be binary/JSON + blob files, but must be:

- versioned;
- bounded;
- checksum/validation aware;
- migration aware;
- atomic;
- tolerant of missing plugins;
- independent of transient editor-open state.

### 10.2 Last-known-good durability

Rack persistence must be stronger than a blind overwrite.

Preferred transaction:

```text
serialize candidate
-> validate candidate fully
-> write temp
-> flush
-> atomically rotate current valid to previous/LKG where needed
-> atomically replace primary
-> validate/read-back where practical
```

Interrupted candidate write must not destroy the only recoverable Rack.

### 10.3 Preset Library

User-level location separate from Session Snapshot.

Preset identity:

```text
preset_uuid != display_name
```

Renaming must not change identity.

Preset stores:

- format/version;
- stable preset UUID;
- display name;
- ordered slots;
- logical VST3 identity;
- complete component/controller states;
- bypass;
- rack-level controls;
- metadata needed for Missing placeholders/relink diagnostics.

### 10.4 Preset load semantics

Loading a preset:

- creates a new working Rack state;
- does not bind autosave back to the preset file;
- later Rack edits update Session Snapshot only;
- saved preset changes only after explicit **Update Preset**.

---

## 11. OBS-side UX implementation

### 11.1 v2 compatibility-first rule

Use public `obs_properties` only.

Do not depend on:

- OBS internal Qt object names;
- widget hierarchy injection;
- undocumented GUI ownership;
- custom linked Qt runtime.

### 11.2 Dynamic Properties safety

Respect existing Properties ownership lessons:

- user-driven property callback may request supported refresh semantics;
- scanner/recovery/background thread must not rebuild an open Properties tree behind OBS;
- asynchronous runtime state should be displayed through safe snapshots/status text rather than invalidating property pointers.

### 11.3 Slot controls

Because stock Properties is not a drag canvas, v2 can use explicit controls:

- `Move Up`
- `Move Down`
- `Replace`
- `Remove`
- `Bypass`
- `Open Plug-in Interface`

This is acceptable and professional if hierarchy is clear.

### 11.4 Normal-user hierarchy

Top to bottom:

```text
Rack Preset
Current Rack status / total latency
Slot 1
Slot 2
...
+ Add Effect
Preset management
Diagnostics only when needed
```

Do not expose ClassID, process IDs, shared-memory names or protocol generations in normal controls.

---

## 12. Test architecture

### 12.1 Pure/unit seams

Test without OBS or commercial VSTs where possible:

- slot IDs/order;
- immutable generation builder;
- latency sum;
- bypass/missing semantics;
- recovery/quarantine policy;
- snapshot encode/decode/validation;
- preset identity/CRUD semantics;
- atomic persistence recovery decisions.

### 12.2 Deterministic plugin fixtures

Need deterministic effects for Rack correctness:

- Gain;
- fixed latency Gain;
- process-error-after-N-blocks;
- crash-on-process slot;
- hang-on-process slot;
- large state;
- state reject/corruption behavior;
- latency-change behavior.

Prefer extending the project's deterministic VST3 torture/fixture family when available rather than building unrelated test infrastructure.

### 12.3 Rack integration tests

At minimum before v2 RC:

- 0-slot dry;
- 1-slot equivalence;
- 2-slot order;
- 4-slot and 8-slot processing;
- bypass combinations;
- add/insert/remove/reorder loops;
- latency change;
- active slot process error -> whole-block dry;
- helper kill/hang;
- correlated crash quarantine;
- missing slot restore;
- vendor editor open/close across multiple slots;
- state capture while audio active;
- helper recovery restores all good slots;
- corrupt primary snapshot fallback;
- multiple Rack filters on independent sources;
- scene collection save/reload.

### 12.4 Preset mandatory tests

- Save as Preset;
- list/select/load;
- load into independent Rack;
- Rename;
- Delete;
- explicit Update Preset;
- post-load edits do not mutate saved preset;
- missing plugin placeholder;
- corrupt/interrupted write preserves previous preset;
- Session Snapshot remains independent.

### 12.5 Single regression gate

Every runtime Rack PR after R0 must run the established Single regression contract.

Rack is not allowed to “improve shared code” at the cost of Single behavior drift.

---

## 13. Performance gates

For v2 scope, measure rather than assume.

Track at least:

- Rack helper CPU at 1 / 2 / 4 / 8 deterministic slots;
- processing time per block distribution;
- deadline misses;
- helper restart time;
- topology rebuild time;
- state capture time and size;
- editor/control stall impact on DSP;
- memory per Rack and per slot.

Performance work is evidence-driven. Do not add lock-free complexity unless a measured bottleneck requires it.

---

## 14. Real-machine qualification sequence

Do not start with a giant random compatibility sweep.

### R1 alpha

Deterministic fixtures only + one known-good simple commercial effect if needed.

### R2 beta

Known-good representative chain, e.g.:

```text
Brusfri / RX
-> FabFilter Pro-Q 3
-> FabFilter Pro-C 2
```

Verify state/recovery.

### R3 beta UX

Test at least:

- add/reorder/bypass/open UI;
- close/reopen OBS zero-action restore;
- preset reuse on another source.

### R4 RC compatibility matrix

Representative categories:

- iZotope Ozone/RX;
- FabFilter EQ/dynamics/reverb/saturation;
- Waves channel strip;
- Klevgrand;
- Process Audio;
- other currently known-good effects.

Also include deterministic bad-plugin fixtures.

Commercial compatibility never replaces deterministic fault tests.

---

## 15. Stop conditions

Stop implementation and return to research/spec if any of these occurs:

- Rack requires changing Single protocol layout;
- Rack code loads vendor plugin into `obs64.exe`;
- realtime path requires unbounded mutex/wait/allocation;
- one topology mutation needs unsafe in-place modification of live DSP objects;
- persistence cannot distinguish coherent generation from mixed slot states;
- custom Qt becomes required for core Rack operations;
- crash attribution cannot distinguish evidence from guess;
- a required Single regression changes unexpectedly;
- compatibility floor fails after shared-code extraction.

Do not solve a stop condition with a local workaround. Re-open the architecture decision.

---

## 16. Definition of done for a ticket

A Rack ticket is complete only when all applicable items exist:

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
Manual evidence if ticket requires it
No known invariant violation
```

A changed head invalidates final review/qualification evidence tied to the older head.

---

## 17. v2 release non-goals

Do not pull these into Rack v2 “because the new seam makes them easy”:

- sidechain;
- parallel routing;
- arbitrary patchbay;
- MIDI;
- VST3 instruments;
- live MIDI mapping;
- macro controls;
- nested racks;
- scene-aware performance switching;
- custom graph editor;
- per-slot bridge process;
- Float64;
- arbitrary multichannel;
- cross-platform Rack.

Those are post-v2 product phases.

---

## 18. First implementation action

The first production-code action after these docs is **not R1**.

It is:

> **REG-0 — audit current Single stable fixed point and explicitly authorize or block R0 extraction.**

Only after REG-0 is accepted should the first fresh implementation thread take `R0-1` from `VST3_RACK_TICKETS.md`.
