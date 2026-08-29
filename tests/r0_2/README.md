# R0-2 HostedPlugin characterization

This focused Windows-only harness is the R0-2 extraction gate for issue #66.

It builds a deterministic real VST3 effect with a separated edit controller and exercises the new protocol-neutral `HostedPlugin` object directly. The characterization locks:

- closed/open/close/reopen lifecycle;
- `ProcessBlockView` processing without Single `AudioSlot` API;
- fixed latency exposure and latency-restart transaction;
- component + controller state capture/restore with observable audio state;
- helper-facing edit-controller accessor lifetime;
- deep header/source contract rejecting Single transport types/layout includes and `Vst3Engine` dependency;
- focused target links `HostedPlugin` without the Single `vst3_engine.cpp` adapter.

The fixture is a correctness oracle only. It does not implement Rack transport, Rack helper, multi-plugin processing, graphical Rack UI, scanner changes or any R1 behavior.
