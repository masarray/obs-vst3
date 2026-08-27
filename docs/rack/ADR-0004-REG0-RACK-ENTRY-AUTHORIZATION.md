# ADR-0004 — REG-0 Rack Entry Authorization

- Status: Accepted
- Date: 2026-08-27
- Parent: #56
- Gate ticket: #57 REG-0
- Audited `main` fixed point: `97188252532b5e81e348442a2e97f3794fb3eeaf`

## Decision

Safe VST3 Rack v2 is authorized to proceed from the audited Single Host baseline **without claiming that historical S6 / Single Host v1.0 breadth is complete**.

This is a narrow phase-order exception to the historical `S6 Single Host v1.0 LOCK -> R0` sequencing rule. It authorizes only the accepted Rack dependency spine:

`REG-0 -> UI-0 -> R0-1 -> R0-2 -> remaining Rack tracer tickets`

All existing safety invariants remain locked. In particular, this ADR does not authorize moving vendor code or GUI dependencies into `obs64.exe`, changing the Single Host protocol, weakening fail-dry/recovery/state behavior, or skipping exact-head qualification.

UI-0 is the next fresh ticket. It is helper-only, loads no VST3, and must not perform R0 extraction. R0 starts only after UI-0 reaches its own GO gate.

## Why the exception is safe

### Exact baseline provenance

The REG-0 audit pinned current `main` to:

`97188252532b5e81e348442a2e97f3794fb3eeaf`

Current exact-head CI #351 / run `33061484347` completed successfully on that SHA.

The shipping v0.5.0 source fixed point is:

`b16183b1a0e55df15657c539dfc0454bd93f32f4`

That exact source head passed:

- CI #334 / run `33043181249`;
- Compatibility Test Build #156 / run `33043181248`;
- the compatibility workflow's official OBS 29.1.3 loader probe;
- real-machine iZotope acceptance recorded by PR #51: Ozone 11 and RX reached Ready and helper-owned native vendor editors opened normally.

GitHub compare from `b16183b1a0e55df15657c539dfc0454bd93f32f4` to the REG-0 fixed point is 11 commits ahead / 0 behind and contains documentation/static-site changes only. No `src/`, `CMakeLists.txt`, workflow, protocol, installer-runtime, DSP, scanner, or shipping dependency change separates the qualified v0.5.0 source from the audited runtime.

Evidence links:

- current CI: https://github.com/masarray/obs-vst3/actions/runs/33061484347
- qualified v0.5.0 CI: https://github.com/masarray/obs-vst3/actions/runs/33043181249
- qualified Compatibility Test Build: https://github.com/masarray/obs-vst3/actions/runs/33043181248
- stable integration evidence: https://github.com/masarray/obs-vst3/pull/51
- source-lineage compare: https://github.com/masarray/obs-vst3/compare/b16183b1a0e55df15657c539dfc0454bd93f32f4...97188252532b5e81e348442a2e97f3794fb3eeaf

### Extraction seam is narrow and behavior-preserving

The current Single Host coupling that blocks protocol-neutral reuse is `Vst3Engine::process(AudioSlot&)`.

`AudioSlot` contributes only the Single transport view: frame count, channel count, input pointers, and output pointers. VST3 `ProcessData`, process context/sample position, mono/stereo adaptation, parameter queues, latency, component/controller ownership, state capture/restore, lifecycle and restart behavior already live behind `Vst3Engine`.

Therefore R0-1 may use the already accepted expand -> migrate -> contract strategy:

1. add a protocol-neutral `ProcessBlockView`;
2. keep `process(AudioSlot&)` as the Single adapter;
3. prove Single behavior unchanged;
4. only then let R0-2 extract/rename the proven lifecycle unit to `HostedPlugin` as far as Rack requires.

The Single shared-memory protocol remains byte/layout compatible throughout R0.

### Lifecycle and ownership are already separated enough

The audited helper has a dedicated DSP worker. `Vst3Engine::process()` and processor-side parameter transfer run on that DSP path. Controller/editor/restart/state work runs on the helper control path and crosses lifecycle frontiers only after bounded DSP pause/reconciliation.

The engine owns strict component/controller initialization, connection, initial component-state synchronization, setup/activation/latency/processing ordering, state capture/restore, dynamic I/O restart, latency restart and teardown. Existing source-contract and portable transaction tests lock those orders.

Native vendor editors remain helper-owned. R0 does not move or rewrite `NativeEditorWindow`, `IPlugView`, the Windows message pump, or OBS Properties ownership.

### State and recovery are sufficient for extraction

The current state model keeps complete component and controller-private blobs in `PluginStateSnapshot`, bounds and checksums the serialized envelope, and persists snapshots by atomic replacement.

State commands pause DSP and reconcile pending controller/processor edits before capture/restore. Restore order is component state -> controller component-state sync -> controller-private state -> reactivate/restart processing -> refresh latency/parameter mirror.

OBS recovery creates a fresh helper, restores the last-known-good exact snapshot before publishing it as active, and falls back to parameter settings only when exact state is missing/invalid/rejected. Wet output is copied back to OBS only after a successful helper result; timeout/error leaves the original dry block untouched.

## REG-0 test inventory

At the audited fixed point the exact-head Windows helper lane executed 23/23 CTest tests successfully. The relevant regression inventory includes:

- `lifecycle-restart-policy`
- `state-restore-policy`
- `latency-restart-transaction`
- `parameter-refresh-transaction`
- `io-restart-transaction`
- `reload-component-transaction`
- `process-context-policy`
- `process-context-source-contract`
- `strict-lifecycle-source-contract`
- `obs-properties-ownership`
- `parameter-control`
- `state-snapshot`
- `recovery-policy`
- `hang-watchdog`
- `control-stall-dsp`
- `recovery-checkpoint`
- `protocol-layout`
- scanner/startup diagnostics and source-selection coverage.

The CI matrix additionally builds the helper/scanner, real OBS module, protocol test and installer. The Compatibility Test Build rebuilds against current OBS SDK and probes the module against official OBS 29.1.3, then validates packaging/runtime dependencies.

## Mandatory Single regression gates for UI-0 / R0

These are requirements, not suggestions.

1. **Exact source SHA only.** Never combine test evidence from a different source head.
2. **CI must be green** on any source/CMake/workflow change: protocol Linux, Windows helper/tests, OBS module against current OBS SDK, installer lane.
3. **Compatibility Test Build must be green** for any source/CMake/host/plugin/packaging change. Preserve the official OBS 29.1.3 loader probe and current-OBS build.
4. **All existing CTest tests remain green**, with the lifecycle/state/recovery/protocol/OBS-ownership tests listed above treated as mandatory extraction gates.
5. **Single protocol remains unchanged in R0.** No `kProtocolVersion` bump and no `AudioSlot`/Single control-layout redesign as part of extraction.
6. **No lifecycle/controller/editor work enters OBS `filter_audio`.** Dry fail-open and isolated helper ownership remain invariant.
7. **Exact state remains complete.** Component + controller-private state, restore order, atomic LKG persistence and recovery-before-publish semantics must remain unchanged.
8. **Native vendor editor boundary remains unchanged in UI-0/R0.** Do not share/move the existing editor implementation merely to build the Rack editor shell.
9. **OBS Properties ownership guard remains green.** R0 must not couple shared runtime extraction to OBS UI lifetime.
10. **R0-1 must add deterministic process characterization before production mutation.** The present suite proves IPC audio survival, watchdog/fail-dry behavior, channel-adaptation utilities and process-context ordering, but it does not directly characterize a real `Vst3Engine::process` call across the proposed `AudioSlot -> ProcessBlockView` adapter. Before changing that method, add a deterministic VST3 processing fixture/harness that locks the current success/failure, mono/stereo and block semantics. Only after the unchanged Single adapter passes that characterization may production process code migrate to `ProcessBlockView`.
11. **R0 extraction candidate requires real-machine smoke before merge.** Reconfirm representative commercial effects on the exact candidate, including normal audio, helper-owned vendor editor open/hide, exact-state continuity through helper recreation, and dry fail-open behavior.

UI-0 does not touch VST3 processing and therefore does not need to solve item 10; that requirement is the first gate inside R0-1.

## Deferred Single breadth is not a Rack safety prerequisite

The following historical/future Single breadth is not required to safely extract the mono/stereo Float32 serial-effect seam:

- sidechain;
- MIDI or VST3 instruments;
- arbitrary graph/parallel routing;
- arbitrary multichannel or Float64;
- embedded vendor editors;
- cross-platform Rack work;
- other post-v2 features.

Those items remain incomplete/deferred. This ADR does **not** mark S6 complete and does not rename the current Single release as the historical v1.0 lock.

## Phase-order clarification

Where the older execution contract states that full S6 / Single Host v1.0 lock must precede R0, this accepted ADR replaces only that ordering constraint for the Safe VST3 Rack v2 program.

The truthful status is:

- current Single v0.5.0 safety/runtime baseline: sufficiently proven for behavior-preserving shared seam extraction;
- historical S6 breadth: not declared complete;
- Rack production work: still forbidden until REG-0 is accepted and UI-0 independently passes;
- R0: authorized only under the mandatory Single regression gates above.

No other architecture or safety contract is weakened by this exception.

## REG-0 outcome

**GO**

Next unblocked ticket: **UI-0 — isolated graphical helper dependency/window proof** in a fresh engineering thread.
