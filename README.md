# OBS Safe VST3 Host

**Crash-isolated VST3 audio-effect hosting for OBS Studio.**

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases)
[![Stable](https://img.shields.io/badge/status-stable-22c55e)](https://github.com/masarray/obs-vst3/releases)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Current stable release: v0.4.0 — Phase S stable, Windows x64.**
> One isolated VST3 audio effect per OBS filter, with full component/controller state persistence, dedicated realtime DSP isolation, watchdog recovery, native vendor UI support, and dry fail-open behavior.

## Why this project exists

OBS can host VST2 filters directly, while many current plug-ins are VST3-only. Loading third-party plug-in code directly into `obs64.exe` also means a faulty plug-in can crash or hang OBS itself.

OBS Safe VST3 Host keeps VST3 runtime and vendor UI code outside the OBS process:

```text
OBS Studio (obs64.exe)
        │
        │ bounded shared-memory audio + control/state IPC
        ▼
obs-safe-vst3-host.exe
        │
        ├── dedicated realtime DSP worker
        ├── native vendor VST3 editor window
        └── full VST3 state capture / restore
        │
        ▼
third-party VST3 effect
```

If the isolated helper exits, crashes, hangs, or fails the DSP delivery contract, OBS keeps the original dry buffer and the recovery layer can recreate the helper from the last-known-good state checkpoint.

## Phase S stable capabilities

- Windows x64 runtime targeting OBS Studio 32.x; release CI is pinned to OBS/libobs **32.2.2**.
- Installed VST3 discovery with isolated per-candidate scanner probing.
- Runtime VST3 process isolation: third-party DSP and vendor GUI do not load into `obs64.exe`.
- Native vendor VST3 editor via `IEditController::createView()` / `IPlugView`.
- Generic normalized parameter controls as fallback when the native editor is unavailable.
- Protocol **v6** with separate control and DSP heartbeats.
- Dedicated MMCSS **Pro Audio** DSP worker; vendor UI/control stalls do not automatically stop healthy DSP.
- Fixed-capacity allocation-free SPSC queues for control↔DSP parameter transfer.
- Audio-first bounded command servicing to reduce parameter-burst starvation.
- Full opaque VST3 **component + controller state** persistence.
- Versioned state envelope with CRC validation and per-source LocalAppData persistence.
- Last-known-good state checkpoint retained across transient capture/IPC/disk failures.
- Restore ordering: component state → controller component state → controller state.
- State capture/restore uses an acknowledged DSP pause frontier so processor/controller snapshots do not intentionally mix generations.
- Generation-coupled packed Paused/Running transition tokens prevent stale pause/resume acknowledgements.
- Watchdog distinguishes exited helpers from live-but-hung DSP.
- Exponential recovery backoff up to 30 seconds; 10 seconds of stable operation resets crash-loop history.
- Processor-delivery, zero-sample flush, and VST process failures fail closed into helper recovery instead of allowing silent controller/component divergence.
- Dry fail-open audio when no valid wet result is available within the realtime deadline.

## User workflow

1. Add **VST 3.x Plug-in (Safe Host)** to an OBS audio source.
2. Click **Rescan Installed Plug-ins**.
3. Choose an installed VST3 effect, or use **Browse / Custom VST3…** for a non-standard bundle.
4. Confirm the status reports the plug-in name and latency.
5. Click **Open Plug-in Interface** for the vendor UI.
6. Tune normally. State is checkpointed outside the realtime callback and restored when the helper is recreated.

The normal properties panel intentionally stays simple. The installed plug-in selector is authoritative, and fallback parameter controls appear only when the native editor is unavailable or unusable.

## Download v0.4.0

Use the assets on [GitHub Releases](https://github.com/masarray/obs-vst3/releases).

### Recommended: Smart Installer

`OBS-Safe-VST3-Host-v0.4.0-Setup-x64.exe`

Close OBS first. The installer supports a normal OBS installation and a validated Custom / Portable OBS root, and registers a normal Windows uninstaller.

### Portable / manual install

`OBS-Safe-VST3-Host-v0.4.0-Windows-x64-Portable.zip`

Extract directly into the OBS root. The package contains:

```text
obs-plugins/64bit/obs-safe-vst3.dll
obs-plugins/64bit/obs-safe-vst3-host.exe
obs-plugins/64bit/obs-safe-vst3-scanner.exe
data/obs-plugins/obs-safe-vst3/locale/en-US.ini
README-FIRST.txt
UNINSTALL-MANUAL.cmd
```

The release also publishes `SHA256SUMS.txt`.

Example verification:

```powershell
Get-FileHash .\OBS-Safe-VST3-Host-v0.4.0-Setup-x64.exe -Algorithm SHA256
```

## Compatibility scope and current limits

`v0.4.0` marks the **Single Host Phase S architecture and recovery/state invariants as stable**. It is not a claim that every third-party VST3 is compatible.

Current scope:

| Capability | v0.4.0 |
|---|---|
| Windows x64 | ✅ |
| VST3 audio effects | ✅ |
| Installed VST3 discovery | ✅ |
| Scanner crash isolation | ✅ |
| Runtime process isolation | ✅ |
| Native vendor editor | ✅ |
| Generic fallback controls | ✅ |
| Mono / stereo | ✅ |
| Float32 processing | ✅ |
| Full component/controller state persistence | ✅ |
| Last-known-good restore checkpoint | ✅ |
| Dedicated DSP/control isolation | ✅ |
| DSP watchdog + bounded restart backoff | ✅ |
| Dry fail-open on timeout/failure | ✅ |
| Complete `restartComponent()` I/O reconfiguration | Partial |
| Sidechain / multiple audio buses | Not yet |
| Instruments / MIDI | Not supported |
| Arbitrary multichannel / float64 fallback | Not yet |
| Linux/macOS runtime packages | Not yet |
| Safe VST3 Rack | Later phase |

## Safety model

### Runtime isolation

The VST3 binary and native editor run in `obs-safe-vst3-host.exe`, not in `obs64.exe`. Audio uses fixed shared-memory slots and a dedicated DSP wake event. Control, editor, state, restart, and disk work stay outside the OBS realtime audio callback.

A responsive control/UI thread is not required for the watchdog to consider DSP healthy: protocol v7 publishes separate control and DSP heartbeats. Conversely, a stalled DSP becomes watchdog-visible even if the helper process itself is still alive. Protocol v7 also publishes parameter metadata through a bounded coherent-catalog generation so OBS never consumes a mixed old/new parameter list.

### State consistency

State capture waits for an acknowledged paused DSP frontier, drains/flushes pending processor changes, and preserves the last-known-good checkpoint if a transient capture/storage operation fails. Corrupt or rejected snapshots are not promoted as good state.

### Scanner isolation

Standard Windows VST3 locations are scanned out-of-process. Each candidate is probed in its own short-lived child with a timeout so a bad candidate does not directly load into OBS.

### Not a malware sandbox

Crash/process isolation is not a security sandbox. Only load VST3 software you trust.

## SmartScreen / publisher warning

Binaries are not Authenticode-signed yet, so Windows may show **Unknown publisher** or a SmartScreen reputation warning. Do not disable Defender or SmartScreen for this project. Download only from this repository's Releases page and verify the published SHA-256 checksums when desired.

## Automated validation

CI and the Windows release pipeline validate:

- protocol v7 layout and portable parameter/catalog/state/recovery tests;
- SPSC FIFO/wrap/full-empty/concurrent producer-consumer behavior;
- Windows helper and isolated scanner build + scanner smoke test;
- watchdog classification for a live helper with a stalled DSP;
- control-thread stall while DSP heartbeat and passthrough audio remain alive;
- real WinObsBridge checkpoint restore across helper death/recreation;
- full OBS module compilation against pinned OBS/libobs 32.2.2;
- portable ZIP layout;
- Smart Installer install → uninstall → reinstall using a remembered portable target;
- SHA-256 generation for release assets.

Automated tests cannot prove compatibility with every commercial VST3. Please report plug-in-specific issues with OBS version, Windows version, VST3 name/version, relevant `[obs-safe-vst3]` log lines, and whether the issue affects scanning, GUI, state restore, DSP, or recovery.

## Engineering direction

Phase S intentionally keeps the product as a **simple, familiar, crash-resistant Single VST3 filter for OBS**. The broader parent roadmap can later add richer `restartComponent()` handling, sidechain/multi-bus support, compatibility hardening, and a separate Safe VST3 Rack without making the stable Single Host workflow complicated.

Architecture notes:

- [P1 architecture](docs/P1_ARCHITECTURE.md)
- [Original P0 isolation architecture](docs/P0-ARCHITECTURE.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Security policy](SECURITY.md)

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This independent open-source project is not endorsed by Steinberg or the OBS Project.
