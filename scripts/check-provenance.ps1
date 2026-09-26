#Requires -Version 5.1
<#
.SYNOPSIS
  Fail when a file tests/config_differential/provenance.txt lists no longer has its hash.
.DESCRIPTION
  The config differential test is only as good as its claim about what it compiled: the
  published build's reader, the core sources it used, the first-run files and the frozen
  import. Each line of provenance.txt is a SHA-256, a repo path and where the file came from.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$provenance = Join-Path $projectRoot 'tests/config_differential/provenance.txt'

# SHA256 through .NET, because Get-FileHash is not found when a runner's pwsh runs this under
# Windows PowerShell.
$sha256 = [System.Security.Cryptography.SHA256]::Create()
$checked = 0
foreach ($line in Get-Content $provenance) {
    if ($line -match '^\s*(#|$)') { continue }
    $hash, $path = ($line -split '\s+', 3)[0, 1]
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $projectRoot $path))
    $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
    $checked++
}
Write-Host "provenance: $checked files match provenance.txt" -ForegroundColor Green
