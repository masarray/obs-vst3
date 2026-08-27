# VST3 Rack — New Thread Handoff

> **READ THIS FIRST IN A NEW RACK THREAD.**

This file exists so a fresh AI/coding thread can become productive without reconstructing project history from chat.

---

## 1. Current product direction

The public product is a stable isolated **Single VST3 audio-effect host for OBS**.

The next major target is **VST3 Rack v2**:

```text
OBS source
-> VST3 Rack filter
-> bounded Rack IPC
-> isolated Rack helper
-> ordered serial VST3 effects
-> OBS output
```

The Rack helper also owns the dedicated graphical Rack Editor.

Core product promise:

**third-party VST3 failures must not be trusted with the stability of `obs64.exe`; normal audio work stays bounded; the user's Rack/state is recoverable; complex Rack workflow does not depend on OBS private GUI internals.**

Do not turn v2 into a DAW, patchbay or MIDI workstation.

---

## 2. Mandatory read order

Before planning/coding/reviewing, read exactly:

1. `/AGENTS.md`
2. `/docs/CODEX_EXECUTION_CONTRACT.md`
3. Rack section of `/docs/NORTH_STAR_PRD.md`
4. `/docs/rack/ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`
5. `/docs/rack/ADR-0003-ISOLATED-RACK-EDITOR.md`
6. `/docs/rack/VST3_RACK_RESEARCH.md`
7. `/docs/rack/RACK_EDITOR_SPEC.md`
8. `/docs/rack/VST3_RACK_EXECUTION_SPEC.md`
9. `/docs/rack/VST3_RACK_TICKETS.md`
10. `/docs/rack/CURRENT_STATUS.md`
11. current GitHub parent/ticket assigned to this thread
12. actual repository code at the exact fixed point declared by the thread

Repository docs—not prior chat—are architecture authority.

If older text says “stock OBS Properties is the full Rack editor,” ADR-0003 supersedes that historical UI implementation decision. OBS Properties is now only the thin launcher/status surface.

---

## 3. Current locked Rack architecture

### Process topology

```text
OBS64.EXE
  VST3 Rack filter
     |
     | bounded audio/control IPC
     v
obs-safe-vst3-rack-host.exe
  |-- helper control/UI owner
  |    |-- graphical RackEditorWindow
  |    |-- plugin browser/presets
  |    `-- native vendor editor window manager
  |
  `-- Rack DSP worker
       Slot A -> Slot B -> ... -> Slot N
```

### Hard rules

- separate OBS `VST3 Rack` filter;
- separate Rack helper executable;
- separate Rack protocol; never generalize Single protocol into Rack protocol;
- one Rack helper process per Rack filter;
- all hosted VST3 + vendor GUI remain outside `obs64.exe`;
- graphical Rack Editor also lives outside `obs64.exe`;
- serial chain only in v2;
- maximum qualification scope 8 slots, mono/stereo Float32;
- preallocated ping-pong serial processing;
- immutable chain generation swap at safe block frontier;
- whole-block wet validity: any active-chain failure -> original dry block;
- stable slot IDs independent of order;
- failure breadcrumb before vendor work;
- repeated correlated failure may quarantine slot;
- Missing/Quarantined slot remains visible/pass-through;
- automatic Session Snapshot != named reusable Rack Preset.

### GUI boundary

OBS Properties:

```text
VST3 Rack
Broadcast Vocal
Ready · 4 effects · 128 samples
[ Open Rack ]
```

Rack Editor owns:

- search/add;
- drag reorder + Move Up/Down fallback;
- replace/remove/bypass;
- health/latency;
- vendor UI commands;
- preset browser/management.

No private OBS Qt/widget injection.

### Default graphical toolkit

After UI-0 proof, default Windows v2 candidate is **Dear ImGui + Win32 + DirectX 11**, pinned exact version/commit, helper-only.

JUCE is architecture/reference material, not the default dependency. Current JUCE licensing requires an explicit licensing/dependency decision before public use.

---

## 4. Existing Single Host facts relevant to R0

Current code already has:

- isolated helper process;
- isolated scanner;
- helper-owned native vendor editor HWND/IPlugView path;
- Windows helper message pump;
- bounded shared-memory audio transport;
- separate control/DSP health concepts;
- VST3 lifecycle/restart compatibility work;
- parameter handling;
- complete component/controller state snapshot type;
- atomic Single state replacement;
- watchdog/recovery/fail-dry behavior;
- Windows packaging and OBS compatibility workflows.

Critical R0 seam:

```text
Single AudioSlot adapter
        |
        v
protocol-neutral ProcessBlockView
        |
        v
HostedPlugin / proven VST3 lifecycle engine
```

Current `Vst3Engine::process(AudioSlot&)` coupling must be expanded safely without changing Single protocol or observable behavior.

---

## 5. Current ticket is REG-0 — do not code Rack yet

Historical contract says Rack follows Single v1.0 lock; product direction now wants Rack next.

Current open ticket #57 is:

**REG-0 — Prove and authorize the Rack extraction baseline.**

REG-0 must end with exactly one result:

### GO

- exact fixed-point SHA;
- exact baseline CI/compat evidence;
- extraction seam characterization;
- explicit accepted phase-order clarification;
- mandatory Single regression set named;
- next ticket = UI-0.

or:

### BLOCKED

- exact missing prerequisite(s);
- why each is safety-critical;
- minimum prerequisite ticket(s);
- no Rack production code started.

Do not start UI-0/R0 in the REG-0 thread.

---

## 6. After REG-0 GO: UI-0 first

UI-0 exists to prevent a late GUI dependency surprise.

It proves only:

- pinned Dear ImGui source/license evidence;
- Win32 + D3D11 helper-only window;
- 3 dummy Rack cards;
- search input;
- drag reorder emits command event only;
- repeated open/close/reopen clean;
- no VST3 loading;
- OBS module dependencies unchanged;
- Single/minimum-current OBS compatibility green.

UI-0 result:

- `GO IMGUI`; or
- `BLOCKED` with bounded fallback ADR.

Stop after UI-0. R0-1 starts fresh.

---

## 7. Engineering loop for every ticket

```text
1. Establish exact main fixed-point SHA.
2. Read authoritative docs + current ticket.
3. Run/inspect baseline tests relevant to the seam.
4. Research API/dependency behavior only where uncertain.
5. Restate behavior, non-goals, failure modes and test seam.
6. Add smallest deterministic failing test first where feasible.
7. Implement minimum production change.
8. Run focused test.
9. Run surrounding Single/Rack regressions.
10. Refactor only while all tests stay green.
11. Review diff from fixed point:
    a. Standards/invariants review
    b. Spec/ticket review
12. Resolve every finding.
13. If head changes, repeat required final review/qualification.
14. Run exact-head CI/compat/manual gates required by ticket.
15. Merge only unchanged qualified head.
16. Record evidence in parent #56 + child ticket.
17. STOP. Start next ticket in a fresh thread.
```

Bug loop:

```text
reproduce -> minimise -> hypothesise -> instrument -> fix -> permanent regression test
```

---

## 8. Standards review checklist

Every applicable runtime/UI PR must answer YES with evidence:

- third-party VST3 remains outside `obs64.exe`;
- Rack graphical dependency remains outside OBS module;
- Single protocol unchanged unless explicitly approved;
- Rack protocol separate;
- OBS `filter_audio` has no vendor lifecycle/UI/state/scan/filesystem/process work;
- Rack DSP has no GUI/D3D/filesystem/preset work;
- normal DSP project-owned processing allocation-free;
- waits bounded;
- invalid/unavailable wet fails dry;
- topology mutation off realtime;
- published Rack generation coherent;
- stable slot IDs survive reorder;
- state generation coherent across topology + complete slot states;
- Missing/Quarantined placeholders preserved;
- crash attribution evidence-based;
- editor uses command/snapshot model, not direct DSP mutation;
- UI thread owns no lock required by DSP;
- OBS Properties ownership respected;
- scanner has no unsafe in-process vendor fallback;
- Single regression contract green;
- exact artifact provenance identifies source head.

Any NO/UNKNOWN => do not merge.

---

## 9. UI command/snapshot rule

The Rack Editor is not another mutable Rack model.

```text
User input
-> RackCommand
-> RackControlPlane validates/commits
-> coherent Rack generation/state
-> immutable RackUiSnapshot
-> editor renders authoritative result
```

Use stable IDs + command IDs. No raw runtime pointers in UI command payloads.

Do not permanently reorder/add/replace optimistically before acknowledgement. Pending UI is allowed; authoritative snapshot wins.

Vendor editors remain floating helper-owned native windows in v2; do not embed them in cards.

---

## 10. Scope guardrails

### Rack v2 includes

- isolated graphical Rack Editor;
- serial VST3 audio effects;
- add/insert/remove/replace/reorder;
- per-slot bypass;
- real floating vendor UI;
- searchable plugin browser;
- total latency;
- automatic Rack Session Snapshot;
- recovery/quarantine semantics;
- named Rack Preset Library;
- thin OBS `Open Rack` launcher/status surface;
- up to 8 slots qualification.

### Rack v2 does NOT include

- arbitrary graph/cables;
- parallel routing;
- sidechain;
- MIDI;
- VST3 instruments;
- nested racks;
- direct audio-device I/O;
- embedded vendor editors;
- private OBS Qt integration;
- per-slot processes;
- Float64;
- arbitrary multichannel;
- cross-platform Rack.

If implementation starts needing one of these, stop and inspect scope drift.

---

## 11. External research already done

Do not repeat broad competitor research unless a ticket creates a new uncertainty.

- **atkAudio PluginHost2:** take thin OBS launcher + dedicated host-window workflow; do not copy in-process process topology or source.
- **Kushview Element:** take processor/topology/render-plan separation, stable IDs, window management, buffer/latency discipline; do not copy free-form graph product shape.
- **Cantabile:** take Rack-as-black-box, simple default signal flow and reusable state/preset thinking.
- **Gig Performer:** take separation between configuration and live-performance concerns.
- **Carla:** learn from rack/patchbay/bridging; do not add routing/per-slot IPC breadth before evidence.
- **Dear ImGui:** preferred permissive helper-only graphical candidate after UI-0.

See `VST3_RACK_RESEARCH.md` before researching again.

---

## 12. Dependency spine

```text
REG-0
  -> UI-0
  -> R0-1 protocol-neutral process seam
  -> R0-2 HostedPlugin extraction
  -> R1-1 two-plugin separate Rack helper
  -> R1-2 bypass + latency + whole-block fail-dry
  -> R1-3 immutable topology generations
  -> R1-4 crash breadcrumb + restart
  -> R2-1 Rack Session Snapshot
       |-> R2-2 missing/quarantine recovery
       `-> R2-3 Preset Save/Load reuse
  -> R3-0 production Rack Editor shell + command/snapshot bridge
  -> R3-1 OBS thin launcher/status filter
  -> R3-2 graphical slot editing + plugin browser
  -> R3-3 vendor editor orchestration
  -> R3-4 complete preset management UX
  -> R4-1 deterministic runtime/UI stress
  -> R4-2 package + OBS + commercial compatibility
  -> R5-1 v2.0 exact-head lock
```

Use `VST3_RACK_TICKETS.md` for acceptance/non-goals.

---

## 13. Copy/paste prompt for the next fresh thread

> Continue `masarray/obs-vst3`, target Safe VST3 Rack v2. Treat repository docs—not prior chat—as architecture authority. Read `AGENTS.md`, `docs/CODEX_EXECUTION_CONTRACT.md`, Rack section of `docs/NORTH_STAR_PRD.md`, then all `docs/rack/` files in the read order from `THREAD_HANDOFF.md`. Open parent issue #56 and work only the currently unblocked child ticket. Establish exact current `main` SHA and baseline evidence before changing anything. Use the pre-agreed deterministic test seam, implement only the minimum vertical behavior, keep the Single regression contract green, review Standards + Spec from the fixed point, and exact-head qualify before merge. Stop after that ticket. If current ticket is #57 REG-0, do evidence/ADR only—no UI-0, R0 or Rack production code.

---

## 14. End-of-thread evidence format

```text
Ticket:
Fixed-point base SHA:
Final source SHA:
Files changed:
Behavior proved:
Tests added:
Focused tests executed:
Regression tests executed:
CI/compat run IDs:
Manual evidence (if required):
Review findings resolved:
Known blockers:
Next unblocked ticket:
```

No “looks good” completion without exact evidence.
