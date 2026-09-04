$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$skin = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p1_skin.hpp')
$cmake = Get-Content -Raw (Join-Path $root 'src\rack\CMakeLists.txt')

function Require-Text([string]$haystack, [string]$needle, [string]$description) {
    if (-not $haystack.Contains($needle)) {
        throw "P2 Rack Editor contract missing: $description"
    }
}

Require-Text $skin 'kSlotCardHeight = 82.0f' 'compact 82 px effect strips'
Require-Text $skin 'kConsoleSplitThreshold = 680.0f' 'responsive two-column console threshold'
Require-Text $skin 'C:\\Windows\\Fonts\\segoeui.ttf' 'Segoe UI regular font with Windows-local loading'
Require-Text $skin 'C:\\Windows\\Fonts\\seguisb.ttf' 'Segoe UI semibold hierarchy font'
Require-Text $skin 'ImGuiConfigFlags_NavEnableKeyboard' 'keyboard navigation'
Require-Text $skin 'style.ChildRounding = 9.0f' 'rounded compact Rack surfaces'
Require-Text $skin 'style.FrameRounding = 5.0f' 'compact rounded controls'
Require-Text $skin 'SafeVst3P1Button' 'action hierarchy button skin'
Require-Text $skin 'SafeVst3P1Text' 'Rack/plugin/status hierarchy text skin'
Require-Text $skin 'SafeVst3P2TextDisabled' 'snapshot-derived Rack summary capture'
Require-Text $skin 'rack-console-lane' 'left serial Rack lane'
Require-Text $skin 'rack-console-master' 'right Rack status console'
Require-Text $skin 'METER TELEMETRY' 'meter-ready console section'
Require-Text $skin 'Live meter telemetry is not exposed by the current Rack UI snapshot.' 'honest no-fake-meter state'
Require-Text $skin 'SIGNAL CHAIN' 'signal-chain section treatment'
Require-Text $skin 'TO OBS' 'output section treatment'
Require-Text $skin 'health_color' 'slot health mapping'
Require-Text $skin 'aggregate_health_text' 'Rack aggregate status mapping'
Require-Text $skin '+##slot-actions' 'compact slot action affordance'
Require-Text $skin 'UI##slot-open-ui' 'compact vendor editor affordance'
Require-Text $skin 'ON##slot-enable-state' 'compact enabled-state control'
Require-Text $skin 'OFF##slot-enable-state' 'compact bypassed-state control'
Require-Text $skin 'meter_warm()' 'restrained warm meter accent'
Require-Text $cmake 'rack_editor_p1_skin.hpp' 'source-local Rack Editor skin force include'
Require-Text $cmake 'set_source_files_properties' 'visual isolation to rack_editor_window.cpp'

if ($cmake.Contains('target_compile_options(obs-safe-vst3-rack-host') -and
    $cmake.Contains('rack_editor_p1_skin.hpp')) {
    throw 'P2 regression: visual skin must not be force-included into the whole Rack host target'
}

if ($skin.Contains('Waves') -or $skin.Contains('StudioRack')) {
    throw 'P2 regression: shipping Rack skin must not copy competitor branding or product names'
}

Write-Host 'P2 Rack Editor compact console visual contract: PASS'
