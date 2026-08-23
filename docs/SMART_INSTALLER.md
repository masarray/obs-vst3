# Smart Installer behavior

The Windows installer is designed to make both standard OBS and portable OBS updates repeatable and safe.

## Portable target memory

After a successful install, Setup stores the last installation mode. For portable/custom mode it also stores the validated OBS root containing `bin\64bit\obs64.exe`.

On the next installer run:

- if the last mode was portable and the remembered root is still valid, Portable mode is preselected and the root is filled automatically;
- if the remembered root no longer contains `bin\64bit\obs64.exe`, Setup falls back to normal discovery/defaults instead of installing to a stale location;
- `/PortableObsDir="..."` always overrides remembered state for automation or explicit targeting;
- uninstall intentionally does not erase the remembered target, so reinstall/update remains one-click.

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

The Windows release workflow performs a real installer round-trip against a synthetic portable OBS tree:

1. install once with an explicit `/PortableObsDir`;
2. verify the remembered registry mode and portable root;
3. uninstall and verify plug-in files are removed;
4. install again without `/PortableObsDir`;
5. verify the remembered target is reused and all plug-in files return to the same portable OBS tree;
6. uninstall again and verify cleanup.
