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

function Find-Browser {
    $candidates = @()

    $edgeCommand = Get-Command 'msedge.exe' -ErrorAction SilentlyContinue
    if ($edgeCommand) { $candidates += $edgeCommand.Source }

    $candidates += @(
        "$env:ProgramFiles(x86)\Microsoft\Edge\Application\msedge.exe",
        "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
    )

    $chromeCommand = Get-Command 'chrome.exe' -ErrorAction SilentlyContinue
    if ($chromeCommand) { $candidates += $chromeCommand.Source }

    $candidates += @(
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
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

Start-Process -FilePath $browser -ArgumentList @(
    '--app=https://www.xbox.com/play',
    '--start-maximized'
)

Start-Sleep -Seconds 3

# Starting from neural-runtime is intentional: the upstream dxgi.dll,
# RenoDX add-on and locked neural DLLs are local to this process only.
Start-Process -FilePath $hostExe -WorkingDirectory $runtimeDir
