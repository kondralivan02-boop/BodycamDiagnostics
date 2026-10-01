$ErrorActionPreference = "Stop"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    $args = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
    Start-Process powershell.exe -Verb RunAs -ArgumentList $args
    exit
}

$source = Join-Path $PSScriptRoot "BodycamFullscreenOverlay.exe"
if (-not (Test-Path $source)) {
    throw "BodycamFullscreenOverlay.exe not found next to this script."
}

$installDir = Join-Path $env:ProgramFiles "BodycamDiagnostics"
$target = Join-Path $installDir "BodycamFullscreenOverlay.exe"
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

$subject = "CN=Bodycam Diagnostics Local UIAccess"
$cert = Get-ChildItem Cert:\LocalMachine\My |
    Where-Object {
        $_.Subject -eq $subject -and
        $_.HasPrivateKey -and
        $_.NotAfter -gt (Get-Date).AddDays(7)
    } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation "Cert:\LocalMachine\My" -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(3)
}

$tempCer = Join-Path $env:TEMP "BodycamDiagnostics-UIAccess.cer"
Export-Certificate -Cert $cert -FilePath $tempCer -Force | Out-Null
Import-Certificate -FilePath $tempCer -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
Import-Certificate -FilePath $tempCer -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" | Out-Null
Remove-Item $tempCer -Force -ErrorAction SilentlyContinue

Copy-Item $source $target -Force
$signature = Set-AuthenticodeSignature -FilePath $target -Certificate $cert -HashAlgorithm SHA256

if ($signature.Status -ne "Valid") {
    throw "Signing failed: $($signature.Status) $($signature.StatusMessage)"
}

Write-Host ""
Write-Host "Fullscreen HUD helper installed successfully."
Write-Host "Installed to: $target"
Write-Host "Signature: $($signature.Status)"
Write-Host ""
Write-Host "Now start Bodycam Diagnostics normally."
Read-Host "Press Enter to close"
