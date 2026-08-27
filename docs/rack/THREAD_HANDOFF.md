# VST3 Rack — New Thread Handoff

> **READ THIS FIRST IN A NEW IMPLEMENTATION THREAD.**

This file exists so a fresh AI/coding thread can become productive without reconstructing project history from chat.

---

## 1. Current product direction

The current public product is a safe isolated **Single VST3 audio-effect host for OBS**.

The next major target is **VST3 Rack v2**:

```text
OBS source
-> VST3 Rack filter
-> ordered serial VST3 effects
-> OBS output
```

The v2 Rack must preserve the project's core promise:

**third-party VST3 failures must not be trusted with the stability of `obs64.exe`, normal audio work stays bounded, and the user's Rack/state must be recoverable.**

Do not turn Rack v2 into a DAW, graph host or MIDI workstation.

---

## 2. Mandatory read order

Before planning or coding, read exactly in this order:

1. `/AGENTS.md`
2. `/docs/CODEX_EXECUTION_CONTRACT.md`
   - product finish line;
   - persistence contract;
   - implementation order;
   - runtime invariant checklist;
   - Rack v2 closure evidence.
3. `/docs/NORTH_STAR_PRD.md`
   - Part B / Rack product definition, architecture, R0–R5.
4. `/docs/rack/ADR-0002-RACK-RUNTIME-ARCHITECTURE.md`
5. `/docs/rack/VST3_RACK_RESEARCH.md`
6. `/docs/rack/VST3_RACK_EXECUTION_SPEC.md`
7. `/docs/rack/VST3_RACK_TICKETS.md`
8. the current GitHub parent/ticket assigned to this thread.
9. the actual repository code at the exact fixed point you declare.

Do not use old chat messages as architecture authority when repository docs exist.

---

## 3. Important current architecture facts

### Existing Single Host

Current code already has:

- isolated helper process;
- isolated scanner;
- native vendor editor owned outside OBS;
- bounded shared-memory audio transport;
- separate control/DSP health concepts;
- VST3 lifecycle/restart compatibility work;
- parameter handling;
- complete component/controller state snapshot type;
- atomic Single state file replacement;
- watchdog/recovery/fail-dry behavior;
- Windows packaging and OBS compatibility workflows.

### Critical seam for R0

Current `src/host/vst3_engine.hpp` owns one VST3 instance well, but its processing function is still coupled to Single protocol through `process(AudioSlot&)`.

The intended safe extraction is:

```text
Single AudioSlot adapter
        |
        v
protocol-neutral ProcessBlockView
        |
        v
HostedPlugin / proven VST3 lifecycle engine
```

Rack later consumes the protocol-neutral seam without changing the Single protocol.

### Rack architecture already decided

- separate OBS filter;
- separate Rack helper executable;
- separate Rack protocol;
- one Rack helper process per Rack filter;
- multiple plugin slots inside that helper;
- serial chain only for v2;
- preallocated ping-pong buffers;
- immutable chain generation swap at safe block frontier;
- whole-block wet validity: active-chain failure -> original dry block;
- stable slot IDs independent of order;
- crash breadcrumb before vendor calls;
- repeated slot-correlated failure may quarantine slot;
- Missing/Quarantined slot remains visible/pass-through;
- automatic Session Snapshot and named Rack Preset are different features;
- stock public OBS Properties is the v2 UI baseline;
- maximum qualification scope: 8 serial slots, mono/stereo Float32.

Do not reopen these decisions casually. If evidence proves one is wrong, write/update an ADR instead of silently diverging.

---

## 4. First ticket is REG-0, not Rack coding

The existing normative contract says Rack work follows Single v1.0 lock. Product direction now wants Rack next.

Therefore the first work item is:

**REG-0 — Prove and authorize the Rack extraction baseline.**

REG-0 must either:

- explicitly authorize R0 with evidence and a phase-order clarification; or
- identify the minimum prerequisite gap that must be fixed first.

Do not simply write “Single is good enough.” Cite exact tests, workflow evidence, current code seams and fixed-point SHA.

---

## 5. Engineering loop for every ticket

Use this exact loop:

```text
1. Establish exact main fixed-point SHA.
2. Read authoritative docs + current ticket.
3. Run/inspect baseline tests relevant to the seam.
4. Research API behavior only where uncertain.
5. Restate ticket behavior, non-goals, failure modes and test seam.
6. Add smallest failing deterministic test first where feasible.
7. Implement minimum production change.
8. Run focused test.
9. Run surrounding Single/Rack regression tests.
10. Refactor only while all tests stay green.
11. Review diff from fixed point:
    a. Standards/invariants review
    b. Spec/ticket review
12. Resolve every finding.
13. If source head changed, repeat final review/qualification on new head.
14. Run exact-head CI/compatibility required by the ticket.
15. Merge only the unchanged qualified head.
16. Record evidence in parent/ticket.
17. STOP. Start next ticket in a fresh thread.
```

Bug loop:

```text
reproduce -> minimise -> hypothesise -> instrument -> fix -> permanent regression test
```

---

## 6. Standards review checklist

Every runtime PR must answer YES with evidence where applicable:

- Does third-party VST3 code remain outside `obs64.exe`?
- Is Single protocol unchanged unless the ticket explicitly requires an approved Single change?
- Is Rack protocol separate?
- Is OBS `filter_audio` still free of vendor lifecycle/UI/state/scanning/filesystem/process work?
- Is realtime work allocation-free for project-owned normal Rack DSP?
- Are waits bounded?
- Does unavailable/invalid wet output fail dry?
- Is topology mutation off realtime?
- Is published Rack generation coherent?
- Are slot IDs stable across reorder?
- Is state generation coherent across topology + per-slot component/controller states?
- Are Missing/Quarantined slots preserved as pass-through placeholders?
- Is crash attribution evidence-based rather than guessed?
- Are OBS Properties ownership rules respected?
- Does Single regression contract remain green?
- Does exact artifact provenance identify the actual source head?

If any answer is NO/UNKNOWN, do not merge.

---

## 7. Scope guardrails

### Rack v2 includes

- serial VST3 audio effects;
- add/insert/remove/replace/reorder;
- per-slot bypass;
- real vendor UI;
- total latency;
- automatic Rack Session Snapshot;
- slot-level recovery/quarantine semantics;
- named Rack Preset Library;
- stock OBS Properties workflow;
- up to 8 slots for qualification.

### Rack v2 does NOT include

- graph/cables;
- parallel routing;
- sidechain;
- MIDI;
- VST3 instruments;
- nested racks;
- direct audio device I/O;
- custom Qt Rack editor dependency;
- per-slot processes;
- Float64;
- arbitrary multichannel;
- macOS/Linux.

If implementation starts needing one of these, stop and check whether scope has drifted.

---

## 8. External host lessons already researched

Do not repeat broad competitor research unless the current ticket creates a new uncertainty.

The architecture study already concluded:

- **Kushview Element:** take processor/topology/render-plan separation, stable IDs, buffer planning and latency discipline; do not copy free-form graph UI for v2.
- **Cantabile:** take Rack-as-black-box, simple default signal flow and reusable state/preset product thinking.
- **Gig Performer:** take separation between configuration and live-performance control plus reusable rack concepts.
- **Carla:** learn from Rack/Patchbay/bridging, but treat bridge/routing breadth as added failure surface; do not add per-slot process IPC by default.

See `VST3_RACK_RESEARCH.md` before researching again.

---

## 9. Ticket dependency spine

The intended spine is:

```text
REG-0
  -> R0-1 protocol-neutral process seam
  -> R0-2 HostedPlugin extraction
  -> R1-1 two-plugin separate Rack helper
  -> R1-2 bypass + latency + whole-block fail-dry
  -> R1-3 immutable topology generations
  -> R1-4 crash breadcrumb + restart
  -> R2-1 Rack Session Snapshot
       |-> R2-2 missing/quarantine recovery
       `-> R2-3 Preset Save/Load reuse
  -> R3-1 native OBS Rack filter
  -> R3-2 complete slot controls
  -> R3-3 complete preset CRUD/update UX
  -> R4-1 deterministic stress
  -> R4-2 package + OBS + commercial compatibility
  -> R5-1 v2.0 exact-head lock
```

Use `VST3_RACK_TICKETS.md` for full acceptance/non-goals.

---

## 10. Copy/paste prompt for the next fresh thread

Use this as the first message in a new implementation thread:

> We are continuing `masarray/obs-vst3`. The next product target is Safe VST3 Rack v2. Read `AGENTS.md`, `docs/CODEX_EXECUTION_CONTRACT.md`, Rack sections of `docs/NORTH_STAR_PRD.md`, then every file under `docs/rack/` in their documented order. Treat those repository files—not prior chat—as architecture authority. Take only the first currently unblocked Rack ticket assigned to this thread. Establish the exact current `main` fixed-point SHA and baseline green evidence before changing code. Use a deterministic failing test first at the pre-agreed seam where technically feasible, implement the minimum vertical behavior, keep Single Host regression contract green, review Standards + Spec from the fixed point, and exact-head qualify before merge. Do not implement the next ticket in this thread. If the ticket is REG-0, do research/evidence/ADR only and do not start Rack production code.

---

## 11. What to report at the end of a thread

Keep the handoff compact and evidence-based:

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
