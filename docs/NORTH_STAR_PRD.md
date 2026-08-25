# OBS Safe VST3 Host — North Star Product & Engineering PRD

**Status:** Proposed source-of-truth roadmap  
**Scope:** Windows x64 first; Single Host → Safe Rack → global professional hardening  
**Audience:** Maintainers, AI coding agents, reviewers, release testers, contributors  
**Primary repository:** `masarray/obs-vst3`  
**Last updated:** 2026-08-25

---

## 1. Executive decision

OBS Safe VST3 Host will not compete by becoming the largest audio workstation inside OBS.

It will compete by becoming the **safest, simplest, most reliable way to run VST3 audio effects in OBS Studio**.

The product sequence is deliberately locked:

1. **Single VST3 Host** becomes boringly stable, crash-contained, recoverable, easy to install, and easy for a non-expert to operate.
2. That Single Host architecture is **locked as a compatibility contract**.
3. **Safe VST3 Rack** is then built as a separate OBS-facing product surface with its own helper executable and protocol, sharing only deep internal VST3 lifecycle modules.
4. The Rack becomes stable and is locked.
5. Only after both products are stable do we expand toward the North Star: broader compatibility, sidechain/multichannel improvements where justified, cross-platform packages, signed releases, compatibility intelligence, and world-class diagnostics.

This sequencing is non-negotiable unless a new Architecture Decision Record explicitly replaces it.

---

## 2. North Star

> **The global professional OBS VST3 host that users can trust during a live stream.**

A user should be able to install the product, add a filter, select a VST3, open the vendor UI, tune it, close OBS, reopen OBS, and continue exactly where they left off.

If the third-party VST3 crashes, hangs, misbehaves during scanning, opens a broken GUI, rejects state, or misses a realtime deadline:

- OBS remains alive;
- the stream remains alive;
- audio fails open to dry/pass-through where a valid wet result is unavailable;
- the failure is visible and understandable;
- recovery is automatic when safe;
- repeated failures are quarantined instead of causing restart storms;
- the user is never asked to understand helper processes, Class IDs, IPC, cache files, or VST3 lifecycle rules.

### North Star product sentence

**“Use modern VST3 effects in OBS without trusting third-party plug-ins with the stability of OBS itself.”**

---

## 3. Product positioning

### 3.1 What we are

- OBS-native VST3 audio-effect workflow.
- Third-party VST3 code isolated outside `obs64.exe`.
- Simple Single Host for the common case.
- Simple serial Rack for multi-effect chains.
- Crash containment, hang detection, fail-dry behavior, exact state recovery, and diagnostics as first-class product features.

### 3.2 What we are not

- A DAW.
- A free-form node graph in the first Rack release.
- A MIDI workstation.
- An ASIO/CoreAudio routing suite.
- An instrument/sampler host in the first product generations.
- A replacement for vendor plug-in UIs.
- A security sandbox for untrusted malware.

### 3.3 Competitive strategy

#### atkAudio

atkAudio is feature-rich and mature: VST hosting, sidechain, MIDI, device I/O, multi-plugin graphs, routing and multi-platform support. It is the reference competitor for breadth.

We should learn from its mature workflows and compatibility experience, but we should not copy its product shape. Our differentiation is **OBS-specific simplicity + runtime isolation + deterministic recovery**.

#### Native OBS VST3 work

OBS itself is considering native VST3 hosting. Built-in hosting will naturally win on convenience.

Our long-term reason to exist is therefore not merely “OBS can load VST3”. It is:

- vendor crash isolation;
- hang isolation;
- scanner isolation;
- deterministic fail-dry behavior;
- last-known-good recovery;
- quarantining bad plug-ins;
- compatibility diagnostics.

If native OBS VST3 eventually becomes broadly available, OBS Safe VST3 should remain the **Safe Host / production-risk-reduction option**.

---

## 4. Engineering constitution — invariants that must never regress

These are product laws, not implementation suggestions.

### I1 — Third-party isolation

`obs64.exe` must never load third-party VST3 runtime or vendor GUI code.

### I2 — Realtime callback discipline

OBS `filter_audio` must never perform:

- VST3 lifecycle work;
- editor/UI calls;
- scanning;
- state serialization;
- filesystem access;
- process creation/destruction;
- unbounded waits;
- project-owned heap allocation;
- blocking mutex acquisition.

### I3 — Bounded audio contract

OBS may wait only inside an explicitly bounded realtime budget for an isolated wet result. If the result is invalid, late, unavailable, or the helper is unhealthy, return dry/pass-through audio.

### I4 — Control/DSP separation

Vendor editor/control stalls must not automatically stop healthy DSP progress. DSP ownership and controller/editor ownership remain separated by bounded queues and explicit state/lifecycle frontiers.

### I5 — State consistency

A state snapshot is either a coherent, accepted checkpoint or it is not promoted. Never persist a known mixed controller/processor generation as last-known-good.

### I6 — Recovery is bounded

Recovery uses health classification, bounded retries, exponential backoff, and quarantine. No VST3 can cause an infinite process-spawn loop.

### I7 — Scanner safety

No normal scan path may intentionally load vendor plug-in code into OBS. A failed isolated scan must never offer an “unsafe in-process retry”.

### I8 — Single Host compatibility contract

After Single Host v1.0, Rack work must not expand or redesign the Single Host OBS-facing protocol/lifecycle. Shared code can be deepened behind stable internal seams, but Single Host behavior must remain covered by its v1.0 regression suite.

### I9 — Rack separation

Safe Rack has a separate helper executable and independently versioned protocol. Rack topology changes must not require changes to the Single Host protocol.

### I10 — Bugs become permanent tests

Every reproducible regression that reaches users must produce a regression test at the highest stable seam that can reproduce the behavior before the fix is accepted.

---

## 5. Definition of “stable”

“Stable” is not “it worked on one machine.”

A milestone can be called stable only when all applicable gates below pass.

### 5.1 Safety gates

- Helper process killed repeatedly → OBS stays alive.
- Helper hangs while process remains alive → watchdog detects and recovers/backoffs.
- Vendor GUI/control thread stalls → healthy DSP continues where the architecture promises it can.
- VST `process()` returns error → helper is tainted/recovered; OBS returns dry audio.
- No free IPC slot or missed deadline → dry audio; no unbounded wait.
- Scanner candidate crashes/hangs → scanner parent and OBS survive; other candidates continue.
- Corrupt/oversized state → rejected without invalidating last-known-good.
- Repeated failure → quarantine rather than restart storm.

### 5.2 State gates

- Component state round-trips byte-identically with deterministic fixtures.
- Controller state round-trips where supplied.
- Correct restore ordering is preserved.
- Restore across helper death/recreation returns observable state to the expected value.
- Vendor editor changes are included in the next valid checkpoint.
- State capture while no normal audio is arriving still reaches a coherent frontier.
- Interrupted disk write cannot destroy the only known-good state.

### 5.3 Lifecycle gates

- `setActive`, `setProcessing`, setup/release ordering is deterministic.
- `restartComponent()` supported flags are handled as explicit lifecycle transactions, never ad-hoc calls from realtime code.
- Parameter changes are delivered to processor and mirrored to controller without direct cross-thread controller calls.
- Latency metadata refreshes when the plug-in reports a change.
- I/O-change requests either complete a safe reconfiguration or fail dry with a clear compatibility state.

### 5.4 OBS integration gates

- Add/remove filter while OBS is active.
- Enable/disable rapidly.
- Duplicate source/filter.
- Change scenes repeatedly.
- Save/reload scene collection.
- Close OBS with native editor open.
- Restore a scene whose plug-in is missing.
- Multiple independent filter instances.
- Multiple copies of the same VST3 on separate sources.

### 5.5 Installation gates

- Normal OBS installation.
- Custom OBS root.
- Portable OBS root.
- Steam/custom launcher path where supported.
- Upgrade over prior product version.
- Move from remembered OBS root A to root B without leaving a stale loadable copy in A.
- Uninstall removes only files owned by this product.
- Reinstall restores a valid package.
- Loader probe against the minimum supported OBS runtime.

---

## 6. Current baseline and Gate 0

The current architecture already has the right foundational direction:

- isolated VST3 helper process;
- isolated scanner;
- native vendor editor in helper process;
- bounded shared-memory audio slots;
- dedicated MMCSS DSP worker;
- separate control and DSP heartbeats;
- bounded SPSC parameter transfer;
- component/controller state persistence;
- last-known-good checkpoint;
- watchdog and exponential recovery backoff;
- dry fail-open processing.

### Gate 0 — finish stabilization train

**Current gate:** PR #22, Phase S stabilization.

PR #22 is a stabilization freeze. Do not add Rack, sidechain, new audio formats, new routing, or architecture redesign to it.

**Gate 0 Definition of Done:**

1. Exact PR head passes all CI and compatibility workflows.
2. Review threads are resolved on the exact head.
3. Real OBS machine test verifies the actual DLL/scanner/helper loaded from the expected OBS root.
4. Scan/rescan/editor policy works without surprise vendor GUI opens.
5. Installer path behavior works for the tested normal/custom/portable scenario.
6. Existing state/recovery/watchdog tests remain green.
7. Merge to `main`.
8. Tag a known-good stabilization baseline.
9. No new product feature enters before that baseline exists.

**Public trial:** `v0.4.1-stabilize.*` prerelease(s), then one final stabilization build if needed.

---

# PART A — SINGLE HOST TO v1.0

## 7. Single Host target architecture

Conceptually expose one deep module to the OBS adapter:

```text
OBS filter
   |
   | SafePluginSession API
   v
OBS-side session/recovery layer
   |
   | versioned bounded IPC
   v
isolated VST3 helper process
   |-- control/editor owner
   |-- dedicated DSP worker
   |-- HostedPlugin lifecycle module
   |-- state transaction coordinator
   v
third-party VST3 effect
```

### 7.1 Deep seams

The code should converge toward these conceptual seams even if names differ:

- `SafePluginSession` — OBS-side lifecycle facade.
- `HostedPlugin` — helper-side VST3 lifecycle facade.
- `AudioBridge` — realtime bounded audio transport only.
- `ControlBridge` — parameter/editor/restart metadata.
- `StateStore` — versioned/atomic last-known-good persistence.
- `PluginCatalog` — installed identity and scan health.
- `RecoveryPolicy` — pure deterministic health/backoff/quarantine policy.

The goal is not more classes. The goal is **more behavior behind fewer stable interfaces**.

---

## 8. Milestone S1 — VST3 lifecycle compliance

**Target public release:** `v0.5.0-preview.1`

### Goal

Close host lifecycle gaps before adding more product features.

### Required work

1. Build a complete `restartComponent()` transaction handler.
2. Handle at minimum:
   - `kLatencyChanged`;
   - `kParamValuesChanged`;
   - `kParamTitlesChanged`;
   - `kIoChanged`.
3. Define explicit behavior for `kReloadComponent`.
4. Re-evaluate `setComponentState()` semantics against Steinberg host guidance; do not reject otherwise valid restore solely because a plug-in returns an expected non-success value where the spec/FAQ says hosts should tolerate it.
5. Re-query parameter metadata after title/count changes at a safe frontier.
6. Re-query latency after relevant restart flags/state restore.
7. Reconfigure buses only while DSP is quiesced and component lifecycle permits it.
8. Make failed dynamic I/O reconfiguration an explicit compatibility outcome with dry pass-through, not a half-configured running helper.
9. Add deterministic process-context coverage used by common effects.

### Acceptance tests

- synthetic plug-in requests each supported restart flag;
- latency changes propagate to OBS metadata;
- parameter title/value refresh works after restart request;
- I/O change succeeds for a supported mono/stereo transition fixture;
- unsupported I/O change fails dry and never hangs OBS;
- state restore tolerates known legal vendor return behavior;
- no lifecycle call occurs from OBS `filter_audio`.

### Lock condition

No new feature work until lifecycle tests are green on Windows integration CI.

---

## 9. Milestone S2 — Compatibility Lab / Evil VST3

**Target public release:** `v0.6.0-preview.1`

### Goal

Stop discovering host bugs only through commercial plug-ins and real users.

### Build a deterministic internal test VST3

Create a test plug-in family, e.g. `SafeVst3Torture.vst3`, configurable to:

- expose multiple classes in one bundle;
- crash during module/factory probe;
- hang during scanner probe;
- fail component/controller initialization;
- reject selected bus layouts;
- support mono only / stereo only / mono+stereo;
- request dynamic I/O changes;
- request latency changes;
- request parameter metadata/value refresh;
- crash/hang during `process()`;
- return process errors after N blocks;
- stall editor message processing while DSP remains healthy;
- fail `createView()`;
- resize editor aggressively;
- emit large state;
- emit corrupt/non-repeatable state for negative tests;
- reject state restore;
- create slow state capture;
- change parameters from vendor UI and processor output queues;
- report many parameters near supported limits;
- simulate plug-in upgrade with same ClassID and changed path/version.

### Why this is mandatory

Every future hard bug should be reproduced by adding a mode to this deterministic fixture where possible. The fixture becomes our compatibility laboratory and regression corpus.

### Acceptance tests

- scanner chaos suite;
- helper chaos suite;
- editor chaos suite;
- state chaos suite;
- repeated kill/hang loop;
- no orphan probe/helper processes after test completion;
- no OBS-side crash in integration harness.

---

## 10. Milestone S3 — Scanner intelligence, identity and quarantine

**Target public release:** `v0.7.0-beta.1`

### Goal

Make scanning and plug-in selection feel immediate and safe for non-experts.

### Plugin identity model

Treat VST3 **ClassID as the primary logical identity**. Path is a resolution location, not the sole identity.

Store enough metadata to diagnose and relink:

- ClassID;
- display name;
- vendor;
- version string if available;
- bundle path;
- bundle fingerprint/hash metadata appropriate for cost;
- last successful probe time;
- last failure type;
- failure count;
- quarantine state;
- editor capability result;
- supported tested bus layout metadata when known.

### Scanner behavior

1. Publish cached installed list immediately.
2. Discover filesystem bundles quickly without loading vendor code in OBS.
3. Probe candidates in isolated children.
4. Preserve valid previous identity on transient probe failure.
5. Remove genuinely missing bundles from active catalog while retaining enough history for relink diagnostics.
6. Same ClassID found at a new path after update → prefer a safe relink flow rather than silently treating it as an unrelated plug-in.
7. Duplicate ClassID → deterministic policy and visible diagnostic.
8. Never fall back to in-process scanning.

### Quarantine

A repeatedly failing candidate/session transitions through explicit states:

- `Ready`
- `Recovering`
- `Backoff`
- `Quarantined`
- `Missing`
- `Incompatible`

Quarantine is recoverable through a user action such as **Retry Safely**.

### User-visible copy

Prefer understandable messages:

- “Plug-in stopped responding; audio is passing through.”
- “Plug-in disabled for safety after repeated failures.”
- “Plug-in moved or was updated; matching installation found.”

Do not expose ClassID/IPC details in primary UX.

---

## 11. Milestone S4 — Fool-proof UX, installer and diagnostics

**Target public release:** `v0.8.0-beta.1`

### Goal

A non-technical OBS user should succeed without reading installation documentation.

### OBS properties UX

Keep the normal surface small:

1. Plug-in selector.
2. Rescan.
3. Custom Browse as secondary path.
4. Status line.
5. Open Plug-in Interface.
6. Sidechain selector only when sidechain phase is enabled.
7. Advanced/Diagnostics collapsed by default.

### Status model

One primary status, e.g.:

- Ready
- Loading
- Recovering
- Passing through
- Quarantined
- Missing
- Incompatible

### Diagnostics button

Add **Copy Diagnostics** or **Save Diagnostic Report** containing:

- product version;
- exact git commit/build ID;
- loaded OBS module path;
- helper path;
- scanner path;
- cache path;
- OBS version;
- Windows version/architecture;
- VST3 name/vendor/version/ClassID/path;
- helper health state;
- last recovery reason;
- deadline miss counters;
- last scanner result;
- state checkpoint generation/status;
- no secrets or unrelated user files.

No automatic telemetry is required for v1.0. Privacy-friendly manual diagnostics are the default.

### Installer requirements

- Detect/validate actual OBS root containing `bin\64bit\obs64.exe`.
- Refuse clearly invalid targets.
- Detect OBS running and ask user to close it before replacing binaries.
- Clean historical copies owned by this product, including previously remembered root where safe.
- Do not delete unrelated OBS plug-ins.
- Install/uninstall/reinstall automation must run in CI.
- Package includes exact version/build ID.
- Portable ZIP layout is validated automatically.

---

## 12. Milestone S5 — Single Host sidechain and compatibility beta

**Target public release:** `v0.9.0-beta.1`, then `v0.9.0-rc.1`

### Goal

Add the highest-value remaining Single Host capability only after the crash/state/lifecycle foundation is mature.

### Scope

- One main input bus.
- One main output bus.
- At most one aux/sidechain input bus in v1.0.
- Mono/stereo only.
- Float32 only.
- Source enumeration limited to valid OBS audio sources.
- Missing/deleted sidechain source degrades gracefully.
- Sidechain timing is explicitly tested.

### Architectural rule

Sidechain data must extend a bounded audio contract; it must not introduce OBS callbacks that perform vendor lifecycle work or unbounded synchronization.

### Acceptance tests

- no sidechain plug-in;
- mono sidechain;
- stereo sidechain;
- source disappears while running;
- scene/source rename;
- sidechain selection persists across restart;
- source timing drift is bounded/handled according to the chosen design;
- helper failure still returns main input dry.

### Release-candidate freeze

At `v0.9.0-rc.1`:

- no feature additions;
- only release blockers and regression fixes;
- every fix requires a regression test;
- exact RC commit must pass full release qualification.

---

## 13. Milestone S6 — Single Host v1.0 lock

**Public release:** `v1.0.0`

### Definition of Done

Single Host may ship v1.0 only when:

- all constitution invariants are covered by automated tests or explicit manual release tests;
- lifecycle compliance milestone is complete;
- torture plug-in suite is running in CI;
- scanner failures cannot crash/block OBS;
- state survives helper death/recreation;
- repeated helper failure enters bounded backoff/quarantine;
- installer has real install/uninstall/reinstall coverage;
- minimum supported OBS runtime loader test passes;
- current supported OBS runtime integration test passes;
- representative real-world plug-in compatibility matrix has been manually exercised;
- release assets have SHA-256 hashes;
- release build provenance is attested;
- release notes clearly state supported scope and known limitations.

### What “lock” means

After v1.0:

- Single Host public workflow remains simple.
- Single Host protocol is changed only when required for a Single Host feature/bug and must remain backward-safe where practical.
- Rack development cannot repurpose Single Host protocol.
- No broad refactor may land merely because Rack needs different semantics.
- v1.0 regression suite becomes a mandatory required check for all future runtime changes.

---

# PART B — SAFE RACK TO v2.0

## 14. Rack product definition

Safe Rack is a separate OBS filter for an ordered serial effects chain.

Example:

```text
Mic
  ↓
[ Denoise ]
  ↓
[ EQ ]
  ↓
[ Compressor ]
  ↓
[ Limiter ]
  ↓
OBS
```

V1 Rack is not a free-form graph.

Each slot has:

- stable slot ID;
- VST3 identity;
- bypass state;
- health state;
- latency;
- full component/controller state;
- Open Plug-in Interface action;
- replace/remove/reorder actions.

Missing/failed/quarantined slots pass audio through and remain visible.

---

## 15. Rack architecture

```text
OBS Safe Rack filter
        |
        | separate rack protocol
        v
obs-safe-vst3-rack-host.exe
        |
        |-- Rack Runtime
        |-- Slot 1 HostedPlugin
        |-- Slot 2 HostedPlugin
        |-- Slot N HostedPlugin
        |-- rack control/editor coordinator
        |-- rack DSP worker
        v
serial preallocated processing chain
```

### v2.0 isolation level

The complete rack runs inside one isolated rack process.

Reason:

- OBS remains protected from vendor crashes;
- avoids IPC/context-switch between every plug-in;
- keeps Rack v1 performance predictable;
- one crashing slot can be identified by publishing the currently-processing slot ID before vendor `process()`.

On repeated recovery associated with one slot:

1. quarantine suspect slot;
2. restart rack helper from last-known-good rack snapshot;
3. keep suspect slot visible but bypassed;
4. continue remaining chain.

Per-slot worker-process isolation is a later optional “maximum isolation” mode only if real-world evidence justifies its CPU/latency cost.

---

## 16. Milestone R0 — Extract reusable HostedPlugin seam

**Target release:** `v2.0.0-alpha.1`

### Goal

Make Single Host and Rack reuse deep VST3 lifecycle/state behavior without sharing OBS-facing protocols.

### Rules

- Start from v1.0 fixed point.
- Write characterization/contract tests first.
- Extract only behavior needed by both products.
- Single Host tests must remain byte/behavior compatible.
- Do not generalize prematurely for MIDI, graphs, devices, or formats outside scope.

### Gate

If extraction changes observable Single Host behavior without an intentional approved spec change, stop and fix before Rack work continues.

---

## 17. Milestone R1 — Serial Rack runtime tracer bullet

**Target release:** `v2.0.0-alpha.2`

### First vertical slice

Two deterministic test plug-ins in serial:

`Gain A → Gain B`

Prove through the real rack runtime seam:

- ordered processing;
- bypass;
- add/remove;
- reorder;
- total latency;
- missing slot pass-through;
- one helper process crash → OBS dry/pass-through;
- rack helper restart.

Do not build the final UI first.

### Processing design

- preallocated ping-pong buffers;
- no allocation during normal DSP block processing;
- topology change happens off realtime path;
- build next immutable/owned chain state on control plane;
- swap only at a safe frontier;
- while reconfiguring, OBS remains bounded and fail-open.

---

## 18. Milestone R2 — Rack persistence, recovery and quarantine

**Target release:** `v2.0.0-beta.1`

### Session Snapshot

Automatic crash-safe persistence of the current Rack:

- ordered slots;
- stable slot IDs;
- bypass state;
- plug-in identity;
- opaque component/controller state per slot;
- rack metadata;
- versioned format;
- atomic write `temp → replace`;
- last-known-good backup.

### Rack Preset

Explicit user-named portable chain.

Separate concept from automatic Session Snapshot.

### Recovery tests

- helper dies between slots;
- same slot repeatedly crashes;
- slot quarantined and remaining chain restored;
- missing plug-in on preset load;
- corrupt primary snapshot recovers from backup;
- controller state for each slot restores independently.

---

## 19. Milestone R3 — Rack UX

**Target release:** `v2.0.0-beta.2`

### UX model

A vertical signal lane, not cables/nodes.

Each compact card shows:

- plug-in name;
- Ready / Missing / Failed / Quarantined;
- bypass;
- latency;
- Open UI;
- drag handle;
- remove/replace.

Between cards: `+ Add effect` insertion point.

### Primary workflows

- create empty rack;
- add effect;
- insert effect between two slots;
- reorder;
- bypass;
- remove;
- open vendor UI;
- save Rack Preset;
- load Rack Preset;
- recover a quarantined slot;
- replace a missing slot.

The user should never need to draw a graph to build a normal vocal chain.

---

## 20. Milestone R4 — Rack stress, compatibility and RC

**Target release:** `v2.0.0-rc.1`

### Required stress cases

- 1, 2, 4, 8 serial deterministic effects;
- repeated reorder while audio active;
- add/remove loop;
- vendor UI open/close loop across slots;
- one slow plug-in;
- one crashing plug-in;
- one hanging plug-in;
- slot state save during active processing;
- rack helper killed repeatedly;
- OBS scene collection reload with multiple racks;
- multiple Rack filters on different sources.

### RC freeze

No new capability after RC except blocker fixes.

---

## 21. Milestone R5 — Rack v2.0 lock

**Public release:** `v2.0.0`

Rack v2.0 is stable when:

- serial chain runtime is deterministic;
- slot failure cannot crash OBS;
- bad slot can be identified/quarantined/recovered;
- complete rack session restores automatically;
- presets are versioned and validated;
- missing plug-ins preserve document topology;
- installer/packages include both Single Host and Rack companions correctly;
- Single Host v1.0 regression gate remains green.

---

# PART C — NORTH STAR GLOBAL PRODUCT

## 22. Post-v2 capability order

Do not start all of these in parallel. Priority is evidence-driven.

Recommended order:

1. **Compatibility intelligence** — richer known-good/known-bad local metadata, easy diagnostics, community issue templates.
2. **Authenticode signing** and stable publisher identity when certificate/process is available.
3. **OBS latest-version qualification lane** separate from minimum-version compatibility.
4. **Broader channel/layout support** only where OBS/user demand is real.
5. **Float64 fallback** if compatibility data proves meaningful benefit.
6. **Cross-platform runtime**: macOS, then Linux, each treated as its own product-quality project rather than a compile checkbox.
7. **Optional advanced Rack features** only after serial Rack remains simple.

### Explicitly deferred

- MIDI instruments.
- arbitrary graph routing.
- direct device I/O.
- parallel sends/buses.
- nested racks.
- cloud accounts/telemetry dependency.

These can be reconsidered only if user evidence shows they strengthen the Safe Host mission rather than turning the product into another DAW.

---

# PART D — GITHUB ACTIONS AS THE QUALITY FACTORY

## 23. Workflow architecture

GitHub Actions should become the primary deterministic quality factory.

Use reusable workflows to prevent CI logic drift.

Recommended structure:

```text
.github/workflows/
  ci-fast.yml
  ci-windows-integration.yml
  ci-obs-compat.yml
  ci-vst3-compliance.yml
  ci-package.yml
  ci-security.yml
  nightly-chaos.yml
  nightly-obs-latest.yml
  release-candidate.yml
  release-stable.yml
  _build-windows.yml
  _test-runtime.yml
  _package-windows.yml
```

Names are illustrative; migration should be incremental.

---

## 24. PR required checks — fast feedback lane

Target: useful feedback quickly enough that AI agents do not keep coding on top of a broken assumption.

Required on every runtime PR:

- configure/build portable tests;
- protocol/layout tests;
- state codec tests;
- recovery policy tests;
- SPSC tests;
- scanner pure/discovery tests;
- deterministic lifecycle tests that do not need OBS;
- formatting/static compiler warnings gate;
- Windows helper/scanner build;
- test VST3 build;
- targeted torture scenarios related to changed modules.

Use workflow concurrency with `cancel-in-progress: true` for superseded PR heads.

---

## 25. Windows integration lane

Required when touching OBS adapter, IPC, host, scanner, installer, state, or packaging.

Run:

- actual helper/scanner binaries;
- real shared-memory IPC;
- helper death/recreation;
- live-but-hung helper;
- control stall while DSP remains healthy;
- state restore through real bridge;
- Torture VST3 modes;
- OBS module compilation;
- loader probe.

Where practical, add MSVC AddressSanitizer builds for deterministic native tests.

---

## 26. OBS compatibility matrix

Maintain two distinct concepts.

### Required compatibility floor

Pinned oldest supported OBS runtime, e.g. the currently declared 29.1.x floor until policy changes.

The built DLL must actually load against that runtime. Advertising an ABI version without a real loader probe is insufficient.

### Current supported OBS

Pinned known-current stable OBS version used for normal release build/integration qualification.

### Nightly latest lane

A non-blocking scheduled workflow checks newest supported OBS release or selected OBS development branch.

Purpose:

- detect upcoming OBS breakage early;
- do not make every PR depend on a moving external target.

---

## 27. VST3 compliance lane

Use Steinberg SDK test assets as part of host validation where applicable:

- Host Checker VST3 to inspect host call behavior;
- SDK validator/test host for our deterministic test plug-ins;
- Editor Host examples as reference for platform editor behavior.

Important: vendor compatibility remains broader than SDK compliance. Both are required.

---

## 28. Chaos/nightly lane

Nightly tests are allowed to be slower and more aggressive.

Examples:

- kill helper 100 times;
- hang helper at different lifecycle phases;
- scanner crash/hang corpus;
- repeated state capture/restore;
- 10,000 deterministic process blocks;
- repeated editor open/close/resize;
- rapid scene/filter lifecycle harness where automation permits;
- leak/orphan-process detection;
- Rack topology mutation stress after Rack exists.

Nightly failure opens or updates one tracking issue rather than creating spam for every run.

---

## 29. Package qualification lane

Every release candidate must test the actual public package, not merely loose build outputs.

Required:

1. Build binaries.
2. Build Smart Installer and portable ZIP.
3. Validate file layout.
4. Install to synthetic/realistic OBS root.
5. Probe installed module against supported OBS runtime.
6. Verify helper/scanner are found from installed layout.
7. Uninstall.
8. Verify owned files removed.
9. Reinstall using remembered target.
10. Test target migration A → B.
11. Generate SHA-256 manifest.
12. Generate build provenance attestation.
13. Generate SBOM/third-party dependency record where practical.

---

## 30. Supply-chain/security automation

For a public binary product, add progressively:

- CodeQL for C/C++ and GitHub Actions workflow analysis;
- dependency review for PR dependency changes;
- Dependabot for GitHub Actions where useful;
- pin critical third-party build dependencies/submodules to reviewed commits/tags;
- GitHub artifact attestations for release binaries;
- SBOM attestation for stable releases;
- least-privilege workflow permissions;
- release jobs with write permission separated from ordinary test jobs;
- immutable exact commit/tag displayed in every binary/package diagnostic.

Do not treat provenance as proof of safety. It proves where/how an artifact was built; runtime quality is still established by the test gates in this document.

---

# PART E — PUBLIC RELEASE STRATEGY

## 31. Every milestone produces a public trial

Do not wait until v1.0/v2.0 to get real-world plug-in coverage.

### Channels

#### Preview

For a new technical capability.

Audience: technical testers willing to provide logs.

Examples:

- `v0.5.0-preview.1`
- `v2.0.0-alpha.1`

#### Beta

Architecture is settled; compatibility/UX hardening remains.

Examples:

- `v0.7.0-beta.1`
- `v2.0.0-beta.1`

#### Release Candidate

Feature freeze.

Examples:

- `v0.9.0-rc.1`
- `v2.0.0-rc.1`

#### Stable

Only after release qualification + public feedback shows no release blocker.

Examples:

- `v1.0.0`
- `v2.0.0`

### Rule

A prerelease is not a license to ship known-dangerous architecture. Core safety invariants apply to every public build.

---

## 32. Compatibility reporting

Create issue forms that ask users for structured fields:

- OBS version;
- Windows version;
- product version/build SHA;
- VST3 product/vendor/version;
- scan/load/DSP/GUI/state/recovery category;
- whether OBS crashed;
- whether audio failed dry;
- Copy Diagnostics output;
- reproduction steps.

Use labels such as:

- `area/scanner`
- `area/runtime`
- `area/editor`
- `area/state`
- `area/installer`
- `area/rack`
- `severity/obs-crash`
- `severity/audio-loss`
- `compat/vendor`
- `regression`

This makes public trials feed an analyzable compatibility corpus rather than free-form anecdotes.

---

# PART F — MATT POCOCK–STYLE AI ENGINEERING MODE

## 33. Working model

This roadmap adopts the useful discipline from Matt Pocock's engineering skills:

- research uncertainty instead of guessing;
- build a shared spec/domain language;
- break large plans into dependency-aware tracer bullets;
- implement through TDD at stable seams;
- review the diff against a fixed point before commit/merge;
- use a wayfinder/decision-ticket approach for work too large for one agent session.

The repository does not need to depend on a particular AI vendor or skill runtime for these rules to work.

---

## 34. AI execution protocol — mandatory sequence

An AI coding agent must not take “implement milestone S3” as one coding task.

For every capability:

### Step 1 — Establish fixed point

Record:

- base branch;
- base commit SHA;
- parent milestone/ticket;
- exact tests currently green;
- files/modules expected to be touched.

### Step 2 — Research before design when uncertain

If behavior depends on:

- VST3 specification;
- OBS API/lifecycle;
- Windows process/thread semantics;
- vendor behavior;
- existing repository implementation;

inspect authoritative docs/code first.

Do not invent an API behavior from memory.

### Step 3 — Write one tracer-bullet ticket

The ticket must state:

- user/system behavior;
- scope;
- non-goals;
- seam under test;
- failure modes;
- acceptance tests;
- dependency/blocking edges.

### Step 4 — Red

Create the smallest failing test reproducing the desired behavior or bug at the highest stable seam.

If no automated seam exists, first create a deterministic harness rather than adding production behavior blindly.

### Step 5 — Green

Implement the minimum production change that makes the new test pass without breaking previous gates.

### Step 6 — Refactor

Improve design only while all tests remain green.

Do not combine unrelated cleanup with the tracer bullet.

### Step 7 — Code review

Review from the fixed point on two axes:

1. **Correctness/standards** — races, lifecycle, ownership, error paths, realtime behavior, OBS/VST contract.
2. **Architecture** — did the change deepen a module or leak more complexity across seams?

### Step 8 — Exact-head CI

Only the exact reviewed head is eligible for merge/release.

If new commits are pushed after review, required review/tests are repeated as appropriate.

### Step 9 — Merge and update roadmap

Close the tracer ticket, update parent milestone progress, and record any new decision/risk discovered.

---

## 35. Bug protocol

For difficult bugs use this loop:

```text
reproduce
  ↓
minimise
  ↓
hypothesise
  ↓
instrument
  ↓
fix
  ↓
regression test
```

Never start by rewriting the subsystem.

A fix without a reproducer/test is allowed only for an emergency where automation is technically impossible; the PR must document why and add the nearest feasible guard test.

---

## 36. Architecture decision protocol

Use an ADR when a change affects one of these:

- process isolation boundary;
- Single vs Rack protocol;
- realtime waiting model;
- state persistence format;
- plug-in identity model;
- recovery/quarantine policy;
- supported OBS/VST3 compatibility floor;
- major third-party framework/dependency;
- cross-platform UI/runtime strategy.

An ADR contains:

- context;
- decision;
- alternatives considered;
- consequences;
- migration plan;
- tests that protect the decision.

Do not bury architectural decisions only inside chat history.

---

## 37. Ticket template

```markdown
# <Milestone.ticket> — <behavioral title>

Parent: #<epic>
Depends on: #<blocking tickets>
Fixed point: <commit SHA>

## Goal
One observable vertical behavior.

## User/system outcome
What becomes true when this lands?

## In scope
- ...

## Non-goals
- ...

## Design constraints
- isolation invariant
- realtime invariant
- state invariant

## Test seam
What public/stable seam proves the behavior?

## Failing test first
- ...

## Acceptance
- [ ] behavior test
- [ ] failure-path test
- [ ] old regression gates green
- [ ] exact-head review green

## Release impact
Which preview/beta/RC consumes this ticket?
```

---

## 38. PR size rule

Prefer one behavioral tracer bullet per PR.

A PR should be easy to answer:

> “What new externally observable behavior is proven by this diff?”

Avoid giant mixed PRs containing installer + scanner + DSP + UI + unrelated refactors unless the work is explicitly a stabilization integration train with a fixed checklist.

---

# PART G — REPOSITORY AND RELEASE GOVERNANCE

## 39. Branch strategy

- `main` is always releasable or intentionally prerelease-ready.
- Feature/fix branches are short-lived.
- Stabilization branches are exceptional and have a written exit gate.
- Do not maintain a long-lived Rack branch that diverges for months; land small compatibility-preserving Rack foundations behind separate targets/protocols.

### Required main rules

Use GitHub rulesets/branch protection where available:

- pull request required;
- required status checks;
- block force-push/delete;
- require conversation resolution;
- optionally require fresh approval for sensitive runtime/release changes;
- release tags created only from qualified commits.

---

## 40. Release qualification checklist

Before every public milestone release:

### Source

- [ ] exact commit identified
- [ ] version updated consistently
- [ ] no uncommitted/generated source dependency
- [ ] release notes written

### CI

- [ ] fast PR suite green
- [ ] Windows integration green
- [ ] OBS floor loader test green
- [ ] current OBS integration green
- [ ] VST3 compliance/torture relevant suite green
- [ ] package install/uninstall/reinstall green

### Runtime

- [ ] helper crash → OBS survives
- [ ] helper hang → bounded recovery
- [ ] scanner bad candidate → OBS survives
- [ ] state restore verified
- [ ] native editor open/close tested

### Package

- [ ] installer artifact
- [ ] portable artifact
- [ ] SHA-256 manifest
- [ ] build SHA visible in diagnostics
- [ ] artifact provenance attestation
- [ ] SBOM for stable release when pipeline is ready

### Public trial notes

- [ ] supported OBS versions stated
- [ ] supported VST3 scope stated
- [ ] known limitations stated
- [ ] issue-report link/template stated

---

# PART H — QUALITY METRICS

## 41. Engineering KPIs

Metrics should drive safer decisions, not vanity.

### Safety

- zero test-harness cases where injected vendor crash/hang crashes OBS;
- zero known unbounded waits on OBS audio callback;
- zero unsafe in-process vendor scan fallback;
- all repeated-failure scenarios enter bounded backoff/quarantine.

### Regression prevention

- 100% of confirmed project regressions receive a regression test where technically feasible;
- every public release built from an exact CI-qualified commit;
- every runtime PR passes v1.0 Single Host contract suite after v1.0.

### Compatibility

Track compatibility by stage:

- scan;
- instantiate;
- DSP;
- editor;
- state restore;
- recovery;
- sidechain;
- rack.

A plug-in is not simply “works/doesn't work”. This stage model tells us what to fix.

### UX

Target normal first-use path:

```text
Install → Add Filter → Select Plug-in → Open UI → Tune
```

No manual ClassID or filesystem knowledge for normally installed VST3s.

---

# PART I — RISK REGISTER

## 42. Highest risks

### Risk A — Architecture churn during bug fixing

**Mitigation:** fixed-point debugging, reproducer first, one tracer bullet per PR, ADR for boundary changes.

### Risk B — Commercial plug-in behavior impossible to reproduce in CI

**Mitigation:** Torture VST3 + manual compatibility ring + structured diagnostics + regression fixture whenever behavior can be generalized.

### Risk C — GUI vendor code can hang/crash unpredictably

**Mitigation:** helper-owned UI, separate DSP/control health, UI never prerequisite for DSP readiness, quarantine/recovery.

### Risk D — Rack work destabilizes Single Host

**Mitigation:** separate rack executable/protocol + mandatory v1.0 contract suite + deep reusable HostedPlugin seam only.

### Risk E — CI becomes too slow and agents stop using feedback

**Mitigation:** tiered workflows: fast required PR lane, targeted integration lane, nightly chaos lane, release qualification lane.

### Risk F — Moving OBS versions break compatibility

**Mitigation:** pinned minimum loader test + pinned current stable + non-blocking nightly latest lane.

### Risk G — Installer shadow copies make debugging misleading

**Mitigation:** canonical OBS-root install, old-root cleanup, loaded-module path logging, Copy Diagnostics, package tests.

### Risk H — Feature race against atkAudio/native OBS

**Mitigation:** refuse breadth race. Win on Safe Host mission: crash containment, recovery, simplicity, diagnostics.

---

# PART J — MASTER IMPLEMENTATION ORDER

## 43. Do this in order

```text
GATE 0
PR #22 stabilization
    ↓ lock baseline

S1
VST3 lifecycle / restartComponent compliance
    ↓ public preview

S2
Deterministic Torture VST3 compatibility lab
    ↓ public preview

S3
Scanner identity + health + quarantine
    ↓ public beta

S4
Fool-proof UX + installer + diagnostics
    ↓ public beta

S5
Single Host sidechain + compatibility hardening
    ↓ beta → RC

S6
Single Host v1.0.0
    ↓ ARCHITECTURE CONTRACT LOCK

R0
Extract stable HostedPlugin seam under v1.0 tests
    ↓ alpha

R1
Serial Rack runtime tracer bullet
    ↓ alpha

R2
Rack persistence + recovery + slot quarantine
    ↓ beta

R3
Simple signal-lane Rack UX
    ↓ beta

R4
Rack stress/compatibility/release qualification
    ↓ RC

R5
Rack v2.0.0
    ↓ RACK CONTRACT LOCK

NORTH STAR
Signing → compatibility intelligence → broader layouts/formats
→ macOS → Linux → evidence-driven advanced capabilities
```

Do not skip a lock gate because the next feature is exciting.

---

## 44. Immediate next tickets after Gate 0

When PR #22 is merged and the baseline tag exists, create tickets in this order:

1. **S1.1 — lifecycle behavior matrix and test seam**
2. **S1.2 — tolerant state-restore semantics fixture**
3. **S1.3 — latency changed transaction**
4. **S1.4 — parameter values/titles refresh transaction**
5. **S1.5 — safe I/O changed transaction**
6. **S1.6 — reload-component policy**
7. **S1.7 — release `v0.5.0-preview.1`**
8. **S2.1 — Torture VST3 skeleton**
9. **S2.2 — scanner crash/hang modes**
10. **S2.3 — DSP crash/hang/error modes**
11. **S2.4 — editor failure/stall modes**
12. **S2.5 — state corruption/oversize/rejection modes**
13. **S2.6 — nightly chaos workflow**
14. **S2.7 — release `v0.6.0-preview.1`**

Each ticket is implemented independently through the AI execution protocol in this document.

---

# PART K — RESEARCH BASIS

## 45. Primary references

### Matt Pocock engineering workflow

- https://github.com/mattpocock/skills

Useful concepts adopted here:

- research before guessing;
- spec/domain grounding;
- dependency-aware tickets;
- vertical tracer bullets;
- TDD red/green/refactor;
- diagnosing-bugs loop;
- fixed-point code review;
- wayfinder for work larger than one agent session.

### Steinberg VST3

- https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Hosting.html
- https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Communication.html
- https://github.com/steinbergmedia/vst3sdk

Important design implications:

- host should implement important `restartComponent()` flags;
- ClassID is treated as globally unique;
- UI and processing are distinct responsibilities;
- SDK includes test host/validator assets that should be used as validation references.

### OBS native VST3 direction

- https://github.com/obsproject/obs-studio/pull/12752

Use as a compatibility/UX reference, not as the safety architecture to copy.

### atkAudio

- https://github.com/atkAudio/PluginForObsRelease

Use as a breadth/real-world workflow reference and issue corpus. Do not copy unsafe fallback behavior or product complexity that conflicts with Safe Host positioning.

### GitHub Actions / supply chain

- https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows
- https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations
- https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/manage-your-dependency-security/configure-dependency-review-action

---

## 46. Final product rule

When choosing between two designs, prefer the design that makes this statement more true:

> **A broken VST3 may break itself, but it must not be allowed to take OBS, the stream, or the user's confidence down with it.**

Features are valuable only after this promise remains true.
