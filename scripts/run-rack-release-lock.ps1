param(
    [string]$BuildRoot = 'build/release-lock'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Suites = @(
    @{ Name = 'R0-2 HostedPlugin Characterization'; Path = 'tests/r0_2' },
    @{ Name = 'R1-1 Rack Serial Tracer'; Path = 'tests/r1_1' },
    @{ Name = 'R1-2 Rack Safety Tracer'; Path = 'tests/r1_2' },
    @{ Name = 'R1-3 Rack Topology Tracer'; Path = 'tests/r1_3' },
    @{ Name = 'R1-4 Rack Recovery Tracer'; Path = 'tests/r1_4' },
    @{ Name = 'R2-1 Rack Session Snapshot'; Path = 'tests/r2_1' },
    @{ Name = 'R3-0 Rack Editor Bridge'; Path = 'tests/r3_0' },
    @{ Name = 'R3-1 OBS Rack Launcher'; Path = 'tests/r3_1' },
    @{ Name = 'R3-2 Rack Slot Browser'; Path = 'tests/r3_2' },
    @{ Name = 'R3-3 Rack Vendor Editor'; Path = 'tests/r3_3' },
    @{ Name = 'R3-4 Rack Preset UX'; Path = 'tests/r3_4' },
    @{ Name = 'P0 Rack Shutdown'; Path = 'tests/p0_shutdown' },
    @{ Name = 'P1 Rack Editor Polish'; Path = 'tests/p1_rack_editor' },
    @{ Name = 'R5-1 Stable Rack Lock'; Path = 'tests/r5_1' }
)

$ResolvedBuildRoot = Join-Path $RepoRoot $BuildRoot
New-Item -ItemType Directory -Force -Path $ResolvedBuildRoot | Out-Null

$SharedFetch = Join-Path $ResolvedBuildRoot '_fetchcontent'
New-Item -ItemType Directory -Force -Path $SharedFetch | Out-Null

$Summary = [System.Collections.Generic.List[string]]::new()
$Summary.Add('### Rack stable release focused gates')
$Summary.Add('')
$Summary.Add("Source commit: ``$env:GITHUB_SHA``")
$Summary.Add('')

foreach ($Suite in $Suites) {
    $Source = Join-Path $RepoRoot $Suite.Path
    if (-not (Test-Path $Source)) {
        throw "Focused release gate source is missing: $($Suite.Path)"
    }

    $Slug = ($Suite.Path -replace '[^A-Za-z0-9_.-]', '-')
    $Build = Join-Path $ResolvedBuildRoot $Slug
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Build

    Write-Host "::group::$($Suite.Name) — configure"
    & cmake -S $Source -B $Build -A x64 "-DFETCHCONTENT_BASE_DIR=$SharedFetch"
    if ($LASTEXITCODE -ne 0) {
        Write-Host '::endgroup::'
        throw "$($Suite.Name) configure failed with exit code $LASTEXITCODE"
    }
    Write-Host '::endgroup::'

    Write-Host "::group::$($Suite.Name) — build"
    & cmake --build $Build --config Release --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Host '::endgroup::'
        throw "$($Suite.Name) build failed with exit code $LASTEXITCODE"
    }
    Write-Host '::endgroup::'

    Write-Host "::group::$($Suite.Name) — test"
    & ctest --test-dir $Build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Host '::endgroup::'
        throw "$($Suite.Name) test failed with exit code $LASTEXITCODE"
    }
    Write-Host '::endgroup::'

    $Summary.Add("- PASS — $($Suite.Name)")
}

if ($env:GITHUB_STEP_SUMMARY) {
    $Summary | Add-Content -Path $env:GITHUB_STEP_SUMMARY -Encoding UTF8
}

Write-Host "All $($Suites.Count) focused Rack stable-release gates passed on $env:GITHUB_SHA"
