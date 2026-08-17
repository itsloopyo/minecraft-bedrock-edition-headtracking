"""Get Minecraft from cold start to standing in a world, unattended.

The camera hooks only run during gameplay, so every camera experiment needs a
world loaded. Doing that by hand each time is what made this mod's discovery
loop so slow, and this removes the human from it.

SAFETY RAILS - these are requirements, not preferences:

  * Never touches Realms or Servers. This drives single-player worlds only.
  * Never clicks a world's edit pencil, so it can never reach a delete.
  * Never clicks once gameplay starts. In-world a left click breaks a block
    and a right click places one, so after the world loads this sends keyboard
    input only and the world is left exactly as it was found.
  * Picks a world by name from a screenshot rather than by fixed coordinates,
    because the list is ordered most-recently-played and a blind click would
    land on a different world the second time it ran.

Usage:
    python scripts/bootstrap_world.py to-list        # cold start -> world list
    python scripts/bootstrap_world.py enter X Y      # click a verified tile
    python scripts/bootstrap_world.py state          # where are we now
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import game_harness as gh  # noqa: E402

SHOTS = Path(__file__).parent.parent / "build" / "shots"

# Client-relative fractions, so a resized window still works.
DISMISS_POPUP = (0.396, 0.770)
PLAY_BUTTON = (0.499, 0.485)

# The world list draws these two tabs, and this script must never click them.
FORBIDDEN_BANDS = (
    ("Realms tab", 0.34, 0.66, 0.08, 0.16),
    ("Servers tab", 0.66, 0.99, 0.08, 0.16),
)


def shot(name):
    SHOTS.mkdir(parents=True, exist_ok=True)
    path = SHOTS / name
    gh.screenshot(str(path))
    return path


def click_fraction(fx, fy):
    found = gh.find_window()
    if not found:
        raise SystemExit("game window not found")
    _, _, w, h = gh.window_rect(found[0])
    for label, x0, x1, y0, y1 in FORBIDDEN_BANDS:
        if x0 <= fx <= x1 and y0 <= fy <= y1:
            raise SystemExit(f"refusing to click the {label}: multiplayer is off limits")
    gh.click(fx * w, fy * h)


def to_list():
    """Cold start through to the single-player world list."""
    if not gh.launch():
        raise SystemExit("game window never appeared")
    time.sleep(10)

    found = gh.find_window()
    gh.focus(found[0])
    time.sleep(1)

    # The marketplace promo is not always present, so this is best-effort: the
    # click lands on empty panorama when it is absent, which does nothing.
    shot("10-launched.png")
    click_fraction(*DISMISS_POPUP)
    time.sleep(1.5)

    shot("11-menu.png")
    click_fraction(*PLAY_BUTTON)
    time.sleep(3)

    path = shot("12-worldlist.png")
    print(f"world list at {path} - identify the Creative world, then: enter X Y")


def enter(x, y):
    """Click a world tile whose identity the caller has already verified from
    the screenshot, then wait for gameplay."""
    found = gh.find_window()
    gh.focus(found[0])
    gh.click(float(x), float(y))

    for _ in range(60):
        time.sleep(1)
        if "camera setup ran" in _log_text():
            break
    time.sleep(5)
    path = shot("13-ingame.png")
    print(f"in game at {path}")


def _log_text():
    try:
        return gh.LOG_PATH.read_text(errors="replace")
    except OSError:
        return ""


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    command = sys.argv[1]
    if command == "to-list":
        to_list()
    elif command == "enter":
        enter(sys.argv[2], sys.argv[3])
    elif command == "state":
        print(shot("state.png"))
    else:
        raise SystemExit(f"unknown command {command}")


if __name__ == "__main__":
    main()
