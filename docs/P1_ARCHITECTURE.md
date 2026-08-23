# P1 Architecture — Proper VST Host

P1 converts the P0 spike into a VST3 host with explicit controller, state, editor, latency, and threading responsibilities while keeping the crash-isolation boundary.

## Invariants

1. OBS never loads the third-party VST3 binary.
2. OBS `filter_audio` never performs VST3 lifecycle/UI/state work.
3. The helper owns VST3 initialization, controller callbacks, state, and native editor operations.
4. The isolated helper owns `IAudioProcessor::process()`; OBS never invokes VST3 processing directly.
5. Any control operation that requires DSP quiescence temporarily makes OBS fail open to dry audio.
6. No unbounded wait is introduced into the OBS audio callback.

## Shared-memory protocol v2

P1 upgrades the protocol from v1 to v2.

The region contains:

- versioned header and host status;
- 4 fixed audio slots;
- plug-in metadata and reported latency;
- up to 256 parameter descriptors;
- bounded editor/control command state.

Audio completion signaling remains independent of editor/control state so opening or resizing a vendor UI cannot impersonate an audio completion.

## Audio thread

The isolated helper requests Windows MMCSS `Pro Audio` / high priority.

For every ready audio slot it:

1. atomically claims the slot;
2. maps the shared-memory planar float buffers directly into `HostProcessData`;
3. drains pending parameter edits into VST3 `IParameterChanges`;
4. calls `IAudioProcessor::process()`;
5. captures DSP-originated parameter changes for host-visible delivery;
6. publishes completion.

No plug-in editor, file operation, state serialization, scanner operation, or OBS API call occurs in `obs64.exe`'s realtime callback.

### P2.1 implementation note

`v0.3.0-preview.1` restores the helper-owned native editor by pumping Win32 messages in the isolated helper while preserving the OBS-side bounded/fail-open seam. This intentionally favors restoring the correct user workflow without moving third-party code back into OBS. A later phase will split helper UI/control ownership and DSP processing onto dedicated helper threads once native-editor compatibility has been validated in public testing.

## Parameter bridge

P2.1 uses bounded/coalesced parameter state between OBS and the helper.

Host/generic UI → processor:

- OBS fallback parameter controls publish normalized values through shared control state;
- the helper drains those values into VST3 `IParameterChanges`;
- edits are flushed with a zero-sample `process()` call when normal audio blocks are inactive.

Native VST3 editor → processor:

- the helper installs an `IComponentHandler` on the controller;
- `performEdit()` is routed into the same processor parameter queue;
- the native editor remains in the helper process.

Processor → host/controller:

- output `IParameterChanges` are captured;
- current normalized values are reflected into bounded shared metadata.

The Steinberg `ParameterChanges` containers are pre-sized during initialization to reduce first-use allocation on the processing path.

## `restartComponent()`

`IComponentHandler::restartComponent()` records flags rather than rebuilding OBS routing inline.

P2.1 handles:

- `kParamValuesChanged` by refreshing controller parameter values;
- dynamic reload/I/O flags as deferred diagnostics so OBS remains fail-open.

Full dynamic routing/reload and latency reconfiguration remain later phases.

## State

The full-state phase will store two VST3 state streams:

1. component state;
2. controller state, when supplied.

They will be wrapped in a versioned bounded state blob before crossing IPC.

Planned restore order:

1. quiesce DSP;
2. `setProcessing(false)`;
3. deactivate component;
4. component `setState`;
5. controller `setComponentState`;
6. controller `setState` when present;
7. reactivate component;
8. refresh parameter/latency metadata;
9. resume processing.

If a state operation fails, the audio callback must remain bounded and return dry audio rather than waiting indefinitely.

## Native editor

P2.1 creates the VST3 `IPlugView` in the helper and attaches it to a helper-owned top-level Win32 `HWND`.

This avoids cross-process HWND embedding and keeps the editor, GPU context, licensing UI, and DSP module outside `obs64.exe`.

The helper implements `IPlugFrame::resizeView()`, pumps Windows messages, and routes native editor parameter gestures through `IComponentHandler`.

The OBS filter exposes **Open Plug-in Interface**. If the native editor is unsupported or unusable, OBS-side fallback parameter controls remain available.

## Generic editor / fallback controls

The OBS filter builds normalized 0..1 controls from parameter metadata:

- hidden parameters are omitted;
- read-only parameters are disabled;
- discrete parameters use `1 / stepCount` increments;
- continuous parameters use a small normalized increment.

These controls are a fallback and diagnostic surface, not a replacement for the vendor GUI.

## Failure behavior

| Failure | Behavior |
| --- | --- |
| helper not running | dry audio |
| no free audio slot | dry audio + deadline miss |
| VST `process()` error | dry audio |
| processing deadline miss | dry audio |
| native editor unsupported | fallback controls remain available |
| native editor closes | helper and DSP remain alive |
| helper crashes with editor open | OBS stays alive; filter becomes dry and recovery can restart helper |
| dynamic I/O/reload request | defer flag for later routing work |

## P2.1 non-goals

P2.1 does not yet add full component/controller state blobs, sidechain, arbitrary multichannel adaptation, float64 fallback, DAW-style global PDC, or a Safe Rack. Those remain separate phases so native-editor/control correctness and crash isolation can be evaluated directly.
