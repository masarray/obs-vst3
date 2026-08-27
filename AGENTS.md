# OBS Safe VST3 Host — Agent Instructions

These instructions apply to the entire repository.

## Mandatory first read

Before planning, coding, reviewing, or declaring a milestone complete, read in this order:

1. `docs/CODEX_EXECUTION_CONTRACT.md` — **normative product/AI execution contract**.
2. `docs/ADR-0001-GATE0-STABILIZATION-SUCCESSOR.md` when working on Gate 0 / stabilization.
3. The relevant section of `docs/NORTH_STAR_PRD.md` — full roadmap, rationale, gates, testing and release strategy.
4. When working on Rack planning or implementation, read `docs/rack/THREAD_HANDOFF.md` and then follow its Rack-specific read order.
5. The current GitHub parent issue/tracer ticket and the repository code at its declared fixed-point SHA.

If wording conflicts, `docs/CODEX_EXECUTION_CONTRACT.md` wins. An accepted ADR may clarify or supersede a historical implementation decision, but must not silently reorder the North Star phases.

Do not use chat history, stale issue names, old PR descriptions, or historical branch labels as architecture or milestone sources of truth.

## Current next planning target — VST3 Rack

The next major product target is the separate **VST3 Rack** serial multi-effect system described by the North Star.

Before any Rack production code, execute **REG-0 Rack Entry Gate** from `docs/rack/VST3_RACK_TICKETS.md`. REG-0 exists because the historical locked phase order requires Single v1.0 before Rack while current product direction wants Rack next. Do not resolve that conflict informally: REG-0 must produce exact evidence and an explicit accepted clarification/ADR before R0 extraction is unlocked.

Rack-specific authoritative planning files:

1. `docs/rack/ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`
2. `docs/rack/VST3_RACK_RESEARCH.md`
3. `docs/rack/VST3_RACK_EXECUTION_SPEC.md`
4. `docs/rack/VST3_RACK_TICKETS.md`
5. `docs/rack/THREAD_HANDOFF.md`

The v2 Rack architecture is serial-first, one isolated Rack helper process per Rack filter, a separate Rack protocol, protocol-neutral deep VST3 lifecycle seams, immutable chain-generation swaps, preallocated serial processing, whole-block fail-dry semantics, automatic Session Snapshot persistence, named reusable Rack Presets, and compatibility-first stock OBS Properties UI. Free-form graph, sidechain/routing, MIDI/instruments and custom Qt Rack UI are not v2 scope.

## Current Gate 0 routing — do not regress

- PR #22 (`fix/obs-load-compatibility`) is **historical/reference only** for runtime stabilization. It was superseded after real OBS 32.2.2 testing exposed a Properties-lifetime crash in the mixed stabilization approach. Do not merge, revive, or continue #22 as the Gate 0 implementation branch.
- PR #23 (`stabilize/v040-crashproof-baseline`) is the current stabilization successor/candidate. Useful fixes from #22 may be ported only selectively and must be independently reviewed and revalidated on the crash-proof baseline.
- PR #23 being green is **not by itself Gate 0 LOCK**, because its maintenance baseline is not the canonical `main` baseline. Gate 0 closes only after the validated stabilization result is integrated onto a clean `main`-target path, exact-head CI/review passes again, required real-machine evidence is tied to that final integration head, and the known-good baseline is recorded/tagged.
- Do not begin North Star S1 implementation until Gate 0 is formally locked under those rules.

## Product finish line

The intended product has two separate OBS filters in the supported Windows package:

- `VST3` — one isolated VST3 effect, simple and production-safe.
- `VST3 Rack` — a separate isolated serial multi-VST3 chain with automatic Session Snapshot persistence and a reusable named Rack Preset Library.

Normal user work must survive restart/recovery without a manual Save action. Rack presets must support safe reuse of complex chains across independent filters/sources/scenes/project workflows as specified by the execution contract.

## Non-negotiable architecture invariants

- Third-party VST3/vendor GUI code never runs in `obs64.exe`.
- OBS `filter_audio` never performs vendor lifecycle/UI/state/scanning/filesystem/process work, project-owned heap allocation, blocking mutex acquisition, or unbounded waits.
- Invalid/late/unavailable wet processing fails open to bounded dry/pass-through audio.
- Scanner/vendor failures remain isolated; never introduce an unsafe in-process vendor scan retry.
- Recovery is bounded; repeated failures back off/quarantine rather than restart-loop.
- State promoted as last-known-good must be coherent and validated.
- OBS Properties objects remain OBS-owned. Runtime/recovery/scanner code must not rebuild an open Properties tree in a way that can invalidate OBS-owned property pointers mid-callback.
- After Single Host v1.0 lock, Rack work must keep the Single v1.0 regression contract green.
- Rack owns a separate helper/protocol; do not repurpose the Single Host protocol into a general Rack protocol.

## Required engineering loop

Never treat a whole milestone as one coding task.

For one vertical tracer-bullet behavior:

1. Establish fixed-point base SHA and current green gates.
2. Research authoritative OBS/VST3/Windows behavior when uncertain.
3. Define scope, non-goals, failure modes and acceptance tests.
4. Add the smallest failing behavior/regression test first at the highest stable seam.
5. Implement the minimum change to make it green.
6. Refactor only while all previous/new tests remain green.
7. Review correctness/realtime/lifecycle/ownership **and** architecture boundaries from the fixed point.
8. Resolve every review finding. If the source head changes, repeat the required review on the new head; older review/qualification evidence does not authorize the new head.
9. Run required CI and any compatibility/package/manual qualification on the exact final reviewed source head.
10. If any later change modifies that source head, invalidate the prior final qualification and return to review + required qualification.
11. Merge only the unchanged, fully qualified exact source head.
12. Record milestone/release evidence as required by the execution contract.

Bug work follows:

`reproduce → minimise → hypothesise → instrument → fix → permanent regression test`

Do not start a difficult bug by rewriting the subsystem.

## Locked phase order

Do not skip lock gates:

`Gate 0 crash-proof stabilization successor → S1 → S2 → S3 → S4 → S5 → S6 Single v1.0 LOCK → R0 → R1 → R2 → R3 → R4 → R5 Rack v2.0 LOCK → post-v2 North Star`

Historical issue names such as old `S1`/`S2` tickets created before the North Star contract do **not** redefine these phases. When a legacy issue label conflicts with the execution contract, treat the issue as historical implementation evidence and follow the phase meaning in `docs/CODEX_EXECUTION_CONTRACT.md`.

Do not begin Rack implementation before the Single Host v1.0 contract is locked with the evidence required by `docs/CODEX_EXECUTION_CONTRACT.md` **unless REG-0 results in an explicit accepted contract/ADR clarification that authorizes a narrower R0 extraction path**. Planning/research may proceed before that decision; Rack production code may not.

## Completion is evidence, not a label

Do not write ceremonial `PASS` values.

For mandatory product locks, cite the exact final SHA, exact-head CI run IDs/URLs, named tests that actually executed, and the specific public-candidate real-machine workflows required by `docs/CODEX_EXECUTION_CONTRACT.md`.

Artifact provenance must identify the **actual source head commit** being qualified. A synthetic pull-request merge SHA may be useful to GitHub internally, but it must not be presented as the source commit of a public candidate.

In particular:

- Gate 0 cannot lock while the validated stabilization result exists only on a non-`main` maintenance baseline.
- Single v1.0 cannot lock without proven zero-action close/reopen state restoration and helper-recovery restoration.
- Rack v2.0 cannot lock without proven zero-action Rack Session Snapshot restoration.
- Rack v2.0 cannot lock without the full named Preset Library workflow, including Save, Load/reuse into an independent Rack, Rename, Delete, explicit Update Preset, missing-plugin behavior and crash-safe persistence, with the required exact-head CI and public-candidate evidence.

If required evidence is missing, the milestone is not complete and the next locked phase must not start.
