# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

# pixi.toml carries the canonical version; CMakeLists.txt and
# package-release.ps1 both read the same line, so the ZIP the publisher looks
# for cannot drift from what is compiled in.
$pixiFile = Join-Path $ProjectRoot 'pixi.toml'
$versionMatch = Select-String -Path $pixiFile -Pattern '^version\s*=\s*"([^"]+)"'
if (-not $versionMatch) {
    throw "Could not extract version from $pixiFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'minecraft' `
    -ModName 'MinecraftHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
