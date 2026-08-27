# VST3 for OBS Studio — OBS Safe VST3 Host

**A native OBS filter plug-in for using modern VST3 audio effects on Windows, with crash isolation designed to keep third-party VST3 code out of the OBS process.**

[![Stable v0.5.0](https://img.shields.io/badge/stable-v0.5.0-22c55e)](https://github.com/masarray/obs-vst3/releases/latest)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases/latest)
[![OBS 29.1+](https://img.shields.io/badge/OBS-29.1%2B-302E31?logo=obsstudio&logoColor=white)](https://obsproject.com/)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Current public stable release: v0.5.0.**  
> If you searched for **VST3 for OBS**, **OBS VST3 plugin**, **native VST3 in OBS**, or a safer replacement for direct in-process plug-in hosting, this project is built for that workflow.

**Product website:** https://masarray.github.io/obs-vst3/

## Download

### Recommended for most users — Windows installer

**[Download the latest stable release](https://github.com/masarray/obs-vst3/releases/latest)** and choose:

`OBS-Safe-VST3-Host-v0.5.0-Setup-x64.exe`

Close OBS, run the installer, select your OBS folder, then start OBS again.

A portable/manual ZIP is also available on the same release page:

`OBS-Safe-VST3-Host-v0.5.0-Windows-x64-Portable.zip`

Every release includes `SHA256SUMS.txt` for file verification.

**Beginner guides:** [Install on Windows](https://masarray.github.io/obs-vst3/install.html) · [VST3 compatibility](https://masarray.github.io/obs-vst3/compatibility.html) · [Why the crash isolation is safer](https://masarray.github.io/obs-vst3/safety.html)

## Why use this instead of loading a VST3 directly into OBS?

A normal audio plug-in is native code. If a host loads unstable third-party code directly into its own process, a plug-in crash can take the host down with it.

OBS Safe VST3 Host changes that boundary:

```text
OBS Studio (obs64.exe)
        │
        │ bounded audio/control IPC
        ▼
obs-safe-vst3-host.exe   ← isolated helper process
        │
        ├── VST3 DSP
        ├── vendor plug-in window
        └── state / recovery
        │
        ▼
third-party VST3 audio effect
```

The VST3 binary and its vendor GUI run in the helper process, **not inside `obs64.exe`**. If the helper or plug-in becomes unhealthy, the OBS filter is designed to fail open to dry audio and recover instead of intentionally blocking OBS's realtime audio path.

No software can promise that OBS will never crash for every possible system failure, driver problem, or malicious plug-in. The important difference here is architectural: **third-party VST3 code is kept outside the OBS process.**

## Easy workflow

After installation:

1. In OBS, open an audio source → **Filters**.
2. Press **+** and add **VST 3.x Plug-in**.
3. Under **1. Plug-in Source**, keep **Installed plug-ins** or browse to a `.vst3` file.
4. Choose the effect under **2. Installed Plug-in**.
5. Click **3. Open Plug-in Interface**.
6. Tweak the normal vendor GUI. It opens in front of OBS so you can work immediately.

Installed VST3 effects are discovered automatically. **Refresh Plug-in List** is available when you install a new effect while OBS is already running.

## What works in v0.5.0?

The project targets **VST3 audio effects** for Windows x64 and OBS Studio 29.1+.

Real-machine qualification has included commercial effects from vendors such as:

- **iZotope** — Ozone 11, RX 9 Spectral De-noise
- **FabFilter** — Pro-Q 3, Pro-C 2, Pro-R, Saturn
- **Waves** — SSL Channel
- **Klevgrand** — Brusfri
- **Neuro Audio** — Westwood
- **Process Audio** — Sugar

This is not an artificial whitelist. The scanner uses VST3 metadata conservatively and keeps normal audio effects visible while excluding plug-ins that explicitly identify as instrument-only.

See the [live compatibility page](https://masarray.github.io/obs-vst3/compatibility.html) for the current scope and known limits.

## What users get

- **Native OBS filter workflow** — add it from the normal OBS Filters window.
- **Installed VST3 discovery** — no need to type Class IDs or manually configure most plug-ins.
- **Real vendor GUI** — use the same knobs and interface you know from a DAW.
- **Foreground editor behavior** — the plug-in window opens in front instead of hiding behind OBS.
- **Crash-isolated runtime** — VST3 DSP and vendor UI stay outside `obs64.exe`.
- **Crash-isolated scanner** — plug-in probing also happens outside OBS.
- **Dry fail-open behavior** — unhealthy wet processing does not intentionally block the OBS realtime callback.
- **Automatic recovery** — hung/exited helpers use bounded recovery/backoff.
- **Full VST3 state persistence** — component/controller state can be restored after helper recreation.
- **Installer + portable ZIP** — normal OBS, custom, Steam-style and portable roots can use the same root-local layout.
- **Open source** — GPL-3.0-or-later, with public release builds and SHA-256 checksums.

## Current compatibility scope

| Capability | v0.5.0 |
|---|---|
| Windows x64 | ✅ Supported |
| OBS Studio 29.1+ | ✅ Supported compatibility floor |
| VST3 audio effects | ✅ Supported |
| Installed plug-in discovery | ✅ |
| Native vendor editor | ✅ |
| Mono / stereo Float32 | ✅ |
| Full VST3 state persistence | ✅ |
| Crash/hang isolation from `obs64.exe` | ✅ Architectural boundary |
| Instrument-only VST3 / MIDI | ❌ Not supported |
| Sidechain / multiple audio buses | ⚠️ Not fully supported |
| Arbitrary multichannel / Float64 fallback | ⚠️ Not yet |
| macOS / Linux packages | ❌ Not currently shipped |

Compatibility with a specific third-party effect can still depend on the plug-in version and its VST3 implementation. If you find a reproducible exception, please open an issue with the OBS version, Windows version, plug-in name/version and relevant `[obs-safe-vst3]` log lines.

## Windows SmartScreen / Unknown publisher

The current Windows packages are **not commercially Authenticode-signed**, so Windows may show **Unknown publisher** or a SmartScreen reputation warning.

That warning is about publisher signing/reputation; it is not proof that the file is malicious. Keep Windows security enabled, download only from this repository's **Releases** page, and verify `SHA256SUMS.txt` when you want an additional integrity check.

Do **not** disable Defender or SmartScreen globally for this project.

## Safety in plain language

This project is designed for **crash containment**, not malware containment. A VST3 is native software and still runs with your Windows user permissions inside the helper process. Only use plug-ins from vendors you trust.

Read [Security and crash isolation](https://masarray.github.io/obs-vst3/safety.html) for the exact boundary.

## For developers and maintainers

The public user experience is intentionally simple. Engineering details remain available for anyone who wants to audit the implementation:

- [P1 architecture](docs/P1_ARCHITECTURE.md)
- [Original P0 isolation architecture](docs/P0-ARCHITECTURE.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Security policy](SECURITY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

Release CI builds against pinned OBS/libobs, runs protocol/lifecycle/scanner/recovery tests, validates the Windows package, smoke-tests the Smart Installer, and checks loading against the supported OBS compatibility floor.

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
