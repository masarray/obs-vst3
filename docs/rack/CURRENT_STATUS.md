# VST3 Rack — Current Execution Status

**Updated:** 2026-09-05  
**Parent evidence ledger:** [#56 — VST3 Rack v2.0 execution parent](https://github.com/masarray/obs-vst3/issues/56)  
**Current stable release target:** `v0.6.1`

---

## Current state

The Safe VST3 Rack is a public stable Windows product surface. v0.6.0 introduced the serial Rack; v0.6.1 hardens the normal working-session lifecycle after repeated real OBS restart testing.

**Qualified v0.6.1 runtime source head:** `ce5eb052c97076df735b95b55328f76e222475ee`  
**Merged runtime commit:** `f91847744a1c824c255666b4a2f9e34b28db3905` (PR #109)

PR #109 integrated:

- durable per-OBS-Rack working-session identity and storage;
- automatic working-chain recall without requiring a named preset;
- immediate topology/bypass autosave plus bounded OBS save/close state capture;
- CRC-protected atomic session writes with last-known-good recovery;
- a bounded DSP-safe state-capture frontier without a DSP-required control mutex;
- dry-first restore followed by atomic complete-generation publication;
- explicit helper save success/failure outcome instead of conflating completed failures with timeouts;
- split controller/processor VST3 native-editor parameter delivery and preset-wide resync before state capture;
- restored native editor thread-affinity hardening;
- Rack foreground activation on explicit Open Rack;
- project companion icon wiring for native vendor editor host windows.

The v0.6.1 public release marker is intentionally a documentation/version descendant of the qualified runtime merge. Release packaging does not reinterpret the engineering evidence; it rebuilds the marked source through the release qualification pipeline.

---

## Stable v0.6.1 product shape

```text
OBS Studio
  │
  ├─ VST 3.x Plug-in
  │    └─ obs-safe-vst3-host.exe
  │         └─ one isolated VST3 effect
  │
  └─ VST3 Rack
       └─ obs-safe-vst3-rack-host.exe
            ├─ graphical Rack Editor
            ├─ serial VST3 effect chain
            ├─ native vendor editor windows
            ├─ durable working-session recall
            ├─ Session Snapshot / LKG recovery
            └─ named Rack presets
```

The Rack remains a **serial graphical Rack**, not a free-form routing graph.

The graphical Rack Editor, VST3 DSP, vendor editors and Rack persistence work remain helper-owned and outside `obs64.exe`.

---

## Delivered Rack capabilities

- separate OBS `VST3 Rack` filter;
- separate Rack helper executable and protocol;
- serial multi-effect processing;
- stable slot identity;
- add / replace / remove / reorder;
- per-slot enable/bypass workflow;
- immutable topology-generation publication;
- whole-block fail-dry behavior;
- bounded recovery behavior;
- automatic durable per-OBS-Rack working-session recall;
- CRC-protected atomic snapshot writes and LKG recovery;
- DSP-safe state capture with fail-dry realtime exclusion;
- dry-first asynchronous restore and atomic complete-generation publication;
- graphical helper-owned Rack Editor;
- thin OBS Properties launcher/status surface;
- plug-in browser and slot editing;
- native vendor editor orchestration;
- split-component VST3 controller-to-processor parameter synchronization;
- named preset Save As / load / rename / update / delete;
- missing VST3 placeholders preserved safely;
- corrupt/failed preset load cannot replace the current working Rack;
- bounded Rack helper shutdown;
- close/reopen Rack Editor lifecycle hardening;
- explicit Needs Attention state when durable Rack storage is unavailable.

---

## v0.6.1 exact-head qualification

PR #109 recorded PASS for exact source head `ce5eb052c97076df735b95b55328f76e222475ee`:

| Gate | Run |
|---|---:|
| P0 Rack Shutdown | `33965258169` |
| P1 Rack Editor Polish | `33965258181` |
| R0-1 Process Seam Characterization | `33965258174` |
| R0-2 HostedPlugin Characterization | `33965258206` |
| R1-1 Rack Serial Tracer | `33965258176` |
| R1-2 Rack Safety Tracer | `33965258202` |
| R1-3 Rack Topology Tracer | `33965258164` |
| R1-4 Rack Recovery Tracer | `33965258172` |
| R2-1 Rack Session Snapshot | `33965258178` |
| R3-0 Rack Editor Bridge | `33965258212` |
| R3-1 OBS Rack Launcher | `33965258187` |
| R3-2 Rack Slot Browser | `33965258226` |
| R3-3 Rack Vendor Editor | `33965258281` |
| R3-4 Rack Preset UX | `33965258218` |
| CI | `33965258196` |
| Compatibility Test Build | `33965258227` |

Compatibility qualification included Windows tests, scanner smoke, supported OBS loader/ABI-floor checks, package build, PE inspection, portable validation and canonical OBS-root installer smoke.

Representative real OBS validation covered:

- working Rack chain retained through repeated full OBS restarts;
- Rack settings deliberately changed across three successive OBS sessions;
- the latest settings restored after every restart;
- split controller/processor VST3 DSP state restored, not only controller/preset-title metadata;
- native VST3 editor windows remained openable after restore;
- normal Open Rack foreground behavior.

The four review findings raised during PR #109 were fixed before qualification and resolved after the exact-head matrix completed green:

1. state capture required a DSP-safe frontier;
2. slow persisted chains could not remain coupled to the old helper-ready startup deadline;
3. completed save failures needed an explicit outcome rather than a timeout;
4. a writable Rack could not silently start without durable session storage.

---

## Persistence contract

### Automatic working session != named preset

The current working Rack is operational continuity. It is saved automatically per OBS Rack filter and restored on normal OBS lifecycle.

A named Rack preset is user-managed reusable content. Presets remain independent so users can intentionally save multiple chains without changing the semantics of automatic working-state recall.

### Realtime boundary

Persistence remains control-plane work:

- no filesystem I/O in the OBS audio callback;
- no VST state serialization in the OBS audio callback;
- DSP never waits on a control mutex;
- capture exclusion is atomic and bounded;
- capture/restore conflicts degrade to dry/pass-through rather than unbounded waiting.

### Restore behavior

A saved Rack is not allowed to make helper readiness depend on arbitrary aggregate third-party load time. The helper becomes safely available dry-first, materializes the persisted chain on the control owner, then atomically publishes the complete coherent generation.

---

## Deliberately outside v0.6.1

Do not describe these as shipped Rack features:

- free-form graph / patchbay routing;
- sidechain or advanced multi-bus routing;
- MIDI / VST3 instruments;
- arbitrary multichannel;
- Float64 processing fallback;
- nested Racks;
- embedded vendor editors inside OBS;
- per-slot worker processes;
- the planned expanded R2-2 quarantine/recovery work;
- macOS/Linux runtime packages.

---

## Engineering continuation

Issue #56 remains the historical engineering evidence ledger. Future Rack hardening must preserve the stable public contracts established by the Single Host and the v0.6.1 Rack line.

For public product status, use `README.md`, `CHANGELOG.md`, `ROADMAP.md` and the GitHub Pages site rather than older ticket sequencing text.
