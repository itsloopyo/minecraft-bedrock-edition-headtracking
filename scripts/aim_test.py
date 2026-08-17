"""Test that head movement does not move the player's aim.

The promise of the mod is that the head moves the view while the mouse still
controls where the player aims. Minecraft draws an outline around the block it
believes you are targeting, so that outline is a direct readout of the game's
aim and needs no world interaction to observe.

Decoupled looks like this: with the camera rotated, the SAME block stays
outlined, and the outline sits off-centre from the crosshair. Coupled looks
like the outline staying glued to the centre of the screen.

Everything happens in one process so the game keeps focus and never pauses.
The only input sent is relative mouse motion, which aims the player exactly as
a mouse would; no clicks, so no block is ever placed or broken.

Usage:
    python scripts/aim_test.py [pitch_down]
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import game_harness as gh  # noqa: E402
import game_state  # noqa: E402

OUT = Path(__file__).parent.parent / "build" / "aim"
WANTED = ("neutral", "YAW +25", "PITCH +25")


def current_phase():
    try:
        return (gh.PHASE_LINE.findall(gh.LOG_PATH.read_text(errors="replace")) or [None])[-1]
    except OSError:
        return None


def main():
    pitch_down = float(sys.argv[1]) if len(sys.argv) > 1 else 260.0
    OUT.mkdir(parents=True, exist_ok=True)

    found = gh.find_window()
    if not found:
        raise SystemExit("game window not found")
    gh.focus(found[0])
    time.sleep(1.5)

    probe = OUT / "_state.png"

    def ensure_gameplay():
        """Focus can be lost at any point and Bedrock pauses the moment it is,
        so this is checked before every capture rather than once up front."""
        for _ in range(4):
            gh.screenshot(str(probe))
            if game_state.in_gameplay(probe):
                return True
            window = gh.find_window()
            if window:
                gh.focus(window[0])
                time.sleep(0.8)
            gh.screenshot(str(probe))
            if game_state.in_gameplay(probe):
                return True
            gh.key("ESCAPE")
            time.sleep(1.5)
        return False

    if not ensure_gameplay():
        raise SystemExit("could not reach gameplay")

    # Aim down so a nearby block is targeted and its outline is drawn.
    gh.mouse_move(0, pitch_down)
    time.sleep(1.0)
    gh.screenshot(str(OUT / "00-aimed.png"))
    print("aimed down; captured 00-aimed.png", flush=True)

    # One frame per phase of interest, taken mid-phase.
    captured = {}
    last = None
    deadline = time.time() + 80
    while time.time() < deadline and len(captured) < len(WANTED):
        phase = current_phase()
        if phase and phase != last:
            last = phase
            if phase in WANTED and phase not in captured:
                time.sleep(1.2)
                if not ensure_gameplay():
                    print("lost gameplay; skipping", flush=True)
                    continue
                path = OUT / f"{phase.replace(' ', '_').replace('+', 'p')}.png"
                gh.screenshot(str(path))
                captured[phase] = path.name
                print(f"{phase:<10} -> {path.name}", flush=True)
        time.sleep(0.15)

    print(f"\n{len(captured)} frames in {OUT}")


if __name__ == "__main__":
    main()
