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

# Attach-only mirror mode:
# - The launcher does NOT start Chrome/Edge/Brave.
# - It does NOT add browser command-line switches.
# - It does NOT change browser focus.
# Open Xbox Cloud Gaming normally first (with Better xCloud if desired), verify
# the controller works, then start this shortcut. The host waits up to 2 minutes
# for an existing xCloud browser window and captures that entire HWND.

$env:BETTER_XCLOUD_DLSS5_ALLOW_UNSUPPORTED_SR = '1'

# Keep the GPU identity on the real Turing route. The pinned ShortFuse
# 310.8.SF-v2 neural runtime is the component that provides the experimental
# Turing neural path; version.dll remains a local compatibility shim.
$hostArgs = @(
    '--dlss-arch=turing',
    '--dlss-hags=sys',
    '--dlss-logging=on'
)

Start-Process -FilePath $hostExe -WorkingDirectory $runtimeDir -ArgumentList $hostArgs
