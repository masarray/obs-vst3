$ErrorActionPreference = 'Stop'

$bridge = Get-Content -Raw (Join-Path $PSScriptRoot '..\..\src\platform\windows\win_rack_bridge.cpp')

function Require-Text([string]$needle, [string]$description) {
    if (-not $bridge.Contains($needle)) {
        throw "P0 shutdown contract missing: $description"
    }
}

Require-Text 'kRackHelperGracefulShutdownTimeoutMs = 250' '250 ms graceful timeout'
Require-Text 'kRackHelperForcedShutdownWaitMs = 250' '250 ms forced-exit observation timeout'
Require-Text 'InterlockedExchange(&region_->shutdown_requested, 1)' 'shutdown gate publication'
Require-Text 'SetEvent(request_event_)' 'request worker wake'
Require-Text 'SetEvent(ui_open_event_)' 'UI worker wake'
Require-Text 'SetEvent(response_event_)' 'response waiter wake'
Require-Text 'TerminateProcess(process_.hProcess, 0xDEAD)' 'bounded force termination fallback'

if ($bridge.Contains('WaitForSingleObject(process_.hProcess, 2000)')) {
    throw 'P0 regression: legacy 2000 ms graceful shutdown timeout is still present'
}
if ($bridge.Contains('WaitForSingleObject(process_.hProcess, 1000)')) {
    throw 'P0 regression: legacy 1000 ms forced shutdown wait is still present'
}

Write-Host 'P0 Rack shutdown contract: PASS'
