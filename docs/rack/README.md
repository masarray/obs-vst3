# VST3 Rack — Architecture, Evidence & Execution Index

This folder records the architecture and engineering evidence behind the isolated **VST3 Rack** shipped in the public stable Windows package. The Rack entered stable in **v0.6.0** and receives durable working-session recall plus state-safety hardening in **v0.6.1**.

For public product information, start with:

- [`../../README.md`](../../README.md) — product overview and download;
- [`../../CHANGELOG.md`](../../CHANGELOG.md) — release changes;
- [`../../ROADMAP.md`](../../ROADMAP.md) — current roadmap;
- [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — latest Rack engineering status.

The ticket/ADR files in this folder preserve the staged engineering history. Older sequencing statements inside historical execution documents should not be interpreted as the current public product status when they conflict with `CURRENT_STATUS.md` or the shipped release documentation.

## Current status

- Public release target: **v0.6.1 stable**.
- Qualified v0.6.1 runtime source head: `ce5eb052c97076df735b95b55328f76e222475ee`.
- Runtime hardening merged in PR #109 as `f91847744a1c824c255666b4a2f9e34b28db3905`.
- Parent/evidence ledger: [GitHub issue #56](https://github.com/masarray/obs-vst3/issues/56).
- Durable per-OBS-Rack working-session recall, DSP-safe state capture, dry-first restore and split-component VST3 state synchronization are integrated.
- Rack foreground activation, vendor-editor icon polish, shutdown hardening and editor close/reopen lifecycle hardening are integrated.
- P0/P1, R0, R1, R2, R3, CI and Compatibility Test Build passed on the qualified v0.6.1 runtime candidate recorded in PR #109.
- Public v0.6.1 release preparation re-runs an exact-head aggregate release-candidate gate before the release marker is advanced to `main`.

See [`CURRENT_STATUS.md`](CURRENT_STATUS.md) for exact qualification runs and the stable release scope.

## Product architecture

```text
OBS64.EXE
┌──────────────────────────────────────┐
│ VST3 Rack OBS filter                 │
│  status + [Open Rack]                │
│                                      │
│ bounded realtime audio bridge        │
└──────────────┬───────────────────────┘
               │ bounded Rack IPC
               ▼
obs-safe-vst3-rack-host.exe
┌──────────────────────────────────────┐
│ Graphical Rack Editor                │
│  add / replace / reorder / bypass    │
│  presets / floating vendor editors   │
│                                      │
│ Rack control plane                   │
│  immutable chain generations         │
│  durable working-session snapshot    │
│  named presets / LKG recovery        │
│                                      │
│ Rack DSP worker                      │
│  A -> B -> C -> ... -> N             │
└──────────────────────────────────────┘
```

Core law: **VST3 DSP, vendor GUI and Rack graphical UI dependencies stay outside `obs64.exe`.**

## Stable v0.6.1 shape

The Rack is graphical, but the DSP topology remains a focused serial effects lane:

```text
INPUT -> Slot 1 -> Slot 2 -> ... -> Slot N -> OUTPUT
```

v0.6.1 includes:

- separate Rack helper and protocol;
- serial multi-effect processing;
- stable slot identity and coherent topology generation swaps;
- add / replace / remove / reorder / bypass;
- graphical helper-owned Rack Editor;
- native vendor editor orchestration;
- automatic durable working-Rack recall across normal OBS restarts;
- CRC-protected atomic session snapshots with last-known-good recovery;
- DSP-safe state capture without a realtime-required control mutex;
- dry-first persisted-chain restore followed by atomic whole-generation publication;
- split controller/processor VST3 parameter synchronization before state capture;
- named Rack presets for reusable chains;
- missing-plug-in placeholders that preserve saved definitions;
- fail-dry behavior;
- bounded shutdown and editor lifecycle hardening.

Not in v0.6.1:

- arbitrary graph/patchbay routing;
- sidechain;
- MIDI / VST3 instruments;
- arbitrary multichannel / Float64 fallback;
- nested Racks;
- per-slot worker processes;
- expanded R2-2 quarantine work.

## Architecture and design records

Recommended order for understanding the design:

1. [`../NORTH_STAR_PRD.md`](../NORTH_STAR_PRD.md)
2. [`ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`](ADR-0002-RACK-RUNTIME-ARCHITECTURE.md)
3. [`ADR-0003-ISOLATED-RACK-EDITOR.md`](ADR-0003-ISOLATED-RACK-EDITOR.md)
4. [`ADR-0004-REG0-RACK-ENTRY-AUTHORIZATION.md`](ADR-0004-REG0-RACK-ENTRY-AUTHORIZATION.md)
5. [`VST3_RACK_RESEARCH.md`](VST3_RACK_RESEARCH.md)
6. [`RACK_EDITOR_SPEC.md`](RACK_EDITOR_SPEC.md)
7. [`VST3_RACK_EXECUTION_SPEC.md`](VST3_RACK_EXECUTION_SPEC.md)
8. [`VST3_RACK_TICKETS.md`](VST3_RACK_TICKETS.md) — historical staged ticket plan
9. [`THREAD_HANDOFF.md`](THREAD_HANDOFF.md) — historical execution handoff format
10. [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — current pointer

## Development rule after v0.6.1

Future Rack work must preserve the public stable contracts rather than treating the shipped Rack as disposable prototype code. Regressions found in real use should become permanent deterministic tests at the highest stable seam that reproduces them.
