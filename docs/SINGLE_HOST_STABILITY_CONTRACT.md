# Single Host stability contract

This contract freezes the safety boundaries of the existing `VST 3.x Plug-in`
Single Host. Stabilization work must preserve these rules before adding new UX,
scanner lifecycle or compatibility behavior.

## Runtime invariants

1. Third-party VST3 code never loads into `obs64.exe`.
2. The OBS audio callback does only bounded audio IPC and fails open to dry audio.
3. VST3 lifecycle, state, controller and native-editor work remain in the isolated helper/control plane.
4. The native vendor editor is helper-owned; it is never embedded into the OBS process.
5. Helper crash/hang/recovery must not require rebuilding the OBS Properties tree.
6. Scanner vendor probing stays outside OBS and outside the isolated DSP helper.

## OBS Properties ownership

OBS owns the lifetime of `obs_properties_t` / `obs_property_t` used by its Qt
Properties view. A plug-in must not rebuild that tree from recovery, scanner or
other worker/runtime code.

For a user-driven property whose layout must change:

- update the selected identity/state in its property-modified callback;
- return `true` from that callback;
- let OBS queue `RefreshProperties` after `obs_property_modified()` returns.

Do not call `obs_source_update_properties()` from the Single Host module as a
shortcut for user-driven refresh. In OBS 32, `WidgetInfo::ControlChanged()` still
uses its `obs_property_t*` through `obs_property_modified()`, so an independent
full-tree rebuild can invalidate that pointer while the callback is active.

## Scanner contract

The scanner is a separate executable. Each discovered bundle is probed in its
own short-lived child process with a hard timeout. A probe child must prove its
scanner-parent watchdog before loading vendor code. Partial failures preserve
only the failed bundle's previous cache entries; an all-bundles failure keeps
the previous cache intact.

The crash-proof baseline intentionally keeps the OBS Rescan button synchronous.
Do not add background completion refresh into OBS Properties until there is a
separately proven UI-lifetime design and regression test for it.

## UX freeze

Stabilization must not redesign the Single Host workflow. Keep the established
installed-list / Rescan / custom Browse / status / Open Plug-in Interface /
generic-fallback model. Safe Rack and other broader UX belong behind separate
protocol/lifecycle seams.

## Required gates

- protocol/layout tests;
- state snapshot and recovery checkpoint tests;
- watchdog and control-stall/DSP survival tests;
- scanner discovery self-test;
- OBS Properties ownership guard;
- real OBS module compilation;
- manual OBS 32.2.2 regression: repeated Properties open/close, installed-list
  selection, Rescan, vendor editor open/hide, scene restore and helper recovery.

A branch is not release-ready merely because it compiles. The manual Properties
regression must pass without OBS crash before merge or installer publication.
