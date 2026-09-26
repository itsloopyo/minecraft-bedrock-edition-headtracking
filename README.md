# Minecraft: Bedrock Edition Head Tracking

Head tracking for Minecraft for Windows (Bedrock Edition): your head moves the
view while the mouse still controls where you aim, mine and build, with no VR
headset needed.

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/minecraft-bedrock-edition-headtracking/main/assets/readme-clip.gif)

> **Settings have moved.** From this version the mod keeps its settings in
> `CameraUnlock.ini`, beside `MinecraftHeadTracking.dll`. The first time it starts
> without one, it reads your settings from `MinecraftHeadTracking.ini`, the file
> earlier versions used, and never changes that file. See
> [Configuration](#configuration).

## Features

- **Decoupled look and aim** - your head moves the camera, the mouse still
  targets the block you mine and build on
- **6DOF positional tracking** - lean and peek to shift the viewpoint, on top of
  yaw, pitch and roll
- **Off in PvP** - disables itself when the `pvp` game rule is on and another
  player is in the session

## Requirements

- [Minecraft for Windows (Bedrock Edition)](https://apps.microsoft.com/detail/9nblggh2jhxj)
  from the Microsoft Store
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a
  webcam or VR headset, or a phone app such as [Headcam](https://headcam.app)
- Windows 10 version 1903 or newer, x64

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Minecraft: Bedrock Edition**, and click
**Play with head tracking**.

### Standalone Launcher

1. Download `MinecraftHeadTracking-v<version>-installer.zip` from the
   [Releases page](https://github.com/itsloopyo/minecraft-bedrock-edition-headtracking/releases).
2. Extract it anywhere writable, for example
   `%LOCALAPPDATA%\CameraUnlock\MinecraftHeadTracking`, keeping the DLL and the
   launcher together (see [Manual Installation](#manual-installation)).
3. Configure your tracker to send OpenTrack UDP packets to `127.0.0.1:4242`
   (see [Setting Up OpenTrack](#setting-up-opentrack)).
4. Run `MinecraftHeadTrackingLauncher.exe` from the folder you extracted,
   instead of the usual Minecraft shortcut. It starts Minecraft through the
   Store's own activation path and injects the mod.
5. Load a world and move your head. `MinecraftHeadTracking.log`, written beside
   the launcher, records the result:

   ```
   [session] players=1 pvp=on remote=no
   Head tracking active.
   ```

**If the launcher cannot start or find your game**, it takes flags rather than a
game path:

```powershell
MinecraftHeadTrackingLauncher.exe --preview                 # launch Minecraft Preview instead of the release build
MinecraftHeadTrackingLauncher.exe --attach                  # inject into a copy of Minecraft that is already running
MinecraftHeadTrackingLauncher.exe --dll "D:\mods\MinecraftHeadTracking.dll"
MinecraftHeadTrackingLauncher.exe --wait 120                # allow longer for a slow first start
```

There is no game-path override or `MINECRAFT_PATH` environment variable, and
there is nothing to point one at: Bedrock is a packaged Store app, so it is
launched by package identity rather than by executable path.

### Manual Installation

There is no loader to bootstrap and no installer script to run, so placing the
files by hand is all there is to it. Put `MinecraftHeadTracking.dll` and
`MinecraftHeadTrackingLauncher.exe` in the same writable folder, because the
launcher injects the DLL it finds beside itself, and start the game with the
launcher instead of the usual Minecraft shortcut.

Nothing is ever copied into the game folder. Bedrock installs under
`C:\Program Files\WindowsApps`, which Windows signature-checks and does not
allow files to be added to, so the mod lives entirely in the folder you chose.
`CameraUnlock.ini` and `MinecraftHeadTracking.log` are written there on the
first run. For the same reason there is no separate Nexus ZIP: with nothing
to extract into the game directory, it would be identical to the release ZIP.

## Setting Up OpenTrack

In OpenTrack, set **Output** to `UDP over network`, open its options and set the
address to `127.0.0.1` and the port to `4242`. Pick your tracker under **Input**,
then press **Start**. Leave OpenTrack running while you play.

### VR Headset Setup

A headset makes the most accurate tracker. Connect it over Air Link or Virtual
Desktop, start SteamVR, then choose `SteamVR` as the OpenTrack input. The
headset streams its own rotation and position, so no camera calibration is
needed. Keep the headset on your head, not on the desk.

### Webcam Setup

Choose `neuralnet tracker` as the OpenTrack input, pick your webcam in its
options and set the field of view to match the camera. Face a light source, not
a window behind you. Sit in your normal playing position and press **Start**. Centre the view with
OpenTrack's Center bind whenever it drifts; the mod applies the pose the tracker
sends and keeps no centre of its own.

### Phone App Setup

A phone app such as Headcam sends OpenTrack packets itself. Point it at
`127.0.0.1:4242` (or your PC's LAN address, port 4242) and skip OpenTrack
entirely, which is the better option when the app already does its own
smoothing. Relay through OpenTrack instead if you want its curve mapping: set
the app to send to a different port, and OpenTrack's input to `UDP over network`
on that port with its output on 4242.

## Controls

Each action has two keys by default, so use whichever your keyboard has. The
nav-cluster keys are quicker; the chords work on keyboards without a nav cluster.
Both are ordinary entries in the action's key list in `CameraUnlock.ini`
(`ToggleKey`, `CycleTrackingModeKey`, `YawModeKey`), so either can be changed or
removed there.

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

Cycling the tracking mode steps through: normal head-tracked gameplay,
rotation only, position only, then back to normal.

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` as soon as you
change them, and come back at the next start. Toggling tracking with `End` lasts
for the current session only: whether tracking is on at the next start is
`EnableOnStartup`.

The yaw mode switches between world-locked yaw, the default, which turns your
head about the world's up axis so up stays constant (point the mouse at your
feet and turning your head still pans across the floor), and camera-local yaw,
which turns about the camera's own up axis and so leans and rolls the view at
steep mouse angles. Some people prefer camera-local for flying and falling.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini`. The file sits beside MinecraftHeadTracking.dll, in Lopari's mod_home folder or wherever a manual install was extracted, never in the game folder. It creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `MinecraftHeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `MinecraftHeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `MinecraftHeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `MinecraftHeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `MinecraftHeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `MinecraftHeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Minecraft: Bedrock Edition head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Discovery]
; Developer tool. true: instead of head tracking, drive the camera through one axis at a
; time and name each phase in MinecraftHeadTracking.log, to measure the axis mapping of a
; new Minecraft build. It obeys the same PvP rules as head tracking. Needs you in a world.
RunDiscovery=false
; How long each discovery run lasts, in seconds. A value below 1 runs for 1, and one above
; 3600 for 3600.
DurationSeconds=40
```
<!-- /cameraunlock:config -->

The file is read at startup, so restart the game after editing it.

Minecraft for Windows runs as a packaged Store app, so this mod never creates
`Defaults.ini`. `MinecraftHeadTracking.log` says whether it found one.

The sensitivity and inversion settings are gone: `YawSensitivity`,
`PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and
`InvertRoll` in `[Tracking]`, and `SensitivityX`, `SensitivityY`, `SensitivityZ`,
`InvertX`, `InvertY` and `InvertZ` in `[Position]`. Set these in your tracker
instead. The pitch and roll inversion earlier versions shipped as their defaults
is part of the mod's own axis conversion now, so the view moves as it did with
those settings at their defaults.

## Troubleshooting

**Mod not loading.** No `MinecraftHeadTracking.log` appears next to the
launcher, or it stops after the first few lines.

- Run the launcher, not the Minecraft shortcut. Started any other way, the game
  runs completely vanilla.
- Minecraft has to be the Microsoft Store build of Minecraft for
  Windows. Java Edition is a different game and is not supported.
- After a Minecraft update the log says the running build is not recognised.
  The mod keeps working: it finds the camera in the running game rather than
  from a table of addresses, so a patch that moves or reshapes the camera is
  absorbed at load time. If it cannot find the camera it stays dormant on
  purpose, rather than hooking stale addresses and crashing the game; check the
  Releases page then. Older Minecraft builds keep working with the newest mod
  release.

**No tracking response.** The game runs fine but the view does not follow your
head.

- Check the log for `Head tracking active.` If it says the UDP port could not be
  bound, something else already has port 4242, usually another head tracking mod
  in a game you left running, or OpenTrack configured to receive rather than
  send. Close it and tracking comes up on its own within about half a second, no
  restart needed.
- Check the `[session]` line. The mod disables itself when the `pvp` game rule
  is on and another player is in the session, and treats host, guest, Realm and
  dedicated server alike. This is not configurable.
- Confirm OpenTrack is started and its output is `UDP over network` to
  `127.0.0.1:4242`, matching `UdpPort` in `CameraUnlock.ini`.
- Press `End` (`Ctrl+Shift+Y`) in case tracking was toggled off.
- If the view sits off centre, centre it in the tracker app (OpenTrack's Center
  bind, the CENTER button in Headcam).

**Jittery or unstable tracking.** The view shakes or twitches while your head is
still.

- Raise the one that applies to your setup toward 0.3 in `[Smoothing]` in
  `CameraUnlock.ini` and restart the game, and leave the other alone:
  `RemoteSmoothing` for a phone or a second PC, `LocalSmoothing` for a tracker
  running on this PC. Wireless and
  webcam trackers need more than a headset does. The log line beginning
  `Tracker source is` says which of the two is in effect.
- Improve the lighting for a webcam tracker, or move a phone tracker to a stable
  mount instead of holding it.
- On a phone app, leave smoothing to the app and keep `RemoteSmoothing` low,
  rather than smoothing twice.

**Wrong rotation axis.** Nodding rolls the view, or an axis moves the wrong way.

- The mod has no inversion settings: invert the axis in your tracker, and
  report it on Discord so the mod's own axis conversion can be checked.
- If yaw feels wrong only when looking steeply up or down, toggle yaw mode with
  `Page Down` (`Ctrl+Shift+H`). World-locked is horizon-stable; camera-local
  follows the camera's up axis. The log records which one is live.

**Tracking stays on in menus.** The view still follows your head in the pause
menu or the inventory.

- Menus are detected from the OS cursor, which Bedrock only shows on mouse and
  keyboard. On a controller the game draws its own cursor and leaves the OS
  pointer hidden, so the mod cannot tell a menu from gameplay. Press `End` (or
  `Ctrl+Shift+Y`) to toggle tracking off while you are in a menu.

## Updating

Download the new release and extract it over the folder you installed to, then
run the launcher again. The release ZIP carries no settings file, so
`CameraUnlock.ini` is kept as it is. Updating from a version that kept its
settings in `MinecraftHeadTracking.ini` imports that file once, as
[Configuration](#configuration) describes.

## Uninstalling

Delete `MinecraftHeadTracking.dll` and `MinecraftHeadTrackingLauncher.exe` from
the folder you extracted. That is the whole uninstall. `CameraUnlock.ini`, a
`MinecraftHeadTracking.ini` from an earlier version and the log stay where they
are, so a reinstall into the same folder keeps your settings. Nothing was written
to the game's install directory and there is no mod loader to remove, so there is
no `uninstall.cmd`. Minecraft itself is untouched. If you installed from source
instead, `pixi run uninstall` removes the deployed DLL and launcher the same way.

## Building from Source

Requires Visual Studio 2022 or newer with the C++ workload, and
[pixi](https://pixi.sh).

```powershell
git clone --recursive https://github.com/itsloopyo/minecraft-bedrock-edition-headtracking.git
cd minecraft-bedrock-edition-headtracking
pixi run build
pixi run install     # deploys the DLL and launcher to %LOCALAPPDATA%\CameraUnlock\MinecraftHeadTracking
pixi run test        # builds and runs the shared library's test suite; binds loopback ports
```

Run the launcher from the deployment folder and check
`MinecraftHeadTracking.log` beside it.

When a Minecraft update lands, both the camera's addresses and its struct
layout are recovered from the running game at load time, so the mod runs on an
unrecognised build without waiting for a release. A build profile records that
the build was also checked by hand, which is what the PvP gate's offsets come
from; until one is added the gate uses the previous build's offsets, refuses to
allow tracking unless every read still checks out, and leaves its in-game
notice unsent. `pixi run check-fingerprint` (with the game running) prints the
new build's PE fingerprint, and the mod's own log prints a paste-ready profile
stub.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and
  new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and
  launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head
  tracker

## License

MIT License, copyright itsloopyo / CameraUnlock - see [LICENSE](LICENSE) for the
full text, which also ships in the release ZIP. Third-party components linked
into the mod are listed with their own licenses in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Mojang Studios and Microsoft for Minecraft
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol
- [MinHook](https://github.com/TsudaKageyu/minhook) for function hooking
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) for the
  shared tracking pipeline

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Mojang Studios or
Microsoft. Use at your own risk. It only changes what is drawn on your screen,
and the rotation the server sees is the one your mouse sets, exactly as in an
unmodded game. A wider view is still a real change to how the game plays, so use
your judgement on servers with rules about client modifications.
