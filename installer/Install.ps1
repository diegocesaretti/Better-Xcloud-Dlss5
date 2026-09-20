[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'BetterXcloudDLSS5')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$UpstreamVersion = '0.24.0'
$UpstreamUrl = 'https://github.com/2600th/dlss5-video-player/releases/download/dlss5-video-player-v0.24.0/dlss5-video-player-v0.24.0-win64.zip'
$UpstreamSha256 = '66947ca33b84459d13b3002cfea153f295c1d0543260713eced6b9766a136e78'

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Fail([string]$Text, [int]$Code = 1) {
    Write-Host ''
    Write-Host "ERROR: $Text" -ForegroundColor Red
    exit $Code
}

if (-not [Environment]::Is64BitOperatingSystem) {
    Fail 'Windows x64 is required.' 10
}

$os = [Environment]::OSVersion.Version
if ($os.Major -lt 10) {
    Fail 'Windows 10 or Windows 11 is required.' 11
}

$packageRoot = Split-Path -Parent $PSScriptRoot
$hostExe = Join-Path $packageRoot 'XCloudDLSS5Host.exe'
$launcherSource = Join-Path $packageRoot 'launcher\Start-XCloud-DLSS5.ps1'

if (-not (Test-Path -LiteralPath $hostExe)) {
    Fail "XCloudDLSS5Host.exe is missing. Use the packaged release/artifact, not GitHub's source-code ZIP." 12
}
if (-not (Test-Path -LiteralPath $launcherSource)) {
    Fail 'The launcher file is missing from the package.' 13
}

Write-Host ''
Write-Host 'Better Xcloud DLSS5 alpha installer' -ForegroundColor Green
Write-Host 'No administrator rights are required.'
Write-Host ''
Write-Host 'Important: the neural runtime is a community-modified, unsigned runtime.'
Write-Host 'It is downloaded directly from the DLSS5 Video Player upstream release.'
Write-Host 'This installer verifies the pinned SHA-256 before using it.'
Write-Host ''

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('BetterXcloudDLSS5-' + [guid]::NewGuid().ToString('N'))
$zipPath = Join-Path $tempRoot 'dlss5-video-player.zip'
$extractRoot = Join-Path $tempRoot 'extract'
$runtimeDestination = Join-Path $InstallRoot 'Runtime'

try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

    Write-Step "Downloading pinned DLSS5 Video Player $UpstreamVersion runtime"
    Invoke-WebRequest -Uri $UpstreamUrl -OutFile $zipPath -UseBasicParsing

    Write-Step 'Verifying upstream package SHA-256'
    $actual = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $UpstreamSha256) {
        Fail "Upstream package hash mismatch. Expected $UpstreamSha256 but received $actual. Nothing was installed." 20
    }
    Write-Host 'SHA-256 OK.' -ForegroundColor Green

    Write-Step 'Extracting runtime'
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force

    $player = Get-ChildItem -LiteralPath $extractRoot -Recurse -File -Filter 'DLSSVideoPlayer.exe' |
        Select-Object -First 1
    if (-not $player) {
        Fail 'The verified upstream ZIP does not contain DLSSVideoPlayer.exe.' 21
    }

    $upstreamRoot = $player.Directory.FullName
    $upstreamNeural = Join-Path $upstreamRoot 'neural-runtime'
    $required = @(
        (Join-Path $upstreamNeural 'dxgi.dll'),
        (Join-Path $upstreamNeural 'renodx-dlss5.addon64'),
        (Join-Path $upstreamNeural 'nvngx_dlssnr.dll'),
        (Join-Path $upstreamNeural 'ReShade.ini')
    )
    foreach ($file in $required) {
        if (-not (Test-Path -LiteralPath $file)) {
            Fail "The upstream package is incomplete: missing $([IO.Path]::GetFileName($file))." 22
        }
    }

    Write-Step 'Installing per-user files'
    if (Test-Path -LiteralPath $runtimeDestination) {
        Remove-Item -LiteralPath $runtimeDestination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $runtimeDestination -Force | Out-Null
    Copy-Item -Path (Join-Path $upstreamRoot '*') -Destination $runtimeDestination -Recurse -Force

    $installedNeural = Join-Path $runtimeDestination 'neural-runtime'
    Copy-Item -LiteralPath $hostExe -Destination (Join-Path $installedNeural 'XCloudDLSS5Host.exe') -Force

    New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
    Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $InstallRoot 'Start-XCloud-DLSS5.ps1') -Force

    @"
Better Xcloud DLSS5
Installed: $(Get-Date -Format o)
DLSS5 Video Player: $UpstreamVersion
Upstream package SHA256: $UpstreamSha256
Host source: https://github.com/diegocesaretti/Better-Xcloud-Dlss5
"@ | Set-Content -LiteralPath (Join-Path $InstallRoot 'INSTALL_INFO.txt') -Encoding UTF8

    Write-Step 'Creating Start Menu shortcut'
    $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
    $shortcutPath = Join-Path $startMenu 'Better Xcloud DLSS5.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $launcherInstalled = Join-Path $InstallRoot 'Start-XCloud-DLSS5.ps1'
    $shortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$launcherInstalled`""
    $shortcut.WorkingDirectory = $InstallRoot
    $shortcut.Description = 'Launch Xbox Cloud Gaming with the DLSS5 neural overlay'
    $shortcut.Save()

    Write-Step 'Installation complete'
    Write-Host "Installed to: $InstallRoot" -ForegroundColor Green
    Write-Host ''
    Write-Host 'Use Start > Better Xcloud DLSS5.'
    Write-Host 'F8 = show/hide DLSS overlay'
    Write-Host 'F9 = stop the DLSS host'
    Write-Host ''
    Write-Host 'Better xCloud is recommended and should be installed from its official project.'

    $answer = Read-Host 'Open the official Better xCloud installation page now? [Y/n]'
    if ([string]::IsNullOrWhiteSpace($answer) -or $answer -match '^[YySs]') {
        Start-Process 'https://better-xcloud.github.io/'
    }

    $launch = Read-Host 'Launch Xbox Cloud Gaming with DLSS5 now? [Y/n]'
    if ([string]::IsNullOrWhiteSpace($launch) -or $launch -match '^[YySs]') {
        Start-Process powershell.exe -ArgumentList @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', "`"$launcherInstalled`""
        )
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
