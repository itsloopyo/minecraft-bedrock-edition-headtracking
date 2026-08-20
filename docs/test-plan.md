# Test plan

What to check before this mod is considered playable, and what each check is
actually for. Ordered so that a failure early on explains failures later.

Most of it can be driven without a human: `scripts/bootstrap_world.py` takes
the game from cold start into a world, and `scripts/send_opentrack.py` supplies
tracker data. What genuinely needs eyes is marked **manual**.

## Before you start

Run the automated checks on a **separate UDP port**. An idle OpenTrack streams
zero poses at about 400Hz, which drowns a 60Hz synthetic stream roughly 7 to 1
and makes tracking look broken when it is not.

```
# in MinecraftHeadTracking.ini
[Tracking]
Port=4243
```

Set it back to 4242 afterwards, or a real tracker will not be heard.

## 1. Loader and build profile

| Check | How | Pass |
|---|---|---|
| The mod loads | Launch, read `MinecraftHeadTracking.log` | `Activated build profile store-win64-...` |
| Hooks install | Same log | `Camera hook installed at ...` and `Crosshair follows the aim point.` |
| Unknown build stays dormant | Edit the profile fingerprint to a wrong value, rebuild, launch | `Dormant. No hooks installed.` and the game plays exactly vanilla |

The dormancy check matters more than it looks. The failure mode it prevents is
head tracking silently active with a fairness gate that cannot read anything.

## 2. The fairness gate

| Check | How | Pass |
|---|---|---|
| Solo world allows tracking | Load a single-player world | `[session] players=1 pvp=on remote=no` then `Head tracking active.` |
| It is not gated on the rule alone | Same log line | `pvp=on` in a solo world is expected and must NOT disable tracking |
| Turning on takes several polls | Watch the timestamps | About a second between the `[session]` line and `Head tracking active.` |
| **manual** Another player disables it | Have someone join a world with PvP on | `players=2`, tracking stops, and stops within a fifth of a second |
| **manual** Mid-session toggle | In a solo world with a second player present, `/gamerule pvp false` then `true` | Tracking follows the rule without a relog |
| Leaving a world resets it | Quit to title, join a different world | The confirmation delay applies again, not just on the first world |

The last row is the one worth being fussy about. Gate state that survives a
world change is what removes the delay protecting the join window, where the
roster is briefly self-only because `PlayerListPacket` has not arrived.

## 3. Axes and signs

```
python scripts/bootstrap_world.py to-list
python scripts/bootstrap_world.py enter <x> <y>     # verify the world by name first
python scripts/position_test.py 15 4243
```

| Check | Pass |
|---|---|
| X | head right moves the scene left |
| Y | head up moves the scene down |
| Z | leaning forward magnifies the scene |
| Return to centre | every centre frame matches the reference to a residual near 0 |

That last row is the real assertion: it proves the post-view transform is
restored exactly and nothing accumulates over a session.

For rotation, `Discovery/Enabled=true` cycles one axis at a time and names each
phase in the log; `scripts/calibrate_capture.py` screenshots each phase. Yaw
should be a clean pan, pitch a clean tilt, roll a clean horizon rotation, with
no cross-contamination.

## 4. Combined poses (manual, and the one most likely to be wrong)

Single-axis tests pass under a reversed rotation order. Only combined poses
diverge, so this is the check that actually exercises the composition.

**Stand facing open water or a flat skyline.** Then:

1. Glance diagonally: yaw and pitch together, about 20 degrees of each.
   The horizon must stay level. A tilt of roughly 7 degrees at 20/20, tipping
   the opposite way on the opposite diagonal, is the signature of the
   composition being built in the wrong order.
2. Tilt your head while looking down. The view must not swing sideways.

`scripts/combined_pose_test.py` automates this, but only where a genuine
horizon is in view. It measures its own reference first and refuses to report
if that reference is not level, because broken terrain reads anywhere from -21
to +1 degrees and would give confident nonsense.

## 5. Aim decoupling

| Check | How | Pass |
|---|---|---|
| Aim ignores the head | Point at a block so its outline shows, then move your head | The **same block** stays outlined |
| The reticle follows the aim point | Same | The crosshair sits on the outlined block, off screen centre |
| Nothing leaks to the server | **manual**, multiplayer | Other players see your head pointing where your mouse points |

## 6. Reticle litmus tests (manual)

From the catalogue doctrine. All four must pass:

1. Pure roll, no pitch: the crosshair stays at screen centre.
2. Pure pitch, no roll: the crosshair moves vertically only.
3. Pitch and roll together: no horizontal wander as roll changes.
4. Lean sideways with position tracking on: the crosshair stays on the aimed
   block. This is the one that regressed before, because the projection used
   the rotation only and dropped the 6DOF translation.

## 7. Tracking loss and controls

| Check | How | Pass |
|---|---|---|
| Loss holds, never snaps | Stop the tracker mid-pose | The view **freezes** where it was. A snap to centre is a failure |
| Resume blends | Start it again | Smooth return, no jump |
| Centre follows the tracker | Look away, then centre in the tracker app | The view returns to neutral immediately. The mod keeps no centre of its own, so nothing lags behind the tracker |
| Toggle | End | Tracking stops and the view eases back to neutral with no residual |
| Position toggle | PageUp | No jump when toggling |
| Chords | Ctrl+Shift+Y / G | Same two actions |

## 8. Stability

| Check | Pass |
|---|---|
| Long session | 10 minutes in game, log shows zero faults |
| Level transitions | Cross dimensions, reload; tracking survives, gate re-evaluates |
| Pause menu | **known gap**: tracking currently stays live in menus and inventory |
| Alt-tab | Bedrock pauses on focus loss; no crash, no runaway camera on return |
| Vanilla parity | With tracking disabled, gameplay is identical to unmodded |

## Known gaps

- No pause or menu detection, so tracking stays live in the pause menu and
  inventory.
- The reticle projects at a fixed block-reach distance rather than the true
  distance to the target, because the mod has no raycast. The error goes to
  zero as the target gets further away and as the head returns to centre.
- The in-game notice explaining why tracking is off reaches the chat history
  but does not appear on the HUD, because it is posted during world load
  before the chat panel mounts. The log line is accurate meanwhile.
- No release packaging. Note that this package cannot use manifest-mode
  deployment because it installs outside the game folder.
