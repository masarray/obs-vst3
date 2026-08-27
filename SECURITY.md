# Security policy

## The important distinction: crash isolation is not a malware sandbox

OBS Safe VST3 Host is designed so third-party VST3 DSP and vendor GUI code run in the external `obs-safe-vst3-host.exe` helper instead of being deliberately loaded inside `obs64.exe`.

That process boundary is intended to contain common plug-in crashes and hangs and to let the OBS filter fail open to dry audio while recovery is attempted.

It does **not** make an untrusted VST3 safe to run. A VST3 is native code and the helper process still runs with the permissions of the current Windows user. A malicious plug-in could use files, network access, operating-system APIs or other resources available to that account.

**Only install VST3 software from vendors you trust.**

## Windows package signing

Current Windows release binaries are not commercially Authenticode-signed. Windows can therefore show **Unknown publisher** or a SmartScreen reputation warning.

Do not disable Defender or SmartScreen globally for this project. Download only from the official repository Releases page and verify the release `SHA256SUMS.txt` when you want to confirm file integrity.

Official releases: https://github.com/masarray/obs-vst3/releases/latest

Plain-language safety explanation: https://masarray.github.io/obs-vst3/safety.html

## Reporting security-sensitive issues

Please report security-sensitive issues privately to the repository owner before public disclosure. For normal compatibility, installation or plug-in-specific bugs, use the public GitHub issue tracker and include enough information to reproduce the problem.
