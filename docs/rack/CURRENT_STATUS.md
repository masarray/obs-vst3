# VST3 Rack — Current Execution Status

**Updated:** 2026-08-27  
**Parent issue:** [#56 — VST3 Rack v2.0 execution parent / evidence ledger](https://github.com/masarray/obs-vst3/issues/56)  
**Current unblocked ticket:** [#57 — REG-0 prove and authorize the Rack extraction baseline](https://github.com/masarray/obs-vst3/issues/57)

---

## Current state

The Rack architecture/spec/ticket strategy is published in the repository.

No Rack production implementation ticket is authorized yet.

The only unblocked work is **REG-0**.

REG-0 must resolve the milestone-order conflict with exact evidence before R0 extraction begins:

- historical normative order says Single v1.0/S6 lock precedes Rack;
- current product direction makes Rack the next major target.

Expected REG-0 result:

- `GO R0` with an explicit accepted contract/ADR clarification and named mandatory Single regression gates; or
- `BLOCKED` with the smallest prerequisite issue(s).

Do not start Rack protocol, Rack helper, HostedPlugin extraction, multi-plugin runtime, Rack UI or presets before #57 reaches an accepted GO decision.

---

## Current repository planning baseline

The strategy was merged to `main` in commit:

`613d4256f4e4f6bd346eafd95a01256f0f6ad1be`

This is a **planning baseline only**. REG-0 must re-read the actual current `main` and declare a fresh fixed-point SHA before using any evidence or modifying files.

---

## Where to start in a new thread

1. Read `THREAD_HANDOFF.md`.
2. Open issue #57.
3. Follow its mandatory read order.
4. Perform REG-0 only.
5. Stop after GO/BLOCKED and leave the next ticket for a fresh thread.

Do not use this status file as a substitute for the normative execution contract or ADRs.
