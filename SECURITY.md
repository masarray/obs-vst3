# Security policy

## Crash isolation is not a malware sandbox

OBS Safe VST3 Host deliberately keeps third-party VST3 DSP and vendor GUI code outside `obs64.exe`:

- the **Single** `VST 3.x Plug-in` filter uses `obs-safe-vst3-host.exe` for one effect;
- **VST3 Rack** uses one `obs-safe-vst3-rack-host.exe` process per Rack, containing the Rack Editor and up to eight serial VST3 effects.

These process boundaries are intended to contain common plug-in crashes/hangs and let the OBS-side filter prefer dry/fail-open behavior instead of waiting indefinitely on vendor code.

Rack isolation is **not** one process per slot. A severe Rack-helper failure is a Rack-level process failure. Finer correlated per-slot repeated-failure quarantine/recovery remains hardening work.

The process boundary also does **not** make an untrusted VST3 safe to run. A VST3 is native code and the helper still runs with the permissions of the current Windows user. A malicious plug-in could use files, network access, operating-system APIs or other resources available to that account.

**Only install VST3 software from vendors you trust.**

## Bounded Rack shutdown

v0.6.0 requests Rack shutdown before waiting, wakes the relevant helper IPC paths, and uses a short bounded graceful-exit budget before forcing a stuck Rack helper to terminate. This is a responsiveness/failure-containment measure; it is not a security sandbox.

## Windows package signing

Current Windows release binaries are not commercially Authenticode-signed. Windows can therefore show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen globally for this project. Download only from the official repository Releases page and verify the release `SHA256SUMS.txt` when you want to confirm file integrity.

Official releases: https://github.com/masarray/obs-vst3/releases/latest

Plain-language safety explanation: https://masarray.github.io/obs-vst3/safety.html

## Reporting security-sensitive issues

Please report security-sensitive issues privately to the repository owner before public disclosure. For normal compatibility, installation or plug-in-specific bugs, use the public GitHub issue tracker and include enough information to reproduce the problem.