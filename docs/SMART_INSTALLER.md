# Smart Installer behavior

The Windows installer uses one canonical target model for every OBS installation: the **OBS root folder that contains `bin\64bit\obs64.exe`**.

This avoids a class of failures where a plug-in is installed into a per-machine OBS plug-in directory while the user is actually running OBS in portable mode. On Windows, the root-local layout is valid for normal, Steam/custom, and portable-mode launches:

- plug-in binaries: `<OBS root>\obs-plugins\64bit`
- plug-in data: `<OBS root>\data\obs-plugins\obs-safe-vst3`

## Remembered OBS root

After a successful install, Setup stores the validated OBS root in:

`HKCU\Software\masarray\OBS Safe VST3 Host\LastObsRoot`

On the next installer run:

- the remembered root is reused when it still contains `bin\64bit\obs64.exe`;
- an invalid/stale remembered root is ignored;
- `C:\Program Files\obs-studio` is used as the initial default when it is a valid OBS root;
- `/ObsRoot="..."` explicitly selects the target for automation;
- `/PortableObsDir="..."` remains accepted as a backwards-compatible alias for older scripts/installers;
- uninstall intentionally does not erase the remembered target, so reinstall/update can return to the same OBS installation.

Before installing, Setup removes only this product's historical copies from the old ProgramData/AppData plug-in roots and from the selected OBS root. It never deletes unrelated OBS plug-ins.

## Running OBS handling

Interactive Setup detects `obs64.exe` before writing plug-in files.

1. It asks permission to close OBS and warns the user to stop/save active recording or streaming first.
2. With permission, Setup requests a normal close and waits up to 10 seconds.
3. If OBS is still running, Setup asks separately before any force-close attempt.
4. If permission is declined or OBS still cannot be closed, installation stops before replacing files.

Silent automation never force-closes OBS implicitly:

- `/CloseObs=yes` permits the normal close attempt only;
- `/CloseObs=force` permits a force-close fallback if the normal close times out;
- without either option, a silent install fails if OBS is running.

## Release regression test

The Windows release workflow performs a real installer round-trip against a synthetic OBS root:

1. install once with an explicit `/ObsRoot`;
2. verify `LastObsRoot` points to that exact tree;
3. verify DLL, isolated host, scanner, and locale files use the root-local OBS layout;
4. uninstall and verify those product files are removed;
5. install again without a target argument;
6. verify the remembered root is reused and all product files return to the same OBS tree;
7. uninstall again and verify cleanup.
