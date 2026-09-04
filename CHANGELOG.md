# Changelog

All notable public product changes are documented here. Release artifacts are published on the [GitHub Releases](https://github.com/masarray/obs-vst3/releases) page.

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
