[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'BetterXcloudDLSS5')
)

$ErrorActionPreference = 'Stop'

Write-Host ''
Write-Host 'Better Xcloud DLSS5 uninstaller' -ForegroundColor Cyan

Get-Process -Name 'XCloudDLSS5Host' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

$shortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Better Xcloud DLSS5.lnk'
if (Test-Path -LiteralPath $shortcut) {
    Remove-Item -LiteralPath $shortcut -Force
}

if (Test-Path -LiteralPath $InstallRoot) {
    Remove-Item -LiteralPath $InstallRoot -Recurse -Force
}

Write-Host 'Uninstalled.' -ForegroundColor Green
