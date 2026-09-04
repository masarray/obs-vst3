$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$p2 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p1_skin.hpp')
$p3 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p3_premium.hpp')
$p4 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p4_live_meter.hpp')
$meter = Get-Content -Raw (Join-Path $root 'src\rack\rack_meter_telemetry.hpp')
$cmake = Get-Content -Raw (Join-Path $root 'src\rack\CMakeLists.txt')
$rc = Get-Content -Raw (Join-Path $root 'src\rack\rack_host.rc')
$iconPath = Join-Path $root 'src\rack\rack_host.ico'
$iconSourcePath = Join-Path $root 'src\rack\assets\OBS_Studio_Logo.svg'

function Require-Text([string]$haystack, [string]$needle, [string]$description) {
    if (-not $haystack.Contains($needle)) {
        throw "P4 Rack Editor contract missing: $description"
    }
}

# Qualified P2 base remains present under the premium layers.
Require-Text $p2 'C:\\Windows\\Fonts\\segoeui.ttf' 'Segoe UI regular font with Windows-local loading'
Require-Text $p2 'C:\\Windows\\Fonts\\seguisb.ttf' 'Segoe UI semibold hierarchy font'
Require-Text $p2 'ImGuiConfigFlags_NavEnableKeyboard' 'keyboard navigation'
Require-Text $p2 'health_color' 'slot health mapping'
Require-Text $p2 'aggregate_health_text' 'Rack aggregate status mapping'

# Premium P3 layout / hierarchy.
Require-Text $p3 'kPremiumSlotHeight = 74.0f' '74 px premium Rack slots'
Require-Text $p3 'kPremiumConsoleShare = 0.40f' '40 percent master-console share'
Require-Text $p3 'surface_app()' 'three-level tonal surface hierarchy'
Require-Text $p3 'surface_lane()' 'Rack lane surface'
Require-Text $p3 'surface_slot()' 'effect-strip surface'
Require-Text $p3 'OPEN##premium-slot-ui' 'clear vendor-editor OPEN affordance'
Require-Text $p3 'rack-premium-lane' 'premium left Rack lane'

# Realtime P4 metering. IN/OUT are authoritative; GR stays explicitly N/A.
Require-Text $p4 'g_rack_meter_telemetry.snapshot()' 'live atomic meter snapshot read'
Require-Text $p4 'kMeterReleaseDbPerSecond = 22.0f' 'meter release ballistics'
Require-Text $p4 'kPeakHoldSeconds = 0.80f' 'peak hold ballistics'
Require-Text $p4 'draw_live_meter' 'live vertical meter renderer'
Require-Text $p4 'METERING / PEAK dBFS' 'professional dBFS meter labelling'
Require-Text $p4 'GR remains deliberately non-fabricated' 'honest GR semantics'
Require-Text $p4 'const char* gr_value = "N/A"' 'GR disabled until authoritative telemetry exists'
Require-Text $p4 'IN %.1f dBFS' 'numerical input readout'
Require-Text $p4 'OUT %.1f dBFS' 'numerical output readout'

# Audio-thread telemetry must be lock-free and bounded.
Require-Text $meter 'std::atomic<std::uint32_t> input_peak_' 'atomic input peak transport'
Require-Text $meter 'std::atomic<std::uint32_t> output_peak_' 'atomic output peak transport'
Require-Text $meter 'std::atomic<std::uint64_t> sequence_' 'release/acquire sequence publication'
Require-Text $meter 'rack_meter_block_peak' 'bounded block peak scan'
Require-Text $meter 'rack_meter_linear_to_db' 'dBFS conversion'
if ($meter.Contains('std::mutex') -or $meter.Contains('condition_variable')) {
    throw 'P4 regression: realtime meter telemetry must not introduce locks'
}

# Shipping target instrumentation stays generated/source-local so older
# deterministic tracers keep compiling their checked-in Rack implementation.
Require-Text $cmake '_safevst3_meter_main_embed' 'generated shipping meter-instrumented Rack source'
Require-Text $cmake 'rack_meter_telemetry.hpp' 'meter telemetry include injection'
Require-Text $cmake 'g_rack_meter_telemetry.publish' 'DSP peak publication injection'
Require-Text $cmake 'rack_editor_p4_live_meter.hpp' 'P4 source-local force include'
Require-Text $cmake 'set_source_files_properties' 'visual isolation to Rack Editor translation unit'

# Native child-app identity must be attached to both the EXE and Win32 window.
Require-Text $cmake 'rack_host.rc' 'Rack EXE icon resource source'
Require-Text $cmake 'wc.hIcon' 'large Win32 Rack window icon'
Require-Text $cmake 'wc.hIconSm' 'small Win32 Rack window icon'
Require-Text $cmake 'MAKEINTRESOURCEW(101)' 'stable Rack icon resource id'
Require-Text $rc '101 ICON "rack_host.ico"' 'Rack icon resource declaration'
Require-Text $rc '80% of the icon canvas' 'smaller OBS companion icon treatment'
if (-not (Test-Path $iconSourcePath)) {
    throw 'P4 Rack Editor contract missing: maintainer-provided OBS_Studio_Logo.svg source'
}
$iconSource = Get-Content -Raw $iconSourcePath
Require-Text $iconSource '<title>OBS Studio</title>' 'OBS SVG source identity'

if (-not (Test-Path $iconPath)) {
    throw 'P4 Rack Editor contract missing: rack_host.ico'
}
$iconBytes = [System.IO.File]::ReadAllBytes($iconPath)
if ($iconBytes.Length -lt 256) {
    throw 'P4 Rack icon is unexpectedly small or empty'
}
if ($iconBytes[0] -ne 0 -or $iconBytes[1] -ne 0 -or
    $iconBytes[2] -ne 1 -or $iconBytes[3] -ne 0) {
    throw 'P4 Rack icon is not a valid Windows ICO container'
}
$iconImageCount = [BitConverter]::ToUInt16($iconBytes, 4)
if ($iconImageCount -lt 4) {
    throw 'P4 Rack icon must provide at least 16/32/48/256 px Windows icon variants'
}

if ($cmake.Contains('target_compile_options(obs-safe-vst3-rack-host') -and
    $cmake.Contains('rack_editor_p4_live_meter.hpp')) {
    throw 'P4 regression: visual skin must not be force-included into the whole Rack host target'
}

if ($p2.Contains('Waves') -or $p2.Contains('StudioRack') -or
    $p3.Contains('Waves') -or $p3.Contains('StudioRack') -or
    $p4.Contains('Waves') -or $p4.Contains('StudioRack')) {
    throw 'P4 regression: shipping Rack skin must not copy competitor branding or product names'
}

Write-Host 'P4 Rack Editor premium live-meter + app-icon contract: PASS'
