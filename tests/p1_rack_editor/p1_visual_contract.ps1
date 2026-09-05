$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$p2 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p1_skin.hpp')
$p3 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p3_premium.hpp')
$p4 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p4_live_meter.hpp')
$p5 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p5_broadcast.hpp')
$meter = Get-Content -Raw (Join-Path $root 'src\rack\rack_meter_telemetry.hpp')
$loudness = Get-Content -Raw (Join-Path $root 'src\rack\rack_broadcast_loudness.hpp')
$cmake = Get-Content -Raw (Join-Path $root 'src\rack\CMakeLists.txt')
$rc = Get-Content -Raw (Join-Path $root 'src\rack\rack_host.rc')
$iconPath = Join-Path $root 'src\rack\rack_host.ico'
$iconSourcePath = Join-Path $root 'src\rack\assets\OBS_Studio_Logo.svg'

function Require-Text([string]$haystack, [string]$needle, [string]$description) {
    if (-not $haystack.Contains($needle)) {
        throw "P5 Rack Editor contract missing: $description"
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

# Realtime P4 peak metering remains the compact LEVEL source; GR is honest N/A.
Require-Text $p4 'g_rack_meter_telemetry.snapshot()' 'live atomic meter snapshot read'
Require-Text $p4 'kMeterReleaseDbPerSecond = 22.0f' 'meter release ballistics'
Require-Text $p4 'kPeakHoldSeconds = 0.80f' 'peak hold ballistics'
Require-Text $p4 'draw_live_meter' 'live vertical meter renderer'
Require-Text $p4 'GR remains deliberately non-fabricated' 'honest GR semantics'
Require-Text $p4 'const char* gr_value = "N/A"' 'GR disabled until authoritative telemetry exists'

# P5 makes the master side visually quiet: status -> LEVEL -> LOUDNESS, with two
# large broadcast metrics instead of adding a dense technical dashboard.
Require-Text $p5 'render_broadcast_master_console' 'minimal broadcast master console'
Require-Text $p5 'ImGui::TextDisabled("LEVEL")' 'compact LEVEL section'
Require-Text $p5 'ImGui::TextDisabled("LOUDNESS")' 'compact LOUDNESS section'
Require-Text $p5 '"LUFS-I"' 'Integrated LUFS focal metric'
Require-Text $p5 '"dBTP"' 'true-peak focal metric'
Require-Text $p5 '25.0f' 'large loudness metric typography'
Require-Text $p5 'rack-premium-console-broadcast' 'P5 broadcast console surface'
Require-Text $p5 'Maximum reconstructed true peak' 'dBTP semantic tooltip'
Require-Text $p5 'Integrated programme loudness' 'LUFS-I semantic tooltip'

# Audio-thread sample-peak telemetry must remain lock-free and bounded.
Require-Text $meter 'std::atomic<std::uint32_t> input_peak_' 'atomic input peak transport'
Require-Text $meter 'std::atomic<std::uint32_t> output_peak_' 'atomic output peak transport'
Require-Text $meter 'std::atomic<std::uint64_t> sequence_' 'release/acquire peak publication'
Require-Text $meter 'rack_meter_block_peak' 'bounded block peak scan'
if ($meter.Contains('std::mutex') -or $meter.Contains('condition_variable')) {
    throw 'P5 regression: realtime peak telemetry must not introduce locks'
}

# Authoritative broadcast loudness / true peak engine.
Require-Text $loudness 'kLoudnessOffset = -0.691' 'BS.1770 loudness offset'
Require-Text $loudness 'kAbsoluteGateLufs = -70.0' 'BS.1770 absolute gate'
Require-Text $loudness 'energy_to_lufs(absolute_mean) - 10.0' 'relative gate 10 LU below absolute-gated mean'
Require-Text $loudness 'std::array<double, 4> subblock_energies_' '400 ms window built from four 100 ms subblocks'
Require-Text $loudness 'kTruePeakTaps = 33' '33-tap true-peak reconstruction'
Require-Text $loudness 'kTruePeakPhases' 'quarter-sample 4x polyphase reconstruction'
Require-Text $loudness 'std::atomic<std::int32_t> integrated_millilu_' 'lock-free LUFS-I publication'
Require-Text $loudness 'std::atomic<std::int32_t> true_peak_millidb_' 'lock-free dBTP publication'
if ($loudness.Contains('std::mutex') -or $loudness.Contains('condition_variable') -or
    $loudness.Contains('std::vector')) {
    throw 'P5 regression: broadcast meter audio path must remain allocation-free and lock-free'
}

# Shipping instrumentation remains generated/source-local so older deterministic
# tracers continue compiling the checked-in qualified Rack implementation.
Require-Text $cmake '_safevst3_meter_main_embed' 'generated shipping meter-instrumented Rack source'
Require-Text $cmake 'rack_meter_telemetry.hpp' 'peak telemetry include injection'
Require-Text $cmake 'rack_broadcast_loudness.hpp' 'broadcast loudness include injection'
Require-Text $cmake 'g_rack_broadcast_loudness.process' 'final-output LUFS-I/dBTP processing injection'
Require-Text $cmake 'endpoint.region->sample_rate' 'actual Rack sample-rate handoff'
Require-Text $cmake 'rack_editor_p5_broadcast.hpp' 'P5 source-local force include'
Require-Text $cmake 'safevst3-rack-broadcast-meter-contract' 'deterministic broadcast meter executable/test registration'
Require-Text $cmake 'set_source_files_properties' 'visual isolation to Rack Editor translation unit'

# Native child-app identity must remain attached to both EXE and Win32 window.
Require-Text $cmake 'rack_host.rc' 'Rack EXE icon resource source'
Require-Text $cmake 'wc.hIcon' 'large Win32 Rack window icon'
Require-Text $cmake 'wc.hIconSm' 'small Win32 Rack window icon'
Require-Text $cmake 'MAKEINTRESOURCEW(101)' 'stable Rack icon resource id'
Require-Text $rc '101 ICON "rack_host.ico"' 'Rack icon resource declaration'
Require-Text $rc '80% of the icon canvas' 'smaller OBS companion icon treatment'
if (-not (Test-Path $iconSourcePath)) {
    throw 'P5 Rack Editor contract missing: maintainer-provided OBS_Studio_Logo.svg source'
}
$iconSource = Get-Content -Raw $iconSourcePath
Require-Text $iconSource '<title>OBS Studio</title>' 'OBS SVG source identity'

if (-not (Test-Path $iconPath)) {
    throw 'P5 Rack Editor contract missing: rack_host.ico'
}
$iconBytes = [System.IO.File]::ReadAllBytes($iconPath)
if ($iconBytes.Length -lt 256) {
    throw 'P5 Rack icon is unexpectedly small or empty'
}
if ($iconBytes[0] -ne 0 -or $iconBytes[1] -ne 0 -or
    $iconBytes[2] -ne 1 -or $iconBytes[3] -ne 0) {
    throw 'P5 Rack icon is not a valid Windows ICO container'
}
$iconImageCount = [BitConverter]::ToUInt16($iconBytes, 4)
if ($iconImageCount -lt 4) {
    throw 'P5 Rack icon must provide at least 16/32/48/256 px Windows icon variants'
}

if ($cmake.Contains('target_compile_options(obs-safe-vst3-rack-host') -and
    $cmake.Contains('rack_editor_p5_broadcast.hpp')) {
    throw 'P5 regression: visual skin must not be force-included into the whole Rack host target'
}

if ($p2.Contains('Waves') -or $p2.Contains('StudioRack') -or
    $p3.Contains('Waves') -or $p3.Contains('StudioRack') -or
    $p4.Contains('Waves') -or $p4.Contains('StudioRack') -or
    $p5.Contains('Waves') -or $p5.Contains('StudioRack')) {
    throw 'P5 regression: shipping Rack skin must not copy competitor branding or product names'
}

Write-Host 'P5 Rack Editor premium LEVEL + LUFS-I + dBTP broadcast contract: PASS'
