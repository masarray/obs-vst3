# R0-1 deterministic process characterization

This directory is a test-only pre-mutation gate for Rack R0-1.

It builds two minimal VST3 modules with the pinned Steinberg SDK already used by the repository:

- a fixed-mono effect;
- a fixed-stereo effect.

`vst3_engine_process_characterization.cpp` opens those modules through the production `Vst3Engine` and exercises the unchanged Single `process(AudioSlot&)` seam. The fixture encodes `projectTimeSamples` into deterministic audio so the test locks current block-position behavior in addition to direct mono/stereo processing, mono/stereo adaptation, validation failures, and VST3 process-error propagation.

This harness is non-shipping and must remain independent of the Single IPC protocol and Rack production runtime.
