# VST3 for OBS Studio — OBS Safe VST3 Host

**Modern VST3 effects in OBS Studio, with third-party plug-in code kept outside `obs64.exe`.**

[![Stable v0.6.0](https://img.shields.io/badge/stable-v0.6.0-22c55e)](https://github.com/masarray/obs-vst3/releases/latest)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases/latest)
[![OBS 29.1+](https://img.shields.io/badge/OBS-29.1%2B-302E31?logo=obsstudio&logoColor=white)](https://obsproject.com/)
[![VST3 effects](https://img.shields.io/badge/VST3-effects%20%2B%20Rack-8b5cf6)](https://masarray.github.io/obs-vst3/compatibility.html)
[![CI](https://github.com/masarray/obs-vst3/actions/workflows/ci.yml/badge.svg)](https://github.com/masarray/obs-vst3/actions/workflows/ci.yml)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Public stable: v0.6.0** · Single VST3 Host + isolated serial VST3 Rack · graphical Rack Editor · native vendor GUIs · presets · recovery · fail-dry audio

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

`OBS-Safe-VST3-Host-v0.6.0-Setup-x64.exe`

Close OBS, run the installer, select the OBS root containing `bin\64bit\obs64.exe`, then start OBS again.

For advanced/manual installation, use:

`OBS-Safe-VST3-Host-v0.6.0-Windows-x64-Portable.zip`

Every release also publishes `SHA256SUMS.txt` for integrity verification.

**Guides:** [Install](https://masarray.github.io/obs-vst3/install.html) · [Compatibility](https://masarray.github.io/obs-vst3/compatibility.html) · [Safety model](https://masarray.github.io/obs-vst3/safety.html) · [Roadmap](https://masarray.github.io/obs-vst3/roadmap.html)

## What's new in v0.6.0

v0.6.0 promotes the Safe VST3 Rack from architecture work into the public stable package while preserving the proven Single Host workflow.

### Safe VST3 Rack

- separate OBS **VST3 Rack** filter;
- separate `obs-safe-vst3-rack-host.exe` process and Rack protocol;
- serial multi-effect processing with stable slot identity;
- add, replace, remove, reorder and bypass workflow;
- graphical Rack Editor owned by the isolated helper process;
- native vendor editor orchestration;
- Rack Session Snapshot recovery;
- named preset Save As, browse/load, rename, update and delete;
- missing plug-ins preserved as pass-through placeholders rather than destroying the Rack definition;
- invalid/corrupt preset loads cannot replace the current working Rack;
- bounded helper shutdown and close/reopen lifecycle hardening.

### Single Host remains supported

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
        ├── session snapshot / presets
        └── recovery / fail-dry policy
```

The Rack is serial by design in this release. It is not a free-form node graph.

## Quick start

### One VST3 effect

1. In OBS, open an audio source → **Filters**.
2. Press **+** → **VST 3.x Plug-in**.
3. Choose **Installed plug-ins** or browse to a `.vst3` bundle.
4. Select the effect.
5. Click **Open Plug-in Interface**.

### Multiple effects

1. In OBS, open an audio source → **Filters**.
2. Press **+** → **VST3 Rack**.
3. Click **Open Rack**.
4. Add effects in the graphical Rack Editor.
5. Reorder, bypass or open each vendor UI as needed.
6. Save a named Rack preset when you want reusable recall.

## Current stable scope

| Capability | v0.6.0 |
|---|---|
| Windows x64 | ✅ Supported |
| OBS Studio 29.1+ | ✅ Supported compatibility floor |
| VST3 audio effects | ✅ Supported |
| Single VST3 filter | ✅ Stable |
| Serial multi-effect VST3 Rack | ✅ Stable |
| Graphical isolated Rack Editor | ✅ Stable |
| Add / replace / remove / reorder / bypass | ✅ |
| Native vendor editor | ✅ |
| Installed plug-in discovery | ✅ |
| Rack Session Snapshot recovery | ✅ |
| Named Rack presets | ✅ |
| Mono / stereo Float32 | ✅ |
| Crash/hang containment from `obs64.exe` | ✅ Architectural boundary |
| Sidechain / graph routing | 🚧 Future |
| MIDI / VST3 instruments | 🚧 Future |
| Arbitrary multichannel / Float64 fallback | 🚧 Future |
| macOS / Linux packages | Not currently shipped |

A specific third-party effect can still expose vendor-specific behavior. This project intentionally avoids claiming universal compatibility with every VST3 implementation.

## Qualification and release discipline

The v0.6.0 integration candidate passed the project Rack regression workflows, main CI and Compatibility Test Build. Qualification included Windows tests, scanner smoke, OBS loader/ABI-floor checks, package construction, PE inspection, portable validation and canonical OBS-root installer smoke. Real OBS smoke covered serial Rack audio, enable/bypass, native vendor UI and shutdown behavior.

The release workflow rebuilds the public package against pinned OBS/libobs and publishes the Smart Installer, portable ZIP and SHA-256 checksums only after its package tests pass.

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
