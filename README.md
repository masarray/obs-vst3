# OBS Safe VST3 Host

**Crash-isolated VST3 audio-effect hosting for OBS Studio.**

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases)
[![Public Preview](https://img.shields.io/badge/status-public%20preview-f59e0b)](https://github.com/masarray/obs-vst3/releases)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Current target: v0.3.0-preview.1 — Native OBS-like VST3 Recovery Preview, Windows x64.**
> The goal of this preview is the familiar OBS VST workflow—select a plug-in, see that it is ready, open its real interface, and tune it—without loading third-party VST3 code into `obs64.exe`.

## Why this project exists

OBS can host VST2 filters directly, but many current audio plug-ins are VST3-only. A third-party plug-in loaded directly inside `obs64.exe` can also take OBS down if the plug-in crashes or hangs.

OBS Safe VST3 Host keeps VST3 runtime and vendor UI code outside the OBS process:

```text
OBS Studio (obs64.exe)
        │
        │ bounded shared-memory audio + control IPC
        ▼
obs-safe-vst3-host.exe
        │
        ├── native vendor VST3 editor window
        │
        ▼
third-party VST3 effect
```

If the helper exits, crashes, or misses its internal realtime budget, the filter leaves the original dry OBS audio buffer untouched. The helper can then be restarted without making third-party VST3 code part of the OBS process.

## Recovered user workflow

The normal properties panel intentionally stays close to OBS's familiar VST filter workflow:

1. **Installed VST 3 Plug-in** — choose a discovered effect.
2. **Rescan Installed Plug-ins** — refresh the isolated scan cache.
3. **Browse / Custom VST3** — fallback for a development or non-standard `.vst3` bundle.
4. Status such as **`ArSonKuPik — Ready — 48 samples latency`**.
5. **Open Plug-in Interface** — opens the vendor's real VST3 editor in the isolated helper.

There is no user-facing `Realtime deadline fraction` control in the normal panel. The processing deadline remains an internal fail-open safety mechanism.

When a native vendor editor is available, the OBS properties panel does not duplicate hundreds of generic controls beside it. **Plug-in Controls (Fallback)** appear only when the native editor is unavailable or unusable.

The installed plug-in selector is authoritative. A stale custom path cannot silently override a plug-in visibly selected in the dropdown. Choosing **Browse / Custom VST3…** in the installed selector explicitly switches back to the custom-path fallback.

## New in v0.3.0-preview.1

- helper-owned native VST3 vendor editor using `IEditController::createView()` and `IPlugView`;
- Win32 `HWND` hosting plus `IPlugFrame::resizeView()` support;
- native editor `beginEdit / performEdit / endEdit` routed to the processor;
- protocol **v3** control/status ABI;
- helper-published plug-in name and VST3-reported latency for OBS status display;
- installed VST3 discovery with isolated scanning and per-candidate probing;
- bounded host→processor parameter edits through VST3 `IParameterChanges`;
- processor→host parameter feedback;
- zero-audio parameter flush when normal audio blocks are inactive;
- normalized fallback parameter controls for plug-ins without a usable native editor;
- discrete/toggle parameter snapping;
- scoped fallback values per plug-in path + Class ID;
- automatic isolated-helper recovery attempt;
- dry fail-open audio on helper failure/deadline miss;
- Smart Installer and portable ZIP distribution paths retained.

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
| Native VST3 vendor editor | ✅ helper-owned window |
| Plug-in name + latency status | ✅ |
| Native editor gesture → processor bridge | ✅ |
| Generic parameter controls | ✅ fallback only |
| Discrete/toggle parameter snapping | ✅ |
| User fallback values re-applied after helper restart | ✅ |
| Processor parameter feedback | ✅ bounded |
| Edit while source/audio is inactive | ✅ zero-audio flush |
| Full VST3 component/controller state blob | ❌ next phase |
| Last-known-good full state restore | ❌ next phase |
| Complete `restartComponent()` reconfiguration | ❌ later |
| Sidechain | ❌ later |
| Instruments / MIDI | ❌ |
| Arbitrary multichannel | ❌ |
| Linux/macOS package | ❌ |

This remains a **public preview**, not a DAW-style host and not yet a claim of compatibility with every commercial VST3.

## Download

### Recommended: Smart Installer

From [GitHub Releases](https://github.com/masarray/obs-vst3/releases), download:

`OBS-Safe-VST3-Host-v0.3.0-preview.1-Setup-x64.exe`

For a normal OBS installation, Setup installs the module and isolated runtime companions. It also:

- requires administrator permission;
- checks that OBS is closed;
- supports a validated Custom / Portable OBS destination;
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

## First public-preview test

1. Close OBS and install/extract the preview.
2. Start OBS Studio.
3. Open an audio source → **Filters** → **+** → **VST 3.x Plug-in**.
4. Click **Rescan Installed Plug-ins**.
5. Select an effect from **Installed VST 3 Plug-in**, or choose **Browse / Custom VST3…** and browse to a development/non-standard bundle.
6. Confirm the status becomes `<plug-in name> — Ready — <N> samples latency`.
7. Click **Open Plug-in Interface** and confirm the real vendor VST3 editor opens in its own helper-owned window.
8. Change controls in the vendor UI and confirm the processed audio changes without recreating the helper.
9. Close and reopen the vendor editor; resize it if the plug-in supports resizing.
10. For recovery testing, terminate `obs-safe-vst3-host.exe`; OBS should stay alive and dry while the isolated helper is restarted.
11. For a plug-in without a usable native editor, verify **Plug-in Controls (Fallback)** appear and affect processing.

## What to report

Please open an issue and include:

- OBS version;
- Windows version;
- VST3 name and version;
- whether it appears in the installed list;
- whether the status reports the correct name/latency;
- whether **Open Plug-in Interface** opens the correct vendor GUI;
- whether resizing, closing, and reopening the GUI behave correctly;
- whether vendor controls change the processed audio;
- whether fallback controls work when needed;
- whether edits still apply after a silent/inactive period;
- what happens if the helper is terminated while the editor is open;
- relevant OBS log lines beginning with `[obs-safe-vst3]`.

Successful compatibility reports are useful too.

## Safety model

### Runtime isolation

The VST3 binary and native editor are loaded by `obs-safe-vst3-host.exe`, not `obs64.exe`. Audio crosses a fixed shared-memory bridge. Parameter metadata, edits, editor commands, plug-in identity, and latency status use the versioned protocol-v3 shared control state.

The OBS audio callback does not enumerate parameters, open windows, scan plug-ins, or take the configuration/recovery mutex. Bridge ownership stays outside the realtime callback, and a bounded hazard-pointer acquisition protects helper replacement. If no safe wet result is available in time, the original OBS buffer remains dry and unchanged.

### Scanner isolation

The scanner discovers standard Windows VST3 locations. Each candidate is probed out-of-process with a timeout so a bad scanner candidate does not directly load into OBS. The OBS-side scan invocation also has a whole-scan ceiling rather than waiting forever.

### Not a malware sandbox

Crash/process isolation is not a security sandbox. Only load VST3 software you trust.

## SmartScreen / Unknown publisher

Public-preview binaries are not Authenticode-signed yet, so Windows can show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen just for this project. Download only from this repository's Releases page and verify `SHA256SUMS.txt` when desired.

Example:

```powershell
Get-FileHash .\OBS-Safe-VST3-Host-v0.3.0-preview.1-Setup-x64.exe -Algorithm SHA256
```

## Engineering direction after this preview

The next priority order is:

1. full VST3 component/controller state persistence and a last-known-good snapshot;
2. clean helper UI/control vs DSP-thread ownership separation without changing the OBS-side UX;
3. richer fallback parameter presentation and complete `restartComponent()` handling;
4. recovery state machine, crash-loop quarantine, and health diagnostics;
5. sidechain;
6. simple isolated **Safe Rack** rather than a full DAW-style graph.

The project deliberately stays focused on being a **simple, familiar, crash-resistant VST3 filter for OBS** rather than becoming a full DAW, MIDI workstation, or general device-routing environment.

Architecture notes:

- [P1 architecture](docs/P1_ARCHITECTURE.md)
- [Original P0 isolation architecture](docs/P0-ARCHITECTURE.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Security policy](SECURITY.md)

## Build and automated validation

CI validates:

- protocol **v3** layout and parameter normalization semantics on Linux;
- Windows isolated host/controller/native-editor compilation;
- Windows isolated scanner build and smoke execution;
- real OBS module compilation against pinned OBS/libobs 32.2.2;
- Windows protocol/parameter tests;
- release binary presence;
- portable package layout;
- Smart Installer install/uninstall against a synthetic portable OBS tree;
- SHA-256 generation.

Automated compilation cannot prove compatibility with every VST3 or exercise every vendor GUI. Representative desktop runtime testing remains essential during the preview.

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
