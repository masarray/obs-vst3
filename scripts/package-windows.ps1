[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir,

    [Parameter(Mandatory = $true)]
    [string] $Version,

    [string] $OutDir,
    [string] $Iscc
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot 'dist'
}

$BuildDir = (Resolve-Path $BuildDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Find-BuildFile {
    param([Parameter(Mandatory = $true)][string] $Name)

    $Item = Get-ChildItem -Path $BuildDir -Filter $Name -File -Recurse |
        Sort-Object FullName |
        Select-Object -First 1

    if (-not $Item) {
        throw "Required build output not found: $Name under $BuildDir"
    }
    return $Item.FullName
}

$PluginDll = Find-BuildFile 'obs-safe-vst3.dll'
$HostExe = Find-BuildFile 'obs-safe-vst3-host.exe'
$ScannerExe = Find-BuildFile 'obs-safe-vst3-scanner.exe'
$LocaleFile = Join-Path $RepoRoot 'data\locale\en-US.ini'
if (-not (Test-Path $LocaleFile)) {
    throw "Locale file not found: $LocaleFile"
}

$PortableStage = Join-Path $OutDir 'portable-stage'
$InstallerPayload = Join-Path $RepoRoot 'installer\windows\payload'
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $PortableStage, $InstallerPayload

$PortableBin = Join-Path $PortableStage 'obs-plugins\64bit'
$PortableLocale = Join-Path $PortableStage 'data\obs-plugins\obs-safe-vst3\locale'
New-Item -ItemType Directory -Force -Path $PortableBin, $PortableLocale, $InstallerPayload | Out-Null

Copy-Item $PluginDll (Join-Path $PortableBin 'obs-safe-vst3.dll')
Copy-Item $HostExe (Join-Path $PortableBin 'obs-safe-vst3-host.exe')
Copy-Item $ScannerExe (Join-Path $PortableBin 'obs-safe-vst3-scanner.exe')
Copy-Item $LocaleFile (Join-Path $PortableLocale 'en-US.ini')

Copy-Item $PluginDll (Join-Path $InstallerPayload 'obs-safe-vst3.dll')
Copy-Item $HostExe (Join-Path $InstallerPayload 'obs-safe-vst3-host.exe')
Copy-Item $ScannerExe (Join-Path $InstallerPayload 'obs-safe-vst3-scanner.exe')
Copy-Item $LocaleFile (Join-Path $InstallerPayload 'en-US.ini')

$Readme = @"
OBS Safe VST3 Host v$Version - Phase S Stable
==============================================

QUICK INSTALL
1. Close OBS Studio completely.
2. Extract THIS ZIP directly into the OBS Studio root folder.
3. Verify these files exist:
   obs-plugins\64bit\obs-safe-vst3.dll
   obs-plugins\64bit\obs-safe-vst3-host.exe
   obs-plugins\64bit\obs-safe-vst3-scanner.exe
   data\obs-plugins\obs-safe-vst3\locale\en-US.ini
4. Start OBS Studio and add: VST 3.x Plug-in (Safe Host).
5. Click Rescan Installed Plug-ins.
6. Select an effect from Installed VST 3 Plug-in, or select Browse / Custom VST3...
   and browse to a development/non-standard bundle.
7. Confirm the status becomes:
   <plug-in name> - Ready - <N> samples latency
8. Click Open Plug-in Interface to open the vendor's real VST3 editor.

PHASE S STABLE FEATURES
- Installed VST3 discovery and isolated per-candidate scanner probing.
- Native vendor VST3 editor hosted by obs-safe-vst3-host.exe, not obs64.exe.
- Generic parameter controls as fallback when native editor is unavailable.
- Full opaque VST3 component + controller state persistence with CRC validation.
- Last-known-good state checkpoint used for helper recreation.
- Dedicated MMCSS Pro Audio DSP worker separated from vendor UI/control work.
- Protocol v6 with separate control and DSP heartbeats.
- Watchdog recovery for exited or live-but-hung DSP helpers.
- Bounded exponential restart backoff and stable-run reset.
- Fixed-capacity control/DSP queues and audio-first bounded command servicing.
- Fail-closed recovery on processor-delivery/flush/process contract failures.
- Dry fail-open audio when no valid wet result is available in time.

CURRENT SCOPE / LIMITATIONS
- Windows x64 only.
- Mono/stereo float32 audio effects only.
- Complete restartComponent() I/O reconfiguration is partial.
- Sidechain/multiple audio buses are not fully supported.
- VST3 instruments/MIDI and arbitrary multichannel are not supported.
- Safe VST3 Rack is a later phase; this release remains one effect per OBS filter.
- Phase S stable does not guarantee compatibility with every third-party VST3.

The scanner and VST3 DSP run outside OBS. Crash isolation is not a malware sandbox;
only load plug-ins you trust.

For a normal OBS Studio installation, use the Smart Installer from GitHub Releases.
To uninstall this manual package, close OBS and run UNINSTALL-MANUAL.cmd from the OBS root.
"@
Set-Content -Path (Join-Path $PortableStage 'README-FIRST.txt') -Value $Readme -Encoding UTF8

$Uninstall = @'
@echo off
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq obs64.exe" /NH | find /I "obs64.exe" >NUL
if not errorlevel 1 (
  echo OBS Studio is still running.
  echo Close OBS Studio first, then run this file again.
  pause
  exit /b 1
)
del /Q "obs-plugins\64bit\obs-safe-vst3.dll" 2>NUL
del /Q "obs-plugins\64bit\obs-safe-vst3-host.exe" 2>NUL
del /Q "obs-plugins\64bit\obs-safe-vst3-scanner.exe" 2>NUL
rmdir /S /Q "data\obs-plugins\obs-safe-vst3" 2>NUL
echo OBS Safe VST3 Host manual files removed.
pause
'@
Set-Content -Path (Join-Path $PortableStage 'UNINSTALL-MANUAL.cmd') -Value $Uninstall -Encoding ASCII

# UI-0 is a non-shipping dependency/window proof. Even when its standalone
# smoke executable exists elsewhere in the build tree, neither portable nor
# installer staging may acquire it implicitly.
$Ui0Leak = Get-ChildItem -Path $PortableStage, $InstallerPayload -Filter 'obs-safe-vst3-rack-ui-smoke*' -File -Recurse -ErrorAction SilentlyContinue
if ($Ui0Leak) {
    throw "UI-0 smoke proof leaked into a shipping package: $($Ui0Leak.FullName -join ', ')"
}

$PortableZip = Join-Path $OutDir "OBS-Safe-VST3-Host-v$Version-Windows-x64-Portable.zip"
Remove-Item -Force -ErrorAction SilentlyContinue $PortableZip
Compress-Archive -Path (Join-Path $PortableStage '*') -DestinationPath $PortableZip -CompressionLevel Optimal

if ($Iscc) {
    if (-not (Test-Path $Iscc)) {
        throw "Inno Setup compiler not found: $Iscc"
    }

    $IssFile = Join-Path $RepoRoot 'installer\windows\obs-safe-vst3.iss'
    & $Iscc "/DMyAppVersion=$Version" $IssFile
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compiler failed with exit code $LASTEXITCODE"
    }
}

$ReleaseFiles = Get-ChildItem -Path $OutDir -File |
    Where-Object { $_.Name -like '*.zip' -or $_.Name -like '*.exe' } |
    Sort-Object Name

if (-not $ReleaseFiles) {
    throw 'No release assets were produced.'
}

$ChecksumLines = foreach ($File in $ReleaseFiles) {
    $Hash = (Get-FileHash -Algorithm SHA256 -Path $File.FullName).Hash.ToLowerInvariant()
    "$Hash  $($File.Name)"
}
Set-Content -Path (Join-Path $OutDir 'SHA256SUMS.txt') -Value $ChecksumLines -Encoding ASCII

Write-Host "Release package created in: $OutDir"
$ReleaseFiles | ForEach-Object { Write-Host " - $($_.Name)" }
Write-Host ' - SHA256SUMS.txt'