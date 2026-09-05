$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$p2 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p1_skin.hpp')
$p3 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p3_premium.hpp')
$p4 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p4_live_meter.hpp')
$p5 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p5_broadcast.hpp')
$p6 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p6_hardware.hpp')
$p7 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p7_luxury.hpp')
$p8 = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p8_refined.hpp')
$meter = Get-Content -Raw (Join-Path $root 'src\rack\rack_meter_telemetry.hpp')
$loudness = Get-Content -Raw (Join-Path $root 'src\rack\rack_broadcast_loudness.hpp')
$master = Get-Content -Raw (Join-Path $root 'src\rack\rack_master_controls.hpp')
$cmake = Get-Content -Raw (Join-Path $root 'src\rack\CMakeLists.txt')
$rc = Get-Content -Raw (Join-Path $root 'src\rack\rack_host.rc')
$iconPath = Join-Path $root 'src\rack\rack_host.ico'
$iconSourcePath = Join-Path $root 'src\rack\assets\OBS_Studio_Logo.svg'

function Require-Text([string]$haystack, [string]$needle, [string]$description) {
    if (-not $haystack.Contains($needle)) {
        throw "P8 Rack Editor contract missing: $description"
    }
}

# Qualified base / typography / product workflow remains underneath P8.
Require-Text $p2 'C:\\Windows\\Fonts\\segoeui.ttf' 'Segoe UI regular font'
Require-Text $p2 'C:\\Windows\\Fonts\\seguisb.ttf' 'Segoe UI semibold font'
Require-Text $p2 'ImGuiConfigFlags_NavEnableKeyboard' 'keyboard navigation'
Require-Text $p3 'kPremiumSlotHeight = 74.0f' 'qualified prior compact effect-strip baseline'
Require-Text $p3 'OPEN##premium-slot-ui' 'vendor-editor OPEN affordance remains available beneath the overlay'

# P5/P6 measurement and master-control behavior remains authoritative beneath P8.
Require-Text $p5 '"LUFS-I"' 'Integrated LUFS metric'
Require-Text $p5 '"dBTP"' 'true-peak metric'
Require-Text $loudness 'kLoudnessOffset = -0.691' 'BS.1770 loudness offset'
Require-Text $loudness 'kAbsoluteGateLufs = -70.0' 'absolute loudness gate'
Require-Text $loudness 'energy_to_lufs(absolute_mean) - 10.0' 'relative loudness gate'
Require-Text $loudness 'kTruePeakTaps = 33' 'true-peak reconstruction kernel'
Require-Text $p6 'draw_metal_fader' 'qualified P6 satin-metal fader baseline'
Require-Text $p6 'IsMouseDoubleClicked' 'double-click fader reset gesture'
Require-Text $p6 'db = 0.0f' 'double-click reset target'
Require-Text $p6 'fader_reset_lock' 'reset gesture latch'

# P7 keeps stable geometry and no-dim topology transitions beneath the P8 refinement.
Require-Text $p7 'kP7SlotHeight = 44.0f' 'compact one-row Rack strips'
Require-Text $p7 'draw_outer_chassis' 'unified outer metal chassis'
Require-Text $p7 'draw_slot_surface' 'subtle metal-gradient effect strip'
Require-Text $p7 'rack-p7-slot-well' 'contained Rack well'
Require-Text $p7 'ImGuiWindowFlags_AlwaysVerticalScrollbar' 'reserved Rack scrollbar gutter prevents width jumps'
Require-Text $p7 'p7_transition_pending' 'pending-transition visual state'
Require-Text $p7 'style.DisabledAlpha = 1.0f' 'pending commands remain interaction-disabled without whole-UI dim flash'
Require-Text $p7 'std::strcmp(text, "Pending...") == 0' 'pending label is absorbed instead of flashing text'
Require-Text $p7 'SafeVst3P7BeginCombo' 'vertically centered preset dropdown'
Require-Text $p7 'SafeVst3P7InputTextWithHint' 'vertically centered search field'

# P8 is the minimal-noise luxury pass requested from the real P7 screenshot.
Require-Text $p8 'kP8SlotActionReserve = 42.0f' 'single compact slot-action gutter'
Require-Text $p8 'draw_p8_slot_identity' 'clean slot identity renderer'
Require-Text $p8 '##p8-led-toggle' 'LED itself is the bypass/enable control'
Require-Text $p8 '##p8-open-name' 'plug-in name itself opens the vendor UI'
Require-Text $p8 '+##p8-slot-actions' 'single compact slot action button replaces ON/OPEN text noise'
Require-Text $p8 'draw_p8_fader' 'refined recessed hardware fader'
Require-Text $p8 'double-click = 0.0 dB' 'fader reset behavior remains discoverable'
Require-Text $p8 'render_p8_master_surface' 'refined aligned Input/Output hardware grid'
Require-Text $p8 'render_p8_loudness' 'border-light loudness presentation'
Require-Text $p8 '"MASTER"' 'non-redundant master header'
Require-Text $p8 '"OUTPUT  >  OBS"' 'integrated master footer'
Require-Text $p8 'push_p8_scrollbar_style' 'reserved scrollbar gutter with conditional visual suppression'
Require-Text $p8 'const bool overflow' 'scrollbar only becomes visible when Rack content overflows'
Require-Text $p8 'rack-p8-master-console' 'P8 non-scrolling master console'
Require-Text $p8 '"LUFS-I"' 'broadcast loudness label'
Require-Text $p8 '"dBTP"' 'true-peak label'
if ($p8.Contains('"GR"') -or $p8.Contains('Gain Reduction')) {
    throw 'P8 regression: generic gain-reduction meter must not consume the main Rack surface'
}
if ($p8.Contains('"ON"') -or $p8.Contains('"OPEN"')) {
    throw 'P8 regression: repetitive ON/OPEN button text must not return to the refined slot surface'
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
    throw 'P8 regression: master gain path must remain lock-free/allocation-free'
}

# Stereo meter telemetry remains lossy atomic data only.
Require-Text $meter 'publish_stereo' 'stereo peak publication'
Require-Text $meter 'input_left_peak_' 'input-left peak atomic'
Require-Text $meter 'input_right_peak_' 'input-right peak atomic'
Require-Text $meter 'output_left_peak_' 'output-left peak atomic'
Require-Text $meter 'output_right_peak_' 'output-right peak atomic'
Require-Text $meter 'rack_meter_stereo_peaks' 'bounded stereo peak scan'
if ($meter.Contains('std::mutex') -or $meter.Contains('condition_variable')) {
    throw 'P8 regression: stereo telemetry must not introduce locks'
}

# Shipping source remains generated so qualified checked-in R1/R2 DSP stays intact.
Require-Text $cmake 'rack_master_controls.hpp' 'master control include injection'
Require-Text $cmake 'RackMasterGainSmoother' 'shipping smoother state injection'
Require-Text $cmake 'float* trimmed[kMaxChannels]' 'post-input-trim staging buffer'
Require-Text $cmake 'float** current = trimmed' 'Rack chain consumes trimmed input'
Require-Text $cmake 'g_rack_master_controls.snapshot()' 'DSP target snapshot'
Require-Text $cmake 'publish_stereo' 'shipping stereo meter publication'
Require-Text $cmake 'master_controls.output_db' 'final Output Fader application'
Require-Text $cmake 'fail-dry so a muted/attenuated Rack can never jump to full level' 'fail-dry master authority'
Require-Text $cmake 'g_rack_broadcast_loudness.process' 'final post-fader loudness metering'
Require-Text $cmake 'rack_editor_p8_refined.hpp' 'P8 source-local force include'
Require-Text $cmake 'safevst3-rack-master-gain-contract' 'deterministic master-gain test registration'
Require-Text $cmake 'safevst3-rack-broadcast-meter-contract' 'broadcast meter test registration'

# Native companion icon remains intact.
Require-Text $cmake 'rack_host.rc' 'Rack EXE icon resource source'
Require-Text $cmake 'wc.hIcon' 'large Win32 Rack icon'
Require-Text $cmake 'wc.hIconSm' 'small Win32 Rack icon'
Require-Text $rc '101 ICON "rack_host.ico"' 'Rack icon resource declaration'
if (-not (Test-Path $iconSourcePath) -or -not (Test-Path $iconPath)) {
    throw 'P8 Rack companion icon sources are missing'
}

# Competitor references may guide hierarchy only; no branding/assets ship.
foreach ($source in @($p2, $p3, $p4, $p5, $p6, $p7, $p8)) {
    if ($source.Contains('Waves') -or $source.Contains('StudioRack')) {
        throw 'P8 regression: shipping Rack skin must not contain competitor branding/product names'
    }
}

Write-Host 'P8 minimal-noise luxury Rack + refined hardware master contract: PASS'