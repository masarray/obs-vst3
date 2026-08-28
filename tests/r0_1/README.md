# R0-1 deterministic process characterization

This directory is the deterministic real-VST3 gate for Rack R0-1.

It builds two minimal VST3 modules with the pinned Steinberg SDK already used by the repository:

- a fixed-mono effect;
- a fixed-stereo effect.

The gate was deliberately established in two stages. First, before any production process mutation, `vst3_engine_process_characterization.cpp` opened the fixture modules through the production `Vst3Engine` and exercised the unchanged Single `process(AudioSlot&)` seam. That pre-mutation proof passed on source head `819e394949184ed64d86901b0cf3e13a3306c6ea` in R0-1 Process Seam Characterization run `33135381993`.

After that gate passed, R0-1 introduced `ProcessBlockView`. The same harness now keeps all of the original AudioSlot characterization and also drives the protocol-neutral process entry with caller-owned raw buffers, requiring its deterministic output to match an independently opened Single AudioSlot engine. It also verifies invalid neutral buffer tables fail cleanly.

The fixture encodes `projectTimeSamples` into deterministic audio, so the test locks current block-position behavior in addition to direct mono/stereo processing, mono/stereo adaptation, validation failures, and VST3 process-error propagation.

This harness is non-shipping. It does not define or change the Single IPC protocol and it contains no Rack production runtime.
