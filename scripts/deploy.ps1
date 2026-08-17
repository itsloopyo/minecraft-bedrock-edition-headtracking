#Requires -Version 5.1
<#
.SYNOPSIS
  Copy the built mod and launcher into the local deployment folder.
.DESCRIPTION
  Minecraft installs under C:\Program Files\WindowsApps, which is owned by
  TrustedInstaller and signature-checked as a package, so the mod cannot live
  beside the game. It goes to a normal per-user folder instead and the
  launcher injects it from there.
#>
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectDir "build\$Configuration"
$targetDir = Join-Path $env:LOCALAPPDATA 'CameraUnlock\MinecraftHeadTracking'

$artifacts = @('MinecraftHeadTracking.dll', 'MinecraftHeadTrackingLauncher.exe')

foreach ($artifact in $artifacts) {
    if (-not (Test-Path (Join-Path $buildDir $artifact))) {
        throw "$artifact not found in $buildDir. Run 'pixi run build' first."
    }
}

if (-not (Test-Path $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}

foreach ($artifact in $artifacts) {
    Copy-Item (Join-Path $buildDir $artifact) $targetDir -Force
    Write-Host "  $artifact" -ForegroundColor DarkGray
}

Write-Host "Deployed to $targetDir" -ForegroundColor Green
Write-Host "Run MinecraftHeadTrackingLauncher.exe from there to start Minecraft with head tracking."
