# Contributing

Contributions are welcome.

1. Keep the OBS realtime callback allocation-free and mutex-free in steady state.
2. Do not load third-party VST3 binaries in the OBS process.
3. Preserve dry pass-through on helper failure or deadline miss.
4. Keep compatibility workarounds out of the Steinberg SDK source tree.
5. Add tests for protocol changes and bump the protocol version when binary layout compatibility changes.

Use focused pull requests and describe how realtime safety was validated.
