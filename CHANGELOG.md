# Changelog

## [Unreleased]

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Minecraft for Windows runs as a packaged Store app, so this mod does not create it; `MinecraftHeadTracking.log` says whether it found one.

### Changed

- Settings move to `CameraUnlock.ini`, beside `MinecraftHeadTracking.dll`. Earlier versions of the mod kept these settings in `MinecraftHeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `MinecraftHeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `MinecraftHeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity or axis inversion you changed from its default. Set these in your tracker instead.
- An older version of the mod reads `MinecraftHeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `MinecraftHeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `MinecraftHeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `End`, `Page Up` and all three chords were fixed in code before, and only the yaw mode key could be set, as a hexadecimal key code; now every key and chord of all three actions can be changed or removed in `[Hotkeys]` (`ToggleKey`, `CycleTrackingModeKey`, `YawModeKey`). The import carries over the yaw mode key you had set.
- The tracking mode that Page Up or Ctrl+Shift+G selects, and the yaw mode that Page Down or Ctrl+Shift+H selects, are now saved to `CameraUnlock.ini` as soon as you change them and come back at the next start. End still changes the current session only.
- Settings are renamed in `CameraUnlock.ini`: `[Tracking] Port` is `[Network] UdpPort`; `[Tracking] EnableOnStartup` and `WorldSpaceYaw` are in `[General]`; `[Tracking] LocalSmoothing` and `RemoteSmoothing` are in `[Smoothing]`; `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`; and `[Discovery] Enabled` is `[Discovery] RunDiscovery`. `[Position] Enabled` chose the tracking mode at startup; that is now the pair `RotationEnabled` and `PositionEnabled`. Lowering your head was always limited to 0.2 metres; that limit is now `PositionLimitYDown`, which can be set apart from `PositionLimitY`. The import carries every one of these values over.
- A `UdpPort` in `CameraUnlock.ini` takes any port from 1 to 65535. Earlier versions took 1024 to 65535.
- A value in `CameraUnlock.ini` that is out of range or cannot be read keeps its default, and the log names the line. Earlier versions moved an out-of-range smoothing or position limit to the nearest end of its range. The position limits in `CameraUnlock.ini` take 0 to 10 metres, where earlier versions held them between 0.01 and 0.5.

### Removed

- The sensitivity and axis inversion settings: `[Tracking] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and `InvertRoll`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead. With these settings at their shipped defaults the camera moves as it did before: the pitch and roll inversion earlier versions shipped as defaults is now part of the mod's own axis conversion.

## [1.1.2] - 2026-08-29

### Added

- recover the camera layout from the running image, add build 20260829

## [1.1.1] - 2026-08-29

### Added

- recover the camera layout from the running image, add build 20260829

## [Unreleased]

### Added

- Support for Minecraft for Windows 1.26.4501.0 (EXE built 2026-08-29). Earlier
  builds keep working from the same mod binary.
- The camera's struct layout is now recovered from the running game as well as
  its addresses. `setupCamera` is the one function that reads every field the
  mod touches, so the mod reads it: the client instance is the pointer member
  loaded out of the renderer and handed to a call, the orientation is the only
  field read as four consecutive floats, the field of view is the float halved
  and passed to the tangent and the aspect ratio is the float that tangent's
  result is scaled by, and the post-view transform is the only field the
  function takes the address of. Those are the displacements the renderer
  itself uses, so the mod reads the same bytes it does by construction.

### Changed

- An unrecognised Minecraft build no longer leaves the mod dormant. Nothing
  about the camera is pinned any more, so a patch that moves or reshapes it is
  absorbed at load time and head tracking comes up on a build the mod has never
  seen. It stays dormant only when it cannot find the camera at all, which is
  the case that would mean hooking stale addresses.
- A build profile now carries only the PvP gate's offsets, which nothing in the
  image reads in a form the resolver can follow. On an unrecognised build the
  gate uses the newest profile's offsets and says so in the log. It still
  refuses to allow tracking unless every read checks out, and it holds back its
  in-game chat notice, because that call goes through a vtable slot the running
  build was never verified against and is the one reach into the game that
  cannot be checked before it is made.
- The registry of known builds moved next to the profiles it lists, so
  answering a patch is one edit in one file.

### Removed

- The pinned `mce::Camera` view-stack offsets. Nothing read them.


## [1.1.0] - 2026-08-20

### Other

- Remove mod-side recentring, the tracker app owns the centre

## [1.0.1] - 2026-08-18

### Added

- resolve camera addresses at runtime, add build 20260812

### Other

- hello world

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

- Removed recentring from the mod, including the `Home` / `Ctrl+Shift+T` hotkey
  and the handling of a Headcam CENTER press. The tracker app owns the centre,
  so the mod keeping one of its own put a second centre in series with the
  tracker's and the two drifted apart. Centre in your tracker app instead
  (opentrack's Center bind, the CENTER button in Headcam).
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
