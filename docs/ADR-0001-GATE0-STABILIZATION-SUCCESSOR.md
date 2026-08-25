# ADR-0001 — Gate 0 stabilization successor after OBS Properties crash

**Status:** Accepted  
**Date:** 2026-08-25  
**Scope:** Gate 0 only; this ADR does not reorder North Star S1–R5 phases

## Context

PR #22 (`fix/obs-load-compatibility`) combined multiple stabilization changes around scanner lifecycle, OBS Properties behavior, installation/compatibility and Single Host UX. It reached green CI and compatibility-package qualification, but subsequent real-machine testing on OBS Studio 32.2.2 exposed a Properties-lifetime crash. That result invalidated PR #22 as a safe runtime baseline even though parts of its work remain useful reference material.

A replacement stabilization line was therefore rebuilt from the known-good `v0.4.0` maintenance baseline as PR #23 (`stabilize/v040-crashproof-baseline`). The replacement deliberately makes smaller, independently testable changes and freezes the OBS Properties ownership boundary: OBS owns `obs_properties_t` / `obs_property_t` lifetime, and plugin runtime/recovery/scanner code must not rebuild the open Properties tree in a way that invalidates pointers while OBS callbacks are active.

PR #23 subsequently qualified the Single Host source-selector/Browse regression on exact source head `62150908d32dc2e6de93af9ddc39be6856328bd3` with green CI and real OBS Studio 32.2.2 testing, including repeated Properties reopen, isolated vendor editor ownership, helper death/recovery and zero-action OBS restart restore. This is strong stabilization evidence, but PR #23 is based on `maintenance/v0.4.0-stable`, not the canonical North Star `main` branch.

## Decision

1. **PR #22 is superseded for runtime stabilization.**
   - Keep it historical/reference only.
   - Do not merge it as Gate 0.
   - Do not continue feature work on that branch.
   - A useful change from #22 may be ported only as a small independent change onto the crash-proof line, with a regression test and fresh qualification.

2. **PR #23 is the Gate 0 stabilization successor/candidate.**
   - Its purpose is to establish the safe runtime result, not to become a parallel long-term product branch.
   - Preserve its conservative OBS Properties ownership rule unless a later change has an authoritative lifetime model plus deterministic regression coverage and real-machine evidence.

3. **Gate 0 is not locked merely because PR #23 is green.**
   Gate 0 closes only when the validated stabilization result is represented on a clean `main`-target integration head and that exact head passes all required gates again.

4. **Required Gate 0 closure sequence:**

```text
validated PR #23 stabilization result
→ create/identify clean main-target integration fixed point
→ port only the reviewed crash-proof stabilization changes needed for Gate 0
→ review correctness/architecture on the current source head
→ resolve every review finding
→ if a resolution changes the source head, review the new head again
→ run final exact-head CI and compatibility qualification
→ perform required real-machine OBS regression using the artifact from that same source head
→ if any later finding or change modifies the source head, invalidate the prior qualification and loop back through review + CI + compatibility + same-build real-machine qualification
→ merge only the unchanged, fully qualified main-target source head
→ record/tag the known-good Gate 0 baseline
→ only then unlock North Star S1
```

No review, CI, compatibility, package, or real-machine evidence survives a source-head change for the purpose of final Gate 0 authorization. Evidence from an older head may remain useful historical evidence, but it cannot qualify the new head.

5. **Artifact provenance must point to the actual source head.**
   - For pull-request workflows, do not label a public/real-test artifact only with GitHub's synthetic PR merge SHA when the contract is qualifying the PR source head.
   - Release notes, artifact names or provenance metadata must make the actual tested source commit unambiguous.

6. **Legacy milestone names do not redefine the North Star roadmap.**
   Pre-North-Star issues that used labels such as `S1` or `S2` describe historical implementation work. The authoritative phase meanings are those in `docs/CODEX_EXECUTION_CONTRACT.md`.

## Gate 0 minimum evidence

The final main-target Gate 0 head must record, at minimum:

- exact final source SHA;
- correctness/architecture review tied to that final source SHA with no unresolved findings;
- exact-head CI run IDs/URLs with all required jobs executed and green on that same source SHA;
- compatibility qualification required by the supported OBS floor/current target policy on that same source SHA;
- named regression tests that actually executed and passed;
- a public/real-test artifact whose provenance identifies the same source SHA;
- real OBS machine evidence using that exact artifact and covering the stabilization workflows affected by the port, including Properties open/close/reopen, Installed/Browse selection, vendor editor ownership, helper recovery and zero-action restoration where applicable;
- no known OBS crash regression.

If the source head changes after any of these checks, all final-head qualification evidence affected by the change must be repeated before merge.

A green historical #22 run, a green maintenance-only #23 run, or a manual test of a differently built package cannot by itself close the canonical Gate 0.

## Consequences

- The repository has one clear stabilization direction instead of two competing Gate 0 interpretations.
- The crash discovered in real OBS testing becomes a permanent architecture constraint rather than an anecdotal branch comment.
- The North Star phase sequence remains unchanged after Gate 0.
- S1 lifecycle/restartComponent work must not start until the main-target Gate 0 baseline is formally locked.
- Rack work remains forbidden until Single Host v1.0 is locked.

## Superseded interpretation

Any documentation line equivalent to:

`Gate 0 / PR #22 → S1 ...`

must now be read as:

`Gate 0 crash-proof stabilization successor (#23 evidence → clean main-target integration → lock) → S1 ...`

The durable concept is **Gate 0 crash-proof stabilization**, not a permanently hard-coded pull-request number.
