[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackPath,
    [Parameter(Mandatory = $true)][string]$InstalledNeural,
    [Parameter(Mandatory = $true)][string]$WorkRoot,
    [Parameter(Mandatory = $true)][string]$ReportPath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not (Test-Path -LiteralPath $PackPath)) {
    throw "Compatibility pack not found: $PackPath"
}
if (-not (Test-Path -LiteralPath $InstalledNeural)) {
    throw "Neural runtime directory not found: $InstalledNeural"
}

$resolvedPack = (Resolve-Path -LiteralPath $PackPath).Path
$compatRoot = $resolvedPack
$packHash = ''

if (Test-Path -LiteralPath $resolvedPack -PathType Leaf) {
    if ([IO.Path]::GetExtension($resolvedPack) -ne '.zip') {
        throw "Compatibility pack must be a ZIP file or a directory: $resolvedPack"
    }
    $compatExtract = Join-Path $WorkRoot 'compat-pack'
    if (Test-Path -LiteralPath $compatExtract) {
        Remove-Item -LiteralPath $compatExtract -Recurse -Force
    }
    New-Item -ItemType Directory -Path $compatExtract -Force | Out-Null
    Expand-Archive -LiteralPath $resolvedPack -DestinationPath $compatExtract -Force
    $compatRoot = $compatExtract
    $packHash = (Get-FileHash -LiteralPath $resolvedPack -Algorithm SHA256).Hash.ToLowerInvariant()
} else {
    # A folder downloaded from Drive is accepted directly. Produce a stable
    # digest over relevant files so the test machine can still report exactly
    # which payload was used.
    $hashRows = @()
    Get-ChildItem -LiteralPath $compatRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($compatRoot.Length).TrimStart([char]92)
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $hashRows += "$relative|$($_.Length)|$hash"
    }
    $hashText = $hashRows -join [Environment]::NewLine
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($hashText)
        $packHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

$inventoryPatterns = @(
    'version.dll',
    '*nvngx*',
    '*renodx*',
    '*reshade*',
    'sl.*',
    '*streamline*'
)
$inventory = @()
foreach ($pattern in $inventoryPatterns) {
    $inventory += @(Get-ChildItem -LiteralPath $compatRoot -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue)
}
$inventory = @($inventory | Sort-Object FullName -Unique)

# GTX/Turing packs seen in the wild use two different entry paths:
#  1) a local NGX core override (_nvngx.dll / nvngx.dll), or
#  2) a version.dll compatibility proxy plus the Streamline plugin set.
# Stage both layouts when present. System/driver DLLs such as nvapi64.dll and
# nvofapi64.dll remain deliberately excluded.
#
# IMPORTANT: do NOT replace the pinned nvngx_dlssnr.dll. The upstream runtime is
# ShortFuse 310.8.SF-v2, specifically patched for Turing/Ampere/Ada/Blackwell.
# The user-supplied Streamline pack contains a different signed 310.8 runtime;
# replacing SF-v2 with it was the cause of the Feature-18 PlatformError path.
# DLSSG is also irrelevant to this 1:1 neural-rendering bridge and is not staged.
$preservedNeuralPath = Join-Path $InstalledNeural 'nvngx_dlssnr.dll'
$preservedNeuralBefore = if (Test-Path -LiteralPath $preservedNeuralPath) {
    (Get-FileHash -LiteralPath $preservedNeuralPath -Algorithm SHA256).Hash.ToLowerInvariant()
} else { '' }

$copyNames = @(
    'version.dll',
    '_nvngx.dll',
    'nvngx.dll',
    'nvngx_dlss.dll',
    'sl.common.dll',
    'sl.dlss.dll',
    'sl.dlss_g.dll',
    'sl.dlss_nr.dll',
    'sl.interposer.dll',
    'sl.nis.dll',
    'sl.pcl.dll',
    'sl.reflex.dll',
    'renodx-dlss5.addon64',
    'dxgi.dll',
    'ReShade.ini'
)

$copied = @()
foreach ($name in $copyNames) {
    $matches = @(Get-ChildItem -LiteralPath $compatRoot -Recurse -File -Filter $name -ErrorAction SilentlyContinue)
    if ($matches.Count -gt 1) {
        $paths = ($matches | ForEach-Object { $_.FullName }) -join [Environment]::NewLine
        throw "Compatibility pack contains more than one '$name'. Refusing to guess. Candidates:$([Environment]::NewLine)$paths"
    }
    if ($matches.Count -eq 1) {
        $source = $matches[0].FullName
        $destination = Join-Path $InstalledNeural $name
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        $copied += [pscustomobject]@{
            Name = $name
            Source = $source.Substring($compatRoot.Length).TrimStart([char]92)
            Size = (Get-Item -LiteralPath $destination).Length
            SHA256 = $hash
        }
        Write-Host "  override: $name" -ForegroundColor Green
    }
}

$hasLocalCore = @($copied | Where-Object { $_.Name -in @('_nvngx.dll', 'nvngx.dll') }).Count -gt 0
$hasVersionProxy = Test-Path -LiteralPath (Join-Path $InstalledNeural 'version.dll')
$hasStreamlineInterposer = Test-Path -LiteralPath (Join-Path $InstalledNeural 'sl.interposer.dll')
$hasStreamlineNr = Test-Path -LiteralPath (Join-Path $InstalledNeural 'sl.dlss_nr.dll')
$hasDlss = Test-Path -LiteralPath (Join-Path $InstalledNeural 'nvngx_dlss.dll')
$hasDlssNr = Test-Path -LiteralPath (Join-Path $InstalledNeural 'nvngx_dlssnr.dll')
$preservedNeuralAfter = if ($hasDlssNr) {
    (Get-FileHash -LiteralPath (Join-Path $InstalledNeural 'nvngx_dlssnr.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
} else { '' }
$neuralRuntimePreserved = $preservedNeuralBefore -and ($preservedNeuralBefore -eq $preservedNeuralAfter)
$hasStreamlineProxy = $hasVersionProxy -and $hasStreamlineInterposer -and $hasStreamlineNr -and $hasDlss -and $hasDlssNr

$mode =
    if ($hasStreamlineProxy) { 'streamline-version-proxy' }
    elseif ($hasLocalCore) { 'local-ngx-core' }
    else { 'partial-or-unknown' }

$report = New-Object Text.StringBuilder
[void]$report.AppendLine('Better Xcloud DLSS5 compatibility-pack report')
[void]$report.AppendLine("Imported: $(Get-Date -Format o)")
[void]$report.AppendLine("Pack: $resolvedPack")
[void]$report.AppendLine("Pack digest SHA256: $packHash")
[void]$report.AppendLine("Compatibility mode: $mode")
[void]$report.AppendLine("Local NGX core override present: $hasLocalCore")
[void]$report.AppendLine("version.dll proxy present: $hasVersionProxy")
[void]$report.AppendLine("Streamline interposer present: $hasStreamlineInterposer")
[void]$report.AppendLine("Streamline NR plugin present: $hasStreamlineNr")
[void]$report.AppendLine("Pinned neural runtime preserved: $neuralRuntimePreserved")
[void]$report.AppendLine("Pinned neural runtime SHA256 before: $preservedNeuralBefore")
[void]$report.AppendLine("Pinned neural runtime SHA256 after:  $preservedNeuralAfter")
[void]$report.AppendLine('')
[void]$report.AppendLine('Copied overrides:')
if ($copied.Count -eq 0) {
    [void]$report.AppendLine('  (none)')
} else {
    foreach ($item in $copied) {
        [void]$report.AppendLine("  $($item.Name) | $($item.Size) bytes | $($item.SHA256) | $($item.Source)")
    }
}
[void]$report.AppendLine('')
[void]$report.AppendLine('Relevant files discovered in pack:')
if ($inventory.Count -eq 0) {
    [void]$report.AppendLine('  (none)')
} else {
    foreach ($item in $inventory) {
        $relative = $item.FullName.Substring($compatRoot.Length).TrimStart([char]92)
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [void]$report.AppendLine("  $relative | $($item.Length) bytes | $hash")
    }
}
$report.ToString() | Set-Content -LiteralPath $ReportPath -Encoding UTF8

if ($hasStreamlineProxy) {
    Write-Host 'Streamline/version.dll compatibility route staged.' -ForegroundColor Green
} elseif ($hasLocalCore) {
    Write-Host 'Local NGX core override staged.' -ForegroundColor Green
} else {
    Write-Warning 'The pack did not contain a complete local-NGX or Streamline/version.dll compatibility route.'
}

[pscustomobject]@{
    PackSHA256 = $packHash
    Mode = $mode
    LocalCore = $hasLocalCore
    StreamlineProxy = $hasStreamlineProxy
    NeuralRuntimePreserved = $neuralRuntimePreserved
    CopiedCount = $copied.Count
} | ConvertTo-Json -Compress
