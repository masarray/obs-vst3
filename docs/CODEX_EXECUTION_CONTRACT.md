# OBS Safe VST3 Host — Codex Execution & Product Completion Contract

**Status:** NORMATIVE / LOCKED when merged to `main`  
**Applies to:** `docs/NORTH_STAR_PRD.md` and all implementation work that follows it  
**Purpose:** remove ambiguity for Codex/AI agents and define the exact product finish line  
**Updated:** 2026-08-25

---

## 1. Precedence

This file is the compact normative execution contract for the North Star PRD.

- `docs/NORTH_STAR_PRD.md` remains the full product/engineering PRD and research rationale.
- This file defines the **unambiguous product finish line, execution order, persistence contract, preset/reuse contract, and stop/go gates** for AI coding agents.
- If wording in the larger PRD can be interpreted in more than one way, **this contract wins until an explicit ADR updates both documents**.
- Chat history is never a source of truth for architecture or milestone order.

Codex must read this file first, then the relevant milestone section of `NORTH_STAR_PRD.md`, then the current parent issue/ticket and repository code.

---

## 2. Exact product finish line

The finished Windows product exposes **two deliberately separate OBS audio filters**:

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

## 3. “Remember my last settings” is a mandatory zero-action persistence contract

Persistence is not optional polish. It is a release gate.

### 3.1 `VST3` Single Host must automatically remember

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

### 3.2 `VST3 Rack` must automatically remember

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

### 3.3 Rack Preset Library — mandatory reuse/productivity contract

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

1. **Loading a preset creates/restores the Rack working state; it does not make later knob/GUI edits silently overwrite the saved preset.**
2. Autosave/Session Snapshot continues independently after a preset is loaded.
3. The user can experiment after loading a preset and still return to the original saved preset later.
4. A preset must be reusable across sources/scenes/scene collections on the same supported installation without depending on the original filter instance ID.
5. Missing plug-ins do not destroy the preset. Their slots remain visible as Missing/pass-through placeholders so the user can install, relink, replace, or bypass them.
6. Loading a malformed/corrupt preset must fail safely without destroying the current Rack or its last-known-good Session Snapshot.
7. Preset storage is versioned and migration-aware. A future format change must not silently reinterpret old opaque state.
8. Saving/updating a preset uses crash-safe atomic persistence (`temp → validate → replace`) so an interrupted write does not destroy the previously valid preset.
9. Vendor GUI windows are never auto-opened merely because a preset was loaded.
10. Preset names and management UI must be understandable to normal users; ClassID/path details stay in diagnostics/repair surfaces.

Recommended storage model:

- a user-level **Rack Preset Library** owned by OBS Safe VST3, separate from scene JSON and separate from per-filter Session Snapshots;
- readable/versioned manifest plus bounded opaque state blobs;
- stable preset ID independent of display name so rename does not change identity.

Required preset acceptance tests:

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

### 3.4 Do not persist transient UI surprises

Do **not** automatically reopen vendor plug-in windows merely because they were open at shutdown. Audio/configuration state persists; transient window-open state does not override the PRD's no-surprise GUI policy.

Window position/size may be remembered later only if it is safe, useful, and does not trigger vendor GUI creation during scene restore/startup.

### 3.5 Persistence correctness rule

A snapshot becomes last-known-good only after the entire logical state transaction is coherent and validated. Never mix component state from generation N with controller/topology state from generation N+1.

Interrupted/corrupt primary persistence must not destroy the only known-good state.

---

## 4. Competitive target versus atkAudio

Do **not** interpret “beat atkAudio” as “copy every atkAudio feature.”

atkAudio is the breadth reference. OBS Safe VST3 wins when it is the better **production-safety and workflow-productivity choice inside OBS**.

The target is considered surpassed for the intended market when public evidence supports all of the following:

1. **Crash containment:** injected/vendor helper, editor or scanner failures do not take down OBS in supported scenarios.
2. **Hang containment:** bounded deadline/watchdog/recovery behavior; no unlimited OBS audio callback wait.
3. **Scanner safety:** no unsafe in-process retry path; one bad candidate does not block the full library.
4. **State reliability:** Single and Rack restore complete audible state automatically across OBS restart and helper recovery.
5. **Rack safety:** one bad Rack slot can be identified, bypassed/quarantined and the remaining chain recovered.
6. **Preset productivity:** a complex Rack can be saved once and reused safely in another source/scene/project workflow with complete slot states restored.
7. **Simplicity:** a normal user deals with `VST3` or `VST3 Rack`, plug-in names, presets, status and vendor UI — not ClassIDs, helper processes, IPC or cache files.
8. **Diagnostics:** exact build/module/helper/scanner/plugin identity and last failure state can be copied without expert debugging.
9. **Install reliability:** normal/custom/portable supported OBS roots install, upgrade, uninstall and target migration are qualification-tested.
10. **Regression discipline:** every confirmed project regression gets a permanent automated regression test where technically feasible.
11. **Release evidence:** each stable build comes from an exact CI-qualified commit and public trial progression.

Breadth features such as arbitrary graphs, MIDI instruments and direct audio-device routing are not required to win this product target.

---

## 5. Locked implementation order

Codex must execute this order. Do not skip lock gates.

```text
GATE 0
Finish PR #22 stabilization
→ real-machine validation
→ merge
→ tag known-good baseline

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

**This post-v2 sequence is authoritative** and resolves any earlier wording that placed signing before compatibility intelligence.

---

## 6. Codex task-size rule

Codex must never implement an entire milestone from one broad instruction.

For each work item:

1. Establish fixed-point base SHA and green tests.
2. Read this contract + relevant PRD milestone + parent issue.
3. Research authoritative OBS/VST3/Windows behavior when uncertain.
4. Create/take **one vertical tracer-bullet ticket**.
5. Write the failing behavior/regression test first at the highest stable seam.
6. Implement the minimum production change to make it green.
7. Refactor only with all old/new tests green.
8. Review correctness + architecture from the fixed point.
9. Run exact-head required CI.
10. Merge only the reviewed exact head.
11. Update milestone progress and release notes.

A bug follows:

```text
reproduce → minimise → hypothesise → instrument → fix → permanent regression test
```

Do not rewrite a subsystem as the first response to a bug.

---

## 7. Required invariant checks on every runtime PR

Every implementation PR must answer **YES** to these before merge:

- Does third-party VST3 code remain outside `obs64.exe`?
- Is OBS `filter_audio` still free of vendor lifecycle/UI/state/scanning/filesystem/process work and unbounded waits?
- Does failure still return bounded dry/pass-through instead of freezing/crashing OBS?
- Does state remain coherent and last-known-good safe?
- Are Single Host v1.0 contract tests still green after v1.0 exists?
- If Rack code changed, did the separate Rack protocol/helper boundary remain intact?
- If Rack persistence/presets changed, are Session Snapshot and named Rack Preset semantics still separate and crash-safe?
- Is there a deterministic test for the behavior/bug being changed where technically feasible?
- Is the public artifact tied to an exact qualified commit?

If any answer is NO or UNKNOWN, stop the merge and resolve it.

---

## 8. Milestone closure format

An AI agent may mark a milestone locked only when it records:

```text
Milestone:
Fixed-point base:
Final commit SHA:
Required CI runs:
Public trial version:
Known blockers: none
Regression tests added:
Manual real-machine evidence:
Architecture invariants: PASS
Persistence contract: PASS / N/A
Preset/reuse contract: PASS / N/A
Next unlocked milestone:
```

This prevents a future Codex session from guessing what “finished” meant.

---

## 9. Final North Star acceptance

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
