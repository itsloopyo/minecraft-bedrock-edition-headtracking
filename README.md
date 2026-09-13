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
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android
- **Off in PvP** - disables itself when the `pvp` game rule is on and another
  player is in the session

## Requirements

- [Minecraft for Windows (Bedrock Edition)](https://apps.microsoft.com/detail/9nblggh2jhxj)
  from the Microsoft Store or the Xbox app
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
`MinecraftHeadTracking.ini` and `MinecraftHeadTracking.log` are written there on
the first run. For the same reason there is no separate Nexus ZIP: with nothing
to extract into the game directory, it would be identical to the release ZIP.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets, so use whichever your keyboard has. The nav-cluster
keys are quicker; the chords work on keyboards without a nav cluster.

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
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
; Smoothing, 0.0 (none) to 1.0 (heavy). Which of the two applies is
; decided per connection from where the packets come from, so both
; can be set and left alone. Nothing is applied on top of these: 0.0
; means none. Both cover head rotation and head position alike.
; LocalSmoothing: the tracker runs on this PC (loopback). Already
; steady, so smoothing here only costs latency.
LocalSmoothing=0.0
; RemoteSmoothing: the tracker is a phone or another PC on the
; network. Covers the jitter the network adds.
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
`LimitZ=0.40` forward, `LimitZBack=0.10` back, all in meters.

`[Position]` has no smoothing key of its own: `LocalSmoothing` and
`RemoteSmoothing` in `[Tracking]` cover head rotation and head position
together. The single `Smoothing` key that used to sit in both sections is
retired and ignored, and is not migrated into the new keys, because it carried a
hidden 0.15 floor and its number no longer means what it did. The log says so
once if your file still has it.

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
  `127.0.0.1:4242`, matching `Port` in the ini.
- Press `End` (`Ctrl+Shift+Y`) in case tracking was toggled off.
- If the view sits off centre, centre it in the tracker app (OpenTrack's Center
  bind, the CENTER button in Headcam).

**Jittery or unstable tracking.** The view shakes or twitches while your head is
still.

- Raise the one that applies to your setup toward 0.3 and restart the game, and
  leave the other alone: `RemoteSmoothing` in `[Tracking]` for a phone or a
  second PC, `LocalSmoothing` for a tracker running on this PC. Wireless and
  webcam trackers need more than a headset does. The log line beginning
  `Tracker source is` says which of the two is in effect.
- Improve the lighting for a webcam tracker, or move a phone tracker to a stable
  mount instead of holding it.
- On a phone app, leave smoothing to the app and keep `RemoteSmoothing` low,
  rather than smoothing twice.

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

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

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
