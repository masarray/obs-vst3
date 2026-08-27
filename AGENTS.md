# OBS Safe VST3 Host — Agent Instructions

These instructions apply to the entire repository.

## Mandatory first read

Before planning, coding, reviewing, or declaring a milestone complete, read in this order:

1. `docs/CODEX_EXECUTION_CONTRACT.md` — **normative product/AI execution contract**.
2. `docs/ADR-0001-GATE0-STABILIZATION-SUCCESSOR.md` when working on Gate 0 / stabilization.
3. The relevant section of `docs/NORTH_STAR_PRD.md` — full roadmap, rationale, gates, testing and release strategy.
4. When working on Rack planning or implementation, read `docs/rack/THREAD_HANDOFF.md` and then follow its Rack-specific read order.
5. The current GitHub parent issue/tracer ticket and repository code at its declared fixed-point SHA.

If wording conflicts, `docs/CODEX_EXECUTION_CONTRACT.md` wins on product invariants/order. Accepted Rack ADRs may replace historical Rack implementation choices while preserving the normative safety/product contract.

Do not use chat history, stale issue names, old PR descriptions, or historical branch labels as architecture or milestone sources of truth.

---

## Current next planning target — VST3 Rack

The next major product target is the separate **VST3 Rack** serial multi-effect system.

Before Rack production code, execute **REG-0 Rack Entry Gate** from `docs/rack/VST3_RACK_TICKETS.md`. REG-0 exists because historical locked phase order requires Single v1.0 before Rack while current product direction wants Rack next. Do not resolve that conflict informally: REG-0 must produce exact evidence and an explicit accepted clarification/ADR before extraction is unlocked.

Rack-specific authoritative planning files, in order:

1. `docs/rack/ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`
2. `docs/rack/ADR-0003-ISOLATED-RACK-EDITOR.md`
3. `docs/rack/VST3_RACK_RESEARCH.md`
4. `docs/rack/RACK_EDITOR_SPEC.md`
5. `docs/rack/VST3_RACK_EXECUTION_SPEC.md`
6. `docs/rack/VST3_RACK_TICKETS.md`
7. `docs/rack/CURRENT_STATUS.md`

### Locked Rack v2 shape

- separate OBS `VST3 Rack` filter;
- separate Rack helper executable and Rack protocol;
- one isolated Rack helper process per Rack filter;
- protocol-neutral deep VST3 lifecycle seam shared only after behavior-preserving R0 extraction;
- serial effects chain first;
- immutable chain-generation swaps;
- preallocated ping-pong processing;
- whole-block fail-dry semantics;
- automatic Session Snapshot persistence;
- named reusable Rack Presets;
- dedicated graphical Rack Editor owned by the isolated Rack helper;
- OBS Properties is only thin launcher/status surface;
- floating native vendor editor windows remain helper-owned;
- free-form routing, sidechain, MIDI/instruments and embedded vendor editors are post-v2.

### Rack GUI rule

The earlier “stock OBS Properties is the complete Rack editor” decision is superseded by `ADR-0003-ISOLATED-RACK-EDITOR.md`.

Do not reintroduce private OBS Qt/widget injection.

Default Windows v2 graphical candidate is **Dear ImGui + Win32 + DirectX 11**, pinned exact upstream version/commit, helper-only, but it must pass **UI-0** before R0 production extraction.

JUCE/atkAudio/Element are reference material. Do not copy their source. Do not introduce JUCE into the public product without an explicit licensing/dependency ADR.

---

## Current Rack execution gate

Current parent/evidence ledger: **#56**.

Current only unblocked ticket: **#57 REG-0**.

Do not start UI-0/R0 production work until REG-0 reaches accepted GO.

After REG-0 GO:

1. run **UI-0** in a fresh thread;
2. prove helper-only graphical dependency/window boundary;
3. result is `GO IMGUI` or `BLOCKED`;
4. stop;
5. only a later fresh thread starts R0-1.

---

## Current Gate 0 routing — historical / do not regress

- PR #22 (`fix/obs-load-compatibility`) is historical/reference only for runtime stabilization. It was superseded after real OBS 32.2.2 testing exposed a Properties-lifetime crash in the mixed stabilization approach. Do not merge, revive, or continue #22 as the Gate 0 implementation branch.
- PR #23 (`stabilize/v040-crashproof-baseline`) is the historical stabilization successor/candidate. Useful fixes from #22 may be ported only selectively and independently reviewed/revalidated.
- Historical Gate 0 details remain useful as lessons in OBS Properties lifetime/compatibility. Do not infer current milestone status from old PR labels alone.

The key durable lesson is: **OBS owns `obs_properties_t` / `obs_property_t` lifetime.** Background scanner/recovery/runtime code must not rebuild an open Properties tree in a way that invalidates OBS-owned pointers mid-callback.

---

## Product finish line

The intended product has two separate OBS filters in the supported Windows package:

- `VST3` — one isolated VST3 effect, simple and production-safe.
- `VST3 Rack` — a separate isolated serial multi-VST3 chain with graphical helper-owned editor, automatic Session Snapshot persistence and reusable named Rack Preset Library.

Normal user work must survive restart/recovery without manual Save. Rack presets must support safe reuse of complex chains across independent filters/sources/scenes/project workflows.

---

## Non-negotiable architecture invariants

- Third-party VST3/vendor GUI code never runs in `obs64.exe`.
- Rack graphical UI/toolkit code must not be linked into the OBS module merely to implement Rack editing.
- OBS `filter_audio` never performs vendor lifecycle/UI/state/scanning/filesystem/process work, project-owned heap allocation, blocking mutex acquisition, or unbounded waits.
- Rack DSP worker never performs GUI/D3D/filesystem/scanner/preset work.
- Invalid/late/unavailable wet processing fails open to bounded dry/pass-through audio.
- Scanner/vendor failures remain isolated; never introduce unsafe in-process vendor scan retry.
- Recovery is bounded; repeated failures back off/quarantine rather than restart-loop.
- State promoted as last-known-good must be coherent and validated.
- OBS Properties objects remain OBS-owned; async code must not invalidate open property pointers.
- After accepted Single extraction baseline, Rack work must keep the named Single regression contract green.
- Rack owns separate helper/protocol; do not repurpose Single protocol into a general Rack protocol.
- Rack Editor is a command/snapshot view/controller, not a second mutable Rack model.
- UI/control thread must not own a lock required for normal Rack DSP progress.
- Slot identity is stable across reorder; list index is presentation only.
- Vendor editor windows do not auto-open on restore/preset load.

---

## Required engineering loop

Never treat a whole milestone as one coding task.

For one vertical tracer-bullet behavior:

1. Establish fixed-point base SHA and current green gates.
2. Read contract + applicable ADRs + relevant PRD + current ticket.
3. Research authoritative OBS/VST3/Windows/dependency behavior only where uncertain.
4. Define scope, non-goals, failure modes and acceptance tests.
5. Add smallest failing behavior/regression test first at highest stable seam where feasible.
6. Implement minimum change to make it green.
7. Refactor only while all previous/new tests remain green.
8. Review from fixed point in two passes:
   - Standards/invariants;
   - Spec/ticket compliance.
9. Resolve every finding. If source head changes, repeat required final review/qualification.
10. Run required exact-head CI/compat/package/manual qualification.
11. Merge only unchanged qualified source head.
12. Record evidence in parent/child issue.
13. Stop; next ticket gets a fresh context.

Bug loop:

`reproduce → minimise → hypothesise → instrument → fix → permanent regression test`

Do not start a difficult bug by rewriting the subsystem.

---

## Rack execution order

Historical normative product order is maintained by the execution contract. The current Rack entry exception must be explicitly authorized by REG-0.

Within Rack work, use:

```text
REG-0
→ UI-0 helper-only graphical dependency proof
→ R0-1 ProcessBlockView
→ R0-2 HostedPlugin extraction
→ R1 serial Rack runtime
→ R2 persistence/recovery/preset foundation
→ R3 graphical Rack Editor + thin OBS launcher
→ R4 stress/package/compatibility
→ R5 Rack v2.0 LOCK
→ post-v2 routing/sidechain/MIDI/instruments
```

Do not skip lock gates.

---

## Completion is evidence, not a label

Do not write ceremonial `PASS` values.

For mandatory product locks, cite exact final SHA, exact-head CI run IDs/URLs, named tests that actually executed, and public-candidate real-machine workflows required by the execution contract.

Artifact provenance must identify the **actual source head commit**. Synthetic PR merge SHAs may be useful internally but are not the public candidate source identity.

In particular:

- Single/Rack shared-runtime work cannot merge if named Single regressions are not green.
- Rack v2.0 cannot lock without zero-action Rack Session Snapshot restoration.
- Rack v2.0 cannot lock without full named Preset Library workflow.
- Rack v2.0 cannot lock if graphical editor dependencies leak into the OBS module or minimum supported OBS loader compatibility regresses.
- Rack v2.0 cannot lock without helper kill/recovery testing while Rack Editor/vendor windows are exercised.

If required evidence is missing, milestone is not complete and next locked phase must not start.
