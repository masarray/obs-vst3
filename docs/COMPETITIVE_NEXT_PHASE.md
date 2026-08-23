# Competitive Next Phase — Beyond atkAudio PluginHost

Status: strategy / implementation contract candidate

Target product position:

> **OBS Safe VST3 Host — the safest production-oriented VST3 host for live OBS workflows: modern plug-in UX without allowing a third-party VST to take OBS down.**

This document defines the next engineering phases after `v0.2.0-preview.1`. The goal is not to copy every feature in atkAudio PluginHost/PluginHost2. The goal is to reach parity on the features that matter to normal VST users, then create a clear lead in crash containment, recovery, diagnostics, lifecycle correctness, and safe creator workflow.

## Evidence and primary sources

Competitive implementation/source:

- atkAudio repository: https://github.com/atkAudio/PluginForObsRelease
- atkAudio README/capabilities: https://github.com/atkAudio/PluginForObsRelease/blob/main/README.md
- atkAudio OBS filter implementation: https://github.com/atkAudio/PluginForObsRelease/blob/main/src/plugin_host.cpp
- atkAudio native/generic editor implementation: https://github.com/atkAudio/PluginForObsRelease/blob/main/src/core/atkaudio/PluginHost2/Editor/PluginWindow.h
- atkAudio issue #3 — UI/state behavior without audio: https://github.com/atkAudio/PluginForObsRelease/issues/3
- atkAudio issue #45 — scene-switch audio glitch report: https://github.com/atkAudio/PluginForObsRelease/issues/45
- atkAudio issue #46 — repeated update prompt: https://github.com/atkAudio/PluginForObsRelease/issues/46
- atkAudio issue #47 — Flatpak packaging: https://github.com/atkAudio/PluginForObsRelease/issues/47
- atkAudio issue #48 — loopback/device-routing request: https://github.com/atkAudio/PluginForObsRelease/issues/48

Host correctness:

- Steinberg VST3 API documentation: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/API%2BDocumentation/Index.html
- Steinberg communication FAQ: https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Communication.html
- Steinberg EditorHost example: https://steinbergmedia.github.io/vst3_dev_portal/pages/What%2Bis%2Bthe%2BVST%2B3%2BSDK/EditorHost.html
- OBS 32.2.2 source API reference: https://docs.obsproject.com/reference-core-objects

Engineering method follows the useful parts of Matt Pocock's engineering skills repository: research from primary sources, deep-module design, high-level test seams, and tracer-bullet vertical slices rather than broad layer-by-layer rewrites.

## Competitive reality

atkAudio currently has significantly broader functionality. Its public README advertises VST3 hosting, native plug-in workflow, MIDI, sidechain, optional multithreading, direct audio/MIDI device I/O, AU on macOS, LADSPA/LV2 on Linux, and a second host with graphs/chains, routing, sample-rate conversion, drift compensation, and MIDI control.

Its OBS filter code also saves/restores host state and directly calls `pluginHost->process(...)` from the OBS audio filter path. The same filter owns a `PluginHost` instance and opens its editor from OBS controls. That architecture provides low-friction feature richness, but it also creates the strategic opening for this project: our invariant is that OBS does not load or execute the third-party VST3 binary.

The current Safe Host already leads in one important dimension:

- isolated discovery/probing;
- VST3 loaded in a helper process, not `obs64.exe`;
- bounded shared-memory audio handoff;
- dry fail-open behavior;
- automatic helper restart attempt.

However, `v0.2.0-preview.1` is not yet a daily-driver replacement because it lacks editor/parameter UX and complete state persistence.

## What “better” means

Do not define success as “more features than atkAudio.” Define it as:

1. **Safer:** a faulty VST may break itself, but it must not be able to deadlock or crash OBS through the normal hosting path.
2. **Usable:** opening a VST must feel like a normal host: select plug-in, open native UI, edit parameters, reopen OBS, and get the same state back.
3. **Recoverable:** after a helper/VST crash, the filter returns to dry audio immediately, restarts asynchronously, restores the latest safe state, and returns wet audio without an abrupt click.
4. **Observable:** the user can see whether a plug-in is healthy, restarting, quarantined, missing deadlines, or unsupported and can copy a useful diagnostic report without reading raw logs.
5. **Simple:** common creator workflows stay one-filter simple. Advanced routing must not turn the default product into a DAW graph editor.
6. **Correct:** state/UI/parameter handling follows VST3 host requirements, including parameter flushing when normal audio processing is inactive.

## Competitive scorecard

| Capability | Safe Host v0.2 | atkAudio 0.34.x | Strategic target |
|---|---:|---:|---:|
| VST3 effects | trial | yes | production-grade |
| Third-party VST isolated from OBS | **yes** | no equivalent isolation in the normal `PluginHost` filter path | **yes, invariant** |
| Scanner candidate isolation | **yes** | not the core differentiator | **yes + quarantine/cache** |
| Native vendor editor | no | yes | **yes, helper-owned** |
| Generic parameter editor | no | yes | **yes** |
| State/preset persistence | no/full next phase | yes | **yes + crash restore** |
| Sidechain | no | yes | **yes** |
| Serial plug-in chain | via multiple OBS filters only | yes / graph host | **Safe Rack later** |
| MIDI control | no | yes | later |
| Direct audio device routing | no | yes | intentionally not near-term core |
| Cross-platform | Windows only | Windows/macOS/Linux | later, after Windows stability |
| Crash-loop quarantine | partial restart only | not a headline capability | **yes** |
| User-facing health diagnostics | minimal logs | mature UI | **yes, first-class** |
| Per-node crash bypass in a rack | no | no safety claim | **long-term differentiator** |

## Architecture direction: deepen, do not sprawl

Use a small number of deep modules. The caller should not need to understand process creation, shared memory, VST3 lifecycle order, Windows editor handles, state blobs, or recovery timing.

### 1. Safe Plugin Session

The OBS side should treat one hosted VST as one session with a small external interface. Internally it owns:

- helper lifetime;
- realtime bridge publication/retirement;
- command/control IPC;
- parameter catalog;
- state snapshots;
- recovery state machine;
- health information.

The OBS audio callback crosses only the realtime facet. It must never perform editor, scanner, filesystem, serialization, process-management, or VST lifecycle work.

### 2. Hosted VST3

The helper owns one deep module that hides VST3 component/controller complexity:

- component/controller creation and connection;
- bus activation;
- processing setup;
- parameter metadata and edits;
- component/controller state;
- `restartComponent()` handling;
- native `IPlugView` lifecycle;
- latency reporting.

This should be the main correctness seam for host tests.

### 3. Plugin Catalog

Discovery should become a persistent catalog, not just a scan result list. It owns:

- canonical Processor UID;
- bundle path, vendor, name, version;
- scan timestamp/fingerprint;
- editor capability;
- parameter/bus summary;
- last successful load;
- crash/timeout counters;
- quarantine state;
- compatibility notes generated locally.

Do not expose scanner subprocess details to the OBS properties UI.

### 4. Safe Rack — later

Do not build a general graph first. A serial rack is enough for the majority of vocal/music creator workflows and is much easier to make safe.

Long-term, offer two execution policies:

- **Safe mode:** each rack node can fail independently; a failed node is bypassed while the remaining chain continues.
- **Performance mode:** multiple nodes may share a helper to reduce process/IPC overhead, accepting a larger failure blast radius.

The default must remain Safe mode.

## Phase P2 — Daily-driver usability (`v0.3.x`)

This is the highest priority. Do not start graph, MIDI, device routing, or cross-platform work before this phase is solid.

### P2.1 Parameter/control bridge + generic editor

Deliver one complete vertical slice:

- enumerate real VST3 parameters from the controller;
- expose normalized parameter editing in OBS;
- send edits over bounded control IPC;
- forward UI-originated `beginEdit/performEdit/endEdit` semantics correctly;
- capture plug-in-originated parameter changes;
- handle title/value refresh flags from `restartComponent()`;
- keep audio callback free of control work.

The generic editor is the compatibility fallback and the test surface for parameter IPC.

### P2.2 Full VST3 state persistence

Persist both component state and controller state where available.

Required behavior:

- state survives OBS restart;
- state is restored after helper restart;
- malformed/oversized state is rejected safely;
- state save/load never blocks the OBS audio callback;
- control failures leave audio dry rather than deadlocking.

Add a debounced “last known good state” snapshot after meaningful parameter edits so recovery does not revert to an ancient state.

### P2.3 Native vendor GUI in the helper

The VST editor must remain in the helper process with the VST binary.

Windows requirements:

- helper-owned top-level `HWND`;
- `IPlugView` attach/remove lifecycle;
- `IPlugFrame::resizeView()` support;
- DPI-aware sizing;
- editor close does not kill processing;
- unsupported/misbehaving editor falls back to generic editor;
- helper/editor crash still cannot directly crash OBS.

Steinberg's EditorHost is direct primary-source prior art for this implementation.

### P2.4 No-audio control flush

This should be treated as host correctness, not an optional optimization.

Steinberg explicitly documents that when the audio engine is not running but the plug-in remains activated, parameter changes from the UI still need periodic process flushing with null audio so processor state remains synchronized.

This also directly addresses a class of problems visible in atkAudio issue #3.

Acceptance target: state/UI changes remain correct even when the source is silent/inactive and no normal audio block has arrived recently.

## Phase P3 — Production safety lead (`v0.4.x`)

### P3.1 Recovery state machine

Replace “restart attempt” with explicit states:

`Stopped -> Starting -> Running -> Degraded -> Restarting -> Running | Quarantined`

Required behavior:

- helper death is detected without waiting in the audio callback;
- audio immediately remains/passes dry;
- stale shared-memory generations cannot be reused;
- last known good state is restored after restart;
- wet audio returns with a short bounded crossfade to reduce discontinuity;
- repeated failure enters quarantine instead of spawning forever.

### P3.2 Crash-loop quarantine

Per plug-in/session:

- count startup crashes, runtime crashes, scanner timeouts, and deadline misses separately;
- exponential retry backoff outside the audio thread;
- quarantine after a defined threshold;
- explicit user action to retry/reset quarantine;
- never silently delete the user's stored state.

### P3.3 Health/diagnostic UX

The filter properties should expose a compact status area:

- Running / Dry / Restarting / Quarantined;
- reported plug-in latency;
- recent deadline misses;
- helper restart count;
- last failure reason;
- button: `Copy Diagnostic Report`;
- button: `Restart Plug-in`;
- button: `Reset / Clear Quarantine` when applicable.

The diagnostic report should contain only useful compatibility information: OBS version, Safe Host version, OS, VST name/vendor/version/UID/path, editor support, bus summary, latency, restart/failure reason, and relevant Safe Host log tail.

### P3.4 Lifecycle stability through scene activity changes

A scene/source becoming inactive should not unnecessarily destroy/recreate the VST session.

The control plane and saved state should remain valid independently of whether normal audio blocks are currently arriving. Add stress tests for repeated scene activation/deactivation and source visibility changes. This targets the general class of scene-switch glitch reports represented by atkAudio issue #45 without copying its architecture.

## Phase P4 — Creator feature parity where it matters (`v0.5.x`)

### P4.1 Sidechain

Add sidechain only after P2/P3 are stable.

User flow:

1. choose another OBS audio source as sidechain;
2. Safe Host synchronizes the auxiliary bus without unbounded waiting;
3. expose sidechain only when the VST advertises a compatible auxiliary input bus;
4. if sidechain disappears, continue main processing or fail safely according to plug-in capability.

This closes one of the most important practical gaps versus atkAudio for compressors, gates, duckers, and dynamic EQ.

### P4.2 Catalog UX

Turn the installed plug-in selector into a creator-friendly catalog:

- search by name/vendor;
- favorites;
- recently used;
- rescan only changed bundles where possible;
- visible compatibility/health badge;
- hide/quarantine known-bad local entries without deleting them;
- custom bundle path remains available.

### P4.3 Latency correctness

Support dynamic `kLatencyChanged` handling in the helper/control plane and expose reported latency clearly.

Do not invent a DAW-wide PDC system until OBS integration requirements are proven. First ensure this filter reports/handles its own latency consistently and does not deadlock/rebuild routing from the realtime thread.

## Phase P5 — Safe Rack (`v0.6+`)

Only after single-plug-in hosting is boringly reliable.

MVP:

- serial chain only;
- add/remove/reorder nodes;
- per-node bypass;
- per-node native/generic editor;
- save/load rack preset;
- rack state lives inside the OBS filter settings;
- one bad node cannot take OBS down.

Differentiator target:

> In Safe mode, one crashing VST node is bypassed/restarted without killing OBS and, where architecture permits, without discarding the healthy nodes in the rack.

Do not begin with arbitrary audio/MIDI graph routing. atkAudio already serves that advanced market. Our lead should be safety and simplicity.

## Later / optional

After Windows reaches stable production quality:

- macOS VST3/AU adapter;
- Linux VST3/LV2 adapter;
- MIDI parameter mapping/control;
- Windows Arm64 if OBS/runtime ecosystem makes it practical;
- signed installer and stronger release reputation;
- opt-in compatibility report export/import.

Cross-platform should reuse proven domain interfaces but should not trigger speculative abstraction today. Generalize only when the second platform adapter actually exists.

## Explicitly not near-term

Do not spend the next phases cloning these atkAudio capabilities:

- direct ASIO/CoreAudio device matrix;
- arbitrary routing graph;
- sampler/instrument/MIDI workstation behavior;
- ARA hosting;
- full virtual mixer/submix replacement;
- complex device loopback engine.

They are valuable, but they dilute the product wedge and dramatically increase state/routing/threading surface area.

## Quality gates

A phase is not complete because the UI exists. It is complete when the external behavior is verified.

### Realtime gate

- no filesystem I/O in OBS `filter_audio`;
- no VST lifecycle/editor/state work in OBS `filter_audio`;
- no process creation in OBS `filter_audio`;
- no unbounded wait;
- no dependency on the third-party VST returning in order for OBS to continue.

### Recovery gate

Automated failure injection must cover:

- helper killed while processing;
- helper killed while editor open;
- plug-in fails during startup;
- plug-in hangs during processing/deadline miss;
- stale generation completion arrives after bridge retirement;
- repeated crash enters quarantine;
- successful restart restores state.

### State gate

- save in OBS -> restart OBS -> same parameter/state result;
- change native GUI -> state snapshot -> helper crash -> restart -> same state;
- state restore while normal audio is inactive;
- corrupted state cannot crash OBS/helper supervisor.

### Compatibility gate

Before calling the Windows host stable, maintain a representative test matrix including:

- simple free VST3;
- plug-in with vendor native GUI;
- resizeable/high-DPI GUI;
- plug-in with many parameters;
- plug-in with latency;
- plug-in that requests `restartComponent()`;
- plug-in with sidechain bus when that phase lands;
- deliberately failing/hanging test plug-in.

Use the Steinberg validator/test-host utilities where they help distinguish a plug-in defect from a host defect.

## Success metrics

For stable Windows release, aim for measurable product behavior:

- a normal user can install, scan, select, open, edit, save, close OBS, reopen, and continue with the same VST state without reading documentation;
- injected helper/VST failures do not terminate OBS through the supported host path;
- helper failure never causes an unbounded wait in the OBS audio callback;
- recovery restores the last known good state or clearly reports quarantine;
- diagnostic output is sufficient to reproduce compatibility failures;
- the common one-plug-in workflow remains simpler than a graph-based host.

## Recommended execution order

1. **P2.1** parameter/control bridge + generic editor.
2. **P2.2** full state persistence + last-known-good snapshot.
3. **P2.3** native editor in helper.
4. **P2.4** no-audio parameter flush/lifecycle correctness.
5. **P3.1** explicit recovery state machine + state restore + wet crossfade.
6. **P3.2/P3.3** quarantine + health/diagnostic UX.
7. **P3.4** scene/source lifecycle stress hardening.
8. **P4.1** sidechain.
9. **P4.2/P4.3** catalog UX + latency correctness.
10. **P5** Safe Rack serial chain.
11. Cross-platform/MIDI only after the single-host safety model is proven stable.

This order is deliberately tracer-bullet oriented: each item should ship as an end-to-end behavior with tests rather than as a horizontal IPC/UI/host rewrite that remains unusable until the end.
