#Requires -Version 5.1
<#
.SYNOPSIS
  Remove the deployed mod and launcher.
.DESCRIPTION
  The mod never writes into the game's package directory, so removing the
  deployment folder is the whole uninstall. Minecraft is left untouched.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$targetDir = Join-Path $env:LOCALAPPDATA 'CameraUnlock\MinecraftHeadTracking'

if (-not (Test-Path $targetDir)) {
    Write-Host "Nothing to remove; $targetDir does not exist." -ForegroundColor Yellow
    return
}

if (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running. Close it before uninstalling so the mod DLL can be deleted.'
}

Remove-Item $targetDir -Recurse -Force
Write-Host "Removed $targetDir" -ForegroundColor Green
