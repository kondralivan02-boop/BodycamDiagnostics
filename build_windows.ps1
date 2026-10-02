$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'

$buildSha = if ([string]::IsNullOrWhiteSpace($env:GITHUB_SHA)) { 'local' } else { $env:GITHUB_SHA }
$buildBranch = if ([string]::IsNullOrWhiteSpace($env:GITHUB_REF_NAME)) { 'local' } else { $env:GITHUB_REF_NAME }
$buildRun = if ([string]::IsNullOrWhiteSpace($env:GITHUB_RUN_NUMBER)) { 'local' } else { $env:GITHUB_RUN_NUMBER }

cmake -S $root -B $build -A x64 `
    "-DBODYCAM_BUILD_SHA=$buildSha" `
    "-DBODYCAM_BUILD_BRANCH=$buildBranch" `
    "-DBODYCAM_BUILD_RUN=$buildRun"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built: $build\Release\BodycamDiagnostics.exe"
Write-Host "Source: $buildSha [$buildBranch] CI=$buildRun"
