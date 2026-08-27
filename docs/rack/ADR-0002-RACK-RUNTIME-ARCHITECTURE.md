# ADR-0002 — VST3 Rack Runtime Architecture

**Status:** Accepted for planning / implementation entry  
**Date:** 2026-08-27  
**Scope:** Safe VST3 Rack v2.0  
**Clarifies:** `docs/CODEX_EXECUTION_CONTRACT.md`, `docs/NORTH_STAR_PRD.md`  
**Does not supersede:** existing Single Host safety invariants  
**UI clarification:** `ADR-0003-ISOLATED-RACK-EDITOR.md` supersedes the earlier stock-Properties editor decision

---

## 1. Decision summary

Safe VST3 Rack v2.0 will be a **separate OBS audio filter** with a **separate rack helper executable** and **independently versioned rack protocol**.

The first stable Rack is deliberately a **serial effects chain**, not a DAW graph.

```text
OBS VST3 Rack filter
        |
        | bounded rack IPC
        v
obs-safe-vst3-rack-host.exe
        |
        |-- Rack Runtime
        |-- Slot 1 HostedPlugin
        |-- Slot 2 HostedPlugin
        |-- ...
        |-- Slot N HostedPlugin
        |-- Rack control/editor coordinator
        `-- Rack DSP worker
              |
              v
        preallocated serial chain
```

One Rack helper process owns all slots for that Rack filter. Per-slot worker processes are explicitly **out of scope for v2.0** unless later evidence proves their extra isolation is worth the CPU, latency, IPC and state-coordination cost.

The Rack helper also owns the dedicated graphical Rack Editor described by ADR-0003. OBS Properties is only a launcher/status surface.

---

## 2. Why this decision

The existing product differentiator is not “largest host feature list.” It is **OBS-specific workflow + third-party crash containment + bounded realtime behavior + deterministic recovery**.

A Rack must preserve that promise.

Research of mature live/plugin hosts supports several useful patterns:

- Kushview Element demonstrates the value of separating processor runtime, topology, rendering order, shared-buffer planning, latency propagation and dedicated host/editor windows.
- atkAudio PluginHost2 demonstrates a practical OBS workflow where Properties is a thin entry point and a dedicated graphical host window owns complex plug-in workflow; we adopt that product split while keeping our stronger helper-process isolation.
- Cantabile demonstrates the product value of treating a rack as an understandable black box with stable state/reuse concepts and hiding routing complexity until needed.
- Gig Performer demonstrates the value of separating configuration/wiring from live-performance controls and making reusable rack configurations first-class.
- Carla demonstrates the power of rack/patchbay and bridging, but also the additional failure surface that appears when routing and process-bridge complexity are introduced too early.

For OBS Safe VST3 Host, the correct v2 compromise is therefore:

**deep engine separation internally; simple serial product topology; rich helper-owned graphical workflow externally.**

---

## 3. Locked v2 architecture decisions

### 3.1 Separate OBS product surface

The Rack is a separate filter type from the existing Single Host.

Do not turn the Single Host into a mode switch between “single” and “rack.”

Reasons:

- preserves the simple Single workflow;
- prevents Rack topology semantics from contaminating the Single protocol;
- lets Rack evolve independently;
- makes regression boundaries obvious.

### 3.2 Separate Rack protocol and helper

Rack must not reuse or mutate `common/protocol.hpp` into a general-purpose multi-plugin protocol.

The current Single Host protocol remains a Single contract.

Rack gets its own:

- protocol magic/version;
- shared-memory/control layout;
- helper executable;
- lifecycle state machine;
- diagnostics namespace.

Deep VST3 lifecycle code may be shared only behind protocol-neutral internal seams.

### 3.3 One helper process per Rack filter

All Rack slots run inside one isolated Rack helper process in v2.

Advantages:

- third-party VST3 remains outside `obs64.exe`;
- no IPC/context switch between every serial slot;
- one control plane can coordinate state and topology coherently;
- latency and buffer handling remain predictable;
- one helper restart restores one coherent Rack generation;
- one helper-owned editor can control the complete Rack without importing a GUI framework into the OBS module.

A crash in one vendor plugin or helper-owned UI path can terminate the helper. This is acceptable because OBS remains protected and the Rack helper can be restarted from a coherent last-known-good snapshot. UI/control failures must never be able to block normal Rack DSP through a required shared mutex.

### 3.4 Protocol-neutral `HostedPlugin` seam

The current `Vst3Engine` already owns most single-plugin lifecycle, state, controller and processing behavior, but `process(AudioSlot&)` couples it to the Single IPC layout.

R0 must use **expand → migrate → contract**, not a broad rewrite:

1. introduce a small protocol-neutral process view, conceptually `ProcessBlockView`;
2. make the current Single `process(AudioSlot&)` adapter delegate to that view;
3. characterize Single behavior with regression tests;
4. extract/rename the proven lifecycle unit into a `HostedPlugin`-style deep seam only as far as Rack needs it;
5. Rack consumes the protocol-neutral seam;
6. do not remove the Single adapter until Single regressions prove equivalence.

The goal is not class-count growth. The goal is one deep, testable unit that owns **one VST3 instance** independent of which outer protocol delivers audio/control.

### 3.5 Serial immutable chain generation

Normal Rack DSP processes an immutable ordered view of slots.

Topology mutations are control-plane work:

```text
current generation N
    |
    | add/remove/reorder/replace request
    v
build + validate generation N+1 off realtime path
    |
    | publish only when coherent
    v
DSP swaps at a block frontier
```

No vendor lifecycle operation, filesystem operation, topology allocation or unbounded lock may be introduced into OBS `filter_audio` or Rack normal DSP.

During reconfiguration, the bounded safe result is dry/pass-through until a coherent Rack generation is ready.

### 3.6 Preallocated serial processing

R1 uses preallocated ping-pong working buffers.

For a stereo block:

```text
OBS dry input
   |
   +--> preserve original dry source for fail-open
   |
   v
Buffer A -> Slot 1 -> Buffer B
Buffer B -> Slot 2 -> Buffer A
Buffer A -> Slot 3 -> Buffer B
...
```

No project-owned heap allocation occurs during normal Rack block processing.

### 3.7 Whole-block wet validity

Rack output is published as wet only when the entire active serial chain produces one valid completed block.

If an active slot:

- returns a VST process error;
- crashes the helper;
- becomes unavailable;
- misses the bounded deadline;
- produces an invalid block;

then the partially processed intermediate chain is **not** published for that OBS block. OBS returns the original dry/pass-through block.

This avoids unpredictable “half the rack applied” audio.

A bypassed, Missing or Quarantined slot is a defined pass-through node and does not invalidate the block.

### 3.8 Crash attribution breadcrumb

Before entering vendor work, the Rack helper publishes a lightweight diagnostic breadcrumb containing at minimum:

- rack generation;
- audio block sequence;
- stable slot ID;
- processing phase;
- DSP heartbeat/progress generation.

The breadcrumb is diagnostic evidence for recovery after process death. It is **not absolute proof** that the named slot caused every asynchronous crash.

Policy:

- synchronous/repeatable failure while the same slot is active can raise attribution confidence;
- repeated recovery failure correlated with the same slot can quarantine it;
- ambiguous/asynchronous helper death must remain “unknown/suspect” rather than falsely blaming a plugin.

### 3.9 Slot health model

Every slot has a stable ID and explicit health independent of its position.

Minimum states:

- Empty / unresolved;
- Loading;
- Ready;
- Bypassed;
- Missing;
- Failed;
- Suspect;
- Quarantined.

Position is topology. Slot ID is identity. Reorder must never create a new slot identity.

Missing/Failed/Quarantined slots remain visible and pass through.

### 3.10 Latency policy

For a pure serial Rack, total Rack latency is the sum of active processing-slot latencies.

No internal branch-delay compensation is required in v2 because v2 has no parallel graph.

Rules:

- bypass/missing/quarantined slot contributes zero Rack latency when no vendor processing occurs;
- slot latency changes are collected at a safe lifecycle frontier;
- Rack latency metadata is republished only from a coherent generation;
- a topology/latency transition that is not yet coherent fails dry rather than exposing mixed metadata/audio.

### 3.11 Persistence model

Two persistence concepts remain intentionally separate:

**Session Snapshot**
- automatic;
- scoped to the current Rack filter instance;
- protects current work and recovery;
- zero user Save action.

**Rack Preset**
- explicit, named, reusable artifact;
- independent from the original source/filter UUID;
- can be loaded into another Rack;
- only changes through explicit Save/Update/Rename/Delete user actions.

Both formats are versioned and validate all topology/state metadata before replacing a known-good copy.

### 3.12 v2 UX boundary — superseded by ADR-0003

The previous plan made stock OBS Properties the complete Rack editor. That decision is superseded.

The locked v2 boundary is now:

- OBS Properties uses only public OBS primitives and remains a minimal launcher/status surface;
- primary Rack editing occurs in a dedicated graphical `RackEditorWindow` owned by `obs-safe-vst3-rack-host.exe`;
- no private OBS Qt/widget injection;
- no GUI framework dependency is linked into the OBS module for Rack editing;
- the Rack Editor talks to `RackControlPlane` through commands and immutable UI snapshots; it does not mutate DSP/runtime objects directly;
- vendor editors remain floating native helper-owned windows in v2;
- free-form cable routing remains post-v2.

Toolkit, thread and UI-state details are defined by `ADR-0003-ISOLATED-RACK-EDITOR.md` and `RACK_EDITOR_SPEC.md`.

### 3.13 v2 limits

Default engineering limit for v2 qualification: **maximum 8 serial slots**.

Initial supported Rack processing scope:

- Windows x64;
- OBS compatibility floor inherited from the supported Single product;
- mono/stereo;
- Float32;
- audio effects only;
- one serial main signal lane.

Explicitly out of scope for v2:

- free-form graph;
- parallel sends/returns;
- sidechain routing;
- MIDI/VST3 instruments;
- nested racks;
- direct device I/O;
- per-slot helper processes;
- arbitrary multichannel;
- Float64;
- embedded vendor editors;
- private/custom OBS Qt integration;
- macOS/Linux Rack package.

---

## 4. Rack Entry Gate — REG-0

There is a deliberate sequencing conflict to resolve before production Rack code begins:

- the existing normative contract says Rack starts after Single Host v1.0/S6 lock;
- product direction now identifies VST3 Rack as the next major target from the current stable line.

Do **not** silently pretend S6 is complete and do not silently ignore the contract.

REG-0 must produce one explicit outcome:

### Outcome A — Single extraction baseline is proven sufficient

If the current stable Single implementation already satisfies the subset of Single invariants required to safely extract the shared plugin seam, record:

- exact fixed-point SHA;
- exact green CI/compatibility evidence;
- current state/recovery/lifecycle tests;
- real-machine evidence relevant to the extraction boundary;
- known deferred Single features that do not undermine Rack isolation.

Then accept a small ADR that explicitly unlocks **R0 extraction only** while preserving remaining Single roadmap work separately.

### Outcome B — prerequisite gaps exist

If a missing Single behavior makes the shared seam unsafe or under-specified, create the minimum prerequisite tracer ticket and complete it before R0.

Examples of legitimate blockers:

- no deterministic full state restore seam;
- unsafe lifecycle ownership still mixed with protocol code;
- no exact regression coverage for the seam to be extracted;
- helper/realtime invariants not proven on the fixed point.

Examples that are **not automatically blockers** to a serial Rack extraction:

- Single sidechain feature not shipped;
- MIDI/instruments not shipped;
- graph routing not shipped;
- graphical Rack editor not built.

REG-0 exists to make this sequencing decision explicit and evidence-based.

After REG-0 GO, run the architecture-critical **UI-0 graphical helper feasibility gate** before R0 production extraction. UI-0 does not load VST3 or implement Rack runtime; it validates the chosen helper-only GUI dependency and packaging boundary early so a late UI dependency surprise cannot invalidate R0–R2 work.

---

## 5. Consequences

### Positive

- preserves OBS crash containment;
- avoids IPC between every serial plugin;
- prevents Rack semantics from destabilizing Single protocol;
- removes OBS Properties as a graphical UX bottleneck;
- keeps OBS-module GUI dependencies minimal;
- makes drag reorder/search/presets practical without private OBS widgets;
- creates a direct path to deterministic two-plugin tracer tests;
- leaves a clean future seam for routing/MIDI without paying that engine complexity now.

### Negative / accepted tradeoffs

- one vendor or helper-UI crash can restart the whole Rack helper;
- crash attribution is probabilistic for asynchronous failures;
- no true parallel routing/sidechain in v2;
- helper package now includes a small dedicated graphical stack after UI-0 approval;
- one-process Rack has a larger failure domain than per-slot process isolation.

Those tradeoffs are accepted because the primary containment boundary remains **OBS vs third-party Rack process**, which is the product's core safety promise.

---

## 6. Rejected alternatives

### Free-form graph for v2

Rejected. Too much product/engine/UX complexity before the serial safety contract is proven. The graphical Rack is a serial lane, not a patchbay.

### One helper process per slot

Rejected for v2. Adds IPC, context switches, orchestration and persistence complexity at every hop. Keep as evidence-driven future maximum-isolation mode only.

### Reuse Single protocol and add slot arrays

Rejected. Violates the existing Rack separation invariant and couples two product lifecycles.

### Full Rack editing inside OBS Properties

Rejected as the primary v2 workflow. Public Properties remains the safe launcher/status surface, but its layout primitives are not a suitable long-term graphical Rack foundation.

### Private Qt/widget injection into OBS

Rejected. It couples the product to OBS GUI internals and has already demonstrated compatibility-floor risk in prior experiments.

### In-process atkAudio-style Rack runtime/UI inside OBS

Rejected. atkAudio's graphical workflow is useful reference material, but our differentiator requires third-party VST3 and Rack host UI/runtime dependencies to remain outside `obs64.exe`.

### Embedded vendor UI in Rack cards

Rejected for v2. Keep the already proven floating native editor path; revisit embedding only after v2 lock.

### Partial-wet output after a failed slot

Rejected. A Rack block is one logical transaction. Fail open to original dry when the active chain cannot produce a coherent complete block.

---

## 7. Implementation authority

For Rack work, read in this order:

1. `AGENTS.md`
2. `docs/CODEX_EXECUTION_CONTRACT.md`
3. applicable section of `docs/NORTH_STAR_PRD.md`
4. this ADR
5. `docs/rack/ADR-0003-ISOLATED-RACK-EDITOR.md`
6. `docs/rack/VST3_RACK_RESEARCH.md`
7. `docs/rack/RACK_EDITOR_SPEC.md`
8. `docs/rack/VST3_RACK_EXECUTION_SPEC.md`
9. `docs/rack/VST3_RACK_TICKETS.md`
10. the current GitHub ticket and exact fixed-point repository code

If this ADR conflicts with the normative contract on a product invariant, **the normative contract wins** unless that contract is explicitly amended by a later accepted ADR/document change. ADR-0003 specifically supersedes only the older Rack UI implementation decision, not the safety/runtime contract.
