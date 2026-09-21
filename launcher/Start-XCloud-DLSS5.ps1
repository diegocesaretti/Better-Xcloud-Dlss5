$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$hostExe = Join-Path $root 'Runtime\neural-runtime\XCloudDLSS5Host.exe'
$runtimeDir = Split-Path -Parent $hostExe

if (-not (Test-Path -LiteralPath $hostExe)) {
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show(
        'XCloudDLSS5Host.exe is missing. Reinstall Better Xcloud DLSS5.',
        'Better Xcloud DLSS5',
        'OK',
        'Error') | Out-Null
    exit 2
}

function Find-ExistingXCloudBrowser {
    foreach ($name in @('chrome', 'msedge', 'brave')) {
        foreach ($proc in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            if ($proc.MainWindowHandle -ne 0 -and
                $proc.MainWindowTitle -match '(?i)(xbox|cloud gaming|xcloud)') {
                return $proc
            }
        }
    }
    return $null
}

function Find-Browser {
    # Prefer Chrome because Better xCloud is commonly installed there. If an
    # xCloud window is already open, it is reused and this choice is irrelevant.
    $candidates = @()

    $chromeCommand = Get-Command 'chrome.exe' -ErrorAction SilentlyContinue
    if ($chromeCommand) { $candidates += $chromeCommand.Source }

    $candidates += @(
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
    )

    $edgeCommand = Get-Command 'msedge.exe' -ErrorAction SilentlyContinue
    if ($edgeCommand) { $candidates += $edgeCommand.Source }

    $candidates += @(
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
        "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
    )

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }
    return $null
}

$browser = Find-Browser
if (-not $browser) {
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show(
        'Microsoft Edge or Google Chrome was not found.',
        'Better Xcloud DLSS5',
        'OK',
        'Error') | Out-Null
    exit 3
}

$existingXCloud = Find-ExistingXCloudBrowser
if (-not $existingXCloud) {
    # Launch xCloud like a normal user-created browser window. Do not add
    # experimental Chromium switches here: gamepad detection should follow the
    # exact same browser path the user gets when opening xbox.com manually.
    Start-Process -FilePath $browser -ArgumentList @(
        '--new-window',
        'https://www.xbox.com/play'
    )
    Start-Sleep -Seconds 3
}

# Allow the experimental raw-NGX carrier path to continue on GPUs where
# NVIDIA's native DLSS-SR capability bit is unavailable. This does not enable
# DLSS by itself; it only gives the local RenoDX hook a chance to intercept.
$env:BETTER_XCLOUD_DLSS5_ALLOW_UNSUPPORTED_SR = '1'

# Starting from neural-runtime is intentional: the upstream dxgi.dll,
# RenoDX add-on and locked neural DLLs are local to this process only.
#
# Keep the GPU identity Turing. The pinned ShortFuse 310.8.SF-v2 neural runtime
# is specifically modified for Turing, so pretending the GTX 1660 is Ada can
# select an incompatible NR path and produce NGX PlatformError. version.dll may
# still provide its normal DXGI/NVAPI compatibility hooks for the DLSS carrier.
$hostArgs = @(
    '--dlss-arch=turing',
    '--dlss-hags=sys',
    '--dlss-logging=on'
)

Start-Process -FilePath $hostExe -WorkingDirectory $runtimeDir -ArgumentList $hostArgs
