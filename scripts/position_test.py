"""Measure how each tracker position axis moves the camera.

Streams synthetic OpenTrack packets from a background thread while capturing a
frame per axis, so the mapping from tracker axes to view axes is measured
rather than assumed. Sends position only; rotation stays at zero so each frame
isolates one translation.

No input is sent to the game beyond what is needed to resume it if it paused,
so the world is untouched.

Usage:
    python scripts/position_test.py [centimetres]
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import game_harness as gh  # noqa: E402
import game_state  # noqa: E402
import send_opentrack  # noqa: E402

OUT = Path(__file__).parent.parent / "build" / "position"
ADDRESS = (send_opentrack.TEST_HOST,
           int(sys.argv[2]) if len(sys.argv) > 2 else send_opentrack.TEST_PORT)


def main():
    amount = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    OUT.mkdir(parents=True, exist_ok=True)

    window = gh.find_window()
    if not window:
        raise SystemExit("game window not found")
    gh.focus(window[0])
    time.sleep(1.0)

    probe = OUT / "_state.png"

    def ensure_gameplay():
        return game_state.ensure_gameplay(gh, probe)

    if not ensure_gameplay():
        raise SystemExit("could not reach gameplay")

    stream = send_opentrack.PoseStream(ADDRESS)
    stream.start()

    # Centred first, so the auto-recentre lands at the origin.
    stream.set()
    time.sleep(4.0)

    for name, args in [("centre", (0, 0, 0)),
                       (f"X+{amount:g}", (amount, 0, 0)),
                       ("centre2", (0, 0, 0)),
                       (f"Y+{amount:g}", (0, amount, 0)),
                       ("centre3", (0, 0, 0)),
                       (f"Z+{amount:g}", (0, 0, amount))]:
        stream.set(*args)
        time.sleep(2.2)
        if not ensure_gameplay():
            print(f"{name}: lost gameplay", flush=True)
            continue
        path = OUT / f"{name}.png"
        gh.screenshot(str(path))
        print(f"{name:10} -> {path.name}", flush=True)

    stream.set()
    time.sleep(1.0)
    stream.stop()
    print(f"\nframes in {OUT}")


if __name__ == "__main__":
    main()
