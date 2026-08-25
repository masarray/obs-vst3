# OBS Safe VST3 Host — Agent Instructions

These instructions apply to the entire repository.

## Mandatory first read

Before planning, coding, reviewing, or declaring a milestone complete, read in this order:

1. `docs/CODEX_EXECUTION_CONTRACT.md` — **normative product/AI execution contract**.
2. The relevant section of `docs/NORTH_STAR_PRD.md` — full roadmap, rationale, gates, testing and release strategy.
3. The current GitHub parent issue/tracer ticket and the repository code at its declared fixed-point SHA.

If wording conflicts, `docs/CODEX_EXECUTION_CONTRACT.md` wins until an explicit ADR updates the source-of-truth documents.

Do not use chat history as an architecture or completion source of truth.

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
8. Run required CI on the exact reviewed head.
9. Merge only that qualified exact head.
10. Record milestone/release evidence as required by the execution contract.

Bug work follows:

`reproduce → minimise → hypothesise → instrument → fix → permanent regression test`

Do not start a difficult bug by rewriting the subsystem.

## Locked phase order

Do not skip lock gates:

`Gate 0 / PR #22 → S1 → S2 → S3 → S4 → S5 → S6 Single v1.0 LOCK → R0 → R1 → R2 → R3 → R4 → R5 Rack v2.0 LOCK → post-v2 North Star`

Do not begin Rack implementation before the Single Host v1.0 contract is locked with the evidence required by `docs/CODEX_EXECUTION_CONTRACT.md`.

## Completion is evidence, not a label

Do not write ceremonial `PASS` values.

For mandatory product locks, cite the exact final SHA, exact-head CI run IDs/URLs, named tests that actually executed, and the specific public-candidate real-machine workflows required by `docs/CODEX_EXECUTION_CONTRACT.md`.

In particular:

- Single v1.0 cannot lock without proven zero-action close/reopen state restoration and helper-recovery restoration.
- Rack v2.0 cannot lock without proven zero-action Rack Session Snapshot restoration.
- Rack v2.0 cannot lock without the full named Preset Library workflow, including Save, Load/reuse into an independent Rack, Rename, Delete, explicit Update Preset, missing-plugin behavior and crash-safe persistence, with the required exact-head CI and public-candidate evidence.

If required evidence is missing, the milestone is not complete and the next locked phase must not start.
