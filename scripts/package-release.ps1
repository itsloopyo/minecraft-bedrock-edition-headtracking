#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
  Build the release ZIP in release/.
.DESCRIPTION
  MinecraftHeadTracking-v<version>-installer.zip

  Bedrock installs under C:\Program Files\WindowsApps, which is owned by
  TrustedInstaller and signature-checked, so nothing is ever copied beside the
  game. The payload is the launcher plus the mod DLL, which live together in a
  per-user folder; the launcher injects the DLL it finds next to itself. So the
  ZIP is extract-and-run, with both artifacts at its root, and there is no
  second Nexus ZIP: with no loader to bootstrap and no game-directory install,
  a Nexus user does exactly what everyone else does.

  Runs unattended: no prompts, exits non-zero on any failure.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

$modName = 'MinecraftHeadTracking'

# pixi.toml is the canonical version; CMakeLists.txt parses the same line and
# compiles it into both artifacts, so the ZIP name cannot drift from the
# version the mod logs at startup.
$pixiToml = Get-Content (Join-Path $projectRoot 'pixi.toml') -Raw
if ($pixiToml -notmatch '(?m)^version\s*=\s*"([^"]+)"') {
    throw 'Could not parse version from pixi.toml'
}
$version = $matches[1]

$buildDir = Join-Path $projectRoot 'build\Release'
$artifacts = @("$modName.dll", "${modName}Launcher.exe")

foreach ($artifact in $artifacts) {
    if (-not (Test-Path (Join-Path $buildDir $artifact))) {
        throw "$artifact not found in $buildDir. Run 'pixi run build' first."
    }
}

$releaseDir = Join-Path $projectRoot 'release'
if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

$staging = Join-Path $releaseDir '_staging'
New-Item -ItemType Directory -Path $staging -Force | Out-Null

foreach ($artifact in $artifacts) {
    Copy-Item (Join-Path $buildDir $artifact) -Destination $staging -Force
}

foreach ($doc in 'README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md') {
    Copy-Item (Join-Path $projectRoot $doc) -Destination $staging -Force
}

# The launcher reads this to deploy the package natively. Both binaries are
# anchored to mod_home: Bedrock's install directory is read-only, so the
# payload goes to the launcher's own per-game folder and the launcher spawns
# MinecraftHeadTrackingLauncher.exe from there.
$manifest = Get-Content (Join-Path $projectRoot 'launcher-manifest.json') -Raw | ConvertFrom-Json
$manifest.mod_info.version = $version
$manifestJson = $manifest | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText(
    (Join-Path $staging 'launcher-manifest.json'),
    $manifestJson,
    (New-Object System.Text.UTF8Encoding($false)))

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $installerZip -Force
Write-Host "Created: $installerZip" -ForegroundColor Green

Remove-Item $staging -Recurse -Force
