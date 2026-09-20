[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ZipPath,
    [Parameter(Mandatory = $true)][string]$InstalledNeural,
    [Parameter(Mandatory = $true)][string]$WorkRoot,
    [Parameter(Mandatory = $true)][string]$ReportPath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if (-not (Test-Path -LiteralPath $ZipPath)) {
    throw "Compatibility pack not found: $ZipPath"
}
if (-not (Test-Path -LiteralPath $InstalledNeural)) {
    throw "Neural runtime directory not found: $InstalledNeural"
}

$compatExtract = Join-Path $WorkRoot 'compat-pack'
if (Test-Path -LiteralPath $compatExtract) {
    Remove-Item -LiteralPath $compatExtract -Recurse -Force
}
New-Item -ItemType Directory -Path $compatExtract -Force | Out-Null
Expand-Archive -LiteralPath $ZipPath -DestinationPath $compatExtract -Force

$inventoryPatterns = @('*nvngx*', '*renodx*', '*reshade*', 'sl.*', '*streamline*')
$inventory = @()
foreach ($pattern in $inventoryPatterns) {
    $inventory += @(Get-ChildItem -LiteralPath $compatExtract -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue)
}
$inventory = @($inventory | Sort-Object FullName -Unique)

# These are the only files allowed to replace the pinned upstream runtime.
# System/driver DLLs such as nvapi64.dll and nvofapi64.dll are deliberately excluded.
$copyNames = @(
    '_nvngx.dll',
    'nvngx.dll',
    'nvngx_dlss.dll',
    'nvngx_dlssnr.dll',
    'renodx-dlss5.addon64',
    'dxgi.dll',
    'ReShade.ini'
)

$copied = @()
foreach ($name in $copyNames) {
    $matches = @(Get-ChildItem -LiteralPath $compatExtract -Recurse -File -Filter $name -ErrorAction SilentlyContinue)
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
            Source = $source.Substring($compatExtract.Length).TrimStart([char]92)
            Size = (Get-Item -LiteralPath $destination).Length
            SHA256 = $hash
        }
        Write-Host "  override: $name" -ForegroundColor Green
    }
}

$hasLocalCore = @($copied | Where-Object { $_.Name -in @('_nvngx.dll', 'nvngx.dll') }).Count -gt 0
$packHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()

$report = New-Object Text.StringBuilder
[void]$report.AppendLine('Better Xcloud DLSS5 compatibility-pack report')
[void]$report.AppendLine("Imported: $(Get-Date -Format o)")
[void]$report.AppendLine("Pack: $ZipPath")
[void]$report.AppendLine("Pack SHA256: $packHash")
[void]$report.AppendLine("Local NGX core override present: $hasLocalCore")
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
        $relative = $item.FullName.Substring($compatExtract.Length).TrimStart([char]92)
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [void]$report.AppendLine("  $relative | $($item.Length) bytes | $hash")
    }
}
$report.ToString() | Set-Content -LiteralPath $ReportPath -Encoding UTF8

if (-not $hasLocalCore) {
    Write-Warning 'No _nvngx.dll or nvngx.dll was found in the compatibility pack. The driver NGX core may still reject GTX hardware before RenoDX can intercept.'
} else {
    Write-Host 'Local NGX core override staged. NGX will probe it before DriverStore.' -ForegroundColor Green
}

[pscustomobject]@{
    PackSHA256 = $packHash
    LocalCore = $hasLocalCore
    CopiedCount = $copied.Count
} | ConvertTo-Json -Compress
