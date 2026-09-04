# OBS Safe VST3 Host — Product Roadmap

OBS Safe VST3 Host started with a focused problem: **OBS's built-in VST filter does not support VST3, while modern studio plug-ins increasingly ship as VST3-only.**

The stable product now has two workflows:

- **`VST 3.x Plug-in`** — the isolated Single VST3 host.
- **`VST3 Rack`** — shipped in v0.6.0, with a graphical serial chain for up to eight mono/stereo Float32 VST3 effects.

> Roadmap items describe product direction, not guaranteed release dates. New features should ship only after lifecycle, realtime, recovery and compatibility gates are strong enough for live use.

## Product principles

1. **OBS remains the broadcast host.** The normal workflow should not require a separate DAW.
2. **Third-party plug-in code stays outside `obs64.exe`.** New features must not weaken the crash-containment boundary.
3. **Realtime audio stays bounded.** Editor, disk, scanning, state and recovery work must not become unbounded realtime callback work.
4. **Failure should degrade gracefully.** Broken plug-in behavior should not intentionally stall the broadcast path.
5. **The simple workflow stays simple.** Advanced routing must remain optional for users who only want one EQ or compressor.
6. **Compatibility claims remain evidence-based.** Prefer deterministic and real-machine qualification over vendor-specific hacks.

## Stage 1 — Safe Single VST3 Host ✅ shipped

The Single filter provides discovery, external hosting, native vendor GUI, state restore, latency handling, scanner isolation and dry fail-open behavior for one VST3 audio effect per OBS filter.

## Stage 2 — Graphical VST3 Rack ✅ shipped in v0.6.0

v0.6.0 adds a separate `VST3 Rack` OBS filter and one dedicated Rack helper process per Rack.

Delivered Rack surface:

- up to **8 serial VST3 audio effects**;
- mono/stereo Float32 scope;
- helper-owned graphical Rack Editor;
- Search/Add Effect;
- insert, drag/menu reorder, enable/bypass, replace and remove;
- per-slot health and latency plus aggregate Rack latency;
- floating native VST3 vendor editors;
- Session Snapshot state;
- named preset library with Save As, browse/load, explicit Update, Rename and Delete;
- preset load as an independent working copy rather than a live link;
- Missing/pass-through placeholders when a preset references an unavailable plug-in;
- bounded Rack shutdown so a stuck helper does not hold OBS close for seconds;
- professional Rack Editor product skin and keyboard/menu interaction fallbacks.

The v0.6.0 Rack is deliberately **serial**. It is not a graph/patchbay and it does not create one worker process per slot.

## Stage 3 — Rack resilience and qualification 🔧 next

The next priority is not feature breadth. It is stronger evidence and finer failure recovery around the Rack already shipped.

### Missing / repeated-failure correlation

Target behavior:

- preserve missing or repeatedly failing slots visibly in the Rack;
- keep good slots and their state/order intact;
- distinguish correlated slot failures from ambiguous helper death;
- avoid restart storms;
- quarantine only when evidence points to the responsible slot rather than an innocent neighbor.

### Deterministic Rack torture matrix

Expand qualification across:

- 1 / 2 / 4 / 8 deterministic effects;
- repeated add/remove/reorder/bypass loops;
- slow/crash/hang effects;
- repeated helper kills;
- Rack Editor and vendor editor open/close cycles;
- state capture while active;
- multiple independent Racks;
- corrupt Session Snapshots and named presets;
- missing plug-ins;
- malformed/overflow command and snapshot cases.

### Performance evidence

Measure and retain evidence for:

- Rack helper CPU at 1 / 2 / 4 / 8 effects;
- DSP deadline misses;
- memory stability;
- hidden-editor cost;
- visible editor CPU/GPU cost;
- resource stability after repeated editor open/close cycles.

### Broader commercial qualification

Grow the representative real-world VST3 matrix before making stronger public compatibility claims. A stable host cannot promise that every vendor plug-in behaves correctly simply because the VST3 API is common.

## Later stages — architecture expansion

These are **not** part of v0.6.0 and require separate design/qualification work:

- sidechain support;
- parallel lanes or graph/patchbay routing;
- MIDI and VST3 instruments;
- nested Racks;
- live macro/performance control;
- Float64 and arbitrary multichannel buses;
- stable macOS/Linux runtime.

## Non-goal: weaken the isolation model

The project should not trade the process boundary or realtime guarantees for visual complexity. Cable animations, embedded vendor GUIs or large live dashboards are not automatically product improvements. Typography, hierarchy, health visibility and responsive interaction should carry most of the professional UX.

Public roadmap page: https://masarray.github.io/obs-vst3/roadmap.html
