[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'BetterXcloudDLSS5'),
    [string]$CompatibilityPack = '',
    [switch]$NonInteractive
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

function Resolve-CompatibilityPack([string]$ExplicitPath, [string]$PackageRoot) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $resolved = Resolve-Path -LiteralPath $ExplicitPath -ErrorAction SilentlyContinue
        if (-not $resolved) { Fail "Compatibility pack not found: $ExplicitPath" 30 }
        return $resolved.Path
    }

    $candidates = @()
    foreach ($pattern in @('GTX1660*.zip', 'compat*.zip', 'drive-download*.zip')) {
        $candidates += @(Get-ChildItem -LiteralPath $PackageRoot -File -Filter $pattern -ErrorAction SilentlyContinue)
    }
    $streamlineFolder = Join-Path $PackageRoot 'streamline'
    if (Test-Path -LiteralPath $streamlineFolder -PathType Container) {
        $candidates += @(Get-Item -LiteralPath $streamlineFolder)
    }

    $candidates = @($candidates | Sort-Object FullName -Unique)
    if ($candidates.Count -eq 1) { return $candidates[0].FullName }
    if ($candidates.Count -gt 1) {
        Write-Warning 'Multiple compatibility packs were found beside Install.cmd; none was selected automatically.'
    }
    return ''
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
$compatImporter = Join-Path $PSScriptRoot 'Import-CompatibilityPack.ps1'

if (-not (Test-Path -LiteralPath $hostExe)) {
    Fail "XCloudDLSS5Host.exe is missing. Use the packaged release/artifact, not GitHub's source-code ZIP." 12
}
if (-not (Test-Path -LiteralPath $launcherSource)) {
    Fail 'The launcher file is missing from the package.' 13
}
if (-not (Test-Path -LiteralPath $compatImporter)) {
    Fail 'The compatibility-pack importer is missing from the package.' 14
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
$resolvedCompatibilityPack = Resolve-CompatibilityPack $CompatibilityPack $packageRoot

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

    $compatImported = $false
    if (-not [string]::IsNullOrWhiteSpace($resolvedCompatibilityPack)) {
        Write-Step 'Importing known-good GTX/Turing compatibility pack'
        $compatReport = Join-Path $InstallRoot 'COMPATIBILITY_PACK_INFO.txt'
        $compatJson = & $compatImporter -PackPath $resolvedCompatibilityPack -InstalledNeural $installedNeural -WorkRoot $tempRoot -ReportPath $compatReport
        $compatJson | Out-Host
        $compatImported = $true
    } else {
        Write-Host ''
        Write-Host 'No external GTX compatibility pack selected; using the pinned upstream runtime.' -ForegroundColor Yellow
    }

    $streamlineCompat =
        (Test-Path -LiteralPath (Join-Path $installedNeural 'version.dll')) -and
        (Test-Path -LiteralPath (Join-Path $installedNeural 'sl.interposer.dll')) -and
        (Test-Path -LiteralPath (Join-Path $installedNeural 'sl.dlss_nr.dll')) -and
        (Test-Path -LiteralPath (Join-Path $installedNeural 'nvngx_dlss.dll')) -and
        (Test-Path -LiteralPath (Join-Path $installedNeural 'nvngx_dlssnr.dll'))

    $optiDirect = Test-Path -LiteralPath (Join-Path $installedNeural 'BACKEND_OPTISCALER_DIRECT_NR.txt')

    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BetterXcloudIni {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern uint GetPrivateProfileString(string section, string key, string defaultValue, StringBuilder value, uint size, string filePath);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string filePath);
}
'@

    $renoHookMode = 'disabled'

    if ($optiDirect) {
        Write-Step 'Configuring OptiScaler built-in Neural Rendering backend'

        $optiIni = Join-Path $installedNeural 'OptiScaler.ini'
        if (-not (Test-Path -LiteralPath $optiIni)) {
            Fail 'OptiScaler direct-NR package did not provide OptiScaler.ini.' 25
        }

        $settings = @(
            @('ProcessFilter', 'TargetProcessName', 'XCloudDLSS5Host.exe'),
            @('DlssNr', 'Enabled', 'true'),
            @('DlssNr', 'RunBeforeSR', 'false'),
            @('DlssNr', 'Passes', '1'),
            @('DlssNr', 'WorkingScale', '0.50'),
            @('DlssNr', 'AutoCapture', 'true'),
            @('Menu', 'OverlayMenu', 'false'),
            @('Menu', 'ShortcutKey', '-1'),
            @('Hotfix', 'ManualInputPolling', 'false'),
            @('Hotfix', 'PreferDedicatedGpu', 'true'),
            @('Log', 'LogToFile', 'true'),
            @('Log', 'LogLevel', '2'),
            @('Log', 'LogToConsole', 'false'),
            @('Log', 'LogToNGX', 'true'),
            @('Log', 'LogFileName', 'OptiScaler.log')
        )

        foreach ($entry in $settings) {
            if (-not [BetterXcloudIni]::WritePrivateProfileString(
                    $entry[0], $entry[1], $entry[2], $optiIni)) {
                Fail "Could not write OptiScaler setting [$($entry[0])] $($entry[1])." 26
            }
        }

        $runtimeHash = (Get-FileHash -LiteralPath (Join-Path $installedNeural 'nvngx_dlssnr.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        $optiHash = (Get-FileHash -LiteralPath (Join-Path $installedNeural 'winmm.dll') -Algorithm SHA256).Hash.ToLowerInvariant()

        @"
Backend: OptiScaler built-in Neural Rendering
Injection proxy: winmm.dll
Target process: XCloudDLSS5Host.exe
nvngx_dlssnr.dll SHA256: $runtimeHash
OptiScaler proxy SHA256: $optiHash
Initial NR settings:
  Enabled=true
  RunBeforeSR=false
  Passes=1
  WorkingScale=0.50
"@ | Set-Content -LiteralPath (Join-Path $InstallRoot 'NEURAL_BACKEND_INFO.txt') -Encoding UTF8

        Write-Host 'OptiScaler direct-NR backend enabled. RenoDX/ReShade path disabled.' -ForegroundColor Green
        Write-Host "NR runtime SHA256: $runtimeHash" -ForegroundColor DarkGray
    } else {
        # Legacy route retained as fallback for the older compatibility pack.
        $renoHookMode = '2'

        Write-Step 'Configuring RenoDX raw-NGX neural hook'
        $reshadeIni = Join-Path $installedNeural 'ReShade.ini'

        $buffer = New-Object Text.StringBuilder 4096
        [void][BetterXcloudIni]::GetPrivateProfileString('ADDON', 'DisabledAddons', '', $buffer, 4096, $reshadeIni)
        $blocked = @(
            'DLSS 5 Neural Rendering@renodx-dlss5.addon64',
            'DLSS 5 Neural Rendering',
            '@renodx-dlss5.addon64',
            'renodx-dlss5.addon64'
        )
        $kept = @($buffer.ToString() -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -and ($_ -notin $blocked) })
        if (-not [BetterXcloudIni]::WritePrivateProfileString('ADDON', 'DisabledAddons', ($kept -join ','), $reshadeIni)) {
            Fail 'Could not enable the RenoDX neural add-on in ReShade.ini.' 23
        }
        foreach ($entry in @(
            @('EnableHooks', $renoHookMode),
            @('NeuralUplift', '1'),
            @('NREnableUpscaling', '0')
        )) {
            if (-not [BetterXcloudIni]::WritePrivateProfileString('RenoDX.DLSS5', $entry[0], $entry[1], $reshadeIni)) {
                Fail "Could not write RenoDX setting $($entry[0])." 24
            }
        }

        [void][BetterXcloudIni]::WritePrivateProfileString('INPUT', 'KeyOverlay', '0,0,0,0', $reshadeIni)
        [void][BetterXcloudIni]::WritePrivateProfileString('GENERAL', 'TutorialProgress', '4', $reshadeIni)

        if (Test-Path -LiteralPath (Join-Path $installedNeural 'version.dll')) {
            @"
[Debug]
DisableUI=true
EarlyInit=true
UseFsrOnly=true

[UI]
Monitoring=false
SideBar=false

[Performance]
ForceLoadDLSSG=false
DynamicMFG=false
MFGHotkeys=false
"@ | Set-Content -LiteralPath (Join-Path $installedNeural 'dlss-enabler.ini') -Encoding ASCII
        }

        Write-Host "Legacy RenoDX neural hook enabled (EnableHooks=$renoHookMode)." -ForegroundColor Yellow
    }

    New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
    Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $InstallRoot 'Start-XCloud-DLSS5.ps1') -Force

    @"
Better Xcloud DLSS5
Installed: $(Get-Date -Format o)
DLSS5 Video Player: $UpstreamVersion
Upstream package SHA256: $UpstreamSha256
Host source: https://github.com/diegocesaretti/Better-Xcloud-Dlss5
Compatibility pack: $resolvedCompatibilityPack
Compatibility pack imported: $compatImported
Neural backend: $(if ($optiDirect) { 'OptiScaler built-in NR' } else { 'Legacy RenoDX' })
RenoDX EnableHooks: $renoHookMode
OptiScaler direct NR: $optiDirect
Streamline/version.dll compatibility route: $streamlineCompat
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
    $shortcut.Description = 'Open the Better Xcloud DLSS5 Xbox App / browser control and debug panel'
    $shortcut.Save()

    Write-Step 'Installation complete'
    Write-Host "Installed to: $InstallRoot" -ForegroundColor Green
    Write-Host ''
    Write-Host 'Use Start > Better Xcloud DLSS5 to open the Control & Debug panel.'
    Write-Host 'Choose Xbox App (recommended), Browser, or Auto, then click Start mirror.'
    Write-Host 'The mirror never injects controller, keyboard or mouse input into the target.'
    Write-Host 'Use the panel to show/hide the mirror, stop it, edit settings, open logs, or copy diagnostics.'
    if ($optiDirect) {
        Write-Host 'Backend: OptiScaler built-in Neural Rendering (direct compatibility fallback).' -ForegroundColor Green
    }
    Write-Host ''
    Write-Host 'Better xCloud is recommended and should be installed from its official project.'

    if (-not $NonInteractive) {
        $answer = Read-Host 'Open the official Better xCloud installation page now? [Y/n]'
        if ([string]::IsNullOrWhiteSpace($answer) -or $answer -match '^[YySs]') {
            Start-Process 'https://better-xcloud.github.io/'
        }

        $launch = Read-Host 'Open the Better Xcloud DLSS5 Control & Debug panel now? [Y/n]'
        if ([string]::IsNullOrWhiteSpace($launch) -or $launch -match '^[YySs]') {
            Start-Process powershell.exe -ArgumentList @(
                '-NoProfile',
                '-ExecutionPolicy', 'Bypass',
                '-File', "`"$launcherInstalled`""
            )
        }
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
