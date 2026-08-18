# Changelog

## [Unreleased]

### Added

- Support for Minecraft for Windows 1.26.4403.0 (EXE built 2026-08-12). Earlier
  builds keep working from the same mod binary.
- Every camera address is now recovered from the running game at load time
  instead of being pinned per build. Bedrock bakes `__FUNCSIG__` into its own
  assert strings, so `InGamePlayScreen::_renderLevelPrep` is found by name;
  `setupCamera` is the first function it calls carrying the EnTT type hash for
  `MinecraftCamera::CameraComponent`; the component getter is found through the
  accessor keyed on `RenderCameraComponent`; and the crosshair pair is found by
  the hardcoded 16x16 rect the cursor renderer blits. A Minecraft patch that
  only moves code no longer needs anything rederived.
- An unrecognised build now logs a paste-ready build profile, including the
  addresses it recovered, so adding support for a new patch is a copy rather
  than a rederive.
- A daily patch-watch workflow that polls the Microsoft Store listing and opens
  an issue with the profile checklist when the published package changes.

### Changed

- Build profiles now carry only struct field offsets and vtable indices. Those
  move when a class gains or loses a member, which is rare, unlike the code
  addresses which moved on every single patch. The table of pinned RVAs is gone.
- Every float read from the ini that fails its range or finiteness check is now
  named in the log instead of being replaced in silence, and the retired
  `Smoothing` key is reported once per section that still carries it.

### Fixed

- The call into the game's camera-component getter is now inside a fault
  boundary, like every other reach into game memory. It was the one call that
  was not, so a wrong address took the whole session down instead of costing a
  frame of tracking.

## [1.0.0] - 2026-08-17

### Changed

- Smoothing is now two keys in `[Tracking]`: `LocalSmoothing` (default `0.0`)
  for a tracker running on this machine, and `RemoteSmoothing` (default
  `0.15`) for a tracker on a remote network device. The value is chosen per
  connection from the packet source address and re-evaluated every frame, so
  swapping a local OpenTrack instance for a phone on WiFi needs no restart.
- Removed the single `[Tracking] Smoothing` key and the separate `[Position]
  Smoothing` key; both new parameters cover rotation and position.
- Removed the hidden 0.15 baseline smoothing floor, so a tracker on this
  machine gets zero-latency tracking by default.

## [0.1.0] - 2026-08-14

### Fixed

- single source of truth for defaults, harden the render path

### Other

- Hello world

## [0.0.0] - 2026-08-13

### Added
- Added a launcher (`MinecraftHeadTrackingLauncher.exe`) that activates the
  Minecraft Store package and injects the mod. Bedrock installs into a
  directory that cannot be written to, so this stands in for the proxy-DLL
  loader the rest of the catalogue uses.
- Added a mod DLL that logs its bootstrap beside itself and fingerprints the
  running game before touching anything.
- Added an append-only build-profile registry with the dormancy failsafe: an
  unrecognised or not-yet-derived build installs no hooks and leaves the game
  running vanilla. Ships the profile for Minecraft 1.26.4201.0
  (`store-win64-20260806`).
- Added analysis tooling: `build_symbols.py` recovers ~75,000 function names
  from Bedrock's embedded assertion strings, `xrefs.py` answers cross-reference
  queries, and `check_fingerprint.py` reports the running build against the
  registry.
- Added `docs/reverse-engineering.md` recording Bedrock's camera ECS, its
  render pipeline, and the ranked next steps for the camera hook.
- Added the pinned render-phase hook target
  `LevelRendererPlayer::preRenderUpdate` (RVA `0x0314c7d0` on this build),
  identified by both its assert descriptor and its `"Player - Pre render
  update"` profiler zone. The shadow camera has a separate `preRenderUpdate`,
  so this hook cannot disturb the shadow pass.
- Added discovery mode (off by default, enabled in
  `MinecraftHeadTracking.ini`). It hooks the render-phase camera update and
  ranks every plausible-angle float reachable from the call by how much it
  moved, which is what identifies the camera rotation on a new build. Verified
  to hook cleanly and leave the game stable; producing a ranked report needs
  ten seconds of gameplay.
- Added a default `MinecraftHeadTracking.ini` written beside the DLL on first
  run.
- Added the camera hook itself: head rotation and position are composed into
  the camera's post-view transform (`CameraComponent.PostViewTransform`, offset
  `0x5C` on this build) during the render phase, so the view moves while the
  rotation the game reads for aiming, block targeting and the server is
  untouched.
