$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built: $build\Release\BodycamDiagnostics.exe"

