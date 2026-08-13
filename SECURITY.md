# Security policy

## Important trust boundary

The external helper provides **crash isolation**, not a security sandbox. A VST3 plug-in runs as native code with the permissions of the current user.
Only load plug-ins from vendors you trust.

Please report security-sensitive issues privately to the repository owner before public disclosure.
