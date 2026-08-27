# ADR-0003 — Isolated Graphical Rack Editor

**Status:** Accepted for Rack v2 planning  
**Date:** 2026-08-27  
**Scope:** VST3 Rack v2 Windows UX/process architecture  
**Supersedes:** ADR-0002 §3.12 and the ADR-0002 rejection of a dedicated Rack editor  
**Does not supersede:** Rack serial DSP/runtime decisions in ADR-0002 or any Single Host safety invariant

---

## 1. Decision

`VST3 Rack` will use a **dedicated graphical Rack Editor window owned by the isolated Rack helper process**.

OBS Properties is deliberately reduced to a thin launcher/status surface.

```text
OBS64.EXE
┌────────────────────────────────────────────┐
│ VST3 Rack filter                           │
│                                            │
│ Properties                                 │
│   Rack: Broadcast Vocal                    │
│   Ready · 4 effects · 128 samples          │
│   [ Open Rack ]                            │
│                                            │
│ filter_audio()                             │
└──────────────┬─────────────────────────────┘
               │ bounded Rack IPC/shared memory
               ▼
obs-safe-vst3-rack-host.exe
┌────────────────────────────────────────────┐
│ Rack control/UI owner                      │
│   ├─ graphical RackEditorWindow            │
│   ├─ plugin browser/search                 │
│   ├─ preset browser/management             │
│   ├─ slot commands/status                  │
│   └─ native vendor editor window manager   │
│                                            │
│ immutable Rack generation                  │
│                 │                          │
│                 ▼                          │
│ Rack DSP worker                            │
│   Slot A -> Slot B -> Slot C -> ...        │
└────────────────────────────────────────────┘
```

Third-party VST3 code, vendor GUI code and Rack graphical UI dependencies remain outside `obs64.exe`.

---

## 2. Why this replaces the stock-Properties Rack editor plan

The earlier planning choice used public `obs_properties` for all Rack operations to protect OBS compatibility. That correctly rejected private Qt/widget injection, but it also imposed a UI ceiling that conflicts with the long-term product direction:

- graphical serial Rack;
- drag reorder;
- fast searchable plug-in browser;
- clear per-slot health/latency;
- preset browsing;
- later sidechain/routing/MIDI visualisation.

Research of atkAudio PluginHost2 and Kushview Element demonstrates a better product split: OBS can expose a tiny entry surface while a purpose-built host window owns complex audio workflow.

The important correction is **where that window lives**.

atkAudio's product pattern is useful, but its PluginHost2 runtime/UI is constructed inside the OBS plug-in process. OBS Safe VST3 must preserve its stronger isolation law. Therefore we copy the workflow separation, not the process topology.

The resulting principle is:

> **Thin OBS integration; rich helper-owned Rack editor.**

---

## 3. Toolkit decision

### 3.1 Default v2 toolkit

For Windows Rack v2, the default graphical toolkit is:

**Dear ImGui + Win32 platform backend + DirectX 11 renderer backend**

Reasons:

- permissive MIT license;
- small dependency surface;
- no dependency on OBS Qt internals or OBS's Qt version;
- native Win32/D3D11 backends are mature and directly applicable;
- fast implementation of rack cards, drag/drop, search, popups, menus, meters and status surfaces;
- no need to link GUI framework code into the OBS module;
- Windows-only v2 scope makes a Win32/D3D11 backend appropriate.

Pin a specific upstream version/commit. Do not track an unpinned moving branch in release builds.

### 3.2 JUCE is a reference, not the default dependency

atkAudio and Element show that JUCE is effective for plug-in-host UI. However current JUCE is dual licensed under AGPLv3 or a commercial JUCE licence. This repository is currently GPL-3.0.

Do not vendor or link JUCE into the public product merely because reference hosts use it. A future switch to JUCE requires an explicit dependency/licensing ADR and package review.

### 3.3 Qt is not a Rack-helper dependency by default

Do not link the Rack helper to OBS's Qt or depend on OBS private widget APIs.

The failed custom-Qt Properties experiment proved that an OBS-module GUI dependency can weaken the supported OBS compatibility floor. A helper-owned UI avoids that failure mode, but Qt still adds deployment/version weight with no current need.

### 3.4 Source-copy rule

Do not copy atkAudio or Element UI/graph source code.

Use them only as architectural/product references. Implement our own serial Rack editor against our own state/command model.

---

## 4. Process and thread ownership

### OBS process owns

- OBS filter instance;
- bounded Rack audio transport;
- helper health/watchdog/recovery facade;
- lightweight `Open Rack` command;
- read-only/snapshot status exposed in OBS Properties.

OBS does **not** own:

- Rack graphical widgets;
- Direct3D Rack rendering;
- plug-in search UI;
- vendor editor HWNDs;
- Rack topology mutation logic;
- VST3 lifecycle/state calls.

### Rack helper control/UI thread owns

- Rack editor window lifetime;
- Win32 message pump;
- ImGui context and D3D11 renderer resources;
- user input translation into `RackCommand` messages;
- immutable UI snapshot consumption;
- plug-in browser presentation;
- preset management presentation;
- vendor editor open/close orchestration.

### Rack DSP worker owns

- only bounded audio block processing against one coherent immutable chain generation;
- heartbeat/progress publication;
- no GUI, D3D, filesystem, scanner, preset or topology work.

### Scanner/probe processes own

- loading/probing candidate vendor bundles for discovery;
- never fall back to loading vendor scanner work in OBS.

---

## 5. UI state architecture

The editor must not become a second mutable source of Rack truth.

Use one-directional command/snapshot flow:

```text
User input
   |
   v
RackCommand
   |
   v
RackControlPlane validates + commits transaction
   |
   v
new coherent Rack generation/state
   |
   v
RackUiSnapshot
   |
   v
Rack Editor renders authoritative state
```

### `RackCommand`

Conceptual commands include:

- AddSlot(plugin identity, insertion point)
- RemoveSlot(slot_id)
- ReplaceSlot(slot_id, plugin identity)
- MoveSlot(slot_id, target index)
- SetBypass(slot_id, bool)
- OpenVendorEditor(slot_id)
- RescanCatalog
- SavePreset(name)
- LoadPreset(preset_id)
- UpdatePreset(preset_id)
- RenamePreset(preset_id, name)
- DeletePreset(preset_id)

Commands never carry raw pointers to UI/runtime objects.

### `RackUiSnapshot`

A bounded immutable/read-only projection contains only display/control state needed by the editor, for example:

- rack ID/display name;
- current generation;
- health/recovery state;
- total latency;
- slot ID/order;
- plug-in display/vendor/category metadata;
- slot bypass/health/latency/pending action;
- available preset summaries;
- catalog generation/searchable metadata snapshot;
- actionable user diagnostics.

Do not expose mutable `HostedPlugin` objects to the editor.

### No optimistic topology mutation

The UI may display a short Pending state after a command, but it must not permanently assume a drag/add/replace succeeded before the control plane acknowledges a coherent new generation.

---

## 6. Rack Editor v2 product shape

The first stable editor is a **graphical serial Rack**, not a free-form patchbay.

```text
┌────────────────────────────────────────────────────────┐
│ OBS Safe VST3 Rack                    Broadcast Vocal  │
│ [Preset ▼] [Save As] [Update]          ● Ready 128 smp │
├────────────────────────────────────────────────────────┤
│ Search/Add Effect...                                     │
├────────────────────────────────────────────────────────┤
│ INPUT                                                     │
│   │                                                       │
│ ┌──────────────────────────────────────────────────────┐ │
│ │ RX Spectral De-noise        ● Ready       0 samples  │ │
│ │ [Bypass] [Open UI]                         [•••]      │ │
│ └──────────────────────────────────────────────────────┘ │
│   │                                                       │
│ ┌──────────────────────────────────────────────────────┐ │
│ │ FabFilter Pro-Q 3           ● Ready       0 samples  │ │
│ │ [Bypass] [Open UI]                         [•••]      │ │
│ └──────────────────────────────────────────────────────┘ │
│   │                                                       │
│ ┌──────────────────────────────────────────────────────┐ │
│ │ FabFilter Pro-C 2           ● Ready      64 samples  │ │
│ │ [Bypass] [Open UI]                         [•••]      │ │
│ └──────────────────────────────────────────────────────┘ │
│   │                                                       │
│ [+ Add Effect]                                            │
│   │                                                       │
│ OUTPUT TO OBS                                             │
└────────────────────────────────────────────────────────┘
```

Required v2 interactions:

- add via searchable browser;
- drag reorder plus keyboard/menu fallback;
- replace/remove;
- bypass;
- open vendor UI;
- clear health/status;
- preset Save/Load/Rename/Delete/explicit Update;
- Missing/Quarantined cards remain visible/pass-through;
- recovery status is understandable without protocol/process jargon.

A free-form cable canvas is explicitly post-v2.

---

## 7. Vendor editor policy

Vendor VST3 editors remain **floating native helper-owned windows** in v2.

Reuse the proven helper-side native HWND/IPlugView path rather than embedding vendor UI inside Rack cards.

Reasons:

- current commercial compatibility evidence already exists for floating native editors;
- embedding introduces resizing/focus/lifetime complexity for little v2 value;
- one slot card should remain lightweight even for very large vendor UIs.

The Rack editor window manages vendor-window commands, but vendor editor state is not part of transient Rack UI rendering.

Do not auto-open vendor windows on OBS/session restore or preset load.

---

## 8. OBS Properties v2 contract

OBS Properties becomes intentionally small:

```text
VST3 Rack

Broadcast Vocal
Ready · 4 effects · 128 samples latency

[ Open Rack ]

Recovery/diagnostic text only when action is needed
```

The exact available public Properties primitives may shape cosmetics, but these rules are fixed:

- no slot editing in OBS Properties is required for v2;
- no private Qt/widget injection;
- no background Properties-tree rebuild from scanner/recovery threads;
- `Open Rack` is a lightweight control command only;
- closing Properties never closes or resets the Rack;
- closing Rack Editor never disables Rack DSP.

---

## 9. Window lifecycle

- Rack helper starts headless with respect to the Rack Editor; restoring a scene must not surprise-open the editor.
- `Open Rack` creates or shows the helper-owned editor and brings it to foreground.
- Close button hides/destroys editor rendering resources according to the chosen implementation, but does not destroy Rack runtime/state.
- DSP remains functional while the editor is closed.
- Editor can be reopened repeatedly without changing Rack generation.
- Optional Rack-editor window position/size may be stored in UI preferences, separate from Rack audio Session Snapshot and Preset data.
- Vendor windows close/hide independently.
- Helper shutdown closes editor/vendor windows before destroying VST3 controller/runtime ownership.

---

## 10. UI-0 feasibility gate

Because this ADR changes the v2 UI foundation, prove the GUI dependency before investing in Rack runtime breadth.

**UI-0 is a timeboxed architecture/dependency gate, not Rack product implementation.**

It follows REG-0 GO and precedes R0 production extraction.

Required proof on Windows:

1. Pin an exact Dear ImGui source version/commit and document MIT licence files included.
2. Build a standalone/helper-only smoke target using Win32 + D3D11; do not link Dear ImGui into the OBS module.
3. Show one Rack-like window with:
   - three dummy cards;
   - drag reorder;
   - search input;
   - button command event;
   - status badge/text.
4. Open/close/reopen the window repeatedly (automated where possible).
5. Prove message-pump teardown is clean.
6. Prove the OBS module PE dependency list is unchanged by the UI dependency.
7. Prove minimum/current OBS loader probes for the existing Single product remain green.
8. Record binary/package size impact and any extra files required.
9. No third-party VST3 is loaded by UI-0.

UI-0 outcome:

- **GO IMGUI** — use the pinned Dear ImGui Win32/D3D11 stack for Rack editor; or
- **BLOCKED** — record the exact failure and select a bounded fallback ADR (native Win32/Direct2D first; JUCE only after a separate licensing/dependency decision).

Do not let UI-0 become a visual-polish project.

---

## 11. Failure behavior

The editor is non-essential to audio continuity.

- If rendering fails/device is lost, Rack DSP keeps running if the helper remains healthy.
- If the helper process crashes because of UI or vendor code, OBS remains alive and fails dry while normal helper recovery applies.
- UI thread must never hold a lock required by Rack DSP.
- D3D device recreation is UI/control work only.
- no UI failure may corrupt/promote mixed Rack state as last-known-good.

---

## 12. Performance constraints

The editor should be cheap enough to leave open during a stream, but it is not a realtime thread.

v2 targets:

- render only when visible;
- cap/idle frame rate when no animation/input needs high refresh;
- meters use bounded snapshots, never direct DSP locks;
- avoid per-frame filesystem/catalog scans;
- plug-in browser search filters an already published catalog snapshot;
- large diagnostic text/history is bounded;
- no GPU dependency is introduced into audio processing.

Measure UI CPU/GPU with 1/4/8 slots before RC.

---

## 13. Post-v2 extension path

This architecture intentionally supports later evolution without replacing the window system:

```text
v2 serial Rack
  -> richer meters/macros
  -> sidechain input assignment
  -> routed/parallel paths
  -> visual routing canvas
  -> MIDI/event transport
  -> VST3 instruments
  -> performance/snapshot controls
```

The v2 engine remains serial even though the editor architecture is capable of richer future views.

---

## 14. Non-goals for v2

- arbitrary patchbay/cable graph;
- parallel paths;
- sidechain;
- MIDI;
- VST3 instruments;
- embedded vendor editors in Rack cards;
- per-slot worker processes;
- direct audio-device I/O;
- custom OBS Qt integration;
- cross-platform Rack GUI;
- copying atkAudio/Element source.

---

## 15. Authority and read order

For Rack work:

1. `AGENTS.md`
2. `docs/CODEX_EXECUTION_CONTRACT.md`
3. Rack section of `docs/NORTH_STAR_PRD.md`
4. `docs/rack/ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`
5. **this ADR**
6. `docs/rack/VST3_RACK_RESEARCH.md`
7. `docs/rack/RACK_EDITOR_SPEC.md`
8. `docs/rack/VST3_RACK_EXECUTION_SPEC.md`
9. `docs/rack/VST3_RACK_TICKETS.md`
10. current issue and exact fixed-point code

When older Rack docs say “stock OBS Properties is the v2 editor,” this ADR supersedes that historical UI decision. OBS Properties remains the compatibility-safe launcher/status boundary, not the primary Rack editing surface.
