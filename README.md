# Minecraft: Bedrock Edition Head Tracking

Head tracking for Minecraft for Windows (Bedrock Edition): your head moves the
view while the mouse still controls where you aim, mine and build, with no VR
headset needed.

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/minecraft-bedrock-edition-headtracking/main/assets/readme-clip.gif)

## Features

- **Decoupled look and aim** - your head moves the camera, the mouse still
  targets the block you mine and build on
- **6DOF positional tracking** - lean and peek to shift the viewpoint, on top of
  yaw, pitch and roll
- **Off in PvP** - disables itself when the `pvp` game rule is on and another
  player is in the session

## Requirements

- [Minecraft for Windows (Bedrock Edition)](https://apps.microsoft.com/detail/9nblggh2jhxj)
  from the Microsoft Store or the Xbox app
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a
  webcam or VR headset, or a phone app such as [Headcam](https://headcam.app)
- Windows 10 version 1903 or newer, x64

## Installation

**Pre-release.** There is no published release yet. The steps below describe the
release ZIP as it will ship; until then, [build from
source](#building-from-source).

The easiest route is [Lopari](https://lopari.app), which installs and starts
the mod for you: it finds your Store copy of Minecraft by package identity,
keeps the mod in its own folder under `%APPDATA%\Lopari\mods\minecraft`, and
launches the game through it. Nothing goes into the game folder either way.

To install by hand instead:

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
`MinecraftHeadTracking.ini` and `MinecraftHeadTracking.log` are written there on
the first run. For the same reason there is no separate Nexus ZIP: with nothing
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
a window behind you. Sit in your normal playing position and press **Start**,
then use `Home` in game to recenter.

### Phone App Setup

A phone app such as Headcam sends OpenTrack packets itself. Point it at
`127.0.0.1:4242` (or your PC's LAN address, port 4242) and skip OpenTrack
entirely, which is the better option when the app already does its own
smoothing. Relay through OpenTrack instead if you want its curve mapping: set
the app to send to a different port, and OpenTrack's input to `UDP over network`
on that port with its output on 4242.

## Controls

Two equivalent binding sets, so use whichever your keyboard has. The nav-cluster
keys are quicker; the chords work on keyboards without a nav cluster.

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T`  |
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

Cycling the tracking mode steps through: normal head-tracked gameplay,
rotation only, position only, then back to normal.

The yaw mode switches between world-locked yaw, the default, which turns your
head about the world's up axis so up stays constant (point the mouse at your
feet and turning your head still pans across the floor), and camera-local yaw,
which turns about the camera's own up axis and so leans and rolls the view at
steep mouse angles. Some people prefer camera-local for flying and falling.

## Configuration

`MinecraftHeadTracking.ini` sits beside the mod DLL, in the folder you extracted,
and is written with every setting at its default the first time the mod runs. So
is `MinecraftHeadTracking.log`.

```ini
; MinecraftHeadTracking

[Tracking]
; UDP port the tracker sends OpenTrack packets to.
Port=4242
EnableOnStartup=true
; Smoothing is picked per connection from the packet source address and
; covers rotation and position alike. 0.0 is none, 1.0 is heavy.
; LocalSmoothing applies when the tracker runs on this machine
; (loopback); RemoteSmoothing when it is a device on the network, where
; the link's jitter is worth filtering.
LocalSmoothing=0.0
RemoteSmoothing=0.15
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
; Pitch and roll are inverted by default: Bedrock's post-view transform
; runs them opposite to the OpenTrack convention, so leaving these off
; makes leaning and nodding go the wrong way.
InvertYaw=false
InvertPitch=true
InvertRoll=true
; true keeps yaw horizon-locked: turning your head yaws about the world's
; up axis whatever the mouse has the camera pointed at. false yaws about
; the camera's own up axis instead, which leans and rolls the view when
; you are looking at the floor or the sky.
WorldSpaceYaw=true

[Hotkeys]
; Toggles the two yaw modes in game. 0x22 is Page Down; Ctrl+Shift+H does
; the same thing and is not configurable.
YawModeKey=0x22

[Position]
; 6DOF: leaning and moving your head shifts the viewpoint.
Enabled=true
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Flip an axis if your tracker's convention disagrees with the defaults.
InvertX=false
InvertY=false
InvertZ=false

[Discovery]
; Developer tool. Drives the camera through one axis at a time and names
; each phase in the log, which is how the axis mapping is measured for a
; new Minecraft build. Head tracking input is ignored while it runs, and
; it obeys the same PvP rules as head tracking. Needs you in a world.
Enabled=false
DurationSeconds=40
```

The file is read at startup, so restart the game after editing it. `Page Down`
switches yaw mode straight away but does not write the choice back, so the mod
comes up in whatever the file says.

Any setting left out of the file takes its default, so a file written by an older
version keeps working. The travel limits on head position are read from the
`[Position]` section too, and can be added by hand: `LimitX=0.30`, `LimitY=0.20`,
`LimitZ=0.40` forward, `LimitZBack=0.10` back, all in meters. There is no
separate position smoothing key: position uses the same `LocalSmoothing` and
`RemoteSmoothing` as rotation.

Save the file as plain ANSI or UTF-8 without a byte order mark. Notepad adds a
BOM by default, and Windows then cannot read a single setting in the file. The
log says so when it happens.

## Troubleshooting

**Mod not loading.** No `MinecraftHeadTracking.log` appears next to the
launcher, or it stops after the first few lines.

- Run the launcher, not the Minecraft shortcut. Started any other way, the game
  runs completely vanilla.
- Minecraft has to be the Microsoft Store or Xbox app build of Minecraft for
  Windows. Java Edition is a different game and is not supported.
- After a Minecraft update the log says the running build is not recognised and
  the mod stays dormant on purpose, rather than hooking stale addresses and
  crashing the game. Check the Releases page for a build that knows about it.
  Older Minecraft builds keep working with the newest mod release.

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
  `127.0.0.1:4242`, matching `Port` in the ini.
- Press `Home` (or `Ctrl+Shift+T`) to recenter, and `End` (`Ctrl+Shift+Y`) in
  case tracking was toggled off.

**Jittery or unstable tracking.** The view shakes or twitches while your head is
still.

- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (tracker on
  the network) in `[Tracking]` toward 0.3 and restart the game. Wireless and
  webcam trackers need more than a headset does.
- Improve the lighting for a webcam tracker, or move a phone tracker to a stable
  mount instead of holding it.
- On a phone app, leave smoothing to the app and keep the mod's `RemoteSmoothing`
  low, rather than smoothing twice.

**Wrong rotation axis.** Nodding rolls the view, or an axis moves the wrong way.

- Flip `InvertYaw`, `InvertPitch` or `InvertRoll` in `[Tracking]`, one at a time,
  and `InvertX`, `InvertY`, `InvertZ` in `[Position]` for the lean axes.
  `InvertPitch` and `InvertRoll` are on by default and are correct for OpenTrack.
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
run the launcher again. Your config is preserved: `MinecraftHeadTracking.ini` is
only ever written when it is missing.

## Uninstalling

Delete the folder you extracted, along with the `.ini` and `.log` beside the
binaries. That is the whole uninstall. Nothing was written to the game's install
directory and there is no mod loader to remove, so there is no `uninstall.cmd`
and no `/force` flag to undo more than that. Minecraft itself is untouched. If
you installed from source instead, `pixi run uninstall` removes the deployed
files.

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

When a Minecraft update lands, `pixi run check-fingerprint` (with the game
running) prints the new build's PE fingerprint as a paste-ready build-profile
stub. Until that profile's addresses are filled in the mod stays dormant and
the game runs vanilla.

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
