# ============================================================
# New-SelfSignedCert.ps1
# ============================================================
# Creates a self-signed code-signing certificate for DocuSearch
# and exports it to a .pfx file (with password) plus a .cer file
# (for trusting on target machines).
#
# Usage:
#   .\scripts\New-SelfSignedCert.ps1
#   .\scripts\New-SelfSignedCert.ps1 -Subject "CN=DocuSearch" -Password "MySecret"
#
# After running this:
#   1. Install the .cer into "Trusted People" on the target PC:
#        certutil -addstore TrustedPeople DocuSearch.cer
#   2. Use the .pfx to sign the MSIX:
#        .\scripts\build-release.ps1 -MakeMsix -Sign `
#           -CertPfx .\DocuSearch.pfx -CertPassword "MySecret"
#   3. Install the signed MSIX:
#        Add-AppxPackage .\DocuSearch-1.0.0.0-x64.msix
# ============================================================

param(
    [string]$Subject  = "CN=DocuSearch, O=DocuSearch, C=US",
    [string]$Password = "DocuSearch2026!",
    [string]$OutDir   = ".\certs"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

Write-Host ""
Write-Host "Creating self-signed code-signing certificate..." -ForegroundColor Cyan
Write-Host "  Subject  : $Subject"
Write-Host "  Password : $Password"
Write-Host "  Output   : $OutDir"
Write-Host ""

# Create the cert in CurrentUser\My
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -KeyUsage DigitalSignature `
    -FriendlyName "DocuSearch Code Signing" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -HashAlgorithm SHA256 `
    -NotAfter (Get-Date).AddYears(3)

if (-not $cert) {
    Write-Error "Failed to create self-signed certificate."
    exit 1
}

Write-Host "Created certificate with thumbprint: $($cert.Thumbprint)" -ForegroundColor Green

# Export the cert (public key) as .cer
$cerPath = Join-Path $OutDir "DocuSearch.cer"
Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT
Write-Host "Exported public key : $cerPath" -ForegroundColor Green

# Export the cert (with private key) as .pfx
$pfxPath = Join-Path $OutDir "DocuSearch.pfx"
$securePwd = ConvertTo-SecureString -String $Password -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $securePwd
Write-Host "Exported PFX        : $pfxPath" -ForegroundColor Green

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Yellow
Write-Host "  Next steps:" -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Yellow
Write-Host "  1. Install the .cer into Trusted People (one-time, per PC):"
Write-Host "       certutil -addstore TrustedPeople `"$cerPath`""
Write-Host ""
Write-Host "  2. Sign the MSIX with the .pfx:"
Write-Host "       .\scripts\build-release.ps1 -MakeMsix -Sign \"
Write-Host "         -CertPfx `"$pfxPath`" -CertPassword `"$Password`""
Write-Host ""
Write-Host "  3. Install the signed MSIX:"
Write-Host "       Add-AppxPackage .\dist\DocuSearch-1.0.0.0-x64.msix"
Write-Host "==========================================================" -ForegroundColor Yellow
