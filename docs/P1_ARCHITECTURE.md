# P1 Architecture — Proper VST Host

P1 converts the P0 spike into a VST3 host with explicit controller, state, editor, latency, and threading responsibilities while keeping the crash-isolation boundary.

## Invariants

1. OBS never loads the third-party VST3 binary.
2. OBS `filter_audio` never performs VST3 lifecycle/UI/state work.
3. The helper main thread owns VST3 initialization, controller callbacks, state, and native editor operations.
4. The helper DSP thread owns `IAudioProcessor::process()`.
5. Any control operation that requires DSP quiescence temporarily makes OBS fail open to dry audio.
6. No unbounded wait is introduced into the OBS audio callback.

## Shared-memory protocol v2

P1 upgrades the protocol from v1 to v2.

The region contains:

- versioned header and host status;
- 4 fixed audio slots;
- plug-in metadata and reported latency;
- up to 256 parameter descriptors;
- a separate control block;
- a 1 MiB bounded state/control payload.

The audio and control event pairs are independent so a state/editor operation cannot consume or impersonate an audio completion signal.

## Audio thread

The helper DSP thread requests Windows MMCSS `Pro Audio` / high priority.

For every ready audio slot it:

1. atomically claims the slot;
2. maps the shared-memory planar float buffers directly into `HostProcessData`;
3. drains pending parameter edits into VST3 `IParameterChanges`;
4. calls `IAudioProcessor::process()`;
5. captures DSP-originated parameter changes for main-thread delivery;
6. publishes completion.

No plug-in editor, file operation, state serialization, scanner operation, or OBS API call occurs here.

## Parameter bridge

P1 uses fixed pending queues between main/UI and DSP threads.

Main/UI → DSP sources:

- OBS generic editor;
- native VST3 editor through `IComponentHandler::performEdit()`.

DSP → main/UI:

- output `IParameterChanges` from the processor.

The Steinberg `ParameterChanges` containers are pre-sized and pre-warmed during initialization to reduce the likelihood of first-use allocations during realtime processing.

## `restartComponent()`

`IComponentHandler::restartComponent()` only records flags atomically. It does not perform lifecycle work inline, because real-world plug-ins do not always call it from the expected thread.

The helper main thread handles:

- `kParamValuesChanged`;
- `kParamTitlesChanged`;
- `kLatencyChanged`.

Dynamic routing/reload flags are retained as deferred diagnostics because rebuilding OBS routing belongs to later phases.

## State

P1 stores two VST3 state streams:

1. component state;
2. controller state, when supplied.

They are wrapped in `StateBlobHeader` with magic/version/lengths before crossing IPC.

Restore order:

1. quiesce DSP;
2. `setProcessing(false)`;
3. deactivate component;
4. component `setState`;
5. controller `setComponentState`;
6. controller `setState` when present;
7. reactivate component;
8. refresh parameter/latency metadata;
9. resume processing.

If a state operation fails, the audio callback remains bounded and returns dry audio rather than waiting indefinitely.

## Native editor

P1 creates the VST3 `IPlugView` in the helper and attaches it to a helper-owned top-level Win32 `HWND`.

This avoids cross-process HWND embedding and keeps the editor, GPU context, licensing UI, and DSP module in the same process.

The helper implements `IPlugFrame::resizeView()` and pumps Windows messages on the VST3 main thread.

## Generic editor

The OBS filter builds normalized 0..1 controls from parameter metadata:

- hidden parameters are omitted;
- read-only parameters are disabled;
- discrete parameters use `1 / stepCount` increments;
- continuous parameters use a small normalized increment.

Native editor state remains authoritative through normal VST3 component/controller state persistence.

## Failure behavior

| Failure | P1 behavior |
| --- | --- |
| helper not running | dry audio |
| no free audio slot | dry audio + deadline miss |
| VST `process()` error | dry audio |
| processing deadline miss | dry audio |
| state control timeout | report error; no audio-thread deadlock |
| native editor unsupported | generic editor remains available |
| dynamic I/O restart request | defer flag for later routing work |
| helper crash | OBS stays alive; filter becomes dry |

## P1 non-goals

P1 does not add scanner/cache, sidechain, arbitrary multichannel adaptation, float64 fallback, automatic process restart, or DAW-style global PDC. Those remain separate phases so P1 can be evaluated specifically for host correctness and controller/editor interoperability.
