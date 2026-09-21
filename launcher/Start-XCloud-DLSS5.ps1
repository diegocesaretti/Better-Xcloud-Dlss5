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
    # Do not use Chrome's --app mode. A normal browser window keeps the web
    # contents focus model identical to ordinary xCloud/Better xCloud use.
    # The native overlay covers the browser, so also disable Chromium's native
    # occlusion/background throttling for this dedicated xCloud window.
    Start-Process -FilePath $browser -ArgumentList @(
        '--new-window',
        '--start-maximized',
        '--disable-backgrounding-occluded-windows',
        '--disable-renderer-backgrounding',
        '--disable-features=CalculateNativeWinOcclusion',
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
# The known-good GTX/Turing compatibility proxy was previously starting with
# no command-line switches, so it delegated NVAPI architecture reporting to the
# physical GTX card. Force the proxy's own embedded NVAPI + HAGS emulation and
# an Ada target for the experimental Feature-18 path.
$hostArgs = @(
    '--dlss-hags=on',
    '--dlss-nvapi=embedded',
    '--dlss-arch=ada',
    '--dlss-logging=on'
)

Start-Process -FilePath $hostExe -WorkingDirectory $runtimeDir -ArgumentList $hostArgs
