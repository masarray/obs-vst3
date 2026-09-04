# OBS Safe VST3 Host — Product Roadmap

OBS Safe VST3 Host focuses on one product promise: **make modern VST3 audio effects practical in OBS without deliberately trusting third-party plug-in code with the stability of `obs64.exe`.**

> Roadmap items describe product direction, not guaranteed release dates. Features become stable only after their realtime, lifecycle, recovery, OBS-integration and packaging gates are strong enough for live use.

## Product principles

1. **OBS remains the broadcast host.** Normal workflows should not require a separate DAW.
2. **Third-party plug-in code stays outside `obs64.exe`.** New features must preserve the isolation boundary.
3. **Realtime work stays bounded.** UI, scanning, disk/state and lifecycle work must not become unbounded audio-callback work.
4. **Failure degrades gracefully.** An unavailable wet path should fail to dry/pass-through rather than deliberately stall the broadcast path.
5. **Simple stays simple.** The Single Host remains available even as Rack features grow.
6. **Compatibility claims stay evidence-based.** Prefer reproducible qualification and clear diagnostics over vendor-name special cases.

---

## Stage 1 — Safe VST3 Single Host — Stable

**Status: public stable since v0.5.0; retained in v0.6.0**

The Single Host is the focused one-effect workflow.

Available capabilities include:

- native OBS **VST 3.x Plug-in** filter;
- automatic installed VST3 discovery and manual bundle selection;
- isolated scanner probes;
- isolated VST3 runtime helper;
- native vendor GUI;
- component/controller state persistence;
- watchdog health monitoring and bounded recovery;
- dry fail-open behavior;
- Windows x64 / OBS 29.1+ compatibility floor.

The Single Host remains the recommended path when one EQ, compressor, denoiser, reverb or mastering processor is all the user needs.

---

## Stage 2 — Safe VST3 Rack — Stable in v0.6.0

**Status: public stable in v0.6.0**

The Rack turns multiple VST3 audio effects into one isolated serial processing lane while remaining a separate OBS product surface and helper process.

Delivered in v0.6.0:

- separate OBS **VST3 Rack** filter;
- separate Rack helper executable and protocol;
- serial multi-effect chain;
- stable slot identity through topology changes;
- add, replace, remove and reorder;
- per-slot bypass / enable workflow;
- graphical helper-owned Rack Editor;
- native vendor editor orchestration;
- coherent Session Snapshot recovery;
- named Rack preset save/load/rename/update/delete;
- pass-through placeholders for missing VST3 slots;
- bounded handling of invalid preset loads;
- fail-dry behavior when a valid Rack result is unavailable;
- bounded helper shutdown and editor close/reopen hardening.

Current Rack scope is intentionally a **serial effects lane**, not a free-form node graph.

Example:

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

### Remaining Rack hardening

Future Rack work may deepen diagnostics, quarantine/recovery behavior, stress qualification and compatibility intelligence without weakening the stable v0.6.0 product contract.

---

## Stage 3 — Routing, Sidechain and Advanced Audio Buses

**Status: future**

Goal: expand beyond a linear serial lane only after the stable Rack baseline remains dependable.

Potential capabilities:

- sidechain input support;
- VST3 multi-bus audio handling;
- explicit input/output routing;
- parallel paths where justified;
- send/return concepts;
- bus-aware latency handling;
- richer channel-layout negotiation;
- wider multichannel support;
- Float64 fallback where required and evidence-backed.

Possible use cases include music ducking from microphone level, parallel compression and more complex broadcast/master chains.

---

## Stage 4 — MIDI and VST3 Instruments

**Status: future**

Instrument hosting requires a real event/timing model and is deliberately separate from today's audio-effect product.

Potential capabilities:

- VST3 instrument class support;
- MIDI device selection;
- event-bus delivery;
- note, velocity and controller events;
- stable event timing relative to audio blocks;
- sustain/pitch/modulation handling;
- transport/clock concepts where justified;
- instrument state/preset persistence;
- low-latency live monitoring;
- recovery behavior designed to avoid stuck notes where possible.

---

## Stage 5 — Professional live-audio platform

**Long-term direction**

Bring the proven isolation/recovery model to a broader live-performance and broadcast environment without turning the default experience into a DAW-sized control surface.

Potential directions:

- combined effect/instrument rigs;
- MIDI mapping and macro controls;
- scene-aware Rack presets;
- snapshots and fast recall;
- A/B chains;
- richer metering and diagnostics;
- compatibility intelligence;
- signed Windows releases;
- cross-platform packages where engineering and support quality justify them.

## What is deliberately not promised

The roadmap does **not** promise:

- a date for sidechain, routing or MIDI stages;
- universal compatibility with every VST3 implementation;
- a zero-crash guarantee for every Windows/driver/hardware failure;
- macOS/Linux packages on a fixed timeline;
- replacement of a full professional DAW for every production task.

## How a roadmap item becomes stable

A major feature should not be called stable until applicable gates cover:

1. lifecycle/state contracts;
2. bounded realtime behavior;
3. failure and recovery behavior;
4. automated deterministic regression tests;
5. Windows packaging and loader qualification;
6. representative real-machine VST3 evidence;
7. OBS integration and shutdown/reopen behavior;
8. an understandable beginner workflow;
9. synchronized public documentation and release notes.

The project should remain ambitious about workflow and conservative about guarantees.
