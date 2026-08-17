"""Drive Minecraft unattended: launch, screenshot, send input, read the log.

Bedrock has no command line and no headless mode, and the mod's camera hooks
only run once the player is in a world, so every camera experiment needs a
human to click through the menus. This automates that, which is what makes
the write/observe loop runnable without one.

Usage:
    python scripts/game_harness.py launch          # inject and start the game
    python scripts/game_harness.py shot out.png    # capture the game window
    python scripts/game_harness.py click X Y       # window-relative click
    python scripts/game_harness.py key ESCAPE
    python scripts/game_harness.py kill
"""

import ctypes
import re
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

from PIL import ImageGrab

user32 = ctypes.WinDLL("user32", use_last_error=True)

GAME_PROCESS = "Minecraft.Windows.exe"
GAME_WINDOW = "Minecraft"
DEPLOY_DIR = Path(
    subprocess.run(
        ["powershell", "-NoProfile", "-Command", "$env:LOCALAPPDATA"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
) / "CameraUnlock" / "MinecraftHeadTracking"

LOG_PATH = DEPLOY_DIR / "MinecraftHeadTracking.log"
LAUNCHER = DEPLOY_DIR / "MinecraftHeadTrackingLauncher.exe"

# The line discovery mode writes as it enters each phase. Lives here beside
# LOG_PATH because that is the file it is matched against, and because the
# capture scripts that poll for it would otherwise each carry their own copy.
PHASE_LINE = re.compile(r"\[calib\] phase=(.+)$", re.MULTILINE)

VK = {
    "ESCAPE": 0x1B, "RETURN": 0x0D, "SPACE": 0x20, "TAB": 0x09,
    "UP": 0x26, "DOWN": 0x28, "LEFT": 0x25, "RIGHT": 0x27,
    "W": 0x57, "A": 0x41, "S": 0x53, "D": 0x44, "T": 0x54, "E": 0x45,
    "HOME": 0x24, "END": 0x23, "PRIOR": 0x21, "NEXT": 0x22,
}


# --- window -----------------------------------------------------------------

def game_pids():
    """Pids of the game process. Matching windows by title is not safe - any
    editor holding a file with 'Minecraft' in the name matches, which is how
    an early run screenshotted Notepad."""
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_PROCESS}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = set()
    for line in out.splitlines():
        parts = [field.strip('"') for field in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == GAME_PROCESS.lower():
            pids.add(int(parts[1]))
    return pids


def find_window():
    """The game's largest visible top-level window, or None."""
    pids = game_pids()
    if not pids:
        return None
    result = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def callback(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value not in pids:
            return True
        rect = wintypes.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(rect))
        if rect.right < 200 or rect.bottom < 200:
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        buffer = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buffer, length + 1)
        result.append((hwnd, buffer.value, pid.value, rect.right * rect.bottom))
        return True

    user32.EnumWindows(callback, 0)
    if not result:
        return None
    return max(result, key=lambda entry: entry[3])[:3]


def window_rect(hwnd):
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    origin = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return origin.x, origin.y, rect.right, rect.bottom


def focus(hwnd):
    """Force the game to the foreground.

    Windows only lets the process that already owns the foreground call
    SetForegroundWindow, so a bare call silently fails from a script. The
    documented way round it is to borrow the foreground thread's input queue
    for the duration of the call. The synthetic ALT tap is the other half of
    the folklore: it clears the foreground lock timeout."""
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE

    alt = 0x12
    user32.keybd_event(alt, 0, 0, 0)
    user32.keybd_event(alt, 0, 2, 0)  # KEYEVENTF_KEYUP

    target_thread = user32.GetWindowThreadProcessId(hwnd, None)
    current_thread = ctypes.windll.kernel32.GetCurrentThreadId()
    foreground = user32.GetForegroundWindow()
    foreground_thread = user32.GetWindowThreadProcessId(foreground, None) if foreground else 0

    for thread in {target_thread, foreground_thread} - {0, current_thread}:
        user32.AttachThreadInput(current_thread, thread, True)
    try:
        user32.BringWindowToTop(hwnd)
        user32.SetForegroundWindow(hwnd)
    finally:
        for thread in {target_thread, foreground_thread} - {0, current_thread}:
            user32.AttachThreadInput(current_thread, thread, False)
    time.sleep(0.4)


# --- capture ----------------------------------------------------------------

def screenshot(path):
    """Grab the game window. Falls back to the full desktop if the window is
    gone, so a failed capture still shows what state the machine is in.

    all_screens bboxes are relative to the virtual desktop origin, which is not
    (0,0) on a multi-monitor setup - without this translation a window on a
    second monitor captures whatever happens to sit at those coordinates on the
    primary one."""
    found = find_window()
    if not found:
        image = ImageGrab.grab(all_screens=True)
        image.save(path)
        return image.size
    x, y, w, h = window_rect(found[0])
    vx = user32.GetSystemMetrics(76)  # SM_XVIRTUALSCREEN
    vy = user32.GetSystemMetrics(77)  # SM_YVIRTUALSCREEN
    left, top = x - vx, y - vy
    image = ImageGrab.grab(bbox=(left, top, left + w, top + h), all_screens=True)
    image.save(path)
    return image.size


# --- input ------------------------------------------------------------------

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
KEYEVENTF_KEYUP, KEYEVENTF_SCANCODE = 0x0002, 0x0008
MOUSEEVENTF_MOVE_ABSOLUTE = 0x8001
MOUSEEVENTF_VIRTUALDESK = 0x4000
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.POINTER(wintypes.ULONG))]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.POINTER(wintypes.ULONG))]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def _send(*inputs):
    array = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), array, ctypes.sizeof(INPUT))


def key(name, hold=0.05):
    """Scancode input. Bedrock reads raw input for gameplay keys and ignores
    virtual-key-only injection for some of them."""
    vk = VK[name.upper()] if name.upper() in VK else ord(name.upper())
    scan = user32.MapVirtualKeyW(vk, 0)
    down = INPUT(type=INPUT_KEYBOARD,
                 ki=KEYBDINPUT(wVk=0, wScan=scan, dwFlags=KEYEVENTF_SCANCODE,
                               time=0, dwExtraInfo=None))
    up = INPUT(type=INPUT_KEYBOARD,
               ki=KEYBDINPUT(wVk=0, wScan=scan,
                             dwFlags=KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP,
                             time=0, dwExtraInfo=None))
    _send(down)
    time.sleep(hold)
    _send(up)


def ensure_foreground(hwnd, attempts=3):
    """Refuse to proceed unless the game really is the foreground window.

    Other windows steal focus (the Lopari launcher does, right after it
    injects), and a click sent blind then lands in someone else's UI. For a
    harness that clicks buttons in a game with worlds in it, that is a safety
    property, not a convenience."""
    for _ in range(attempts):
        if user32.GetForegroundWindow() == hwnd:
            return True
        focus(hwnd)
        time.sleep(0.6)
    return user32.GetForegroundWindow() == hwnd


def mouse_move(dx, dy, steps=8):
    """Relative mouse motion - turns the player's view, the way the mouse does.

    This is the ONLY in-world input the harness sends. It aims the player, which
    is exactly what an aim-decoupling test needs, and unlike a click it cannot
    place or break a block. Split into small steps because the game samples raw
    motion per frame and one large jump can be clamped.
    """
    for _ in range(steps):
        _send(INPUT(type=INPUT_MOUSE,
                    mi=MOUSEINPUT(dx=int(dx / steps), dy=int(dy / steps), mouseData=0,
                                  dwFlags=0x0001,  # MOUSEEVENTF_MOVE, relative
                                  time=0, dwExtraInfo=None)))
        time.sleep(0.02)


def click(cx, cy):
    """Click at a point given in game-window client coordinates."""
    found = find_window()
    if not found:
        raise SystemExit("game window not found")
    if not ensure_foreground(found[0]):
        raise SystemExit("game window would not take focus; refusing to click blind")
    ox, oy, w, h = window_rect(found[0])
    sx, sy = ox + int(cx), oy + int(cy)
    # Absolute coordinates are normalised across the WHOLE virtual desktop, so
    # both its origin and its extent matter. Without VIRTUALDESK the point is
    # clamped to the primary monitor and a click aimed at a second screen lands
    # somewhere else entirely.
    vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
    vw, vh = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
    ax = int((sx - vx) * 65535 / vw)
    ay = int((sy - vy) * 65535 / vh)
    move = INPUT(type=INPUT_MOUSE,
                 mi=MOUSEINPUT(dx=ax, dy=ay, mouseData=0,
                               dwFlags=MOUSEEVENTF_MOVE_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK,
                               time=0, dwExtraInfo=None))
    _send(move)
    time.sleep(0.15)
    _send(INPUT(type=INPUT_MOUSE,
                mi=MOUSEINPUT(dx=0, dy=0, mouseData=0, dwFlags=MOUSEEVENTF_LEFTDOWN,
                              time=0, dwExtraInfo=None)))
    time.sleep(0.06)
    _send(INPUT(type=INPUT_MOUSE,
                mi=MOUSEINPUT(dx=0, dy=0, mouseData=0, dwFlags=MOUSEEVENTF_LEFTUP,
                              time=0, dwExtraInfo=None)))


# --- lifecycle --------------------------------------------------------------

def running():
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_PROCESS}"],
                         capture_output=True, text=True).stdout
    return GAME_PROCESS.lower() in out.lower()


def kill():
    subprocess.run(["taskkill", "/F", "/IM", GAME_PROCESS], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "GameLaunchHelper.exe"], capture_output=True)


def launch(wait=90):
    if running():
        kill()
        time.sleep(3)
    if LOG_PATH.exists():
        LOG_PATH.unlink()
    subprocess.Popen([str(LAUNCHER)], cwd=str(DEPLOY_DIR))
    for _ in range(wait):
        if find_window():
            return True
        time.sleep(1)
    return False


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    command = sys.argv[1]
    if command == "launch":
        print("window up" if launch() else "no window appeared")
    elif command == "shot":
        found = find_window()
        if found:
            focus(found[0])
        print(screenshot(sys.argv[2]))
    elif command == "focus":
        found = find_window()
        if not found:
            raise SystemExit("game window not found")
        focus(found[0])
    elif command == "click":
        click(float(sys.argv[2]), float(sys.argv[3]))
    elif command == "key":
        key(sys.argv[2])
    elif command == "look":
        found = find_window()
        if found:
            ensure_foreground(found[0])
        mouse_move(float(sys.argv[2]), float(sys.argv[3]))
    elif command == "kill":
        kill()
    elif command == "window":
        print(find_window(), window_rect(find_window()[0]) if find_window() else None)
    else:
        raise SystemExit(f"unknown command {command}")


if __name__ == "__main__":
    main()
