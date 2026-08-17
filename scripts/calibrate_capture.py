"""Screenshot each calibration phase, so each axis can be identified visually.

The probe holds one axis at a large constant offset for a few seconds at a
time and names the phase in the log. This watches the log and grabs a frame
in the middle of each phase, which is when the view is settled.

Capture is passive: it reads the screen and never sends input, so the world is
untouched while it runs.

Usage:
    python scripts/calibrate_capture.py [cycles]
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import game_harness as gh  # noqa: E402
import game_state  # noqa: E402

OUT = Path(__file__).parent.parent / "build" / "calib"
TEMP = OUT / "_state.png"


def current_phase():
    try:
        text = gh.LOG_PATH.read_text(errors="replace")
    except OSError:
        return None
    matches = gh.PHASE_LINE.findall(text)
    return matches[-1].strip() if matches else None


def main():
    cycles = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    OUT.mkdir(parents=True, exist_ok=True)

    found = gh.find_window()
    if not found:
        raise SystemExit("game window not found")
    gh.focus(found[0])
    time.sleep(1.5)

    # Resume if the game paused itself while the foreground was elsewhere. The
    # state is read first because Escape sent during gameplay would open the
    # pause menu rather than close it. Everything from here on happens in this
    # one process, so focus is taken once and never disturbed again.
    for attempt in range(4):
        gh.screenshot(str(TEMP))
        if game_state.in_gameplay(TEMP):
            break
        print(f"paused, resuming (attempt {attempt + 1})", flush=True)
        gh.key("ESCAPE")
        time.sleep(1.5)
    else:
        raise SystemExit("could not reach gameplay; is a world loaded?")

    seen = 0
    last = None
    captured = []
    deadline = time.time() + cycles * 12 * 3 + 30

    while time.time() < deadline and seen < cycles * 12:
        phase = current_phase()
        if phase and phase != last:
            last = phase
            seen += 1
            # Mid-phase: the write is applied every frame, but this avoids the
            # boundary where a capture could straddle two phases.
            time.sleep(1.4)
            name = phase.replace(" ", "_").replace("+", "p").replace(".", "")
            path = OUT / f"{seen:02d}-{name}.png"
            gh.screenshot(str(path))
            captured.append((phase, path.name))
            print(f"{phase:<12} -> {path.name}", flush=True)
        time.sleep(0.15)

    print(f"\n{len(captured)} frames in {OUT}")


if __name__ == "__main__":
    main()
