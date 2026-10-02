param(
    [string]$Root = $PSScriptRoot,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'SelfTest'),
    [switch]$SkipToolExecution
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-RuntimeTool {
    param([string]$Base, [string]$Name)
    $preferred = Join-Path (Join-Path $Base 'Tools') $Name
    if (Test-Path -LiteralPath $preferred -PathType Leaf) { return $preferred }
    $fallback = Join-Path $Base $Name
    if (Test-Path -LiteralPath $fallback -PathType Leaf) { return $fallback }
    return $preferred
}

function Get-ManifestHash {
    param([string]$ManifestPath, [string]$FileName)
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { return $null }
    $wanted = $FileName.ToLowerInvariant()
    $matched = $null
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        if ($line -match '^\s*([0-9a-fA-F]{64})\s+([^#\s]+)\s*$') {
            if ($Matches[2].ToLowerInvariant() -eq $wanted) {
                $hash = $Matches[1].ToLowerInvariant()
                if ($null -ne $matched -and $matched -ne $hash) { return $null }
                $matched = $hash
            }
        }
    }
    return $matched
}

function Get-ToolState {
    param([string]$Path, [string]$ExpectedHash)
    $found = Test-Path -LiteralPath $Path -PathType Leaf
    $actual = $null
    $trusted = $false
    if ($found) {
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
        $trusted = ($null -ne $ExpectedHash -and $actual -eq $ExpectedHash)
    }
    return [ordered]@{
        path = $Path
        found = $found
        expectedSha256 = $ExpectedHash
        actualSha256 = $actual
        trusted = $trusted
    }
}

function Get-ConfiguredMirrorPath {
    param([string]$Base)
    $iniPath = Join-Path $Base 'settings.ini'
    if (Test-Path -LiteralPath $iniPath -PathType Leaf) {
        $inStorage = $false
        foreach ($line in Get-Content -LiteralPath $iniPath) {
            $trimmed = $line.Trim()
            if ($trimmed.Length -eq 0 -or $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) { continue }
            if ($trimmed -match '^\[(.+)\]$') {
                $inStorage = ($Matches[1].Trim() -ieq 'storage')
                continue
            }
            if ($inStorage -and $trimmed -match '^mirror_path\s*=\s*(.*)$') {
                $value = $Matches[1].Trim()
                if ($value.Length -gt 0) { return $value }
            }
        }
    }
    $fromEnvironment = [Environment]::GetEnvironmentVariable('BODYCAM_DIAG_MIRROR')
    if (-not [string]::IsNullOrWhiteSpace($fromEnvironment)) { return $fromEnvironment.Trim() }
    return $null
}

function Test-DirectoryWritable {
    param([string]$Path)
    try {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
        $probe = Join-Path $Path ('.bodycamdiag-selftest-' + [Guid]::NewGuid().ToString('N') + '.tmp')
        [IO.File]::WriteAllText($probe, 'BodycamDiagnostics write test')
        Remove-Item -LiteralPath $probe -Force
        return $true
    }
    catch {
        return $false
    }
}

function Invoke-Capture {
    param([scriptblock]$Command)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $text = (& $Command 2>&1 | Out-String)
        $code = $LASTEXITCODE
        return [ordered]@{ exitCode = $code; output = $text }
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

$Root = [IO.Path]::GetFullPath($Root)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$manifestPath = Join-Path $Root 'runtime-tools.sha256'
$presentMonPath = Resolve-RuntimeTool -Base $Root -Name 'PresentMon.exe'
$ffmpegPath = Resolve-RuntimeTool -Base $Root -Name 'ffmpeg.exe'

$presentMonExpected = Get-ManifestHash -ManifestPath $manifestPath -FileName 'PresentMon.exe'
$ffmpegExpected = Get-ManifestHash -ManifestPath $manifestPath -FileName 'ffmpeg.exe'
$presentMon = Get-ToolState -Path $presentMonPath -ExpectedHash $presentMonExpected
$ffmpeg = Get-ToolState -Path $ffmpegPath -ExpectedHash $ffmpegExpected

$presentMonHelp = $null
$nvencListed = $null
$ddagrabListed = $null
$nvencSmoke = $null
$ddagrabSmoke = $null

if (-not $SkipToolExecution -and $presentMon.trusted) {
    $pm = Invoke-Capture { & $presentMonPath --help }
    # PresentMon intentionally exits non-zero after printing --help.
    $presentMonHelp = ($pm.output -match 'PresentMon')
}

if (-not $SkipToolExecution -and $ffmpeg.trusted) {
    $encoders = Invoke-Capture { & $ffmpegPath -hide_banner -encoders }
    $filters = Invoke-Capture { & $ffmpegPath -hide_banner -filters }
    $nvencListed = ($encoders.exitCode -eq 0 -and $encoders.output -match 'h264_nvenc')
    $ddagrabListed = ($filters.exitCode -eq 0 -and $filters.output -match 'ddagrab')

    if ($nvencListed) {
        $smoke = Invoke-Capture {
            & $ffmpegPath -hide_banner -loglevel error -f lavfi -i 'color=size=128x128:rate=30' -frames:v 2 -c:v h264_nvenc -f null NUL
        }
        $nvencSmoke = ($smoke.exitCode -eq 0)
    }
    else {
        $nvencSmoke = $false
    }

    if ($nvencListed -and $ddagrabListed) {
        $ddaSmoke = Invoke-Capture {
            & $ffmpegPath -hide_banner -loglevel error -f lavfi -i 'ddagrab=framerate=1:draw_mouse=0' -frames:v 1 -c:v h264_nvenc -f null NUL
        }
        $ddagrabSmoke = ($ddaSmoke.exitCode -eq 0)
    }
    else {
        $ddagrabSmoke = $false
    }
}

$sessionsPath = Join-Path $Root 'Sessions'
$sessionsWritable = Test-DirectoryWritable -Path $sessionsPath
$mirrorPath = Get-ConfiguredMirrorPath -Base $Root
$mirrorConfigured = -not [string]::IsNullOrWhiteSpace($mirrorPath)
$mirrorWritable = $null
if ($mirrorConfigured) { $mirrorWritable = Test-DirectoryWritable -Path $mirrorPath }

$bodycamRunning = $false
try {
    $bodycamRunning = $null -ne (Get-Process -Name 'Bodycam-Win64-Shipping' -ErrorAction SilentlyContinue | Select-Object -First 1)
}
catch {
    $bodycamRunning = $false
}

$ready = [bool](
    $presentMon.trusted -and
    $ffmpeg.trusted -and
    $sessionsWritable -and
    (-not $mirrorConfigured -or $mirrorWritable)
)
if (-not $SkipToolExecution) {
    $ready = [bool](
        $ready -and
        $presentMonHelp -and
        $nvencListed -and
        $ddagrabListed -and
        $nvencSmoke -and
        $ddagrabSmoke
    )
}

$report = [ordered]@{
    schemaVersion = 2
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    ready = $ready
    skipToolExecution = [bool]$SkipToolExecution
    root = $Root
    manifest = [ordered]@{
        path = $manifestPath
        found = (Test-Path -LiteralPath $manifestPath -PathType Leaf)
    }
    tools = [ordered]@{
        presentMon = $presentMon
        ffmpeg = $ffmpeg
    }
    capabilities = [ordered]@{
        presentMonHelp = $presentMonHelp
        h264NvencListed = $nvencListed
        ddagrabListed = $ddagrabListed
        nvencSmokeEncode = $nvencSmoke
        ddagrabSmokeCapture = $ddagrabSmoke
    }
    storage = [ordered]@{
        sessionsPath = $sessionsPath
        sessionsWritable = $sessionsWritable
        mirrorConfigured = $mirrorConfigured
        mirrorPath = $mirrorPath
        mirrorWritable = $mirrorWritable
    }
    bodycamRunning = $bodycamRunning
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$jsonPath = Join-Path $OutputDirectory 'environment_selftest.json'
$textPath = Join-Path $OutputDirectory 'environment_selftest.txt'
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath $jsonPath

$lines = @(
    'BODYCAM DIAGNOSTICS ENVIRONMENT SELF-TEST',
    ('Ready: ' + $(if ($ready) { 'YES' } else { 'NO' })),
    ('Manifest: ' + $(if ($report.manifest.found) { 'FOUND' } else { 'MISSING' })),
    ('PresentMon: ' + $(if ($presentMon.trusted) { 'TRUSTED' } elseif ($presentMon.found) { 'HASH MISMATCH' } else { 'MISSING' })),
    ('FFmpeg: ' + $(if ($ffmpeg.trusted) { 'TRUSTED' } elseif ($ffmpeg.found) { 'HASH MISMATCH' } else { 'MISSING' })),
    ('PresentMon --help: ' + $(if ($SkipToolExecution) { 'SKIPPED' } elseif ($presentMonHelp) { 'OK' } else { 'FAILED' })),
    ('h264_nvenc listed: ' + $(if ($SkipToolExecution) { 'SKIPPED' } elseif ($nvencListed) { 'YES' } else { 'NO' })),
    ('ddagrab listed: ' + $(if ($SkipToolExecution) { 'SKIPPED' } elseif ($ddagrabListed) { 'YES' } else { 'NO' })),
    ('NVENC smoke encode: ' + $(if ($SkipToolExecution) { 'SKIPPED' } elseif ($nvencSmoke) { 'OK' } else { 'FAILED' })),
    ('DDAGrab + NVENC smoke capture: ' + $(if ($SkipToolExecution) { 'SKIPPED' } elseif ($ddagrabSmoke) { 'OK' } else { 'FAILED' })),
    ('Sessions writable: ' + $(if ($sessionsWritable) { 'YES' } else { 'NO' })),
    ('Persistent mirror: ' + $(if (-not $mirrorConfigured) { 'NOT CONFIGURED' } elseif ($mirrorWritable) { 'WRITABLE' } else { 'NOT WRITABLE' })),
    ('Mirror path: ' + $(if ($mirrorConfigured) { $mirrorPath } else { '-' })),
    ('Bodycam running: ' + $(if ($bodycamRunning) { 'YES' } else { 'NO' })),
    '',
    ('JSON report: ' + $jsonPath)
)
$lines | Set-Content -Encoding UTF8 -LiteralPath $textPath
$lines | ForEach-Object { Write-Host $_ }

if (-not $ready) {
    throw "Environment self-test failed. See $textPath"
}
