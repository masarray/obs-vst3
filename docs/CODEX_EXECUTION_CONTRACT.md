# OBS Safe VST3 Host — Codex Execution & Product Completion Contract

**Status:** NORMATIVE / LOCKED when merged to `main`  
**Applies to:** `docs/NORTH_STAR_PRD.md` and all implementation work that follows it  
**Purpose:** remove ambiguity for Codex/AI agents and define the exact product finish line  
**Updated:** 2026-08-25

---

## 1. Precedence and sources of truth

This file is the compact normative execution contract for the North Star PRD.

- `docs/NORTH_STAR_PRD.md` remains the full product/engineering PRD and research rationale.
- This file defines the **unambiguous product finish line, execution order, persistence contract, preset/reuse contract, and stop/go gates** for AI coding agents.
- Accepted ADRs may clarify historical implementation decisions. For Gate 0, read `docs/ADR-0001-GATE0-STABILIZATION-SUCCESSOR.md`.
- If wording in the larger PRD, a historical PR, or an old issue can be interpreted in more than one way, **this contract wins** until an explicit documentation change updates the source of truth.
- Chat history is never a source of truth for architecture or milestone completion.
- A historical issue title such as old `S1`/`S2` naming does not redefine North Star phase meaning.

Codex must read this file first, then any applicable ADR, then the relevant milestone section of `NORTH_STAR_PRD.md`, then the current parent issue/ticket and repository code at the declared fixed-point SHA.

---

## 2. Current Gate 0 routing — authoritative correction

The durable milestone is **Gate 0 crash-proof Single Host stabilization**. A pull-request number is only an implementation vehicle.

### 2.1 PR #22 is superseded

PR #22 (`fix/obs-load-compatibility`) is historical/reference material only for runtime stabilization.

Why:

- it combined scanner/UI/lifecycle stabilization changes;
- CI/compatibility evidence was useful;
- subsequent real OBS Studio 32.2.2 testing exposed a Properties-lifetime crash;
- therefore its mixed runtime approach is not a safe merge baseline.

Rules:

- do not merge #22 as Gate 0;
- do not continue new stabilization work on #22;
- do not recreate its mixed approach merely because an old CI run was green;
- useful individual changes may be ported only selectively, with independent review, regression coverage and requalification.

### 2.2 PR #23 is the stabilization successor/candidate

PR #23 (`stabilize/v040-crashproof-baseline`) is the current crash-proof stabilization successor.

Its current evidence is valuable because it was rebuilt from the known-good `v0.4.0` maintenance baseline and introduced a more conservative OBS Properties ownership boundary plus independently testable source-selection/scanner behavior.

However:

**PR #23 being green does not by itself lock Gate 0.**

PR #23 is based on `maintenance/v0.4.0-stable`, while the North Star source of truth is `main`. Gate 0 closes only when the validated stabilization result is represented on a clean `main`-target integration head and that exact source head is requalified.

### 2.3 Gate 0 closure sequence

```text
validated crash-proof stabilization result from PR #23
→ establish clean main-target fixed point
→ port/integrate only the reviewed stabilization changes required for Gate 0
→ exact-head correctness/architecture review
→ resolve all review findings
→ if resolving a finding changes the source head, review the new head again
→ final exact-head CI + compatibility qualification
→ real-machine OBS qualification using the artifact from that same source head
→ if any later finding or change modifies the source head, invalidate prior qualification and repeat review + CI + compatibility + same-build real-machine qualification
→ merge only the unchanged, fully qualified main-target source head
→ record/tag known-good Gate 0 baseline
→ only then unlock North Star S1
```

**A source-head change invalidates final Gate 0 qualification.** Review, CI, compatibility, package provenance and required real-machine evidence used to authorize merge must all refer to the same final source head. Older-head evidence remains historical only.

A maintenance-only branch, historical #22 artifact, or manual test from a different build cannot close canonical Gate 0.

### 2.4 OBS Properties ownership is now a Gate 0 architecture rule

OBS owns the lifetime of `obs_properties_t` / `obs_property_t` used by its Properties UI.

Do not rebuild an open OBS Properties tree from scanner, recovery, helper-runtime, or other asynchronous code in a way that can invalidate OBS-owned pointers during property callbacks.

For user-driven property changes, prefer the supported OBS callback/refresh ownership model and prove any more complex lifecycle behavior with deterministic regression coverage plus real-machine evidence.

### 2.5 Artifact provenance rule

Exact-head qualification means the **actual source head commit** is unambiguous.

On pull-request workflows, GitHub may create a synthetic PR merge SHA. That synthetic SHA may be useful internally, but a public/real-test artifact must not present it as though it were the source commit being qualified.

Artifact name, metadata, release notes or provenance must identify the actual source-head SHA used for the product candidate.

---

## 3. Exact product finish line

The finished Windows product exposes **two deliberately separate OBS audio filters**.

### Filter A — `VST3`

Simple Single Host for one VST3 audio effect.

User workflow:

```text
Add Filter → VST3 → Select Plug-in → Open UI → Tune → Done
```

Contract:

- exactly one hosted VST3 effect per filter instance;
- third-party VST3 code never loads inside `obs64.exe`;
- vendor GUI remains helper-owned;
- helper/scanner failure cannot crash OBS;
- missed processing deadline fails open to dry/pass-through;
- repeated bad-plug-in failure enters bounded recovery/backoff/quarantine;
- plug-in identity and complete safe state restore automatically after OBS restart;
- no user Save action is required for normal session persistence;
- Single Host remains simple after Rack exists.

The OBS source/filter **internal ID must remain backward-compatible** across display-name improvements. Do not rename serialized internal identifiers casually.

### Filter B — `VST3 Rack`

Simple serial multi-VST3 effects chain.

User workflow:

```text
Add Filter → VST3 Rack → Add Effect → Add Effect → reorder/bypass/open UI → Done
```

Contract:

- separate OBS filter type from Single Host;
- separate Rack helper executable and independently versioned Rack protocol;
- serial chain first, not a DAW graph;
- ordered plug-in cards/slots;
- add, insert, remove, replace, reorder and bypass;
- each slot can open the vendor UI;
- missing/failed/quarantined slot passes through and remains visible;
- one bad slot must not make OBS crash;
- complete chain and complete per-slot VST3 states restore automatically;
- a **named Rack Preset Library is mandatory** so a complex chain can be reused in another source, scene, scene collection, or project workflow without rebuilding it manually.

### Product completion rule

The project has not reached the intended North Star product shape until **both filters coexist in the same supported package**, Single Host v1.0 regression tests remain green while Rack is present, and Rack persistence + reusable preset workflows pass their release gates.

---

## 4. “Remember my last settings” is a mandatory zero-action persistence contract

Persistence is not optional polish. It is a release gate.

### 4.1 `VST3` Single Host must automatically remember

At minimum, per OBS filter instance:

- selected logical VST3 identity (ClassID + safe resolution metadata/path);
- complete `IComponent` opaque state;
- complete `IEditController` opaque state when supplied;
- host-owned persistent controls that affect audio behavior, including bypass/mix/sidechain selection if those controls exist in that release;
- last-known-good checkpoint generation/status needed for crash recovery.

Expected behavior:

```text
Tune plug-in → close OBS normally → reopen same scene collection
→ same plug-in is selected → same audible plug-in state is restored automatically.
```

Also:

```text
Tune plug-in → helper crashes → helper is recreated safely
→ last-known-good complete state is restored before wet processing is republished.
```

### 4.2 `VST3 Rack` must automatically remember

At minimum, per Rack filter instance:

- stable rack identity;
- exact ordered slot list;
- stable slot IDs;
- VST3 logical identity per slot;
- complete component/controller state per slot;
- bypass state per slot;
- rack-level persistent audio controls that exist in the release;
- last-known-good snapshot and format version.

Expected behavior:

```text
Build/tune rack → close OBS → reopen same scene collection
→ same ordered chain returns → every available plug-in returns to its last safe audible state
→ missing/quarantined slots remain visible and pass through.
```

### 4.3 Rack Preset Library — mandatory reuse/productivity contract

Automatic Session Snapshot and Rack Preset are **different features with different jobs**:

- **Session Snapshot** protects the user's current work automatically and restores the exact current Rack after restart/recovery.
- **Rack Preset** is a user-named reusable copy of a known-good Rack so the user can apply a complex chain to another source, scene, scene collection, or future project without rebuilding and retuning it.

Rack v2.0 is not complete without the preset workflow.

A Rack Preset must save at minimum:

- preset name and format version;
- ordered slot topology;
- VST3 logical identity for every slot;
- complete opaque `IComponent` state for every slot;
- complete opaque `IEditController` state when supplied;
- bypass state per slot;
- rack-level persistent audio controls included in that release;
- enough metadata to show Missing/Incompatible placeholders and safely relink compatible installations.

Required user operations:

- **Save as Preset** from the current Rack;
- choose a saved preset from a simple preset browser/list;
- load/apply a preset into a new or existing Rack filter;
- rename a preset;
- delete a preset with an intentional user action;
- update/overwrite a preset only through an explicit user action.

Behavioral rules:

1. Loading a preset creates/restores the Rack working state; later edits must not silently overwrite the saved preset.
2. Autosave/Session Snapshot continues independently after a preset is loaded.
3. The user can experiment after loading a preset and still return to the original saved preset later.
4. A preset is reusable across independent filters/sources/scenes/scene collections without depending on the original filter instance ID.
5. Missing plug-ins do not destroy the preset; slots remain visible as Missing/pass-through placeholders.
6. Loading a malformed/corrupt preset fails safely without destroying the current Rack or its last-known-good Session Snapshot.
7. Preset storage is versioned and migration-aware.
8. Saving/updating a preset uses crash-safe atomic persistence (`temp → validate → replace`).
9. Vendor GUI windows never auto-open merely because a preset was loaded.
10. Preset names and normal management UI use normal-user terminology; ClassID/path details remain in diagnostics/repair surfaces.

Recommended storage model:

- user-level **Rack Preset Library** separate from scene JSON and per-filter Session Snapshots;
- readable/versioned manifest plus bounded opaque state blobs;
- stable preset ID independent of display name.

Required preset acceptance scenarios:

```text
complex rack → Save as Preset “Broadcast Vocal”
→ create new Rack on another source/scene
→ load “Broadcast Vocal”
→ same order + same observable plug-in states + same bypass/audio controls.
```

```text
load preset → change plug-in settings
→ autosave current Rack
→ original named preset remains unchanged
→ explicit Update Preset changes it.
```

```text
load preset with one unavailable plug-in
→ unavailable slot stays visible/pass-through
→ rest of chain restores and remains usable.
```

```text
interrupt preset write / corrupt new candidate
→ previous valid preset remains available
→ current Rack Session Snapshot is untouched.
```

### 4.4 Do not persist transient UI surprises

Do **not** automatically reopen vendor plug-in windows merely because they were open at shutdown. Audio/configuration state persists; transient window-open state does not override the no-surprise GUI policy.

Window position/size may be remembered later only if safe/useful and without triggering vendor GUI creation during scene restore/startup.

### 4.5 Persistence correctness rule

A snapshot becomes last-known-good only after the entire logical state transaction is coherent and validated. Never mix component state from generation N with controller/topology state from generation N+1.

Interrupted/corrupt primary persistence must not destroy the only known-good state.

---

## 5. Competitive target versus atkAudio

Do **not** interpret “beat atkAudio” as “copy every atkAudio feature.”

atkAudio is the breadth reference. OBS Safe VST3 wins when it is the better **production-safety and workflow-productivity choice inside OBS**.

The target is surpassed for the intended market when public evidence supports:

1. crash containment;
2. hang containment and bounded recovery;
3. scanner safety without unsafe in-process retry;
4. reliable complete state restoration for Single and Rack;
5. Rack slot-level safety/quarantine/recovery;
6. reusable Rack Preset productivity;
7. simple normal-user UX;
8. actionable diagnostics with exact build/plugin identity;
9. reliable supported installation/upgrade/uninstall/migration;
10. regression discipline with permanent tests where technically feasible;
11. exact-qualified release evidence.

Breadth features such as arbitrary graphs, MIDI instruments and direct audio-device routing are not required to win this product target.

---

## 6. Locked implementation order

Codex must execute this order. Do not skip lock gates.

```text
GATE 0
Crash-proof Single Host stabilization successor
PR #22 = historical/reference only
PR #23 = current stabilization evidence/candidate
→ clean main-target integration
→ exact-head correctness/architecture review
→ resolve findings; any head change returns to review
→ final exact-head CI + compatibility qualification
→ same-source-head real-machine public/real-test qualification
→ any later head change invalidates qualification and loops back
→ merge only unchanged qualified source head
→ record/tag known-good baseline

S1
VST3 lifecycle/restartComponent compliance
→ v0.5.0-preview.1

S2
Deterministic Torture/Evil VST3 compatibility lab
→ v0.6.0-preview.1

S3
Scanner identity + health + quarantine
→ v0.7.0-beta.1

S4
Fool-proof Single UX + installer + diagnostics
→ v0.8.0-beta.1

S5
Single Host sidechain + compatibility hardening
→ v0.9 beta → RC

S6
VST3 Single Host v1.0.0
→ SINGLE HOST CONTRACT LOCK

R0
Extract only proven reusable HostedPlugin seam under v1.0 regression contract
→ v2 alpha

R1
Two-plugin serial Rack tracer bullet
→ v2 alpha

R2
Rack zero-action Session Snapshot + recovery + slot quarantine
+ versioned Rack Preset Library foundation
→ v2 beta

R3
Simple serial signal-lane Rack UX
+ Save as Preset / Load / Rename / Delete / explicit Update Preset workflow
→ v2 beta

R4
Rack stress + preset round-trip/corruption/missing-plugin tests
+ compatibility + package qualification
→ v2 RC

R5
VST3 Rack v2.0.0
→ RACK CONTRACT LOCK

POST-v2
Compatibility intelligence
→ Authenticode signing/stable publisher identity
→ OBS latest-version qualification
→ broader layouts where evidence requires
→ Float64 fallback if justified
→ macOS
→ Linux
→ evidence-driven advanced capabilities
```

**This sequence is authoritative.**

### Legacy issue naming rule

Issues created before this contract may use labels such as `S1`, `S2`, etc. for historical implementation work. They are not allowed to redefine the sequence above.

When an old issue name conflicts with this section:

- preserve useful implementation evidence;
- mark/interpret the issue as pre-North-Star historical work;
- use the milestone meaning in this contract for all new planning and completion decisions.

---

## 7. Codex task-size rule

Codex must never implement an entire milestone from one broad instruction.

For each work item:

1. establish fixed-point base SHA and green tests;
2. read this contract + applicable ADR + relevant PRD milestone + parent issue;
3. research authoritative OBS/VST3/Windows behavior when uncertain;
4. create/take **one vertical tracer-bullet ticket**;
5. write the failing behavior/regression test first at the highest stable seam;
6. implement the minimum production change to make it green;
7. refactor only with all old/new tests green;
8. review correctness + architecture from the fixed point;
9. resolve review findings; if the source head changes, repeat required review and qualification for the new head;
10. run required CI on the exact final reviewed source head;
11. merge only that unchanged, qualified source head;
12. update milestone progress and release evidence.

Bug loop:

```text
reproduce → minimise → hypothesise → instrument → fix → permanent regression test
```

Do not rewrite a subsystem as the first response to a bug.

---

## 8. Required invariant checks on every runtime PR

Every implementation PR must answer **YES** before merge:

- Does third-party VST3 code remain outside `obs64.exe`?
- Is OBS `filter_audio` still free of vendor lifecycle/UI/state/scanning/filesystem/process work, project-owned heap allocation, blocking mutexes and unbounded waits?
- Does failure still return bounded dry/pass-through instead of freezing/crashing OBS?
- Does state remain coherent and last-known-good safe?
- Are OBS Properties ownership/lifetime boundaries respected?
- Are Single Host v1.0 contract tests still green after v1.0 exists?
- If Rack code changed, did the separate Rack protocol/helper boundary remain intact?
- If Rack persistence/presets changed, are Session Snapshot and named Rack Preset semantics still separate and crash-safe?
- Is there a deterministic test for the changed behavior/bug where technically feasible?
- Is the public artifact tied unambiguously to the actual qualified source commit?

If any answer is NO or UNKNOWN, stop the merge and resolve it.

---

## 9. Milestone closure format

An AI agent may mark a milestone locked only when it records:

```text
Milestone:
Fixed-point base:
Final source commit SHA:
Required CI runs: <exact run URLs/IDs tied to Final source commit SHA>
Public trial version/artifact:
Artifact source SHA:
Known blockers: none
Regression tests added: <test names/files proving mandatory workflows>
Manual real-machine evidence: <specific workflow + result + exact build/source SHA; never just “tested”>
Architecture invariants: PASS
Persistence contract: PASS / N/A only when genuinely not yet a required gate
Preset/reuse contract: PASS / N/A only before Rack preset/reuse becomes applicable
Next unlocked milestone:
```

A literal `PASS` without evidence is invalid.

### 9.1 Required evidence for Gate 0 LOCK

Before North Star S1 may start, Gate 0 must identify:

1. clean main-target fixed-point and final source SHA;
2. correctness/architecture review tied to that final source SHA with no unresolved findings;
3. exact-head CI where all required jobs actually executed and passed on that same final source SHA;
4. required supported-OBS compatibility qualification tied to that same final source SHA;
5. named stabilization regression tests that actually executed and passed;
6. public/real-test artifact whose provenance identifies that same source SHA;
7. real-machine OBS evidence using that exact artifact, covering the stabilization paths affected by the integration, including Properties reopen, Installed/Browse behavior, vendor editor ownership, helper recovery and zero-action restoration where applicable;
8. no known OBS crash regression;
9. merge of that unchanged qualified source head to `main` and record/tag of the known-good baseline.

If the source head changes after review, CI, compatibility, packaging, or real-machine qualification, the prior final-head authorization is invalid. Repeat the applicable review and all required qualification on the new source head before merge.

Historical #22 CI, maintenance-only #23 qualification, or a manual test from another build may support the engineering history but cannot substitute for this final canonical Gate 0 evidence.

### 9.2 Required evidence for S6 persistence PASS

Before Single Host v1.0 can lock:

1. automated regression/integration test proving full save → destroy/recreate/reopen → restore through the stable host seam;
2. exact-head CI where that test executed and passed;
3. public-candidate real-machine evidence:

```text
select VST3 → change vendor state → close OBS normally
→ reopen same scene collection → same plug-in and same observable/audio state return automatically
```

4. recovery evidence that helper death/recreation restores last-known-good complete state before wet processing resumes.

`Persistence contract: N/A` is forbidden at S6.

### 9.3 Required evidence for R5 persistence PASS

Before Rack v2.0 can lock:

1. automated tests proving ordered topology + per-slot complete state survive Session Snapshot save/reload;
2. exact-head CI tied to the final source SHA where **all mandatory Rack persistence tests actually executed and passed**;
3. real-machine evidence using the public candidate artifact built from that same source commit:

```text
create/tune multi-slot Rack → close OBS normally
→ reopen same scene collection → same slot order, available plug-in states, bypass and rack controls return automatically
```

4. corrupt/interrupted-primary recovery evidence showing last-known-good Rack remains recoverable.

A failed mandatory persistence test can never support `Persistence contract: PASS`. A local/debug/differently packaged build is insufficient.

### 9.4 Required evidence for R5 preset/reuse PASS

Before Rack v2.0 can lock, evidence must cover the **complete mandatory Preset Library workflow**.

Every mandatory preset test below must **execute and pass on the final source head** in exact-head CI. Automated coverage must include:

- Save as Preset;
- browse/list/select/load;
- independent reuse into another Rack/filter/source;
- Rename;
- Delete;
- explicit Update Preset;
- topology + per-slot component/controller state + bypass/rack controls;
- missing-plug-in placeholder/pass-through;
- crash-safe/corrupt-write behavior;
- post-load edits do not silently mutate saved preset;
- interrupted/corrupt preset write preserves previous valid preset and current Session Snapshot.

Public-candidate evidence from the same final source commit must demonstrate:

```text
Rack A on source/filter A → build and tune complex chain → Save as Preset “Broadcast Vocal”
→ create independent Rack B on a different source/filter or supported scene workflow
→ browse/select/load “Broadcast Vocal”
→ verify equivalent order, available plug-in states, bypass and rack controls
```

It must also prove:

- Rename preserves preset identity/content and changes visible name;
- edits after loading do not mutate preset until explicit Update Preset;
- explicit Update Preset intentionally changes it;
- Delete removes only the selected preset and preserves unrelated presets + current Session Snapshot;
- missing plug-in behavior remains visible/pass-through;
- corrupt write does not destroy the previous valid preset.

A named test that did not execute **and pass** on the exact final source head is not evidence. A failed mandatory preset test can never support `Preset/reuse contract: PASS`. Generic Rack persistence is not a substitute for independent preset reuse.

`Persistence contract: N/A` and `Preset/reuse contract: N/A` are forbidden at R5.

If any mandatory value is not evidence-backed `PASS`, the product lock does not exist and the next locked phase must not start.

---

## 10. Final North Star acceptance

The long-term product is successful when a normal OBS creator can do this without reading engineering documentation:

```text
Single effect:
Add VST3 → choose plug-in → tune → stream → close OBS → reopen → settings are back.

Multiple effects:
Add VST3 Rack → build serial chain → tune each plug-in → stream
→ close OBS → reopen → chain and settings are back.

Reuse:
Build a complex Rack once → Save as Preset “Broadcast Vocal”
→ add VST3 Rack in another source/scene/project workflow
→ load preset → chain and plug-in states are immediately back
→ continue working without rebuilding the chain.

Bad plug-in:
Plug-in crashes/hangs → OBS and stream remain alive → audio passes through safely
→ automatic bounded recovery or understandable quarantine → user can Retry Safely/replace it.
```

The product promise is therefore:

**comfortable to use, safe from third-party plug-in crashes, safe from losing the user's work, easy to reuse, and faster than rebuilding complex audio chains by hand.**

That is the product contract Codex is building toward.
