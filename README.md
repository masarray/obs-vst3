# OBS Safe VST3 Host

**Crash-isolated VST3 audio-effect hosting for OBS Studio.**

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases)
[![Public Trial](https://img.shields.io/badge/status-public%20trial-f59e0b)](https://github.com/masarray/obs-vst3/releases)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Current target: v0.2.0-preview.1 — Windows x64 Public Trial.**
> This release is for compatibility and reliability testing. It is not a full replacement for mature in-process hosts yet.

## Why this project exists

OBS can host VST2 filters directly, but many current audio plug-ins are VST3-only. Loading a third-party audio plug-in inside `obs64.exe` also means a bad plug-in can potentially take OBS down with it.

OBS Safe VST3 Host keeps the third-party VST3 outside the OBS process:

```text
OBS Studio (obs64.exe)
        │
        │ bounded shared-memory audio
        ▼
obs-safe-vst3-host.exe
        │
        ▼
third-party VST3 effect
```

If the helper crashes, exits, or misses the realtime processing deadline, the OBS filter leaves the original dry audio buffer untouched instead of waiting indefinitely. The public trial also attempts to restart a failed helper automatically.

## New in Public Trial 1

- **Installed VST3 list** instead of requiring users to type a Class ID.
- **Rescan Installed VST3 Plug-ins** button.
- **Out-of-process scanner** (`obs-safe-vst3-scanner.exe`).
- Each `.vst3` bundle is probed in its **own scanner process** with a timeout, so one broken plug-in should not directly crash OBS or abort the whole architecture boundary.
- **Custom VST3 bundle** path remains available for non-standard installations.
- **Automatic helper recovery attempt** after a VST3/helper crash.
- Realtime callback remains bounded and fails open to dry audio.
- Smart Installer and portable ZIP both include the host and scanner.
- Release CI builds the real OBS module against **OBS/libobs 32.2.2**.

## Important limitations

This is intentionally a **public trial**, not a stable/full-featured VST host.

| Capability | v0.2.0-preview.1 |
|---|---|
| Windows x64 | ✅ |
| VST3 audio effects | ✅ trial |
| Installed VST3 discovery | ✅ |
| Scanner crash isolation | ✅ |
| Runtime VST3 process isolation | ✅ |
| Dry fail-open on timeout/failure | ✅ |
| Automatic helper restart attempt | ✅ |
| Mono / stereo | ✅ |
| Float32 processing | ✅ |
| Native VST3 editor / vendor GUI | ❌ next phase |
| Generic parameter controls | ❌ next phase |
| Full VST3 state/preset persistence | ❌ next phase |
| Sidechain | ❌ |
| Instruments / MIDI | ❌ |
| Arbitrary multichannel | ❌ |
| Linux/macOS package | ❌ |

**Because editor/parameter bridging is not in this trial yet, this build is primarily for testing plug-in discovery, loading, audio processing, crash isolation and compatibility.** Do not treat it as a production daily-driver host yet.

## Download

### Recommended: Smart Installer

From [GitHub Releases](https://github.com/masarray/obs-vst3/releases), download:

`OBS-Safe-VST3-Host-v0.2.0-preview.1-Setup-x64.exe`

For a normal OBS installation, Setup installs to OBS's modern ProgramData plug-in layout. It also:

- requires administrator permission;
- checks that OBS is closed;
- offers a validated Custom / Portable OBS mode;
- registers a normal Windows uninstaller.

### OBS Portable / manual install

Download:

`OBS-Safe-VST3-Host-v0.2.0-preview.1-Windows-x64-Portable.zip`

Close OBS and extract the ZIP directly into the OBS portable root. The package contains:

```text
obs-plugins/64bit/obs-safe-vst3.dll
obs-plugins/64bit/obs-safe-vst3-host.exe
obs-plugins/64bit/obs-safe-vst3-scanner.exe
data/obs-plugins/obs-safe-vst3/locale/en-US.ini
README-FIRST.txt
UNINSTALL-MANUAL.cmd
```

## First test

1. Close OBS and install the public trial.
2. Start OBS Studio.
3. Open an audio source → **Filters** → **+** → **VST 3.x Plug-in (Safe Host)**.
4. Click **Rescan Installed VST3 Plug-ins**.
5. Choose a VST3 effect from **Installed VST 3 Plug-in**.
6. Watch/listen for processing and check the OBS log if the plug-in fails to start.

The scanner checks standard Windows VST3 locations. Use **Custom VST3 bundle (optional)** for a non-standard `.vst3` location.

## What to report during the trial

Please open an issue and include:

- OBS version;
- Windows version;
- VST3 name and version;
- whether it appears in the scan list;
- whether audio processing starts;
- whether the helper restarts after a failure;
- relevant OBS log lines beginning with `[obs-safe-vst3]`.

Useful reports include both **works** and **does not work** results. A compatibility matrix is one of the main goals of this trial.

## Safety model

### Runtime isolation

The VST3 binary is loaded by `obs-safe-vst3-host.exe`, not `obs64.exe`. Audio crosses a fixed shared-memory bridge. OBS waits only for a bounded fraction of the current audio block duration.

### Scanner isolation

The scanner enumerates installed `.vst3` bundles, then probes each bundle in a separate short-lived child process. A probe that hangs is terminated after its timeout and skipped.

### Not a malware sandbox

Process isolation is for crash/hang containment. It does **not** make untrusted plug-ins safe. Only load VST3 software you trust.

## SmartScreen / Unknown publisher

The public-trial binaries are not Authenticode-signed yet, so Windows may show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen just for this project. Download only from this repository's Releases page and verify the accompanying `SHA256SUMS.txt` when you want to confirm file integrity.

Example:

```powershell
Get-FileHash .\OBS-Safe-VST3-Host-v0.2.0-preview.1-Setup-x64.exe -Algorithm SHA256
```

## Engineering direction

The next major phase after this public trial is:

1. parameter/control IPC;
2. generic parameter fallback UI;
3. native VST3 editor hosted by the isolated helper;
4. component/controller state persistence;
5. restore state after automatic helper restart;
6. latency-change handling and broader compatibility tests.

The project deliberately stays focused on being a **simple and crash-resistant VST3 filter for OBS** rather than becoming a full DAW, MIDI workstation or routing graph.

Architecture notes:

- [P1 architecture](docs/P1_ARCHITECTURE.md)
- [Original P0 isolation architecture](docs/P0-ARCHITECTURE.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Security policy](SECURITY.md)

## Build and automated validation

CI currently validates:

- portable protocol layout tests on Linux;
- Windows isolated host build;
- Windows isolated scanner build and smoke execution;
- real OBS module compilation against pinned OBS/libobs 32.2.2;
- Windows protocol tests;
- release binary presence;
- portable package layout;
- Smart Installer install/uninstall against a synthetic portable OBS tree;
- SHA-256 generation.

This does **not** prove compatibility with every commercial/free VST3. Real plug-in reports are still essential.

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
