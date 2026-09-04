# VST3 for OBS Studio — OBS Safe VST3 Host

**Native VST3 audio effects for OBS Studio on Windows, with crash-isolated single-plug-in hosting and a graphical serial VST3 Rack.**

[![Stable v0.6.0](https://img.shields.io/badge/stable-v0.6.0-22c55e)](https://github.com/masarray/obs-vst3/releases/latest)
[![Windows x64](https://img.shields.io/badge/Windows-x64-2563eb)](https://github.com/masarray/obs-vst3/releases/latest)
[![OBS 29.1+](https://img.shields.io/badge/OBS-29.1%2B-8b5cf6)](https://masarray.github.io/obs-vst3/compatibility.html)

OBS Safe VST3 Host adds two separate OBS audio filters:

- **`VST 3.x Plug-in`** — the proven one-effect Safe Host.
- **`VST3 Rack`** — a graphical serial chain for up to **8 mono/stereo Float32 VST3 effects**.

Third-party VST3 code runs outside `obs64.exe`. The Single filter uses `obs-safe-vst3-host.exe`; each Rack uses one separate `obs-safe-vst3-rack-host.exe`. This process boundary is designed to contain common plug-in crashes/hangs and let OBS fail dry instead of deliberately loading vendor DSP/UI code into the OBS process.

> Crash isolation is **not** a malware sandbox. VST3 plug-ins are native code. Only install plug-ins from vendors you trust.

## v0.6.0 — VST3 Rack

v0.6.0 promotes the Rack workflow from engineering preview to the normal Windows package.

### Graphical Rack Editor

- Search the installed VST3 catalog.
- Add and insert effects into a serial chain.
- Reorder slots by drag/drop or menu fallback.
- Enable / bypass individual slots.
- Replace or remove effects.
- Open each plug-in's native vendor editor in a floating window.
- See per-slot health and latency plus aggregate Rack latency.
- Run up to **8** serial VST3 audio effects.
- Professional dark Rack UI with compact cards, clearer hierarchy, keyboard navigation and explicit interaction states.

The Rack Editor belongs to the external Rack helper, not to `obs64.exe`. Closing the Rack window does not stop Rack DSP.

### Named Rack presets

The Rack now includes a user-level preset library:

- **Save As Preset**
- browse/select/load
- **Update Preset**
- Rename
- Delete with confirmation
- stable preset identity
- atomic/versioned persistence with recovery support

Loading a preset creates an independent working Rack. Later edits to that Rack do **not** mutate the saved preset unless **Update Preset** is explicitly chosen.

If a plug-in referenced by a preset cannot be opened, the slot remains represented as a **Missing/pass-through placeholder** so the rest of the Rack can still be reconstructed and the saved topology/state is not silently discarded.

### Faster bounded Rack shutdown

Real OBS testing exposed that a stuck Rack helper could make OBS take noticeably longer to close. v0.6.0 publishes shutdown immediately, wakes the helper IPC paths and bounds the helper wait to a short grace period before process termination. A deterministic Windows hung-helper test completes in roughly 0.30 s, and real OBS 32.1.2 testing confirmed noticeably faster application close than the pre-fix build.

## Installation

### Recommended: Smart Installer

1. Close OBS Studio completely.
2. Download `OBS-Safe-VST3-Host-v0.6.0-Setup-x64.exe` from the latest GitHub Release.
3. Run the installer and select/confirm the OBS Studio root when requested.
4. Start OBS.

The package installs:

```text
obs-plugins\64bit\obs-safe-vst3.dll
obs-plugins\64bit\obs-safe-vst3-host.exe
obs-plugins\64bit\obs-safe-vst3-rack-host.exe
obs-plugins\64bit\obs-safe-vst3-scanner.exe
data\obs-plugins\obs-safe-vst3\locale\en-US.ini
```

A portable ZIP and `SHA256SUMS.txt` are published alongside the installer.

Full guide: **https://masarray.github.io/obs-vst3/install.html**

## Using the Single filter

1. In OBS, open **Filters** for an audio source.
2. Add **`VST 3.x Plug-in`**.
3. Choose **Installed plug-ins** and select a discovered VST3 effect.
4. Open the plug-in UI when needed.

This remains the simplest path when one VST3 effect per OBS filter is enough.

## Using VST3 Rack

1. In OBS, open **Filters** for an audio source.
2. Add **`VST3 Rack`**.
3. Use **Open Rack** in Properties.
4. In the Rack Editor, search for a plug-in and choose **Add Effect**.
5. Add more effects, reorder/bypass them, or use **Open UI** for the vendor editor.
6. Save the chain with **Save As** when you want a reusable Rack preset.

The Rack is a **serial** signal path:

```text
OBS input → Slot 1 → Slot 2 → … → Slot 8 → OBS output
```

There is **one isolated Rack helper per Rack**, not one worker process per slot.

## Safety architecture

### Single

```text
OBS / obs64.exe
  │ shared-memory + control IPC
  ▼
obs-safe-vst3-host.exe
  └─ one VST3 effect + vendor GUI
```

### Rack

```text
OBS / obs64.exe
  │ Rack IPC
  ▼
obs-safe-vst3-rack-host.exe
  ├─ Rack Editor
  ├─ Slot 1 VST3
  ├─ Slot 2 VST3
  └─ … up to Slot 8
```

Core behavior includes dry fail-open processing when a valid isolated wet block is unavailable in time, bounded helper shutdown, scanner isolation, state/session persistence and immutable Rack generation swaps away from the DSP path.

## Current scope

| Capability | v0.6.0 |
|---|---|
| Windows x64 | ✅ |
| OBS compatibility floor | **29.1+** |
| Single VST3 audio effects | ✅ |
| Graphical VST3 Rack | ✅ |
| Rack length | Up to **8** serial effects |
| Audio buses | Mono / stereo |
| Sample type | Float32 |
| Native vendor editors | ✅ |
| Named Rack presets | ✅ |
| Missing preset placeholders | ✅ |
| Rack aggregate latency | ✅ |
| Parallel graph / patchbay | ❌ |
| Sidechain routing | ❌ |
| MIDI / VST3 instruments | ❌ |
| Nested Racks | ❌ |
| Float64 / arbitrary multichannel | ❌ |
| macOS / Linux | ❌ |

Per-slot correlated crash quarantine/recovery and broader commercial Rack stress qualification remain hardening work; v0.6.0 does not claim every third-party VST3 is compatible.

## Qualification

The v0.6.0 Rack line is covered by deterministic Rack serial/safety/topology/recovery/session/editor/browser/vendor-editor/preset gates, CI and Windows Compatibility builds. Compatibility qualification builds against OBS 32.2.2 SDK and includes a real-loader/ABI-floor probe using official OBS 29.1.3 runtime.

Real user smoke testing on OBS 32.1.2 confirmed serial Rack audio, per-slot enable/bypass, native VST3 vendor UI and the improved shutdown path.

## Links

- **Download:** https://github.com/masarray/obs-vst3/releases/latest
- **Landing page:** https://masarray.github.io/obs-vst3/
- **Install guide:** https://masarray.github.io/obs-vst3/install.html
- **Compatibility:** https://masarray.github.io/obs-vst3/compatibility.html
- **Safety:** https://masarray.github.io/obs-vst3/safety.html
- **Roadmap:** https://masarray.github.io/obs-vst3/roadmap.html
- **Issues:** https://github.com/masarray/obs-vst3/issues

## License

See [LICENSE](LICENSE) and third-party notices in the repository/package.