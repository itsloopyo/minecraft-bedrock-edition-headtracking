#Requires -Version 5.1
<#
.SYNOPSIS
  Remove the deployed mod and launcher.
.DESCRIPTION
  The mod never writes into the game's package directory, so removing the two
  deployed binaries is the whole uninstall. CameraUnlock.ini, a
  MinecraftHeadTracking.ini from an earlier version and the log are left in the
  deployment folder, so a reinstall keeps the settings. Minecraft is left
  untouched.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$targetDir = Join-Path $env:LOCALAPPDATA 'CameraUnlock\MinecraftHeadTracking'
$artifacts = @('MinecraftHeadTracking.dll', 'MinecraftHeadTrackingLauncher.exe')

$deployed = @($artifacts | Where-Object { Test-Path (Join-Path $targetDir $_) })
if ($deployed.Count -eq 0) {
    Write-Host "Nothing to remove; neither binary is in $targetDir." -ForegroundColor Yellow
    return
}

if (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running. Close it before uninstalling so the mod DLL can be deleted.'
}

foreach ($artifact in $deployed) {
    Remove-Item (Join-Path $targetDir $artifact) -Force
    Write-Host "  removed $artifact" -ForegroundColor DarkGray
}
Write-Host "Removed the mod from $targetDir; its settings and log are left there." -ForegroundColor Green
