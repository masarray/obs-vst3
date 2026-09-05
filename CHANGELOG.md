# Changelog

All notable public product changes are documented here. Release artifacts are published on the [GitHub Releases](https://github.com/masarray/obs-vst3/releases) page.

## v0.6.1 — 2026-09-05

### Headline

**Durable working-Rack recall and safer split-component VST3 state persistence.**

v0.6.1 is a stable reliability update for the v0.6 Rack line. It is based on repeated real OBS restart testing and keeps the current serial Rack product scope unchanged.

### Rack persistence

- Every OBS **VST3 Rack** filter now gets a stable durable working-session path derived from its OBS source UUID.
- The current Rack chain is recalled automatically after OBS restarts; users do not need to create a named preset merely to preserve the active working Rack.
- Topology and bypass changes autosave immediately.
- OBS scene-collection serialization requests a bounded fresh VST3 component/controller state capture from the isolated Rack helper.
- Snapshot writes remain CRC-protected, atomic and last-known-good capable.
- Missing plug-ins continue to survive as pass-through placeholders with their saved state retained.
- If durable Rack storage cannot be established, the Rack remains dry and reports **Needs Attention** instead of silently operating without persistence.

### State-safety hardening

- Rack VST3 state capture now uses a bounded DSP-safe frontier before vendor `getState` calls.
- The realtime Rack path never waits on a control mutex; capture conflicts degrade to dry/pass-through behavior.
- Slow persisted chains restore dry-first, then publish the complete restored generation atomically after successful materialization.
- Save handshakes acknowledge both success and completed failure so capture/write errors are not misreported as timeouts.
- Native editor changes from split controller/processor VST3s are forwarded to processor state through a bounded control-to-DSP parameter bridge.
- `restartComponent(kParamValuesChanged)` can trigger a controller-wide parameter resync for vendor preset loads that do not emit individual `performEdit` calls.
- State capture waits for accepted controller edits to reach processor/component state before serialization.

### Windows Rack/editor polish

- **Open Rack** grants the isolated helper a foreground activation opportunity so the Rack normally opens in front of OBS after the user clicks it.
- Native vendor editor host windows use the embedded project companion icon instead of the generic Windows application icon.

### Qualification

The final v0.6.1 runtime candidate `ce5eb052c97076df735b95b55328f76e222475ee` passed:

- P0 Rack Shutdown;
- P1 Rack Editor Polish;
- R0-1 Process Seam Characterization;
- R0-2 HostedPlugin Characterization;
- R1-1 Rack Serial Tracer;
- R1-2 Rack Safety Tracer;
- R1-3 Rack Topology Tracer;
- R1-4 Rack Recovery Tracer;
- R2-1 Rack Session Snapshot;
- R3-0 Rack Editor Bridge;
- R3-1 OBS Rack Launcher;
- R3-2 Rack Slot Browser;
- R3-3 Rack Vendor Editor;
- R3-4 Rack Preset UX;
- CI;
- Compatibility Test Build.

Representative real OBS validation changed commercial split-component VST3 settings across three successive OBS sessions and confirmed that the latest full DSP state—not only preset/controller metadata—returned after each restart.

The runtime candidate was merged in PR #109 as `f91847744a1c824c255666b4a2f9e34b28db3905`. The public v0.6.1 release marker is a documentation/version descendant of that qualified runtime change.

### Stable scope

Supported in this release:

- Windows x64;
- OBS Studio 29.1+ compatibility floor;
- VST3 audio effects;
- mono/stereo Float32 processing;
- Single Host and serial VST3 Rack;
- isolated graphical Rack Editor;
- native vendor UIs;
- automatic working-Rack recall;
- named Rack presets;
- bounded fail-dry behavior.

Still outside the current stable scope:

- free-form graph routing;
- sidechain / advanced multi-bus routing;
- MIDI or VST3 instruments;
- arbitrary multichannel layouts;
- Float64 fallback;
- macOS/Linux runtime packages.

### Upgrade

Use the v0.6.1 Smart Installer from GitHub Releases. It installs the Single Host, Rack Host and scanner into the selected OBS root and remembers that validated target for later updates.

Current packages are not commercially Authenticode-signed. Windows can therefore display **Unknown publisher** or a SmartScreen reputation warning. Download only from this repository and use `SHA256SUMS.txt` when you want an additional integrity check.

---

## v0.6.0 — 2026-09-04

### Headline

**Safe VST3 Rack becomes part of the public stable Windows package.**

v0.6.0 keeps the proven one-effect **VST 3.x Plug-in** workflow and adds a separate, isolated **VST3 Rack** for serial multi-effect chains.

### Added

- Separate OBS **VST3 Rack** filter.
- Separate `obs-safe-vst3-rack-host.exe` process and independently versioned Rack protocol.
- Serial multi-effect Rack processing with stable slot identities.
- Graphical helper-owned Rack Editor; graphical Rack code remains outside `obs64.exe`.
- Plug-in add, replace, remove, reorder and bypass workflow.
- Native vendor editor orchestration from the Rack.
- Rack Session Snapshot recovery.
- Named Rack preset workflow:
  - Save As;
  - browse/select/load;
  - rename;
  - explicit update;
  - delete with confirmation.
- Missing VST3 slots are preserved as pass-through placeholders so Rack definitions can survive unavailable plug-ins.
- Preset-load validation so a corrupt/failed load cannot replace the current working Rack.

### Reliability and UX

- Bounded Rack helper shutdown with fast graceful-exit and forced-exit budgets.
- Deterministic hung-helper shutdown regression coverage.
- Close/reopen lifecycle hardening for the graphical Rack Editor.
- Studio-dark Rack Editor polish with compact slot cards, clearer hierarchy, improved action states and keyboard-navigation behavior.
- Existing fail-dry/pass-through behavior remains the safety fallback when a valid isolated wet block is unavailable.

### Qualification

The final v0.6.0 integration candidate was qualified with the project Rack regression workflows, main CI and Compatibility Test Build. The qualification set covered:

- Rack serial processing, safety, topology and recovery;
- Session Snapshot and preset behavior;
- Rack Editor bridge, OBS launcher, slot browser and vendor-editor orchestration;
- shutdown and product-UX regression gates;
- Windows tests and scanner smoke;
- supported OBS loader / ABI-floor checks;
- package construction and PE inspection;
- portable package validation;
- canonical OBS-root Smart Installer smoke.

Representative real OBS smoke also covered serial Rack audio, Enable/Bypass, native vendor editor operation and improved shutdown behavior.

### Stable scope

Supported in this release:

- Windows x64;
- OBS Studio 29.1+ compatibility floor;
- VST3 audio effects;
- mono/stereo Float32 processing;
- Single Host and serial VST3 Rack;
- isolated graphical Rack Editor;
- native vendor UIs;
- presets and session recovery;
- bounded fail-dry behavior.

Not part of v0.6.0:

- free-form graph routing;
- sidechain / advanced multi-bus routing;
- MIDI or VST3 instruments;
- arbitrary multichannel layouts;
- Float64 fallback;
- macOS/Linux runtime packages;
- the planned expanded R2-2 quarantine work.

### Upgrade

Use the v0.6.0 Smart Installer from GitHub Releases. It installs the Single Host, Rack Host and scanner into the selected OBS root and remembers that validated target for later updates.

Current packages are not commercially Authenticode-signed. Windows can therefore display **Unknown publisher** or a SmartScreen reputation warning. Download only from this repository and use `SHA256SUMS.txt` when you want an additional integrity check.

---

## v0.5.0 — 2026-08-27

### Headline

First public stable release of the isolated **Single VST3 Host** workflow.

### Highlights

- Native OBS **VST 3.x Plug-in** filter.
- Automatic installed VST3 discovery plus manual bundle selection.
- Isolated scanner and VST3 runtime helper.
- Native vendor editor support.
- VST3 component/controller state persistence and recovery.
- Bounded realtime behavior with dry fail-open fallback.
- Smart Installer, portable ZIP and SHA-256 checksums.
- Stable Windows x64 / OBS 29.1+ compatibility floor.
