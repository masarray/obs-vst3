# OBS Safe VST3 Host — Product Roadmap

OBS Safe VST3 Host starts with a focused problem: **OBS's built-in VST filter does not support VST3, while modern studio plug-ins increasingly ship as VST3-only.**

The current stable line solves that problem for VST3 audio effects with an OBS-native workflow and crash-isolated hosting. The roadmap extends the same reliability model into a broader live-audio platform.

> Roadmap items describe product direction, not guaranteed release dates. New features should ship only after their lifecycle, realtime, recovery and compatibility gates are strong enough for live use.

## Product principles

Every roadmap stage should preserve these constraints:

1. **OBS remains the broadcast host.** The user should not need a separate DAW for the normal workflow.
2. **Third-party plug-in code stays outside `obs64.exe`.** New features should not weaken the crash-containment boundary.
3. **Realtime audio stays bounded.** Editor, disk, scanning, state and recovery work must not become unbounded realtime callback work.
4. **Failure should degrade gracefully.** A broken plug-in should not be allowed to intentionally stall the broadcast path.
5. **User workflow stays understandable.** Advanced routing should be optional, not forced on someone who only wants one EQ or compressor.
6. **Compatibility claims remain evidence-based.** Prefer real-machine qualification and exact failure diagnostics over vendor-name hacks.

---

## Stage 1 — Safe VST3 Effects — Stable now

**Status: v0.5.0 public stable**

The current product provides one VST3 audio effect per OBS filter.

### Available capabilities

- native OBS filter: **VST 3.x Plug-in**;
- automatic installed VST3 discovery;
- manual `.vst3` selection for non-standard locations;
- conservative effect-vs-instrument classification;
- isolated scanner probes;
- isolated VST3 runtime process;
- native vendor GUI;
- editor foreground activation for fast tweaking;
- full component/controller state persistence;
- watchdog-based helper health monitoring;
- bounded recovery/backoff;
- dry fail-open behavior when a valid wet block is unavailable;
- OBS 29.1+ Windows compatibility floor for the stable line.

### Stable product goal

Let a streamer use modern EQ, dynamics, restoration, saturation, spatial and mastering-style VST3 processors in OBS without deliberately loading third-party plug-in code into the OBS process.

---

## Stage 2 — Safe VST3 Rack

**Goal: turn multiple single effects into a practical live chain.**

A rack should let users build processing such as:

```text
Input
  ↓
Noise reduction
  ↓
Corrective EQ
  ↓
Compressor / De-esser
  ↓
Saturation
  ↓
Limiter / Maximizer
  ↓
OBS output
```

### Planned rack capabilities

- multiple VST3 effects in one rack;
- insert, remove and reorder;
- per-slot bypass;
- rack-level enable/bypass;
- clear per-slot health state;
- rack preset save/load;
- deterministic state restoration;
- aggregate latency reporting/compensation strategy;
- bounded handling when one slot fails;
- fast access to each native vendor editor;
- simple default view with an optional advanced view.

### Reliability question to solve

A rack must define what happens if one plug-in crashes while the rest are healthy. The implementation should preserve as much of the chain as safely possible without creating an uncontrolled restart loop or corrupting state.

---

## Stage 3 — Routing, Sidechain and Advanced Audio Buses

**Goal: move from a linear chain to a broadcast-grade signal flow.**

### Planned capabilities

- sidechain input support;
- VST3 multi-bus audio handling;
- explicit input/output routing;
- parallel paths where practical;
- send/return-style processing concepts;
- bus-aware latency handling;
- clearer channel-layout negotiation;
- richer multichannel support;
- Float64 fallback where required and justified.

### Example use cases

- duck music from microphone level;
- sidechain a compressor from another OBS source;
- parallel compression;
- route restoration before a creative effects branch;
- build more complex broadcast/master chains without leaving OBS.

---

## Stage 4 — MIDI and VST3 Instruments

**Goal: make OBS capable of hosting live VST3 instruments, not only effects.**

This is intentionally a later stage because instrument hosting is not just “allow synths in the list.” It requires a real event/timing model.

### Planned capabilities

- VST3 instrument class support;
- MIDI input device selection;
- VST3 event-bus delivery;
- note on/off, velocity and controller events;
- stable event timestamps relative to audio blocks;
- sustain/pitch/modulation handling;
- transport/clock concepts where required;
- instrument state and preset persistence;
- low-latency live monitoring path;
- safe editor lifecycle for instruments;
- recovery behavior that avoids stuck notes where possible.

### Example use cases

- play a software piano or synth during a livestream;
- trigger sampler instruments from a MIDI controller;
- combine instrument output with a VST3 effects rack;
- route live-performance instruments directly into OBS scenes.

---

## Stage 5 — Live Performance Rack

**Long-term product vision**

Bring effects, instruments and routing together into a live-performance environment designed specifically for OBS.

Potential directions include:

- combined instrument + effect racks;
- MIDI mapping for rack controls;
- scene-aware rack presets;
- macro controls;
- A/B chains;
- snapshots and fast recall;
- metering per stage;
- safe background preset/state capture;
- performance-oriented switching without audio-path stalls.

The end goal is a creator workflow where OBS can host a **studio-quality live audio rig**: mastering-style stream processing, routed effect chains, sidechains and eventually MIDI instruments—without forcing the user to run a full DAW beside OBS for common live workflows.

---

## What is deliberately not promised yet

The roadmap does **not** currently promise:

- a release date for rack or MIDI stages;
- universal compatibility with every VST3 implementation;
- a zero-crash guarantee for every Windows/driver/hardware failure;
- macOS/Linux packages on a specific timeline;
- replacement of a full professional DAW for every production task.

The project should remain ambitious about workflow and conservative about guarantees.

## How roadmap items become stable

A major roadmap feature should not be considered stable until it has:

1. a clear lifecycle contract;
2. bounded realtime behavior;
3. failure/recovery behavior defined;
4. automated tests for the critical state machine;
5. Windows packaging/loader qualification;
6. real-machine compatibility evidence with representative commercial plug-ins;
7. a beginner workflow that does not expose unnecessary engineering complexity.

That acceptance discipline is part of the product: the goal is not merely to make VST3 features appear in OBS, but to make them dependable enough for a live broadcast.
