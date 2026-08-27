# VST3 Rack — Current Execution Status

**Updated:** 2026-08-27  
**Parent issue:** [#56 — VST3 Rack v2.0 execution parent / evidence ledger](https://github.com/masarray/obs-vst3/issues/56)  
**Current unblocked ticket:** [#57 — REG-0 prove and authorize the Rack extraction baseline](https://github.com/masarray/obs-vst3/issues/57)

---

## Current state

Rack runtime/editor architecture, research, execution spec and tracer-ticket strategy are published in the repository.

No Rack production implementation ticket is authorized yet.

The only currently unblocked work is **REG-0**.

REG-0 resolves the milestone-order conflict with exact evidence before any production extraction:

- historical normative order says Single v1.0/S6 lock precedes Rack;
- current product direction makes Rack the next major target.

Expected REG-0 result:

- `GO` with explicit accepted phase-order clarification + mandatory Single regression gates; next fresh ticket = **UI-0**; or
- `BLOCKED` with the smallest prerequisite issue(s).

Do not start UI-0, R0, Rack protocol/helper, HostedPlugin extraction, multi-plugin runtime, production Rack UI or presets before #57 reaches accepted GO.

---

## Current locked Rack UI architecture

The earlier “edit the whole Rack inside OBS Properties” plan is superseded by ADR-0003.

New boundary:

```text
OBS Properties
-> concise status + Open Rack
-> bounded command
-> isolated Rack helper
-> dedicated graphical Rack Editor
```

The graphical Rack Editor lives in `obs-safe-vst3-rack-host.exe`, not in `obs64.exe`.

v2 editor topology remains **graphical serial Rack**, not free-form routing graph.

Default GUI candidate after UI-0 proof:

- Dear ImGui;
- Win32 backend;
- DirectX 11 backend;
- exact pinned upstream version/commit;
- helper-only dependency.

JUCE/atkAudio/Element are reference patterns, not source/dependency to copy. JUCE use would require a separate licensing/dependency decision.

Vendor editors remain floating native helper-owned windows in v2.

---

## Execution spine

```text
#57 REG-0  CURRENT
   ↓
UI-0 graphical helper dependency proof
   ↓
R0-1 ProcessBlockView
   ↓
R0-2 HostedPlugin extraction
   ↓
R1 serial Rack runtime
   ↓
R2 snapshot/recovery/presets
   ↓
R3 graphical Rack Editor + thin OBS launcher
   ↓
R4 stress/package/commercial qualification
   ↓
R5 v2.0 exact-head lock
```

UI-0 is intentionally early so GUI dependency/package risk is discovered before R0–R2 investment. UI-0 loads no VST3 and does not implement product Rack runtime.

---

## Planning baseline

Earlier planning baseline:

`613d4256f4e4f6bd346eafd95a01256f0f6ad1be`

Later status pointer baseline:

`6111bc75087ff5b3e753c36fa2d7cbc3c95bf445`

These are historical planning references only. REG-0 must fetch the actual current `main` and declare a fresh fixed-point SHA before using evidence or modifying code.

---

## Where to start in a new thread

1. Read `THREAD_HANDOFF.md`.
2. Open issue #57.
3. Follow mandatory read order.
4. Perform REG-0 only.
5. End with GO/BLOCKED evidence.
6. Stop.
7. If GO, create/start UI-0 in a **new** thread.

Do not use this status file as a substitute for normative contract/ADRs.
