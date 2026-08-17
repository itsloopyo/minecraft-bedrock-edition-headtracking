"""Check combined head poses, where a wrong rotation order actually shows.

Single-axis tests pass under a reversed composition; only combined poses
diverge. A yaw plus a pitch composed in the wrong order picks up a parasitic
roll of atan2(sin p * sin y, cos p), which tips the horizon on a diagonal
glance and tips it the other way on the opposite diagonal.

This aims the player at the horizon, holds each pose, and measures the horizon
angle off the rendered frame, so the answer is a number rather than an
impression.

Usage:
    python scripts/combined_pose_test.py [port]
"""

import math
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))

import game_harness as gh  # noqa: E402
import game_state  # noqa: E402
import send_opentrack  # noqa: E402

OUT = Path(__file__).parent.parent / "build" / "combined"
ADDRESS = (send_opentrack.TEST_HOST,
           int(sys.argv[1]) if len(sys.argv) > 1 else send_opentrack.TEST_PORT)


def horizon_angle(path):
    """Angle of the dominant near-horizontal edge, in degrees.

    The sky is bright and fairly uniform and the land below it is not, so the
    lowest bright-to-dark transition in each column tracks the skyline. Fitting
    a line through those points gives the horizon's tilt; a level horizon is 0.
    """
    image = Image.open(path).convert("L")
    pixels = np.asarray(image, dtype=np.float32)
    h, w = pixels.shape
    # Ignore the HUD band and the held item in the lower right.
    top, bottom = int(h * 0.05), int(h * 0.70)
    left, right = int(w * 0.10), int(w * 0.75)
    band = pixels[top:bottom, left:right]

    bright = band > (band.max() * 0.72)
    xs, ys = [], []
    for column in range(0, band.shape[1], 4):
        rows = np.flatnonzero(bright[:, column])
        if rows.size < 5:
            continue
        ys.append(float(rows[-1]))
        xs.append(float(column))
    if len(xs) < 20:
        return None
    slope = np.polyfit(np.array(xs), np.array(ys), 1)[0]
    return math.degrees(math.atan(slope))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    window = gh.find_window()
    if not window:
        raise SystemExit("game window not found")
    gh.focus(window[0])
    time.sleep(1.0)

    probe = OUT / "_state.png"
    if not game_state.ensure_gameplay(gh, probe):
        raise SystemExit("could not reach gameplay")

    stream = send_opentrack.PoseStream(ADDRESS)
    stream.start()
    stream.set()
    time.sleep(4.0)

    # Look at the horizon: a level skyline is what the measurement needs.
    gh.mouse_move(0, -220)
    time.sleep(1.5)

    cases = [("neutral", 0, 0), ("yaw20", 20, 0), ("pitch20", 0, 20),
             ("yaw20_pitch20", 20, 20), ("yaw-20_pitch20", -20, 20)]
    results = {}
    for name, yaw, pitch in cases:
        stream.set(yaw=yaw, pitch=pitch)
        time.sleep(2.0)
        if not game_state.ensure_gameplay(gh, probe):
            print(f"{name}: lost gameplay", flush=True)
            continue
        path = OUT / f"{name}.png"
        gh.screenshot(str(path))
        results[name] = horizon_angle(path)
        shown = "n/a" if results[name] is None else f"{results[name]:+.2f} deg"
        print(f"{name:16} horizon {shown}", flush=True)

    stream.set()
    time.sleep(1.0)
    stream.stop()

    base = results.get("neutral")
    if base is None:
        print("\nno horizon found in the reference frame; point at open sky and retry")
        return

    # The reference has to read level, or the edge being measured is terrain
    # rather than a horizon and every number below is meaningless. Broken
    # terrain reads anywhere from -21 to +1 degrees while the view barely
    # moves, which is confidently wrong rather than merely noisy.
    if abs(base) > 2.0:
        print(f"\nreference horizon reads {base:+.2f} deg, which is not level, so the edge being")
        print("measured is terrain and not a horizon. Stand facing open water or a flat")
        print("skyline and run this again; the numbers below would be meaningless.")
        return
    print("\nparasitic roll relative to neutral (should be near zero):")
    for name in ("yaw20", "pitch20", "yaw20_pitch20", "yaw-20_pitch20"):
        value = results.get(name)
        if value is not None:
            print(f"  {name:16} {value - base:+.2f} deg")


if __name__ == "__main__":
    main()
