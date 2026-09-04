# Contributing to OBS Safe VST3 Host

Contributions and reproducible compatibility reports are welcome. This project is live-audio software, so changes are reviewed against **realtime safety, process isolation, recovery behavior and user clarity**, not only whether a feature works once.

## Before opening a pull request

1. Search existing issues and pull requests first.
2. Keep the change focused; avoid mixing architecture, UI polish and unrelated cleanup in one PR.
3. Describe the exact problem and the observable behavior before/after the change.
4. Add a deterministic regression test when fixing a reproducible bug at a stable seam.
5. Run the relevant local tests and let GitHub Actions qualify the exact PR head.

## Engineering invariants

Changes must preserve these product contracts unless an explicit architecture decision intentionally replaces them:

- Third-party VST3 runtime/vendor GUI code must not be deliberately loaded into `obs64.exe`.
- The OBS realtime audio callback must remain bounded; no unbounded waits, filesystem access, process lifecycle work, VST3 lifecycle calls or project-owned heap allocation should be introduced there.
- Helper failure or a missed wet-result deadline must preserve dry/pass-through behavior rather than intentionally stall the broadcast path.
- Scanner probing stays isolated from OBS.
- Single Host and Rack protocol/layout changes require explicit compatibility reasoning and regression coverage.
- Rack remains a separate helper/protocol product boundary from the Single Host.
- Compatibility workarounds belong in project code/policy, not by modifying the Steinberg SDK source tree.

## Building and testing

The repository uses CMake and C++20. Portable policy/protocol tests can be built without libobs; the full Windows module and release qualification use the pinned OBS/libobs workflow in GitHub Actions.

For a focused local test build, use the relevant test target or workflow described by the code/issue you are changing. Do not interpret a single local compile as release qualification.

Public Windows releases are expected to pass the applicable CI, compatibility, package and installer gates before being called stable.

## Compatibility reports

For a plug-in compatibility issue, include:

- Windows version;
- OBS Studio version;
- exact VST3 vendor, product and version;
- whether the problem occurs in **VST 3.x Plug-in**, **VST3 Rack**, or both;
- exact reproduction steps;
- expected vs actual behavior;
- whether audio remains dry/pass-through;
- relevant OBS log excerpts;
- whether the native vendor editor is involved.

Do **not** upload or redistribute commercial VST3 binaries unless their license explicitly permits it.

## Security reports

Do not publish exploit details as a normal issue when they could put users at risk. Follow [`SECURITY.md`](SECURITY.md) for the project's security-reporting policy.

## Pull request description

A strong PR explains:

- what changed and why;
- which realtime/process-isolation contracts are affected;
- tests added or executed;
- compatibility/package impact;
- user-visible documentation impact;
- any intentionally deferred work or known limitation.

Small, reviewable changes with exact evidence are preferred over broad rewrites.
