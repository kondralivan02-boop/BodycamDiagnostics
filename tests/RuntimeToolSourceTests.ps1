$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourcesPath = Join-Path $repoRoot 'runtime-tools.sources.json'
$manifestPath = Join-Path $repoRoot 'runtime-tools.sha256'
$installerPath = Join-Path $repoRoot 'InstallRuntimeTools.ps1'

foreach ($required in @($sourcesPath, $manifestPath, $installerPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing runtime installer input: $required"
    }
}

$sources = Get-Content -Raw -LiteralPath $sourcesPath | ConvertFrom-Json
if ($sources.schemaVersion -ne 1) {
    throw 'runtime-tools.sources.json schemaVersion must be 1'
}

$manifest = @{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
    if ($line -match '^\s*([0-9a-fA-F]{64})\s+([^#\s]+)\s*$') {
        $manifest[$Matches[2].ToLowerInvariant()] = $Matches[1].ToLowerInvariant()
    }
}

foreach ($entry in @($sources.presentMon, $sources.ffmpeg)) {
    $uri = [Uri]$entry.url
    if ($uri.Scheme -ne 'https') {
        throw "Runtime source must use HTTPS: $($entry.url)"
    }
    if ($uri.Host -ne 'github.com') {
        throw "Runtime source must use the pinned GitHub release mirror: $($entry.url)"
    }
    if ($entry.downloadSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Invalid download SHA-256 for $($entry.outputName)"
    }
    if (-not $manifest.ContainsKey($entry.outputName.ToLowerInvariant())) {
        throw "Missing runtime manifest entry for $($entry.outputName)"
    }
}

if ($sources.presentMon.archiveType -ne 'none') {
    throw 'PresentMon source must be a direct executable'
}
if ($sources.ffmpeg.archiveType -ne 'zip') {
    throw 'FFmpeg source must be a ZIP archive'
}
if ($sources.presentMon.downloadSha256.ToLowerInvariant() -ne
    $manifest[$sources.presentMon.outputName.ToLowerInvariant()]) {
    throw 'PresentMon release asset SHA-256 must match runtime manifest'
}

$tokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile(
    $installerPath,
    [ref]$tokens,
    [ref]$parseErrors) | Out-Null

if ($parseErrors.Count -ne 0) {
    throw "InstallRuntimeTools.ps1 has PowerShell parse errors: $($parseErrors[0].Message)"
}

Write-Host 'Runtime tool source tests passed'
