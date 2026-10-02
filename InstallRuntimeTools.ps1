param(
    [string]$Destination = (Join-Path $PSScriptRoot 'Tools')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Assert-Sha256 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $actual = Get-Sha256Hex -Path $Path
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw "$Label SHA-256 mismatch. Expected $Expected, got $actual"
    }
}

function Get-ManifestSha256 {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$FileName
    )
    $wanted = $FileName.ToLowerInvariant()
    $matched = $null
    foreach ($line in Get-Content -LiteralPath $ManifestPath) {
        if ($line -match '^\s*([0-9a-fA-F]{64})\s+([^#\s]+)\s*$') {
            $name = $Matches[2].ToLowerInvariant()
            if ($name -eq $wanted) {
                $hash = $Matches[1].ToLowerInvariant()
                if ($null -ne $matched -and $matched -ne $hash) {
                    throw "Conflicting SHA-256 entries for $FileName"
                }
                $matched = $hash
            }
        }
    }
    if ($null -eq $matched) {
        throw "No SHA-256 entry for $FileName in $ManifestPath"
    }
    return $matched
}

function Download-VerifiedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Target,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][string]$Label
    )
    Write-Host "Downloading $Label..."
    Invoke-WebRequest -Uri $Url -OutFile $Target -UseBasicParsing
    Assert-Sha256 -Path $Target -Expected $ExpectedSha256 -Label $Label
}

$sourcesPath = Join-Path $PSScriptRoot 'runtime-tools.sources.json'
$manifestPath = Join-Path $PSScriptRoot 'runtime-tools.sha256'
if (-not (Test-Path -LiteralPath $sourcesPath -PathType Leaf)) { throw "Missing runtime source metadata: $sourcesPath" }
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Missing runtime hash manifest: $manifestPath" }

$sources = Get-Content -Raw -LiteralPath $sourcesPath | ConvertFrom-Json
if ($sources.schemaVersion -ne 1) { throw "Unsupported runtime source schema: $($sources.schemaVersion)" }

$presentMonExpected = Get-ManifestSha256 -ManifestPath $manifestPath -FileName $sources.presentMon.outputName
$ffmpegExpected = Get-ManifestSha256 -ManifestPath $manifestPath -FileName $sources.ffmpeg.outputName

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('BodycamDiagnostics-runtime-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

try {
    $presentMonDownload = Join-Path $tempRoot 'PresentMon.download.exe'
    Download-VerifiedFile -Url $sources.presentMon.url -Target $presentMonDownload -ExpectedSha256 $sources.presentMon.downloadSha256 -Label "PresentMon $($sources.presentMon.version)"
    Assert-Sha256 -Path $presentMonDownload -Expected $presentMonExpected -Label 'PresentMon runtime executable'

    $ffmpegArchive = Join-Path $tempRoot 'ffmpeg.download.zip'
    Download-VerifiedFile -Url $sources.ffmpeg.url -Target $ffmpegArchive -ExpectedSha256 $sources.ffmpeg.downloadSha256 -Label "FFmpeg $($sources.ffmpeg.version) archive"

    $extractRoot = Join-Path $tempRoot 'ffmpeg'
    Expand-Archive -LiteralPath $ffmpegArchive -DestinationPath $extractRoot -Force
    $ffmpegCandidates = @(Get-ChildItem -LiteralPath $extractRoot -Filter 'ffmpeg.exe' -File -Recurse)
    if ($ffmpegCandidates.Count -ne 1) { throw "Expected one ffmpeg.exe in archive, found $($ffmpegCandidates.Count)" }

    $ffmpegExe = $ffmpegCandidates[0].FullName
    Assert-Sha256 -Path $ffmpegExe -Expected $ffmpegExpected -Label 'FFmpeg runtime executable'

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $presentMonTarget = Join-Path $Destination 'PresentMon.exe'
    $ffmpegTarget = Join-Path $Destination 'ffmpeg.exe'
    Copy-Item -LiteralPath $presentMonDownload -Destination $presentMonTarget -Force
    Copy-Item -LiteralPath $ffmpegExe -Destination $ffmpegTarget -Force
    Assert-Sha256 -Path $presentMonTarget -Expected $presentMonExpected -Label 'Installed PresentMon'
    Assert-Sha256 -Path $ffmpegTarget -Expected $ffmpegExpected -Label 'Installed FFmpeg'

    Write-Host ''
    Write-Host 'Runtime tools installed and verified:'
    Write-Host "  $presentMonTarget"
    Write-Host "  $ffmpegTarget"
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
