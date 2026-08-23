# OBS Safe VST3 Host

**Crash-isolated VST3 audio-effect hosting for OBS Studio.**

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases)
[![Public Preview](https://img.shields.io/badge/status-public%20preview-f59e0b)](https://github.com/masarray/obs-vst3/releases)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Current target: v0.3.0-preview.1 — P2.1 Native VST3 Interface + Safe Parameter Bridge, Windows x64.**
> This release is for native-editor, parameter-control, compatibility, and crash-isolation testing. Full VST3 component/controller state persistence remains a later phase.

## Why this project exists

OBS can host VST2 filters directly, but many current audio plug-ins are VST3-only. Loading third-party audio plug-in code inside `obs64.exe` also means a bad plug-in can potentially take OBS down with it.

OBS Safe VST3 Host keeps the third-party VST3 outside the OBS process:

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

If the helper crashes, exits, or misses its internal realtime processing budget, the OBS filter leaves the original dry audio buffer untouched instead of waiting indefinitely. The preview attempts to restart a failed helper automatically.

## New in v0.3.0-preview.1

- **Open Plug-in Interface** opens the VST3 vendor's native editor in the isolated helper process.
- The vendor GUI is attached to a helper-owned Win32 `HWND`; third-party editor/GPU/licensing code is not embedded into `obs64.exe`.
- Native editor `beginEdit / performEdit / endEdit` changes are routed back to the processor through the host parameter bridge.
- Helper window messages are pumped while the isolated host remains alive, including plug-in-requested editor resizing.
- **Fallback Parameter Controls** remain available in OBS when a native editor is absent or unusable.
- VST3 controller metadata is discovered in the isolated helper and exposed through protocol v2.
- Continuous parameters use normalized `0.0–1.0` controls; toggle/list/discrete parameters snap to valid VST3 steps.
- Hidden parameters are omitted and read-only parameters are disabled.
- Parameter settings are scoped by plug-in path + Class ID so unrelated plug-ins cannot reuse each other's saved ParamID values.
- User-set fallback values are re-applied after automatic helper recovery.
- Host → processor edits are delivered through VST3 `IParameterChanges`.
- Processor → host parameter feedback is captured and synchronized back to controller/shared status.
- When normal audio blocks are inactive, pending edits are flushed with a zero-audio VST3 `process()` call.
- The old **Realtime deadline fraction** control is no longer exposed to users; fail-open timing is an internal safety mechanism.
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
| Native VST3 vendor editor | ✅ helper-owned window |
| Generic/fallback parameter controls | ✅ normalized preview |
| Native editor gesture → processor bridge | ✅ |
| Discrete/toggle parameter snapping | ✅ |
| User fallback values restored after helper restart | ✅ |
| Processor parameter feedback | ✅ bounded |
| Edit while source/audio is inactive | ✅ zero-audio flush |
| Full VST3 component/controller state blob | ❌ next phase |
| Last-known-good full state restore | ❌ next phase |
| Vendor-formatted generic parameter text/list names | ❌ later |
| More than first 256 exposed fallback parameters | ❌ preview limit |
| Sidechain | ❌ later |
| Instruments / MIDI | ❌ |
| Arbitrary multichannel | ❌ |
| Linux/macOS package | ❌ |

This remains a **public preview**, not a full DAW-style host. The P2.1 objective is a normal VST3 effect workflow—select plug-in, open its real interface, tune it—without giving up the process-isolation boundary.

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

1. Close OBS and install/extract the preview.
2. Start OBS Studio.
3. Open an audio source → **Filters** → **+** → **VST 3.x Plug-in (Safe Host)**.
4. Click **Rescan Installed VST3 Plug-ins**.
5. Choose a VST3 effect from **Installed VST 3 Plug-in**.
6. Click **Open Plug-in Interface**. The vendor's real VST3 editor should open in a separate helper-owned window.
7. Move controls in the vendor UI and confirm the audio changes without OBS restarting or the helper being recreated.
8. If the plug-in has no usable native editor, use **Fallback Parameter Controls** and confirm parameter changes affect audio.
9. Test edits while the source is silent/inactive, then resume audio and confirm the value took effect.
10. For recovery testing, terminate `obs-safe-vst3-host.exe`; OBS should remain alive and dry while the helper is restarted.

The scanner checks standard Windows VST3 locations. **Custom VST3 bundle (advanced / optional)** remains available for non-standard or development `.vst3` bundles.

## What to report during the preview

Please open an issue and include:

- OBS version;
- Windows version;
- VST3 name and version;
- whether it appears in the scan list;
- whether **Open Plug-in Interface** opens the correct vendor GUI;
- whether resizing/closing/reopening the vendor GUI behaves correctly;
- whether native UI controls change the processed audio;
- whether fallback controls work if needed;
- whether edits work after an inactive/silent period;
- whether audio processing starts and stays stable;
- what happens if the helper is terminated while the vendor GUI is open;
- relevant OBS log lines beginning with `[obs-safe-vst3]`.

Reports that a plug-in **works** are useful too; they help build a compatibility matrix.

## Safety model

### Runtime isolation

The VST3 binary and its native editor are loaded by `obs-safe-vst3-host.exe`, not `obs64.exe`. Audio crosses a fixed shared-memory bridge. Parameter metadata, parameter edits, and editor commands cross bounded protocol-v2 control state.

Parameter/property/editor work does not run inside the OBS realtime callback. The audio callback remains focused on acquiring a safe bridge reference, transferring a bounded block, and failing open to the original dry buffer when the helper is unavailable or late.

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
2. split helper UI/control and DSP ownership into dedicated threads without changing the OBS-side seam;
3. richer parameter presentation and complete `restartComponent()` handling;
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
- Windows isolated host/controller/native-editor compilation;
- Windows isolated scanner build and smoke execution;
- real OBS module compilation against pinned OBS/libobs 32.2.2;
- Windows protocol/parameter tests;
- release binary presence;
- portable package layout;
- Smart Installer install/uninstall against a synthetic portable OBS tree;
- SHA-256 generation.

Automated compilation cannot prove compatibility with every commercial/free VST3 or exercise every vendor GUI. Real plug-in reports remain essential during the preview.

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
