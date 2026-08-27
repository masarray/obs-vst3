# VST3 for OBS Studio — OBS Safe VST3 Host

**OBS's built-in VST filter still does not support VST3. Modern studio plug-ins increasingly ship as VST3-only. OBS Safe VST3 Host closes that gap with a native OBS filter and crash-isolated VST3 hosting for Windows.**

[![Stable v0.5.0](https://img.shields.io/badge/stable-v0.5.0-22c55e)](https://github.com/masarray/obs-vst3/releases/latest)
[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-vst3/releases/latest)
[![OBS 29.1+](https://img.shields.io/badge/OBS-29.1%2B-302E31?logo=obsstudio&logoColor=white)](https://obsproject.com/)
[![VST3 audio effects](https://img.shields.io/badge/VST3-audio%20effects-8b5cf6)](https://masarray.github.io/obs-vst3/compatibility.html)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)

> **Public stable: v0.5.0** · Native OBS VST3 audio-effect workflow · Isolated host process · Real vendor GUI · Automatic discovery · State recovery

**Product website:** https://masarray.github.io/obs-vst3/

## The missing VST3 layer for OBS

OBS Studio's official VST documentation currently lists **VST3.x as unsupported** by the built-in VST filter and warns that some VST plug-ins can crash OBS, recommending users save or back up settings when experimenting: https://obsproject.com/kb/vst-2-x-plugin-filter

That creates a growing gap for streamers, creators and broadcast engineers: many modern EQs, compressors, restoration tools, reverbs, saturators, limiters and mastering processors are now distributed primarily—or only—as **VST3**.

OBS Safe VST3 Host is designed to make those tools practical in OBS.

### What you get today

- **VST3 audio effects inside the normal OBS Filters workflow.**
- **Broad compatibility with popular studio VST3 effects**, without a vendor-name whitelist.
- **The real vendor plug-in GUI** brought to the foreground for immediate tweaking.
- **Crash-isolated runtime:** third-party VST3 DSP and GUI code run outside `obs64.exe`.
- **Crash-isolated scanning:** plug-in discovery also probes candidates outside OBS.
- **Dry fail-open audio:** an unhealthy wet path is not allowed to intentionally block the OBS realtime callback.
- **State persistence and recovery:** component/controller state can be restored after helper recreation.
- **A simple Windows installer** plus a portable/manual package.

The result is a practical path to using the same class of studio processing you use in a DAW while livestreaming in OBS.

## Download

### Recommended — Windows installer

**[Download the latest stable release](https://github.com/masarray/obs-vst3/releases/latest)** and choose:

`OBS-Safe-VST3-Host-v0.5.0-Setup-x64.exe`

Close OBS, run the installer, select your OBS folder, then start OBS again.

Portable/manual package:

`OBS-Safe-VST3-Host-v0.5.0-Windows-x64-Portable.zip`

Every release also includes `SHA256SUMS.txt`.

**Beginner guides:** [Install on Windows](https://masarray.github.io/obs-vst3/install.html) · [VST3 compatibility](https://masarray.github.io/obs-vst3/compatibility.html) · [Crash-isolation safety model](https://masarray.github.io/obs-vst3/safety.html) · [Product roadmap](ROADMAP.md)

## From studio processing to livestream audio

OBS Safe VST3 Host does not try to invent a new sound engine or replace the plug-ins you already trust. It gives OBS access to modern VST3 audio effects so you can build a live signal path with tools such as:

- corrective and dynamic EQ;
- compression, expansion and de-essing;
- noise reduction and restoration;
- saturation, coloration and harmonic processing;
- reverb, delay and spatial effects;
- limiting, maximization and mastering-style finishing processors.

That means a livestream can use **studio/mastering-grade processing tools** instead of being limited to the older VST formats available in OBS's built-in filter. Final audio quality still depends on the source, the plug-ins and how they are configured—the host's job is to make those tools usable and reliable inside the live workflow.

## Built to protect the show

A VST3 plug-in is native code. Loading third-party native code directly into the broadcast process creates an obvious failure boundary: if the plug-in fails badly, the host process can fail with it.

OBS Safe VST3 Host changes that boundary:

```text
OBS Studio (obs64.exe)
        │
        │ bounded audio/control IPC
        ▼
obs-safe-vst3-host.exe   ← isolated helper process
        │
        ├── VST3 DSP
        ├── native vendor GUI
        ├── state persistence
        └── watchdog / recovery
        │
        ▼
third-party VST3 audio effect
```

The third-party VST3 module and vendor interface run in the helper process, **not inside `obs64.exe`**. If the helper or plug-in becomes unhealthy, the filter is designed around bounded failure handling, dry fail-open audio and helper recovery instead of intentionally blocking OBS's realtime audio path.

This materially reduces the risk of a plug-in failure taking down OBS together with unsaved session changes. It is still not a mathematical guarantee that OBS can never crash: drivers, the operating system, hardware failures, OBS itself and malicious native code remain outside this project's control.

## Easy OBS workflow

1. In OBS, open an audio source → **Filters**.
2. Press **+** and add **VST 3.x Plug-in**.
3. Under **1. Plug-in Source**, keep **Installed plug-ins** or browse to a `.vst3` file.
4. Choose the effect under **2. Installed Plug-in**.
5. Click **3. Open Plug-in Interface**.
6. Tweak the real vendor GUI. It opens in front of OBS so you can work immediately.

Installed effects are discovered automatically. Use **Refresh Plug-in List** after installing a new VST3 while OBS is already running.

## Broad real-world VST3 compatibility

v0.5.0 targets conventional **Windows x64 VST3 audio effects** in the current mono/stereo Float32 host scope. It is not tied to a vendor whitelist.

Real-machine qualification has included popular commercial effects such as:

- **iZotope** — Ozone 11, RX 9 Spectral De-noise
- **FabFilter** — Pro-Q 3, Pro-C 2, Pro-R, Saturn
- **Waves** — SSL Channel
- **Klevgrand** — Brusfri
- **Neuro Audio** — Westwood
- **Process Audio** — Sugar

The scanner uses VST3 metadata conservatively: normal effects remain eligible, while plug-ins that explicitly identify as instrument-only are excluded from the OBS insert-effect list.

This gives the host broad compatibility across the kinds of VST3 effects commonly used in studio and mastering workflows, while keeping the current product scope honest. See the [live compatibility page](https://masarray.github.io/obs-vst3/compatibility.html) for details.

## Current stable scope

| Capability | v0.5.0 |
|---|---|
| Windows x64 | ✅ Supported |
| OBS Studio 29.1+ | ✅ Supported compatibility floor |
| VST3 audio effects | ✅ Supported |
| Installed plug-in discovery | ✅ |
| Native vendor editor | ✅ |
| Foreground editor workflow | ✅ |
| Mono / stereo Float32 | ✅ |
| Full VST3 state persistence | ✅ |
| Crash/hang isolation from `obs64.exe` | ✅ Architectural boundary |
| Instrument-only VST3 / MIDI | 🚧 Roadmap |
| Multi-effect VST3 rack / chains | 🚧 Roadmap |
| Sidechain / advanced multi-bus routing | 🚧 Roadmap |
| Arbitrary multichannel / Float64 fallback | 🚧 Roadmap |
| macOS / Linux packages | Not currently shipped |

Compatibility with a specific third-party effect can still depend on its VST3 implementation and version. Reproducible compatibility reports are welcome.

## Roadmap — from VST3 effects to a live audio platform

The long-term direction is bigger than a single effect slot.

### Stage 1 — Safe VST3 Effects — **available now**

Stable single-effect hosting, automatic discovery, native vendor editors, state recovery, crash-isolated runtime and scanner, and a simple OBS filter workflow.

### Stage 2 — Safe VST3 Rack

Multiple VST3 effects in one rack with ordered chains, bypass/reorder, rack presets, aggregate latency handling and a workflow designed for live use.

### Stage 3 — Routing and Sidechain

Flexible audio routing, sidechain paths, multi-bus handling and richer signal-flow control for broadcast chains.

### Stage 4 — MIDI and VST3 Instruments

VST3 instrument hosting, MIDI input/event routing, transport/clock-aware processing and a path toward playing software instruments directly in an OBS live-performance setup.

### Long-term vision

A creator should be able to build a **studio-quality live audio rig inside OBS**: effects chains for mastering-style stream processing, rack routing and sidechains for broadcast control, and eventually MIDI instruments for live performance—all through a workflow designed around isolation, recovery and reliability.

See [ROADMAP.md](ROADMAP.md) for the engineering roadmap and scope boundaries.

## Windows SmartScreen / Unknown publisher

The current Windows packages are **not commercially Authenticode-signed**, so Windows may show **Unknown publisher** or a SmartScreen reputation warning.

That warning is about publisher signing/reputation; it is not proof that the file is malicious. Keep Windows security enabled, download only from this repository's **Releases** page, and verify `SHA256SUMS.txt` when you want an additional integrity check.

Do **not** disable Defender or SmartScreen globally for this project.

## Safety in plain language

This project is designed for **crash containment**, not malware containment. A VST3 is native software and still runs with your Windows user permissions inside the helper process. Only use plug-ins from vendors you trust.

Read [Security and crash isolation](https://masarray.github.io/obs-vst3/safety.html) for the exact boundary.

## For developers and maintainers

The public user experience is intentionally simple. Engineering details remain available for anyone who wants to audit the implementation:

- [Product roadmap](ROADMAP.md)
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
