# VST3 Rack Research — What to Learn, What Not to Copy

**Status:** Research input for Rack v2 execution  
**Date:** 2026-08-27  
**Implementation authority:** ADR-0002 + ADR-0003 + Execution Spec, not this document alone

---

## 1. Research question

The question is not:

> “How do we build a full plugin host like a DAW?”

The question is:

> “How do we add a professional graphical multi-VST3 Rack to OBS while preserving the product's strongest advantage: simple workflow, crash isolation, bounded realtime behavior and recoverable user state?”

This research deliberately looks at mature hosts for **patterns**, not source-code copying.

The most important synthesis is now:

> **Use atkAudio/Element-style dedicated host-window workflow, but keep that complete Rack editor/runtime outside `obs64.exe`.**

---

## 2. atkAudio PluginHost2 — the closest OBS product reference

Repository reviewed: `https://github.com/atkAudio/PluginForObsRelease`

Relevant source areas reviewed:

- `src/plugin_host2.cpp`
- `src/core/atkaudio/PluginHost2/PluginHost2.cpp`
- `src/core/atkaudio/PluginHost2/PluginHost2.h`
- `src/core/atkaudio/PluginHost2/Editor/MainHostWindow.*`
- `src/core/atkaudio/PluginHost2/Editor/GraphEditorPanel.*`
- `src/core/atkaudio/PluginHost2/PluginGraph.*`
- `src/core/atkaudio/atkAudioModule.h`
- `src/core/atkaudio/MessagePump.cpp`
- `src/core/atkaudio/SandboxedPluginScanner.*`

### 2.1 What atkAudio proves about OBS workflow

atkAudio PluginHost2 uses an excellent product split for complex hosting:

```text
OBS Properties
-> Open Filter Graph
-> dedicated host window
-> graphical plugin workflow
```

Its OBS Properties surface is deliberately tiny while `MainHostWindow`, a dedicated JUCE `DocumentWindow`, owns graph editing, plugin menus/listing, file/state workflow and plug-in windows.

This directly validates the idea that a professional Rack does **not** need to fit inside OBS Properties.

### 2.2 What atkAudio proves about graphical implementation velocity

Its graph editor uses normal host-GUI primitives:

- dedicated top-level window;
- plugin-list browser;
- node/card components;
- connector/pin components;
- drag/drop;
- popup menus;
- vendor plug-in windows;
- persistent host-window settings.

The lesson is product architecture: once UI is a dedicated application-like window, graphical Rack workflow is straightforward compared with forcing complex controls into `obs_properties`.

### 2.3 Critical safety difference

Do **not** copy atkAudio's process topology.

In PluginHost2, the OBS filter owns a `PluginHost2` object directly and `filter_audio` invokes `pluginHost2->process(...)`. JUCE/host UI lifecycle is integrated with OBS/Qt's process/message loop.

That is a valid design choice for atkAudio, but it is not our product differentiator.

OBS Safe VST3 must preserve:

```text
obs64.exe
   |
   | bounded IPC
   v
isolated Rack helper
   |-- graphical Rack editor
   |-- VST3 controller/editor
   `-- Rack DSP
```

Thus we copy **thin OBS entry point + dedicated host window**, but keep the window/runtime in the helper process.

### 2.4 Scanner lesson

atkAudio has an isolated scanner path, but the reviewed PluginHost2 plugin-list code can fall back to in-process scanning when the sandboxed scanner is unavailable.

Our rule remains stronger:

- scanner/probe stays isolated;
- no unsafe in-process vendor scan fallback in OBS or Rack editor process path.

### 2.5 Licensing/source rule

atkAudio source is reference material only. Do not copy its graph/editor implementation.

Implement the product pattern independently against our own runtime/command/snapshot model.

---

## 3. Kushview Element

Repository: `https://github.com/kushview/element`

Element is a mature C++ plugin host with a rich engine/UI separation.

### 3.1 Useful architecture patterns

Element separates several concerns that should also remain separate in our Rack:

- **Processor/runtime abstraction** — one processing object has lifecycle, ports, latency, bypass and render behavior.
- **Graph topology** — nodes and connections are separate from processor implementation.
- **Render-plan construction** — topology becomes a prepared sequence instead of being rediscovered inside every block.
- **Shared-buffer planning** — rendering reuses buffers according to a precomputed plan.
- **Latency propagation** — latency is explicit engine metadata.
- **State model vs runtime** — persistent state is separate from live processor ownership.
- **message-thread vs audio-thread discipline** — configuration/UI work is off the audio thread.
- **window manager** — plug-in editor windows are managed separately from graph/runtime objects.
- **testable engine core** — engine behavior can be exercised without real hardware/UI.

Relevant source areas reviewed include graph/runtime and window-management code such as `src/ui/windowmanager.cpp`.

### 3.2 What we should take from Element

1. A protocol-neutral **HostedPlugin** runtime seam.
2. Stable IDs independent of visual order.
3. Topology and processing plan as separate concepts.
4. Build/validate a new plan off realtime path.
5. Latency as explicit runtime metadata.
6. State representation separate from live processor ownership.
7. Dedicated window-management responsibility.
8. Deterministic engine tests independent of OBS UI.

### 3.3 What we should NOT take into Rack v2

Do not copy Element's full graph product shape.

For v2 we do **not** need:

- arbitrary cables;
- user-created routing graph;
- MIDI graph;
- nested graph processing;
- arbitrary audio/CV ports;
- parallel branches;
- embedded vendor editors.

The Rack Editor may be graphical while the engine topology remains serial.

### 3.4 Key translation

Element concept:

```text
Graph -> nodes -> connections -> build rendering sequence -> render
```

Our v2 translation:

```text
Rack UI -> ordered stable slot IDs -> build immutable chain generation -> serial render
```

The graphical editor is a better view/controller over this simple topology, not permission to turn R1 into a graph engine.

---

## 4. Cantabile

Cantabile's strongest lesson is how a live host presents complexity.

Useful concepts:

- Rack is an understandable **black box**;
- normal users work with plug-ins and signal flow, not implementation details;
- reusable rack states/configurations reduce repeated setup work;
- advanced routing can exist without dominating the default workflow;
- predictable recall is a core live-performance feature.

### Translation to our v2

- the dedicated Rack Editor shows one clear top-to-bottom serial lane;
- users never need cables for the common case;
- Rack Preset is a first-class reusable artifact;
- later routing/sidechain can add a separate advanced view without replacing the basic Rack lane.

Do not copy linked/embedded rack complexity or large live-state semantics into v2.

---

## 5. Gig Performer

Useful product patterns:

- **configuration view and performance view are different jobs**;
- reusable rackspaces are more valuable than reconstructing chains;
- live software benefits from fast, predictable recall;
- sophisticated topology can coexist with a simple performance surface.

### Translation

For v2:

- Rack Editor is the configuration surface;
- vendor editors are detailed plug-in-control surfaces;
- Presets are reusable Rack configuration;
- OBS Properties is merely launcher/status;
- a future performance/macros panel is post-v2.

---

## 6. Carla

Carla demonstrates both Rack and Patchbay modes and supports plugin bridging.

Useful lessons:

- Rack and Patchbay are legitimately different user modes;
- bridging can isolate plugins/formats;
- patchbay becomes valuable only when routing is the primary user problem.

Warning for this project:

Every extra process boundary/routing mode adds:

- failure states;
- latency accounting;
- synchronization;
- persistence complexity;
- diagnostics;
- UI states.

Our strong outer isolation boundary is already **OBS vs Rack helper**.

For v2, one helper process plus a serial chain remains the best tradeoff. Revisit per-slot processes only if measured crash data proves whole-Rack restart/quarantine insufficient.

---

## 7. GUI toolkit research

The dedicated editor decision does **not** imply that the OBS module should link to a large GUI framework.

### 7.1 JUCE

JUCE is an excellent technical reference because atkAudio and Element demonstrate its suitability for plugin-host windows.

However, current JUCE releases are dual licensed under AGPLv3 or a commercial JUCE licence. This repository currently uses GPL-3.0.

Therefore:

- do not casually vendor JUCE into the public product;
- do not change project licensing merely to get a UI toolkit;
- using JUCE requires a separate licensing/dependency decision.

### 7.2 Dear ImGui

Dear ImGui is the preferred v2 Windows UI candidate because:

- MIT licensed;
- small and self-contained;
- official Win32 platform backend;
- DirectX 11 renderer backend;
- well suited to content-creation/tool UI;
- fast drag/drop/search/card iteration;
- no dependency on OBS Qt.

For v2, use a pinned upstream version/commit and only the modules/backends required by the helper editor.

### 7.3 Why not private OBS Qt

Prior project evidence already showed that custom Qt integration in the OBS module can compromise the minimum OBS loader/compatibility floor.

The new architecture avoids the problem rather than trying another private-widget trick.

### 7.4 Why not native Win32-only immediately

Native Win32/Direct2D is the fallback when dependency evidence requires it, but implementing a polished rack/browser/drag workflow manually would add more project code and slow product iteration.

Use the UI-0 gate to prove Dear ImGui early; fall back only with evidence.

---

## 8. Existing project readiness for a helper-owned editor

The current Single helper already owns native VST editor HWNDs and pumps Windows messages on its control/editor path.

This means the project already has proven concepts for:

- helper-owned top-level windows;
- VST3 `IPlugView` attachment to helper HWND;
- focus/foreground handling across OBS/helper process boundary;
- message pumping separate from DSP worker;
- closing/hiding vendor UI without unloading audio runtime.

The Rack Editor should build on those ownership lessons while remaining a separate v2 helper implementation.

Do not move the current Single native editor into OBS or rewrite it as part of UI-0/R0.

---

## 9. Matt Pocock engineering workflow applied to this repository

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

### Research

Before coding a behavior:

- establish current repository fixed point;
- read authoritative product/architecture docs;
- inspect actual code at the relevant seam;
- research external API/dependency behavior only when uncertain;
- resolve architecture uncertainty before implementation.

### Spec

Define:

- observable behavior;
- scope/non-goals;
- ownership/threading;
- failure behavior;
- persistence semantics;
- test seam;
- exact acceptance gate.

### Tracer bullets

Each ticket proves one vertical behavior.

Bad:

> “Build Rack GUI.”

Good:

> “The isolated helper-only Rack editor shell renders three snapshot-driven dummy slots, drag reorder emits a command without mutating the model directly, repeated open/close teardown is clean, and the OBS module PE dependencies remain unchanged.”

### Fresh context

One unblocked ticket per thread. Stop after acceptance/evidence is recorded.

### Test-driven implementation

Where a deterministic seam exists:

```text
failing test
-> minimum implementation
-> focused green
-> surrounding regressions
-> refactor while green
```

Commercial VST3s remain qualification evidence, not the first correctness oracle.

### Fixed-point review

Review two dimensions separately:

**Standards** — safety, realtime, ownership, protocol, persistence, compatibility.

**Spec** — exact promised behavior, acceptance and non-goals.

A changed source head invalidates final qualification tied to an older head.

---

## 10. Competitive synthesis

| Topic | atkAudio PluginHost2 | Element | Cantabile/Gig Performer | OBS Safe VST3 v2 decision |
|---|---|---|---|---|
| OBS entry surface | thin Open Filter Graph | standalone host | standalone live host | **thin Open Rack/status** |
| Primary UI | dedicated graphical host window | dedicated host/graph UI | rack/live views | **helper-owned graphical serial Rack** |
| Runtime topology | graph + device/MIDI breadth | graph | routes/rackspaces | **serial chain v2** |
| OBS process isolation | runtime is in OBS plug-in context | N/A | N/A | **Rack/VST3/editor outside obs64.exe** |
| Runtime abstraction | JUCE graph/processors | strong processor/node model | product-oriented | **protocol-neutral HostedPlugin** |
| Render planning | graph | graph sequence | internal | **immutable serial generation** |
| Buffer strategy | graph buffers | shared/reused | internal | **preallocated ping-pong** |
| Reuse | graph state/files | sessions | rack/song/rackspace state | **Session Snapshot + Rack Preset** |
| Plug-in browser | dedicated host UI | dedicated host UI | dedicated host UI | **Rack Editor browser/search** |
| Vendor UI | own windows | WindowManager | host windows | **floating helper-owned native HWND** |
| Advanced routing | yes | yes | yes | **post-v2** |
| MIDI/instruments | yes | yes | yes | **post-v2** |
| Isolation priority | not our process model | not OBS-specific | live stability | **OBS/helper isolation is product law** |

---

## 11. Primary strategic conclusion

The fastest path to a professional product is now:

> **Build the smallest serial Rack runtime with the strongest isolation contract, then expose it through a purpose-built graphical helper-owned Rack Editor instead of stretching OBS Properties beyond its role.**

This keeps v2 simple enough to prove while giving the product a UI foundation that can later grow into sidechain/routing/MIDI/instrument workflows without another architectural rewrite.

---

## 12. Sources reviewed

Primary public references:

- atkAudio PluginForObsRelease: `https://github.com/atkAudio/PluginForObsRelease`
- Kushview Element: `https://github.com/kushview/element`
- Cantabile guides: `https://www.cantabilesoftware.com/guides/`
- Gig Performer documentation: `https://gigperformer.com/docs/`
- Carla project: `https://github.com/falkTX/Carla`
- Dear ImGui: `https://github.com/ocornut/imgui`
- JUCE licensing/reference: `https://juce.com/`

External projects are references for architecture/product patterns only. Their source is not copied into this repository.
