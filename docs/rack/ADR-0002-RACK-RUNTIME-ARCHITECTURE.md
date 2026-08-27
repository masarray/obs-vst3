# ADR-0002 — VST3 Rack Runtime Architecture

**Status:** Accepted for planning / implementation entry  
**Date:** 2026-08-27  
**Scope:** Safe VST3 Rack v2.0  
**Clarifies:** `docs/CODEX_EXECUTION_CONTRACT.md`, `docs/NORTH_STAR_PRD.md`  
**Does not supersede:** existing Single Host safety invariants

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

---

## 2. Why this decision

The existing product differentiator is not “largest host feature list.” It is **OBS-specific workflow + third-party crash containment + bounded realtime behavior + deterministic recovery**.

A Rack must preserve that promise.

Research of mature live/plugin hosts supports several useful patterns:

- Kushview Element demonstrates the value of separating processor runtime, topology, rendering order, shared-buffer planning and latency propagation.
- Cantabile demonstrates the product value of treating a rack as an understandable black box with stable state/reuse concepts and hiding routing complexity until needed.
- Gig Performer demonstrates the value of separating configuration/wiring from live-performance controls and making reusable rack configurations first-class.
- Carla demonstrates the power of rack/patchbay and bridging, but also the additional failure surface that appears when routing and process-bridge complexity are introduced too early.

For OBS Safe VST3 Host, the correct v2 compromise is therefore:

**deep engine separation internally; simple serial product surface externally.**

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
- one helper restart restores one coherent Rack generation.

A crash in one vendor plugin can terminate the helper. This is acceptable because OBS remains protected and the Rack helper can be restarted from a coherent last-known-good snapshot.

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

No vendor lifecycle operation, filesystem operation, topology allocation or unbounded lock may be introduced into OBS `filter_audio`.

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

### 3.12 v2 UX boundary

Rack v2.0 must **not depend on custom Qt or internal OBS widget injection**.

Initial Rack UX uses public OBS Properties primitives only.

A normal user sees a vertical signal lane, not nodes/cables:

```text
VST3 Rack

Preset: [ Broadcast Vocal v ]  [Load]

1  RX De-noise       Ready       [Bypass] [Open UI]
   [Replace] [Move Up] [Move Down] [Remove]

2  Pro-Q 3           Ready       [Bypass] [Open UI]
   [Replace] [Move Up] [Move Down] [Remove]

3  Pro-C 2           Ready       [Bypass] [Open UI]
   [Replace] [Move Up] [Move Down] [Remove]

[ + Add Effect ]

[Save as Preset] [Update Preset] [Rename] [Delete]
```

Drag-and-drop is optional future polish, not a v2 release gate.

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
- custom Qt/canvas dependency;
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
- custom Rack UX not built.

REG-0 exists to make this sequencing decision explicit and evidence-based.

---

## 5. Consequences

### Positive

- preserves OBS crash containment;
- avoids IPC between every serial plugin;
- prevents Rack semantics from destabilizing Single protocol;
- keeps v2 user workflow understandable;
- creates a direct path to deterministic two-plugin tracer tests;
- leaves a clean future seam for routing/MIDI without paying that complexity now.

### Negative / accepted tradeoffs

- one vendor crash can restart the whole Rack helper;
- crash attribution is probabilistic for asynchronous failures;
- no true parallel routing/sidechain in v2;
- stock OBS Properties limits visual polish;
- one-process Rack has a larger failure domain than per-slot process isolation.

Those tradeoffs are accepted because the primary containment boundary remains **OBS vs third-party Rack process**, which is the product's core safety promise.

---

## 6. Rejected alternatives

### Free-form graph for v2

Rejected. Too much product/engine/UX complexity before the serial safety contract is proven.

### One helper process per slot

Rejected for v2. Adds IPC, context switches, orchestration and persistence complexity at every hop. Keep as evidence-driven future maximum-isolation mode only.

### Reuse Single protocol and add slot arrays

Rejected. Violates the existing Rack separation invariant and couples two product lifecycles.

### Custom Qt Rack editor as v2 dependency

Rejected. Compatibility floor is more valuable than drag-and-drop polish. Public OBS Properties is the v2 baseline.

### Partial-wet output after a failed slot

Rejected. A Rack block is one logical transaction. Fail open to original dry when the active chain cannot produce a coherent complete block.

---

## 7. Implementation authority

For Rack work, read in this order:

1. `AGENTS.md`
2. `docs/CODEX_EXECUTION_CONTRACT.md`
3. applicable section of `docs/NORTH_STAR_PRD.md`
4. this ADR
5. `docs/rack/VST3_RACK_EXECUTION_SPEC.md`
6. `docs/rack/VST3_RACK_TICKETS.md`
7. the current GitHub ticket and exact fixed-point repository code

If this ADR conflicts with the normative contract on a product invariant, **the normative contract wins** unless that contract is explicitly amended by a later accepted ADR/document change.
