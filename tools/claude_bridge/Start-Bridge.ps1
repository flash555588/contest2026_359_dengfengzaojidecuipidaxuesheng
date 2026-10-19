param([int]$Port = 8787)
$ErrorActionPreference = 'Stop'
$bridgeRoot = $PSScriptRoot
$bridgeState = Join-Path $bridgeRoot '.state'
New-Item -ItemType Directory -Force -Path $bridgeState | Out-Null
$pythonCandidate = Join-Path $bridgeRoot '..\..\..\.hardware-venv\Scripts\python.exe'
if (Test-Path -LiteralPath $pythonCandidate) { $bridgePython = (Resolve-Path -LiteralPath $pythonCandidate).Path }
else { $bridgePython = (Get-Command python -ErrorAction Stop).Source }
$bridgeClaude = (Get-Command claude -ErrorAction SilentlyContinue).Source
if (-not $bridgeClaude) { $bridgeClaude = 'claude.exe' }
$ready = $false
$authPath = Join-Path $bridgeState 'auth.json'
if (Test-Path -LiteralPath $authPath) {
    $auth = Get-Content -LiteralPath $authPath -Raw | ConvertFrom-Json
    try {
        $null = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/status" -Method Post -ContentType 'application/json' -Body '{}' -Headers @{Authorization="Bearer $($auth.token)"} -TimeoutSec 2
        $ready = $true
    } catch {}
}
if (-not $ready) {
    $scriptPath = Join-Path $bridgeRoot 'bridge.py'
    $bridgeArguments = '"' + $scriptPath + '" --port ' + $Port + ' --claude "' + $bridgeClaude + '"'
    $bridgeProcess = Start-Process -FilePath $bridgePython -ArgumentList $bridgeArguments -WorkingDirectory $bridgeRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $bridgeState 'server.log') -RedirectStandardError (Join-Path $bridgeState 'server-error.log')
    Set-Content -LiteralPath (Join-Path $bridgeState 'bridge.pid') -Value $bridgeProcess.Id
    for ($attempt=0; $attempt -lt 30; $attempt++) {
        if ($bridgeProcess.HasExited) { throw 'Bridge exited; see .state/server-error.log' }
        if (Test-Path -LiteralPath $authPath) {
            $auth = Get-Content -LiteralPath $authPath -Raw | ConvertFrom-Json
            try {
                $null = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/status" -Method Post -ContentType 'application/json' -Body '{}' -Headers @{Authorization="Bearer $($auth.token)"} -TimeoutSec 2
                $ready = $true; break
            } catch {}
        }
        Start-Sleep -Milliseconds 200
    }
}
if (-not $ready) { throw 'Bridge did not become ready' }
Write-Output "ESPClaw CLI bridge: http://127.0.0.1:$Port"
Write-Output "ESP pairing code: $($auth.pair_code) (valid for 10 minutes after bridge startup)"
