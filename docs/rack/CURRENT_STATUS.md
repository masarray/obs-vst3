# VST3 Rack — Current Execution Status

**Updated:** 2026-09-04  
**Parent evidence ledger:** [#56 — VST3 Rack v2.0 execution parent](https://github.com/masarray/obs-vst3/issues/56)  
**Stable release target:** `v0.6.0`

---

## Current state

The Safe VST3 Rack has moved from planning/architecture into a qualified public-release candidate.

**Qualified pre-release integration baseline:** `660c83c6c67fbc4643401140c0bf09c31a1111c7` (merged PR #97).

PR #97 integrated:

- R3-4 complete Rack preset management UX;
- P0 bounded Rack shutdown hardening;
- P1 professional Rack Editor visual/product polish;
- final Rack Editor close/reopen lifecycle hardening.

The public packaging/release line is being synchronized as **v0.6.0 stable**. The release marker is expected to be a documentation/release descendant of the qualified integration baseline, not a replacement for its engineering evidence.

---

## Stable v0.6.0 product shape

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
            ├─ Session Snapshot recovery
            └─ named Rack presets
```

The Rack remains a **serial graphical Rack**, not a free-form routing graph.

The graphical Rack Editor, VST3 DSP and vendor editors remain helper-owned and outside `obs64.exe`.

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
- Rack Session Snapshot persistence;
- graphical helper-owned Rack Editor;
- thin OBS Properties launcher/status surface;
- plug-in browser and slot editing;
- native vendor editor orchestration;
- named preset Save As / load / rename / update / delete;
- missing VST3 placeholders preserved safely;
- corrupt/failed preset load cannot replace the current working Rack;
- bounded Rack helper shutdown;
- close/reopen Rack Editor lifecycle hardening.

---

## Final integration qualification

PR #97 recorded PASS for the final exact-head candidate `999b8e06378b8e89dc4a79c5838d4dc0a0d8a247`:

| Gate | Run |
|---|---:|
| P0 Rack Shutdown | `33861625191` |
| P1 Rack Editor Polish | `33861625324` |
| R1-1 Rack Serial Tracer | `33861625190` |
| R1-2 Rack Safety Tracer | `33861625231` |
| R1-3 Rack Topology Tracer | `33861625243` |
| R1-4 Rack Recovery Tracer | `33861625187` |
| R2-1 Rack Session Snapshot | `33861625240` |
| R3-0 Rack Editor Bridge | `33861625258` |
| R3-1 OBS Rack Launcher | `33861625232` |
| R3-2 Rack Slot Browser | `33861625239` |
| R3-3 Rack Vendor Editor | `33861625325` |
| R3-4 Rack Preset UX | `33861625216` |
| CI | `33861625257` |
| Compatibility Test Build | `33861625233` |

Compatibility qualification included Windows tests, scanner smoke, supported OBS loader/ABI-floor checks, package build, PE inspection, portable validation and canonical OBS-root installer smoke.

Representative real OBS smoke covered:

- two-effect serial Rack audio processing;
- Enable/Bypass;
- native VST3 vendor editor operation;
- improved OBS shutdown after P0 hardening.

After merge, main CI for `660c83c6c67fbc4643401140c0bf09c31a1111c7` also completed successfully.

---

## Deliberately outside v0.6.0

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

Issue #56 remains the historical engineering evidence ledger. Future Rack hardening must preserve the stable public contracts established by the Single Host and v0.6.0 Rack release.

For public product status, use `README.md`, `CHANGELOG.md`, `ROADMAP.md` and the GitHub Pages site rather than older ticket sequencing text.
