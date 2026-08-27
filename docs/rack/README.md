# VST3 Rack v2 — Planning & Execution Index

This folder is the source of truth for the next major product target: **Safe VST3 Rack v2**.

If you are a new maintainer, reviewer, or AI coding thread, **do not start by browsing random source files**. Start with the execution state and authoritative read order below.

## Current status

- Parent/evidence ledger: [GitHub issue #56](https://github.com/masarray/obs-vst3/issues/56)
- Current only unblocked ticket: [#57 REG-0](https://github.com/masarray/obs-vst3/issues/57)
- No Rack production code is authorized until REG-0 returns accepted GO.
- After REG-0 GO, the next fresh ticket is **UI-0**, then R0-1.

See [`CURRENT_STATUS.md`](CURRENT_STATUS.md) for the latest execution pointer.

## Mandatory read order

1. [`../../AGENTS.md`](../../AGENTS.md)
2. [`../CODEX_EXECUTION_CONTRACT.md`](../CODEX_EXECUTION_CONTRACT.md)
3. Rack section of [`../NORTH_STAR_PRD.md`](../NORTH_STAR_PRD.md)
4. [`ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`](ADR-0002-RACK-RUNTIME-ARCHITECTURE.md)
5. [`ADR-0003-ISOLATED-RACK-EDITOR.md`](ADR-0003-ISOLATED-RACK-EDITOR.md)
6. [`VST3_RACK_RESEARCH.md`](VST3_RACK_RESEARCH.md)
7. [`RACK_EDITOR_SPEC.md`](RACK_EDITOR_SPEC.md)
8. [`VST3_RACK_EXECUTION_SPEC.md`](VST3_RACK_EXECUTION_SPEC.md)
9. [`VST3_RACK_TICKETS.md`](VST3_RACK_TICKETS.md)
10. [`THREAD_HANDOFF.md`](THREAD_HANDOFF.md)
11. [`CURRENT_STATUS.md`](CURRENT_STATUS.md)
12. current GitHub child ticket + exact current repository code

For a fresh implementation conversation, `THREAD_HANDOFF.md` contains the copy/paste starter prompt and evidence format.

## Product architecture in one picture

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
│  search / add / reorder / presets    │
│  floating native vendor editors      │
│                                      │
│ RackControlPlane                     │
│  immutable chain generations         │
│                                      │
│ Rack DSP worker                      │
│  A -> B -> C -> ... -> N             │
└──────────────────────────────────────┘
```

Core law: **VST3/vendor GUI/Rack graphical UI dependencies stay outside `obs64.exe`.**

## v2 is graphical, but not a graph engine

The v2 user sees a dedicated graphical serial Rack window.

The v2 DSP engine remains:

```text
INPUT -> Slot 1 -> Slot 2 -> ... -> Slot N -> OUTPUT
```

No arbitrary cables, parallel routing, sidechain, MIDI or VST3 instruments in v2.

Those become post-v2 phases after the serial Rack is locked.

## UI toolkit policy

Default Windows candidate after **UI-0** proof:

- Dear ImGui;
- Win32 backend;
- DirectX 11 backend;
- pinned exact upstream version/commit;
- helper-only dependency.

Do not link the Rack editor stack into the OBS module.

JUCE/atkAudio/Kushview Element are references for product/architecture patterns. Do not copy their source. JUCE requires a separate licensing/dependency decision before it may become a public dependency.

## Execution spine

```text
REG-0
-> UI-0
-> R0 reusable HostedPlugin seam
-> R1 serial Rack runtime
-> R2 persistence/recovery/preset foundation
-> R3 graphical Rack Editor + thin OBS launcher
-> R4 stress/package/commercial qualification
-> R5 v2.0 lock
```

One vertical ticket per fresh thread. Exact-head evidence is mandatory before merge.
