# Security Policy

## Crash isolation is not a malware sandbox

OBS Safe VST3 Host is designed so third-party VST3 code is not deliberately loaded into `obs64.exe`.

The stable Windows package uses separate process boundaries:

- `obs-safe-vst3-host.exe` — Single Host VST3 DSP and vendor GUI;
- `obs-safe-vst3-rack-host.exe` — Rack VST3 DSP, graphical Rack Editor and vendor-editor ownership;
- `obs-safe-vst3-scanner.exe` — isolated VST3 discovery/probing.

Those boundaries are intended to contain common plug-in crashes/hangs and to let the OBS audio path remain bounded, including dry/pass-through behavior when a valid isolated wet result is unavailable.

They do **not** make an untrusted VST3 safe to run. VST3 plug-ins are native code, and helper processes still run with the permissions of the current Windows user. Malicious native code could use files, network access, operating-system APIs or other resources available to that account.

**Only install VST3 software from vendors you trust.**

## Supported public release line

Security fixes are prioritized for the current stable release available from:

https://github.com/masarray/obs-vst3/releases/latest

When reporting a problem, include the exact OBS Safe VST3 Host version, OBS Studio version, Windows version and whether the issue affects the Single Host, VST3 Rack, scanner or installer.

## Windows package signing and integrity

Current Windows release binaries are not commercially Authenticode-signed. Windows can therefore show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen globally for this project. Download only from the official repository Releases page and verify the release `SHA256SUMS.txt` when you want an additional integrity check.

Plain-language safety model:

https://masarray.github.io/obs-vst3/safety.html

## Reporting security-sensitive issues

Avoid publishing exploit details in a normal issue when early disclosure could put users at risk. If GitHub presents a **Report a vulnerability** option for this repository, use that private channel. Otherwise, contact the repository owner through the contact method published on the owner's GitHub profile before public disclosure.

For normal compatibility, installation or plug-in-specific bugs, use the public GitHub issue tracker and include enough information to reproduce the problem.
