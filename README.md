# VST3 for OBS Studio — OBS Safe VST3 Host

**Modern VST3 effects in OBS Studio, with third-party plug-in code kept outside `obs64.exe`.**

[![Stable v0.6.1](https://img.shields.io/badge/stable-v0.6.1-22c55e)](https://github.com/masarray/obs-vst3/releases/latest)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases/latest)
[![OBS 29.1+](https://img.shields.io/badge/OBS-29.1%2B-302E31?logo=obsstudio&logoColor=white)](https://obsproject.com/)
[![VST3 effects](https://img.shields.io/badge/VST3-effects%20%2B%20Rack-8b5cf6)](https://masarray.github.io/obs-vst3/compatibility.html)
[![CI](https://github.com/masarray/obs-vst3/actions/workflows/ci.yml/badge.svg)](https://github.com/masarray/obs-vst3/actions/workflows/ci.yml)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Public stable: v0.6.1** · Single VST3 Host + isolated serial VST3 Rack · automatic working-Rack recall · graphical Rack Editor · native vendor GUIs · presets · fail-dry recovery

**Website:** https://masarray.github.io/obs-vst3/  
**Downloads:** https://github.com/masarray/obs-vst3/releases/latest

## Why this project exists

OBS Studio's built-in VST filter does not provide VST3 hosting. Many current studio EQs, compressors, restoration tools, reverbs, saturators, limiters and mastering processors are distributed primarily—or only—as VST3.

OBS Safe VST3 Host adds two OBS-native workflows on Windows:

- **VST 3.x Plug-in** — a simple one-effect filter for the common case.
- **VST3 Rack** — a separate isolated serial multi-effect chain with a dedicated graphical Rack Editor.

Third-party VST3 runtime code, vendor editors and plug-in scanning are deliberately kept outside the OBS process.

## Download

### Recommended — Smart Installer

Open the **[latest stable release](https://github.com/masarray/obs-vst3/releases/latest)** and download:

`OBS-Safe-VST3-Host-v0.6.1-Setup-x64.exe`

Close OBS, run the installer, select the OBS root containing `bin\64bit\obs64.exe`, then start OBS again.

For advanced/manual installation, use:

`OBS-Safe-VST3-Host-v0.6.1-Windows-x64-Portable.zip`

Every release also publishes `SHA256SUMS.txt` for integrity verification.

**Guides:** [Install](https://masarray.github.io/obs-vst3/install.html) · [Compatibility](https://masarray.github.io/obs-vst3/compatibility.html) · [Safety model](https://masarray.github.io/obs-vst3/safety.html) · [Roadmap](https://masarray.github.io/obs-vst3/roadmap.html)

## What's new in v0.6.1

v0.6.1 is a stable Rack reliability and recall update built from real OBS restart testing.

### Durable working-Rack recall

The Rack now keeps its latest working chain automatically per OBS Rack filter. Named Rack presets remain available for reusable chains, but they are **not required** just to survive an OBS restart.

- Rack topology, bypass state and VST3 component/controller state are saved to a per-Rack durable session.
- OBS scene-collection serialization requests a bounded fresh state capture from the isolated Rack helper.
- Missing plug-ins remain safe pass-through placeholders rather than destroying saved state.
- CRC-protected atomic snapshot writes keep a last-known-good recovery path.
- If durable Rack storage is unavailable, the filter stays dry and reports **Needs Attention** instead of pretending the Rack is safely persistent.

### Safer VST3 state capture and restore

- State capture establishes a bounded DSP-safe frontier before vendor `getState` work.
- The realtime Rack path never waits on a control mutex; a capture conflict fails dry instead.
- Slow saved chains restore **dry-first**, then publish the complete restored generation atomically when ready.
- Save completion reports success/failure explicitly rather than turning completed failures into misleading timeouts.
- Split controller/processor VST3 plug-ins now forward native GUI edits and preset-wide parameter resyncs into processor state before capture, improving full DSP recall after restart.

### Windows Rack/editor polish

- **Open Rack** grants the isolated helper a foreground activation opportunity, so the Rack normally opens in front of OBS without becoming permanently topmost.
- Native vendor editor host windows use the embedded project companion icon instead of a generic Windows application icon.

## Stable Rack workflow

1. In OBS, open an audio source → **Filters**.
2. Press **+** → **VST3 Rack**.
3. Click **Open Rack**.
4. Add effects in the graphical Rack Editor.
5. Reorder, bypass or open each vendor UI as needed.
6. Close/reopen OBS normally; the latest working Rack is recalled automatically.
7. Use named Rack presets only when you want reusable named chains or deliberate snapshots.

The Rack remains a serial effects lane by design. It is not a free-form node graph.

## Single Host remains supported

The original **VST 3.x Plug-in** filter remains the simple choice when one effect is enough. It keeps automatic discovery, native vendor GUI, state persistence, scanner isolation, helper recovery and fail-dry behavior.

## Two workflows, one safety model

### Single effect

```text
OBS Studio (obs64.exe)
        │
        │ bounded audio/control IPC
        ▼
obs-safe-vst3-host.exe
        │
        ├── VST3 DSP
        ├── native vendor GUI
        └── state/recovery
```

### VST3 Rack

```text
OBS Studio (obs64.exe)
        │
        │ independent Rack IPC
        ▼
obs-safe-vst3-rack-host.exe
        │
        ├── graphical Rack Editor
        ├── VST3 A → VST3 B → ...
        ├── vendor editor windows
        ├── durable working-session recall
        ├── named Rack presets
        └── recovery / fail-dry policy
```

## Current stable scope

| Capability | v0.6.1 |
|---|---|
| Windows x64 | ✅ Supported |
| OBS Studio 29.1+ | ✅ Supported compatibility floor |
| VST3 audio effects | ✅ Supported |
| Single VST3 filter | ✅ Stable |
| Serial multi-effect VST3 Rack | ✅ Stable |
| Graphical isolated Rack Editor | ✅ Stable |
| Add / replace / remove / reorder / bypass | ✅ |
| Automatic working-Rack recall across OBS restarts | ✅ |
| Native vendor editor | ✅ |
| Installed plug-in discovery | ✅ |
| Named Rack presets | ✅ |
| Mono / stereo Float32 | ✅ |
| Crash/hang containment from `obs64.exe` | ✅ Architectural boundary |
| Sidechain / graph routing | 🚧 Future |
| MIDI / VST3 instruments | 🚧 Future |
| Arbitrary multichannel / Float64 fallback | 🚧 Future |
| macOS / Linux packages | Not currently shipped |

A specific third-party effect can still expose vendor-specific behavior. This project intentionally avoids claiming universal compatibility with every VST3 implementation.

## Qualification and release discipline

The v0.6.1 runtime candidate was qualified on exact source head `ce5eb052c97076df735b95b55328f76e222475ee` before merge. P0/P1, R0, R1, R2, R3, main CI and Compatibility Test Build were green on that exact head. Compatibility qualification covered Windows tests, scanner smoke, supported OBS loader/ABI-floor checks, package construction, PE inspection, portable validation and canonical OBS-root installer smoke.

Real OBS validation included repeated full OBS restarts while changing Rack/VST3 settings across sessions; the latest working chain and full VST3 DSP state restored successfully. The runtime candidate was merged as PR #109. The v0.6.1 release marker is a documentation/version descendant of that qualified runtime change.

The public release workflow rebuilds against pinned OBS/libobs and publishes the Smart Installer, portable ZIP and SHA-256 checksums only after its package tests pass.

See [CHANGELOG.md](CHANGELOG.md) for release-level notes and [docs/rack/CURRENT_STATUS.md](docs/rack/CURRENT_STATUS.md) for the engineering status ledger.

## Compatibility examples

Real-machine qualification across the project has included commercial effects from vendors such as:

- iZotope — Ozone / RX;
- FabFilter;
- Waves;
- Klevgrand;
- Neuro Audio;
- Process Audio.

Compatibility is evidence-based rather than implemented as a vendor whitelist. See the [compatibility page](https://masarray.github.io/obs-vst3/compatibility.html).

## Windows SmartScreen / publisher signing

Current Windows binaries are not commercially Authenticode-signed, so Windows can show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen globally. Download only from this repository's Releases page and verify `SHA256SUMS.txt` when you want an additional integrity check.

## Safety boundary

This project provides **crash containment, not a malware sandbox**. A VST3 is native code and still runs with your Windows user permissions inside the helper process. Only install plug-ins from vendors you trust.

The design reduces the failure surface by keeping third-party VST3 DSP, vendor UI and scanning outside `obs64.exe`, using bounded realtime behavior and failing to dry/pass-through audio when a valid wet result is unavailable. It cannot guarantee that OBS, Windows, drivers, hardware or malicious native code can never fail.

Read the exact [security and crash-isolation model](https://masarray.github.io/obs-vst3/safety.html).

## Developer documentation

- [Product roadmap](ROADMAP.md)
- [North Star PRD](docs/NORTH_STAR_PRD.md)
- [Single Host architecture](docs/P1_ARCHITECTURE.md)
- [Rack architecture and execution docs](docs/rack/README.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Security policy](SECURITY.md)
- [Contributing](CONTRIBUTING.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
