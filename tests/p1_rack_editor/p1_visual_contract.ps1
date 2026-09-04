$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$skin = Get-Content -Raw (Join-Path $root 'src\rack\rack_editor_p1_skin.hpp')
$cmake = Get-Content -Raw (Join-Path $root 'src\rack\CMakeLists.txt')

function Require-Text([string]$haystack, [string]$needle, [string]$description) {
    if (-not $haystack.Contains($needle)) {
        throw "P1 Rack Editor contract missing: $description"
    }
}

Require-Text $skin 'kSlotCardHeight = 96.0f' 'compact 96 px slot cards'
Require-Text $skin 'C:\\Windows\\Fonts\\segoeui.ttf' 'Segoe UI regular font with Windows-local loading'
Require-Text $skin 'C:\\Windows\\Fonts\\seguisb.ttf' 'Segoe UI semibold hierarchy font'
Require-Text $skin 'ImGuiConfigFlags_NavEnableKeyboard' 'keyboard navigation'
Require-Text $skin 'style.ChildRounding = 8.0f' 'rounded rack cards'
Require-Text $skin 'style.FrameRounding = 6.0f' 'rounded controls'
Require-Text $skin 'SafeVst3P1Button' 'action hierarchy button skin'
Require-Text $skin 'SafeVst3P1Text' 'rack/plugin/status hierarchy text skin'
Require-Text $skin 'SIGNAL CHAIN' 'signal-chain section treatment'
Require-Text $skin 'TO OBS' 'output section treatment'
Require-Text $skin 'health_color' 'non-color-neutral health mapping'
Require-Text $cmake 'rack_editor_p1_skin.hpp' 'source-local P1 force include'
Require-Text $cmake 'set_source_files_properties' 'P1 isolation to rack_editor_window.cpp'

if ($cmake.Contains('target_compile_options(obs-safe-vst3-rack-host') -and
    $cmake.Contains('rack_editor_p1_skin.hpp')) {
    throw 'P1 regression: visual skin must not be force-included into the whole Rack host target'
}

Write-Host 'P1 Rack Editor visual contract: PASS'
