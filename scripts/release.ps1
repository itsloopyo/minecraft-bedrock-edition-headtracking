#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Release workflow for Minecraft Head Tracking.

.DESCRIPTION
    Runs end to end with no operator interaction. Typing the command is the
    authorization; there is no second gate.

    1. Resolve the version (major/minor/patch or a literal X.Y.Z).
    2. Preflight: on main, clean tree, tag not already present.
    3. Regenerate CHANGELOG.md from commits. Done before anything is mutated,
       so an abort here leaves the tree clean rather than stranding a version
       bump with no tag.
    4. Bump the version in pixi.toml.
    5. Build and package.
    6. Commit "Release v<version>".
    7. Annotated tag v<version>.
    8. Push commits and tag, which triggers .github/workflows/release.yml.

.PARAMETER Version
    Semver string (e.g. "1.0.0"), or major/minor/patch, or 'nightly' to
    publish a rolling dev pre-release instead of a versioned release.

.PARAMETER Force
    Ship a release even when there are no user-facing commits since the last
    tag (writes a maintenance changelog entry instead of aborting).
#>
param(
    [Parameter(Position = 0)]
    [string]$Version = '',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    Set-Content $Path ($changelog.TrimEnd() + "`n") -NoNewline
}

$pixiPath = Join-Path $projectRoot 'pixi.toml'
$changelogPath = Join-Path $projectRoot 'CHANGELOG.md'

# pixi.toml carries the canonical version; CMakeLists.txt reads it from there
# at configure time, so this one line covers the compiled-in version too.
$pixiToml = Get-Content $pixiPath -Raw
if ($pixiToml -notmatch '(?m)^version\s*=\s*"([^"]+)"') {
    Write-Host 'Could not parse version from pixi.toml' -ForegroundColor Red
    exit 1
}
$current = $matches[1]

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: $current"
    Write-Host 'Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>' -ForegroundColor Red
    exit 1
}

try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$tag = "v$Version"

$branch = (git -C $projectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Host "Must be on main branch to release (currently on '$branch')" -ForegroundColor Red
    exit 1
}
if (-not (Test-CleanGitStatus)) {
    Write-Host 'Working tree has uncommitted changes - commit or stash first.' -ForegroundColor Red
    git -C $projectRoot status --short
    exit 1
}
if (Test-GitTagExists -Tag $tag) {
    Write-Host "Tag '$tag' already exists." -ForegroundColor Red
    exit 1
}

Write-Host "Releasing $current -> $Version" -ForegroundColor Cyan

Write-Host 'Generating CHANGELOG from commits...' -ForegroundColor Cyan
try {
    $changelogArgs = @{
        ChangelogPath = $changelogPath
        Version       = $Version
        ArtifactPaths = @('src/', 'cameraunlock-core', 'CMakeLists.txt', 'launcher-manifest.json', 'scripts/deploy.ps1', 'scripts/uninstall.ps1')
    }
    New-ChangelogFromCommits @changelogArgs | Out-Null
} catch {
    if (-not $Force) {
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host 'No user-facing changes to release. Re-run with -Force for a maintenance release.' -ForegroundColor Yellow
        exit 1
    }
    Write-Host 'No user-facing commits since last tag - writing maintenance entry (-Force).' -ForegroundColor Yellow
    Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
}

Write-Host "Updating pixi.toml to $Version..." -ForegroundColor Cyan
$bumped = [regex]::Replace($pixiToml, '(?m)^version\s*=\s*"[^"]+"', "version = `"$Version`"", 1)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($pixiPath, $bumped, $utf8NoBom)

Write-Host 'Building and packaging...' -ForegroundColor Cyan
Push-Location $projectRoot
try {
    & pixi run build
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & pixi run package
    if ($LASTEXITCODE -ne 0) { throw 'Package failed' }
} finally {
    Pop-Location
}

Write-Host 'Committing version + changelog...' -ForegroundColor Cyan
git -C $projectRoot add $pixiPath $changelogPath
git -C $projectRoot commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) { throw 'Commit failed' }

Write-Host "Creating tag $tag..." -ForegroundColor Cyan
git -C $projectRoot tag -a $tag -m "Release $tag"
if ($LASTEXITCODE -ne 0) { throw 'Tag failed' }

git -C $projectRoot push origin main
if ($LASTEXITCODE -ne 0) { throw 'Push failed' }
git -C $projectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { throw 'Tag push failed' }

Write-Host "Release $tag pushed - CI will build and publish the artifacts." -ForegroundColor Green
