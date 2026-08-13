# OBS Safe VST3 Host

**Use modern VST3 audio effects in OBS Studio without loading the VST3 binary inside the OBS process.**

[![Windows x64](https://img.shields.io/badge/Windows-x64-0078D4?logo=windows11&logoColor=white)](https://github.com/masarray/obs-safe-vst3-host/releases)
[![Public Preview](https://img.shields.io/badge/status-public%20preview-f59e0b)](https://github.com/masarray/obs-safe-vst3-host/releases)
[![GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-22c55e)](LICENSE)
[![Open source](https://img.shields.io/badge/source-open-8b5cf6)](https://github.com/masarray/obs-safe-vst3-host)

> **Current public release: v0.3.0-preview.2 — Windows x64 Public Preview.**  
> Recommended for real-world testing, streaming setups, and feedback. It is not being presented as universally compatible or final/stable yet.

## The simple explanation

OBS has a built-in VST2 filter, but many newer audio plug-ins are VST3-only. OBS Safe VST3 Host adds a **VST 3.x Plug-in** audio filter and runs the third-party VST3 in a separate helper process.

That separation matters: if a VST3 plug-in crashes or stops responding, it does not run inside the OBS process. The host is designed to keep OBS alive and fall back to the original dry audio when processing fails or misses its realtime deadline.

**You do not need to understand VST3 internals, IPC, or DSP threading to use it.**

## Download

### Recommended — Windows Smart Installer

Download the newest **Setup x64** file from:

**[GitHub Releases →](https://github.com/masarray/obs-safe-vst3-host/releases)**

For the current public preview, the recommended file is:

`OBS-Safe-VST3-Host-v0.3.0-preview.2-Setup-x64.exe`

The installer asks you to confirm the exact OBS Studio folder you actually use, installs the plug-in to the correct OBS directories, checks that OBS is closed before copying files, and registers a normal Windows uninstaller.

### Portable / manual install

Advanced users and portable OBS installations can use:

`OBS-Safe-VST3-Host-v0.3.0-preview.2-Windows-x64-Portable.zip`

The portable package contains the OBS module, isolated VST3 host, scanner, locale files, install/uninstall scripts, and SHA-256 checksums.

## Install in 3 steps

1. **Close OBS Studio.**
2. Run the downloaded **Setup x64** installer and confirm the OBS Studio folder you really launch.
3. Open OBS → choose your microphone/audio source → **Filters** → **+** → **VST 3.x Plug-in**.

Then choose a plug-in from **Installed VST 3 Plug-in**, use **Rescan Installed Plug-ins** when needed, or browse to a custom `.vst3` bundle. For plug-ins with their own GUI, click **Open Plug-in Interface**.

That is the normal workflow. No manual Class ID, deadline tuning, or developer settings should be required in the current preview.

## What you should see in OBS

The current UI uses beginner-facing controls such as:

- **Installed VST 3 Plug-in**
- **Rescan Installed Plug-ins**
- **Browse / Custom VST3**
- **Open Plug-in Interface**
- **Plug-in Controls** as a fallback when the VST3 has no graphical editor

### If you still see old controls

If the properties panel shows old fields such as **Enabled**, **VST3 Class ID**, or **Live deadline fraction**, OBS is loading an older development copy.

Re-run the current Preview 2 installer, confirm the exact OBS Studio folder you launch, and let Setup remove known legacy project-owned copies that can shadow the new module.

An existing filter instance name on the left can still say something like `Safe VST3 Host (P1)` because OBS saves instance names in the scene. You can rename or re-add that filter. The **right-side properties** are what identify the current build.

## “Windows Defender / SmartScreen says Unknown publisher. Is it malware?”

The current public-preview installer is **not yet Authenticode code-signed**, so Windows SmartScreen can show an **Unknown publisher** or reputation warning on some PCs. That warning is about publisher identity/reputation; by itself it is not a malware verdict.

This project does **not** ask you to disable Microsoft Defender, antivirus, or SmartScreen.

For the safest install path:

1. Download only from this repository's official **GitHub Releases** page.
2. Download the published `SHA256SUMS.txt` beside the release.
3. Verify your file before running it:

```powershell
Get-FileHash .\OBS-Safe-VST3-Host-v0.3.0-preview.2-Setup-x64.exe -Algorithm SHA256
```

4. Compare the result with the checksum published by the same GitHub Release.
5. If the file origin or checksum does not match, **do not run it**.

For Preview 2, GitHub currently publishes the Setup SHA-256 as:

```text
5e4fda9798a864d0db4bb072c9006f4cbe5afa6c131fa81ab5fc201841d7b1e4
```

If a future release has a different filename, use that release's own `SHA256SUMS.txt` instead of this older value.

## Why this host is different

### VST3 code stays outside OBS

The third-party VST3 module is loaded by `obs-safe-vst3-host.exe`, not directly by `obs64.exe`.

```text
OBS Studio
   │
   │ bounded shared-memory audio
   ▼
OBS Safe VST3 Host helper process
   │
   ▼
Your VST3 audio effect
```

Control/state/editor traffic is kept separate from the realtime audio path. If the helper exits or a block misses its bounded deadline, the OBS filter is designed to leave the original dry audio buffer untouched instead of waiting forever.

### Crash isolation is not a malware sandbox

The separate process helps contain plug-in crashes and hangs. It does **not** make an untrusted VST3 safe. Only load VST3 plug-ins you trust.

## Compatibility

| Item | Public Preview status |
|---|---|
| Windows x64 | ✅ Supported target |
| VST3 audio effects | ✅ Supported |
| Mono / stereo | ✅ Supported |
| Float32 processing | ✅ Supported |
| Plug-in native GUI | ✅ Supported when the VST3 provides one |
| Automatic installed-VST3 discovery | ✅ Included in current preview |
| VST2 | ➖ Use OBS's existing VST2 filter instead |
| VST3 instruments / MIDI | ❌ Not supported |
| 32-bit VST3 | ❌ Not supported |
| Sidechain | ❌ Not yet |
| Arbitrary multichannel buses | ❌ Not yet |
| Linux public package | ❌ Not included in this Windows preview |
| Full DAW-style cross-source PDC | ❌ Not yet |

The release pipeline is validated against **OBS/libobs 31.1.1**. Compatibility with the huge VST3 ecosystem varies by vendor, GUI technology, licensing system, and plug-in behavior, which is why the project remains a Public Preview.

## What has actually been tested automatically

The Windows release pipeline does more than compile the project. Current automated acceptance includes:

- real `libobs` module loading and registration;
- a real Steinberg ADelay VST3 fixture;
- Steinberg Validator with **57 tests passed, 0 failed** for the fixture;
- parameter changes reaching DSP processing;
- real audio processing;
- component/controller state round-trip;
- repeated audio-slot processing;
- forced helper-process termination;
- dry fail-open behavior after helper termination;
- crash-isolated scanner tests, including malformed/crashing probes;
- Smart Installer compilation and synthetic OBS install smoke testing;
- release SHA-256 generation.

Automation cannot represent every commercial/free VST3 in the world. Representative desktop testing remains important, especially for native editors, licensing systems, dynamic latency, long sessions, and unusual plug-in behavior.

## Troubleshooting

### The filter does not appear

- Confirm you are using **Windows x64 OBS Studio**.
- Close OBS and run the newest Setup again.
- Confirm the installer path is the same OBS installation you actually launch.
- Start OBS again and check **Filters → + → VST 3.x Plug-in**.

### My VST3 is not in the installed list

- Click **Rescan Installed Plug-ins**.
- Confirm the plug-in is a **64-bit VST3 audio effect**, not VST2 or an instrument-only VST3.
- Use **Browse / Custom VST3** for a non-standard installation location.

### The plug-in has no graphical interface

Some VST3 effects do not provide a native editor. The host can show **Plug-in Controls** as a generic parameter fallback.

### OBS shows the old P1/P0-style panel

You almost certainly have an older copy being loaded from another OBS installation or a legacy plug-in location. Re-run the newest installer and confirm the exact OBS root you launch.

### A plug-in crashes or stops processing

The isolation boundary is designed so the VST3/helper failure does not become an in-process OBS crash. The affected processing can fall back to dry audio. Automatic restart/recovery policy is still being developed, so you may need to reload the filter or restart OBS after a failed third-party plug-in.

### Still stuck?

Open an issue and include:

- OBS version;
- Windows version;
- VST3 plug-in name + version;
- whether the plug-in GUI opens;
- what you expected vs what happened;
- relevant OBS log excerpt if available.

**[Report a problem →](https://github.com/masarray/obs-safe-vst3-host/issues)**

## For developers and technical reviewers

The beginner-facing README intentionally keeps implementation detail out of the first-use path. Engineering documentation remains available separately:

- [P1 architecture](docs/P1_ARCHITECTURE.md)
- [P0 architecture / original isolation spike](docs/P0-ARCHITECTURE.md)
- [Windows installation notes](INSTALL-WINDOWS.txt)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

Core realtime design principles include a fixed shared-memory audio ring, bounded waits, no VST3 binary in the OBS process, a dedicated helper DSP thread, and dry fail-open behavior.

## Project status

**v0.3.0-preview.2 is a public preview, not a universal “stable” claim.**

The project is deliberately conservative about that label because VST3 compatibility is not just “does it compile?” — it includes vendor GUIs, licensing, state persistence, latency changes, long sessions, malformed plug-ins, and failure behavior while OBS is live.

If it works with your VST3, please report the plug-in name/version. Real compatibility reports are valuable for building a trustworthy compatibility matrix.

## License and trademarks

Project code: **GPL-3.0-or-later**. See [LICENSE](LICENSE).

The Steinberg VST3 SDK used by the build is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a trademark of Steinberg Media Technologies GmbH. OBS is a trademark of its respective owners. This is an independent open-source project and is not endorsed by Steinberg or the OBS Project.
