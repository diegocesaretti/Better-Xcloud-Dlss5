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

# Full-window mirror control-panel mode:
# - The launcher does NOT start or modify Xbox App / Chrome / Edge / Brave.
# - The host opens its own Control & Debug window.
# - Pick Xbox App (recommended), Browser, or Auto and press Start mirror.
# - The selected target keeps ownership of controller/keyboard/mouse input.
# The host captures the selected top-level window and presents only a passive
# processed mirror above it.

$env:BETTER_XCLOUD_DLSS5_ALLOW_UNSUPPORTED_SR = '1'

# Keep the GPU identity on the real Turing route. The pinned ShortFuse
# 310.8.SF-v2 neural runtime is the component that provides the experimental
# Turing neural path; version.dll remains a local compatibility shim.
$optiDirect = Test-Path -LiteralPath (Join-Path $runtimeDir 'BACKEND_OPTISCALER_DIRECT_NR.txt')

if ($optiDirect) {
    # OptiScaler is loaded locally as winmm.dll and owns the NR compatibility
    # fallback. Do not pass legacy DLSS-Enabler command-line switches.
    Start-Process -FilePath $hostExe -WorkingDirectory $runtimeDir
} else {
    $hostArgs = @(
        '--dlss-arch=turing',
        '--dlss-hags=sys',
        '--dlss-logging=on'
    )
    Start-Process -FilePath $hostExe -WorkingDirectory $runtimeDir -ArgumentList $hostArgs
}
