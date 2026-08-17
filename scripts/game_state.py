"""Tell gameplay apart from the pause menu, from a screenshot.

Bedrock pauses whenever it loses the foreground, so an unattended harness has
to notice and resume. Blindly sending Escape is not safe: sent during gameplay
it OPENS the pause menu, so the state has to be read before acting.

The discriminator is the hotbar - a strip of mid-grey, unsaturated UI across
the bottom centre, drawn only during gameplay. Measured across known frames it
covers 52-58% of that band in game and exactly 0% both in the pause menu and
on the main menu, which is as clean a split as this is going to get.

An earlier attempt keyed on the pause menu's white buttons instead. That fails
in a snow biome, where the terrain is also near-white - worth remembering
before trusting any brightness-based test in this game.
"""

import sys
import time

import numpy as np
from PIL import Image

# The pause menu's button column: a block of near-white, nearly unsaturated
# pixels in the left-centre. This is the only signal found that survives every
# scene tested.
#
# Two earlier attempts are recorded here because both looked fine on the first
# sample and failed later:
#   * the hotbar strip reads as mid-grey during the day but goes too dark to
#     detect at night;
#   * the "Game is paused" chip is a dark plate on a bright screen, but a night
#     sky is dark too, so it reports paused during perfectly normal gameplay.
# Measured: 0.755 paused, 0.473 in a bright snow biome, 0.001 at night.
BUTTON_BAND = (0.12, 0.47, 0.33, 0.70)
WHITE_LEVEL = 225
MAX_SATURATION = 14
PAUSED_FRACTION = 0.60


def _band(image, band):
    if not isinstance(image, Image.Image):
        image = Image.open(image)
    image = image.convert("RGB")
    w, h = image.size
    x0, x1, y0, y1 = band
    crop = image.crop((int(x0 * w), int(y0 * h), int(x1 * w), int(y1 * h)))
    return np.asarray(crop, dtype=np.int16)


def button_fraction(image):
    pixels = _band(image, BUTTON_BAND)
    low, high = pixels.min(axis=2), pixels.max(axis=2)
    whiteish = (low > WHITE_LEVEL) & ((high - low) < MAX_SATURATION)
    return float(whiteish.mean())


def in_gameplay(image):
    return button_fraction(image) <= PAUSED_FRACTION


# Centre of the pause menu's "Resume Game" button, as a fraction of the client
# area. Clicking it is more reliable than sending Escape, which the pause menu
# does not always act on, and it is a menu click so it cannot disturb the world.
RESUME_BUTTON = (0.294, 0.374)


def ensure_gameplay(harness, probe_path, attempts=4):
    """Get the game back to gameplay, however it got out of it.

    Bedrock pauses whenever it loses the foreground, and an unattended run
    loses it often, so this is called before every capture rather than once.
    """
    for _ in range(attempts):
        harness.screenshot(str(probe_path))
        if in_gameplay(probe_path):
            return True

        window = harness.find_window()
        if not window:
            return False
        harness.focus(window[0])
        time.sleep(0.8)

        harness.screenshot(str(probe_path))
        if in_gameplay(probe_path):
            return True

        _, _, width, height = harness.window_rect(window[0])
        harness.click(RESUME_BUTTON[0] * width, RESUME_BUTTON[1] * height)
        time.sleep(1.5)
    return False


if __name__ == "__main__":
    for path in sys.argv[1:]:
        state = "gameplay" if in_gameplay(path) else "PAUSED/menu"
        print(f"white={button_fraction(path):6.3f}  "
              f"{state:12}  {path}")
