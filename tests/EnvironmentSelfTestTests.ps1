$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $repoRoot 'ValidateEnvironment.ps1'
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Missing environment self-test: $scriptPath"
}

$tokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count -ne 0) {
    throw "ValidateEnvironment.ps1 has PowerShell parse errors: $($parseErrors[0].Message)"
}

$root = Join-Path $env:TEMP ('BodycamDiagnostics-selftest-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Join-Path $root 'Tools') | Out-Null

try {
    $presentMon = Join-Path $root 'Tools\PresentMon.exe'
    $ffmpeg = Join-Path $root 'Tools\ffmpeg.exe'
    Set-Content -LiteralPath $presentMon -NoNewline -Value 'presentmon-fixture'
    Set-Content -LiteralPath $ffmpeg -NoNewline -Value 'ffmpeg-fixture'

    $pmHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $presentMon).Hash.ToLowerInvariant()
    $ffHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ffmpeg).Hash.ToLowerInvariant()
    @(
        "$pmHash  PresentMon.exe",
        "$ffHash  ffmpeg.exe"
    ) | Set-Content -Encoding ASCII -LiteralPath (Join-Path $root 'runtime-tools.sha256')

    $output = Join-Path $root 'SelfTest'
    & $scriptPath -Root $root -OutputDirectory $output -SkipToolExecution
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $jsonPath = Join-Path $output 'environment_selftest.json'
    $txtPath = Join-Path $output 'environment_selftest.txt'
    if (-not (Test-Path -LiteralPath $jsonPath -PathType Leaf)) { throw 'Missing JSON self-test report' }
    if (-not (Test-Path -LiteralPath $txtPath -PathType Leaf)) { throw 'Missing text self-test report' }

    $report = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
    if ($report.schemaVersion -ne 1) { throw 'Unexpected self-test schema version' }
    if (-not $report.ready) { throw 'Fixture self-test should be ready when execution is skipped' }
    if (-not $report.tools.presentMon.trusted) { throw 'PresentMon fixture should be trusted' }
    if (-not $report.tools.ffmpeg.trusted) { throw 'FFmpeg fixture should be trusted' }
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'Environment self-test regression passed'
