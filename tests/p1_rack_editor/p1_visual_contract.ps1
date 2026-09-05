$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$p2 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p1_skin.hpp')
$p3 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p3_premium.hpp')
$p4 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p4_live_meter.hpp')
$p5 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p5_broadcast.hpp')
$p6 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p6_hardware.hpp')
$meter = Get-Content -Raw (Join-Path $root 'src\rack\rack_meter_telemetry.hpp')
$loudness = Get-Content -Raw (Join-Path $root 'src\rack\rack_broadcast_loudness.hpp')
$master = Get-Content -Raw (Join-Path $root 'src\rack\rack_master_controls.hpp')
$cmake = Get-Content -Raw (Join-Path $root 'src\rack\CMakeLists.txt')
$rc = Get-Content -Raw (Join-Path $root 'src\rack\rack_host.rc')
$iconPath = Join-Path $root 'src\rack\rack_host.ico'
$iconSourcePath = Join-Path $root 'src\rack\assets\OBS_Studio_Logo.svg'

function Require-Text([string]$haystack, [string]$needle, [string]$description) {
    if (-not $haystack.Contains($needle)) {
        throw "P6 Rack Editor contract missing: $description"
    }
}

# Qualified base / typography / product workflow remains underneath P6.
Require-Text $p2 'C:\\Windows\\Fonts\\segoeui.ttf' 'Segoe UI regular font'
Require-Text $p2 'C:\\Windows\\Fonts\\seguisb.ttf' 'Segoe UI semibold font'
Require-Text $p2 'ImGuiConfigFlags_NavEnableKeyboard' 'keyboard navigation'
Require-Text $p3 'kPremiumSlotHeight = 74.0f' 'qualified prior compact effect-strip baseline'
Require-Text $p3 'kPremiumConsoleShare = 0.40f' 'qualified prior master-console share'
Require-Text $p3 'OPEN##premium-slot-ui' 'vendor-editor OPEN affordance'

# P5 broadcast measurement stays authoritative beneath the P6 hardware surface.
Require-Text $p5 '"LUFS-I"' 'Integrated LUFS metric'
Require-Text $p5 '"dBTP"' 'true-peak metric'
Require-Text $loudness 'kLoudnessOffset = -0.691' 'BS.1770 loudness offset'
Require-Text $loudness 'kAbsoluteGateLufs = -70.0' 'absolute loudness gate'
Require-Text $loudness 'energy_to_lufs(absolute_mean) - 10.0' 'relative loudness gate'
Require-Text $loudness 'kTruePeakTaps = 33' 'true-peak reconstruction kernel'

# P6 luxury hardware visual language: contained recessed Rack well, compact
# single-row strips, aligned physical faders + segmented LEDs, and no generic GR.
Require-Text $p6 'draw_brushed_metal_backplate' 'brushed-metal chassis renderer'
Require-Text $p6 'draw_recessed_rack_bay' 'contained recessed Rack well renderer'
Require-Text $p6 'AddRectFilledMultiColor' 'satin/metal gradient rendering'
Require-Text $p6 'draw_segment_meter' 'segmented emissive LED meters'
Require-Text $p6 'draw_metal_fader' 'custom satin-metal fader'
Require-Text $p6 'render_aligned_master_surface' 'single aligned master-control grid'
Require-Text $p6 'kLuxurySlotHeight = 48.0f' 'compact one-row Rack strips'
Require-Text $p6 'rack-p6-slot-well' 'inner Rack containment boundary'
Require-Text $p6 'rack-p6-luxury-lane' 'left luxury chassis pane'
Require-Text $p6 'SafeVst3P6BeginCombo' 'vertically aligned preset combo'
Require-Text $p6 'SafeVst3P6InputTextWithHint' 'vertically aligned search/input frames'
Require-Text $p6 'ImGui::AlignTextToFramePadding()' 'frame-aligned toolbar text'
Require-Text $p6 '"##p6-input-fader"' 'real Input Trim control'
Require-Text $p6 '"##p6-output-fader"' 'real Output Fader control'
Require-Text $p6 'IsMouseDoubleClicked' 'double-click fader reset gesture'
Require-Text $p6 'db = 0.0f' 'double-click reset target'
Require-Text $p6 'fader_reset_lock' 'reset gesture cannot be overwritten by the same second click'
Require-Text $p6 'input_left_peak_linear' 'stereo input L telemetry'
Require-Text $p6 'output_right_peak_linear' 'stereo output R telemetry'
Require-Text $p6 '"LUFS-I"' 'compact broadcast loudness label'
Require-Text $p6 '"dBTP"' 'compact true-peak label'
Require-Text $p6 'rack-p6-hardware-console' 'P6 hardware console surface'
if ($p6.Contains('"GR"') -or $p6.Contains('Gain Reduction')) {
    throw 'P6 regression: generic gain-reduction meter must not consume the main Rack surface'
}

# Master gain controls are real DSP controls, lock-free, bounded and smoothed.
Require-Text $master 'std::atomic<std::int32_t> input_db_' 'atomic Input Trim control'
Require-Text $master 'std::atomic<std::int32_t> output_db_' 'atomic Output Fader control'
Require-Text $master 'rack_apply_smoothed_gain' 'bounded one-block gain ramp'
Require-Text $master 'RackMasterGainSmoother' 'persistent gain smoother state'
Require-Text $master 'kRackMasterMinDb = -60.0f' 'master attenuation floor'
Require-Text $master 'kRackMasterMaxDb = 12.0f' 'master boost ceiling'
if ($master.Contains('std::mutex') -or $master.Contains('condition_variable') -or
    $master.Contains('std::vector')) {
    throw 'P6 regression: master gain path must remain lock-free/allocation-free'
}

# Stereo meter telemetry remains lossy atomic data only.
Require-Text $meter 'publish_stereo' 'stereo peak publication'
Require-Text $meter 'input_left_peak_' 'input-left peak atomic'
Require-Text $meter 'input_right_peak_' 'input-right peak atomic'
Require-Text $meter 'output_left_peak_' 'output-left peak atomic'
Require-Text $meter 'output_right_peak_' 'output-right peak atomic'
Require-Text $meter 'rack_meter_stereo_peaks' 'bounded stereo peak scan'
if ($meter.Contains('std::mutex') -or $meter.Contains('condition_variable')) {
    throw 'P6 regression: stereo telemetry must not introduce locks'
}

# Shipping source is generated so qualified checked-in R1/R2 DSP remains intact.
Require-Text $cmake 'rack_master_controls.hpp' 'master control include injection'
Require-Text $cmake 'RackMasterGainSmoother' 'shipping smoother state injection'
Require-Text $cmake 'float* trimmed[kMaxChannels]' 'post-input-trim staging buffer'
Require-Text $cmake 'float** current = trimmed' 'Rack chain consumes trimmed input'
Require-Text $cmake 'g_rack_master_controls.snapshot()' 'DSP target snapshot'
Require-Text $cmake 'publish_stereo' 'shipping stereo meter publication'
Require-Text $cmake 'master_controls.output_db' 'final Output Fader application'
Require-Text $cmake 'fail-dry so a muted/attenuated Rack can never jump to full level' 'fail-dry master authority'
Require-Text $cmake 'g_rack_broadcast_loudness.process' 'final post-fader loudness metering'
Require-Text $cmake 'rack_editor_p6_hardware.hpp' 'P6 source-local force include'
Require-Text $cmake 'safevst3-rack-master-gain-contract' 'deterministic master-gain test registration'
Require-Text $cmake 'safevst3-rack-broadcast-meter-contract' 'broadcast meter test registration'

# Native companion icon remains intact.
Require-Text $cmake 'rack_host.rc' 'Rack EXE icon resource source'
Require-Text $cmake 'wc.hIcon' 'large Win32 Rack icon'
Require-Text $cmake 'wc.hIconSm' 'small Win32 Rack icon'
Require-Text $rc '101 ICON "rack_host.ico"' 'Rack icon resource declaration'
if (-not (Test-Path $iconSourcePath) -or -not (Test-Path $iconPath)) {
    throw 'P6 Rack companion icon sources are missing'
}

# Competitor reference may guide hierarchy only; no branding/assets ship.
foreach ($source in @($p2, $p3, $p4, $p5, $p6)) {
    if ($source.Contains('Waves') -or $source.Contains('StudioRack')) {
        throw 'P6 regression: shipping Rack skin must not contain competitor branding/product names'
    }
}

Write-Host 'P6 luxury metal hardware + precise alignment + resettable master controls: PASS'
