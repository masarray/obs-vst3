# VST3 Rack Research — What to Learn, What Not to Copy

**Status:** Research input for Rack v2 execution  
**Date:** 2026-08-27  
**Implementation authority:** ADR-0002 + Execution Spec, not this document alone

---

## 1. Research question

The question is not:

> “How do we build a full plugin host like a DAW?”

The question is:

> “How do we add a professional multi-VST3 Rack to OBS while preserving the product's strongest advantage: simple workflow, crash isolation, bounded realtime behavior and recoverable user state?”

This research deliberately looks at mature hosts for **patterns**, not source-code copying.

---

## 2. Kushview Element

Repository: `https://github.com/kushview/element`

Element is a mature C++/JUCE plugin host with VST/VST3, MIDI and graph capabilities.

### 2.1 Useful architecture patterns

Element separates several concerns that should also remain separate in our Rack:

- **Processor/runtime abstraction** — one processing object has lifecycle, ports, latency, bypass and render behavior.
- **Graph topology** — nodes and connections are separate from the processor implementation.
- **Render-plan construction** — the graph computes a sequence of render operations instead of rediscovering topology inside every audio block.
- **Shared-buffer planning** — rendering reuses buffers according to the precomputed execution plan.
- **Latency propagation** — graph construction accounts for node latency.
- **State model vs runtime** — Element's project guidance separates persistent model state from the actual processor/runtime object.
- **message-thread vs audio-thread discipline** — configuration/state work is marshalled away from the audio thread.
- **testable engine core** — engine behavior is intentionally structured so it can be exercised without real hardware.

Relevant Element source areas reviewed:

- `src/engine/graphnode.hpp`
- `src/engine/graphbuilder.hpp`
- `include/element/processor.hpp`
- `include/element/session.hpp`
- `CLAUDE.md`

### 2.2 What we should take from Element

1. A protocol-neutral **HostedPlugin** runtime seam.
2. Stable IDs independent of visual order.
3. Topology and processing plan as separate concepts.
4. Build/validate a new plan off the realtime path.
5. Latency as explicit runtime metadata.
6. State representation separate from live processor ownership.
7. Deterministic engine tests independent of OBS UI.

### 2.3 What we should NOT take into Rack v2

Do not copy Element's full graph product shape.

For v2 we do **not** need:

- arbitrary cables;
- user-created node graph;
- MIDI graph;
- nested graph processing;
- arbitrary audio/CV ports;
- parallel branches;
- graph editor UI.

A free-form graph would multiply the state space before the serial-chain safety contract is proven.

### 2.4 Key translation for our project

Element concept:

```text
Graph -> nodes -> connections -> build rendering sequence -> render
```

Our v2 translation:

```text
Rack topology -> ordered slots -> build immutable chain generation -> serial render
```

We preserve the **engine discipline** but drastically reduce the product topology.

---

## 3. Cantabile

Official product/documentation reviewed from Cantabile's rack, routing and state concepts.

### 3.1 Useful product patterns

Cantabile's strongest lesson is not a specific C++ architecture; it is **how a live host presents complexity**.

Useful concepts:

- a Rack is an understandable **black box**;
- the normal user works with plugins and signal flow, not implementation details;
- ports/routes exist, but advanced routing does not need to dominate the basic workflow;
- rack/song states preserve reusable configuration;
- reusable racks reduce repeated setup work;
- default insertion/routing should be sensible instead of requiring manual wiring for the common case.

### 3.2 What we should take

- Rack is a **user-facing unit**, not merely an array of plugins.
- State and reuse are product features, not engineering afterthoughts.
- A simple chain must work without exposing a patchbay.
- Advanced routing can arrive later without forcing the initial Rack into a graph UI.

### 3.3 What we should not copy yet

- linked/embedded rack complexity;
- arbitrary route matrix;
- live bindings/triggers breadth;
- MIDI route semantics;
- large state/switching model.

Those may inspire post-v2 features, but are not prerequisites for a world-class OBS serial Rack.

---

## 4. Gig Performer

Gig Performer is a live-performance host organized around rackspaces, wiring and performance controls.

### 4.1 Useful product patterns

- **configuration view and performance view are different jobs**;
- a reusable rackspace is more valuable than forcing users to reconstruct a chain;
- live software benefits from fast, predictable recall;
- the audio topology can be sophisticated internally while the performance surface remains simple.

### 4.2 Translation to OBS Safe VST3

For v2:

- OBS Properties is the **configuration surface**;
- native vendor editors remain the detailed plugin-control surface;
- Rack Presets are the reusable configuration artifact;
- a future dedicated live/performance panel can be added only after the Rack runtime is stable.

Do not make v2 depend on a performance dashboard.

---

## 5. Carla

Carla demonstrates both Rack and Patchbay modes and supports plugin bridging.

### 5.1 Useful patterns

- Rack and Patchbay are legitimately different user modes.
- Bridging can isolate plugins or formats.
- A patchbay becomes valuable once routing is a primary problem.

### 5.2 Warning for this project

Every extra process boundary, routing mode and bridge protocol adds:

- more failure states;
- more latency accounting;
- more synchronization;
- more persistence complexity;
- more diagnostics;
- more UI states.

OBS Safe VST3 already has a strong outer isolation boundary: **OBS vs helper**.

For v2, adding an IPC boundary between every serial plugin would solve a problem we have not yet proven is worth the cost.

### 5.3 Decision

Keep one Rack helper process for v2. Revisit per-slot processes only after real crash data shows that whole-rack helper restart/quarantine is insufficient.

---

## 6. Matt Pocock engineering workflow applied to this repository

The useful part of the Matt Pocock workflow is not a magic command name; it is **context discipline and vertical execution**.

Use this sequence:

```text
RESEARCH
  -> SPEC
  -> TRACER-BULLET TICKETS
  -> FRESH CONTEXT PER TICKET
  -> TDD AT A PRE-AGREED SEAM
  -> FIXED-POINT CODE REVIEW
  -> EXACT-HEAD QUALIFICATION
```

### 6.1 Research

Before coding a behavior:

- find the current repository fixed point;
- read authoritative product/architecture docs;
- inspect current code at the relevant seam;
- research external API behavior when uncertain;
- resolve architectural uncertainty before implementation.

### 6.2 Spec

The spec must define:

- observable behavior;
- scope and non-goals;
- ownership/threading boundary;
- failure behavior;
- persistence semantics;
- test seam;
- exact acceptance gate.

### 6.3 Tickets as tracer bullets

Each ticket proves one user-visible or architecture-critical vertical behavior end-to-end.

Bad ticket:

> “Build Rack engine.”

Good ticket:

> “Two deterministic Gain plugins process in serial through the separate Rack helper; Gain A then Gain B produces the expected output, total latency is reported, and killing the Rack helper causes bounded dry output in OBS.”

Every ticket states `Blocked by:` dependencies.

### 6.4 Fresh context per implementation ticket

Do not carry one enormous conversation through R0–R5.

A new implementation thread reads the repository source of truth, takes **one unblocked ticket**, establishes a fresh exact fixed point and finishes only that ticket.

This prevents:

- stale assumptions;
- accidental scope expansion;
- old failed experiments influencing architecture;
- “while we are here” refactors.

### 6.5 Test-driven implementation

For every behavior where a deterministic seam exists:

```text
write failing test
-> implement minimum production change
-> make focused test green
-> run surrounding regressions
-> refactor only while green
```

The first Rack audio tests should use deterministic test processors/plugins, not commercial plugins.

Commercial plugins are qualification evidence after deterministic engine behavior is proven.

### 6.6 Fixed-point code review

Review from the ticket's declared base SHA to the exact candidate head.

Review two dimensions separately:

**Standards review**
- safety invariants;
- realtime discipline;
- ownership/threading;
- protocol separation;
- lifecycle correctness;
- persistence atomicity;
- OBS Properties ownership;
- compatibility floor.

**Spec review**
- did this ticket implement exactly the promised behavior?
- were all acceptance tests actually executed?
- did implementation accidentally add out-of-scope features?

If the head changes after review, old final qualification is no longer authorization for the new head.

---

## 7. Competitive synthesis

| Topic | Element | Cantabile | Gig Performer | Carla | OBS Safe VST3 v2 decision |
|---|---|---|---|---|---|
| Core topology | graph | racks + routes | rackspaces + wiring | rack/patchbay | **serial chain** |
| Runtime abstraction | strong processor/node model | product-oriented | product-oriented | host/bridge oriented | **protocol-neutral HostedPlugin** |
| Render planning | graph sequence | internal | internal | internal | **immutable serial chain generation** |
| Buffer strategy | shared/reused | internal | internal | internal | **preallocated ping-pong** |
| Latency | graph-aware | live host aware | live host aware | host aware | **sum active serial slots** |
| Reuse | session/model | rack/song states | rackspace/favorites | projects | **Session Snapshot + Rack Preset** |
| Live UX | graph/editor rich | simple rack first | performance-oriented | technical | **vertical signal lane** |
| Advanced routing | yes | yes | yes | yes | **defer post-v2** |
| MIDI/instruments | yes | yes | yes | yes | **defer post-v2** |
| Isolation priority | not OBS-specific | live stability | live stability | bridges available | **OBS/helper isolation is core product law** |

---

## 8. Primary strategic conclusion

The fastest path to a professional Rack is **not** to build more features at once.

It is to freeze the v2 problem to this:

> Build the smallest serial Rack that can host multiple real VST3 effects, preserve all state, survive a bad slot without losing OBS or the user's chain, and let the chain be reused as a named preset.

Everything else—routing, sidechain, MIDI, instruments, graph UI, nested racks—becomes easier after that runtime and document model are stable.

---

## 9. Sources reviewed

Primary public references:

- Kushview Element: `https://github.com/kushview/element`
- Element `GraphNode`: `src/engine/graphnode.hpp`
- Element `GraphBuilder`: `src/engine/graphbuilder.hpp`
- Element `Processor`: `include/element/processor.hpp`
- Element engineering guidance: `CLAUDE.md`
- Cantabile guides: `https://www.cantabilesoftware.com/guides/`
- Gig Performer documentation: `https://gigperformer.com/docs/`
- Carla project: `https://github.com/falkTX/Carla`

The external projects are references for architecture/product patterns only. Their source is not copied into this repository.
