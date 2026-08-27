[CmdletBinding()]
param(
    [string] $BuildDir = 'build/ui0',
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = [IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDir))
$DependencyRoot = Join-Path $BuildRoot '_deps'
$ImguiRoot = Join-Path $DependencyRoot 'imgui'

$ImguiTag = 'v1.92.9'
$ImguiCommit = '01380c579715e62fb9a8d6ec0502c4ea83bfde6e'
$ImguiRepository = 'https://github.com/ocornut/imgui.git'

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $BuildRoot
New-Item -ItemType Directory -Force -Path $DependencyRoot | Out-Null

git clone --branch $ImguiTag --depth 1 $ImguiRepository $ImguiRoot
if ($LASTEXITCODE -ne 0) { throw "Dear ImGui clone failed: $LASTEXITCODE" }

$ActualCommit = (git -C $ImguiRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ActualCommit -ne $ImguiCommit) {
    throw "Dear ImGui pin mismatch. expected=$ImguiCommit actual=$ActualCommit"
}

$License = Join-Path $ImguiRoot 'LICENSE.txt'
if (-not (Test-Path $License)) { throw 'Dear ImGui LICENSE.txt missing' }
$LicenseText = Get-Content $License -Raw
foreach ($RequiredClause in @(
    'The MIT License (MIT)',
    'Copyright (c) 2014-2026 Omar Cornut',
    'Permission is hereby granted, free of charge',
    'THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND'
)) {
    if (-not $LicenseText.Contains($RequiredClause)) {
        throw "Dear ImGui MIT license clause missing: $RequiredClause"
    }
}
$LicenseHash = (Get-FileHash -Algorithm SHA256 $License).Hash.ToLowerInvariant()

$SourceDir = Join-Path $RepoRoot 'tests\ui0'
$CmakeBuild = Join-Path $BuildRoot 'cmake'
cmake -S $SourceDir -B $CmakeBuild -A x64 "-DIMGUI_SOURCE_DIR=$ImguiRoot"
if ($LASTEXITCODE -ne 0) { throw "UI-0 CMake configure failed: $LASTEXITCODE" }
cmake --build $CmakeBuild --config $Configuration --target obs-safe-vst3-rack-ui-smoke --parallel
if ($LASTEXITCODE -ne 0) { throw "UI-0 build failed: $LASTEXITCODE" }
ctest --test-dir $CmakeBuild -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "UI-0 smoke test failed: $LASTEXITCODE" }

$Smoke = Get-ChildItem $CmakeBuild -Filter 'obs-safe-vst3-rack-ui-smoke.exe' -File -Recurse |
    Where-Object { $_.FullName -match "[\\/]$Configuration[\\/]" -or $_.FullName -match '[\\/]bin[\\/]' } |
    Select-Object -First 1
if (-not $Smoke) { throw 'UI-0 smoke executable missing after successful build' }

$Dumpbin = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC' `
    -Filter dumpbin.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $Dumpbin) { throw 'dumpbin.exe not found for UI-0 PE evidence' }
$Imports = & $Dumpbin.FullName /DEPENDENTS $Smoke.FullName | Out-String
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed for UI-0 smoke executable' }
if ($Imports -notmatch '(?im)^\s*D3D11\.dll\s*$') {
    throw 'UI-0 smoke executable does not prove a D3D11 runtime dependency'
}

$Evidence = [ordered]@{
    imgui_tag = $ImguiTag
    imgui_commit = $ImguiCommit
    imgui_license = 'MIT'
    imgui_license_sha256 = $LicenseHash
    smoke_executable = $Smoke.FullName
    smoke_bytes = $Smoke.Length
    no_vst3_source_dependency = $true
}
$EvidencePath = Join-Path $BuildRoot 'ui0-evidence.json'
$Evidence | ConvertTo-Json | Set-Content -Path $EvidencePath -Encoding UTF8

Write-Host "UI-0 Dear ImGui pin: $ImguiTag @ $ImguiCommit"
Write-Host "UI-0 Dear ImGui LICENSE SHA256: $LicenseHash"
Write-Host "UI-0 smoke executable: $($Smoke.FullName)"
Write-Host "UI-0 smoke size: $($Smoke.Length) bytes"
Write-Host $Imports
Write-Host "UI-0 evidence: $EvidencePath"
