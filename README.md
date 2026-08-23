# OBS Safe VST3 Host

**Crash-isolated VST3 audio-effect hosting for OBS Studio.**

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases)
[![Public Preview](https://img.shields.io/badge/status-public%20preview-f59e0b)](https://github.com/masarray/obs-vst3/releases)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Current target: v0.3.0-preview.1 — P2.1 Generic Parameter Controls, Windows x64.**
> This release is for compatibility, parameter-control, and reliability testing. Native vendor GUI and full VST3 state persistence are intentionally later phases.

## Why this project exists

OBS can host VST2 filters directly, but many current audio plug-ins are VST3-only. Loading a third-party audio plug-in inside `obs64.exe` also means a bad plug-in can potentially take OBS down with it.

OBS Safe VST3 Host keeps the third-party VST3 outside the OBS process:

```text
OBS Studio (obs64.exe)
        │
        │ bounded shared-memory audio + control IPC
        ▼
obs-safe-vst3-host.exe
        │
        ▼
third-party VST3 effect
```

If the helper crashes, exits, or misses the realtime processing deadline, the OBS filter leaves the original dry audio buffer untouched instead of waiting indefinitely. The preview attempts to restart a failed helper automatically.

## New in v0.3.0-preview.1

- **Generic VST3 Parameters** group directly in the OBS filter properties.
- VST3 controller metadata is discovered in the isolated helper and exposed through protocol v2.
- Editable parameters can be changed without restarting the helper.
- Continuous parameters use normalized `0.0–1.0` controls; toggle/list/discrete parameters snap to valid VST3 steps.
- Hidden parameters are omitted and read-only parameters are disabled.
- Parameter settings are scoped by plug-in path + Class ID so unrelated plug-ins cannot reuse each other's saved ParamID values.
- User-set generic parameter values are re-applied after automatic helper recovery.
- Host → processor edits are delivered through VST3 `IParameterChanges`.
- Processor → host parameter feedback is captured and synchronized back to the controller/shared status.
- When normal audio blocks are inactive, pending edits are flushed with a zero-audio VST3 `process()` call as required by the VST3 host model.
- Parameter queues are pre-sized during initialization to keep normal processing bounded.
- Existing isolated scanner, fail-open audio, automatic helper recovery, Smart Installer, and portable ZIP remain intact.

## Capability matrix

| Capability | v0.3.0-preview.1 |
|---|---|
| Windows x64 | ✅ |
| VST3 audio effects | ✅ preview |
| Installed VST3 discovery | ✅ |
| Scanner crash isolation | ✅ |
| Runtime VST3 process isolation | ✅ |
| Dry fail-open on timeout/failure | ✅ |
| Automatic helper restart attempt | ✅ |
| Mono / stereo | ✅ |
| Float32 processing | ✅ |
| Generic parameter controls | ✅ normalized preview |
| Discrete/toggle parameter snapping | ✅ |
| Parameter values restored after helper restart | ✅ user-set generic values |
| Processor parameter feedback | ✅ bounded |
| Edit while source/audio is inactive | ✅ zero-audio flush |
| Native VST3 editor / vendor GUI | ❌ next phase |
| Full VST3 component/controller state | ❌ next phase |
| Vendor-formatted parameter text/list names | ❌ later |
| More than first 256 exposed parameters | ❌ preview limit |
| Sidechain | ❌ later |
| Instruments / MIDI | ❌ |
| Arbitrary multichannel | ❌ |
| Linux/macOS package | ❌ |

This is still a **public preview**, not a full replacement for a mature DAW-style plug-in host. The purpose of P2.1 is to make real effects tunable inside OBS while proving the control path does not compromise crash isolation or realtime safety.

## Download

### Recommended: Smart Installer

From [GitHub Releases](https://github.com/masarray/obs-vst3/releases), download:

`OBS-Safe-VST3-Host-v0.3.0-preview.1-Setup-x64.exe`

For a normal OBS installation, Setup installs to OBS's modern ProgramData plug-in layout. It also:

- requires administrator permission;
- checks that OBS is closed;
- offers a validated Custom / Portable OBS mode;
- registers a normal Windows uninstaller.

### OBS Portable / manual install

Download:

`OBS-Safe-VST3-Host-v0.3.0-preview.1-Windows-x64-Portable.zip`

Close OBS and extract the ZIP directly into the OBS portable root. The package contains:

```text
obs-plugins/64bit/obs-safe-vst3.dll
obs-plugins/64bit/obs-safe-vst3-host.exe
obs-plugins/64bit/obs-safe-vst3-scanner.exe
data/obs-plugins/obs-safe-vst3/locale/en-US.ini
README-FIRST.txt
UNINSTALL-MANUAL.cmd
```

## First P2.1 test

1. Close OBS and install the preview.
2. Start OBS Studio.
3. Open an audio source → **Filters** → **+** → **VST 3.x Plug-in (Safe Host)**.
4. Click **Rescan Installed VST3 Plug-ins**.
5. Choose a VST3 effect from **Installed VST 3 Plug-in**.
6. The filter properties should refresh and show **Generic VST3 Parameters**.
7. Move one or more parameters and confirm the audio changes without the helper restarting.
8. Close and reopen the filter properties and confirm your user-set values remain.
9. Test while the source is silent/inactive, then resume audio and confirm the parameter value took effect.
10. For recovery testing, terminate `obs-safe-vst3-host.exe`; OBS should remain alive/dry and the helper should be started again, with user-set generic values re-applied.

The scanner checks standard Windows VST3 locations. Use **Custom VST3 bundle (optional)** for a non-standard `.vst3` location.

## What to report during the preview

Please open an issue and include:

- OBS version;
- Windows version;
- VST3 name and version;
- whether it appears in the scan list;
- number/type of generic controls shown;
- whether changing a control changes the effect;
- whether controls work after an inactive/silent period;
- whether user-set controls return after helper recovery;
- whether audio processing starts and stays stable;
- relevant OBS log lines beginning with `[obs-safe-vst3]`.

Reports that a plug-in **works** are useful too; they help build a compatibility matrix.

## Safety model

### Runtime isolation

The VST3 binary is loaded by `obs-safe-vst3-host.exe`, not `obs64.exe`. Audio crosses a fixed shared-memory bridge. Parameter metadata and edits cross a bounded protocol-v2 control area. OBS waits only for a bounded fraction of the current audio block duration.

Parameter/property work does not run inside the OBS realtime callback. The audio callback remains focused on acquiring a safe bridge reference, transferring a bounded block, and failing open to the original dry buffer when the helper is unavailable or late.

### Scanner isolation

The scanner enumerates installed `.vst3` bundles, then probes each bundle in a separate short-lived child process. A probe that hangs is terminated after its timeout and skipped.

### Not a malware sandbox

Process isolation is for crash/hang containment. It does **not** make untrusted plug-ins safe. Only load VST3 software you trust.

## SmartScreen / Unknown publisher

The public-preview binaries are not Authenticode-signed yet, so Windows may show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen just for this project. Download only from this repository's Releases page and verify the accompanying `SHA256SUMS.txt` when you want to confirm file integrity.

Example:

```powershell
Get-FileHash .\OBS-Safe-VST3-Host-v0.3.0-preview.1-Setup-x64.exe -Algorithm SHA256
```

## Engineering direction

After P2.1, the next priority order is:

1. full VST3 component/controller state persistence + last-known-good snapshot;
2. helper-owned native VST3 vendor editor with generic fallback;
3. richer parameter presentation and restart-component metadata refresh;
4. recovery state machine, crash-loop quarantine, and health diagnostics;
5. sidechain;
6. simple isolated **Safe Rack** rather than a full DAW-style graph.

The project deliberately stays focused on being a **simple and crash-resistant VST3 filter for OBS** rather than becoming a full DAW, MIDI workstation, or general device-routing graph.

Architecture notes:

- [P1 architecture](docs/P1_ARCHITECTURE.md)
- [Original P0 isolation architecture](docs/P0-ARCHITECTURE.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Security policy](SECURITY.md)

## Build and automated validation

CI validates:

- protocol v2 layout and parameter normalization semantics on Linux;
- Windows isolated host/controller build;
- Windows isolated scanner build and smoke execution;
- real OBS module compilation against pinned OBS/libobs 32.2.2;
- Windows protocol/parameter tests;
- release binary presence;
- portable package layout;
- Smart Installer install/uninstall against a synthetic portable OBS tree;
- SHA-256 generation.

Automated compilation cannot prove compatibility with every commercial/free VST3. Real plug-in reports remain essential during the preview.

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
