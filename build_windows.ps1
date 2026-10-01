$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
cmake -S $root -B $build -A x64
cmake --build $build --config Release
Write-Host "Built: $build\Release\BodycamDiagnostics.exe"
