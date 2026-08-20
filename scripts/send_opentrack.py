"""Send synthetic OpenTrack UDP packets, to exercise the mod without a tracker.

An OpenTrack packet is six little-endian doubles: X, Y, Z in CENTIMETRES then
yaw, pitch, roll in degrees. The mod's parser converts position to metres on
the way in, so what is sent here is centimetres.

This drives the real pipeline end to end - receiver, processing, camera hook -
which is what makes the axis signs measurable instead of guessed.

Usage:
    # hold a pose (streams until stopped)
    python scripts/send_opentrack.py --x 20
    python scripts/send_opentrack.py --yaw 15 --seconds 10

    # step one axis at a time, naming each phase for a screenshot pass
    python scripts/send_opentrack.py --sweep position --hold 3
    python scripts/send_opentrack.py --sweep rotation --hold 3

"""

import argparse
import socket
import struct
import threading
import time

RATE_HZ = 60

# The capture scripts stream on a separate port so a real OpenTrack instance
# on the default 4242 cannot drown out the synthetic stream. Set the mod ini to
# match when running them.
TEST_HOST = "127.0.0.1"
TEST_PORT = 4243

POSITION_SWEEP = [
    ("centre", 0, 0, 0),
    ("X +20cm", 20, 0, 0),
    ("centre", 0, 0, 0),
    ("Y +20cm", 0, 20, 0),
    ("centre", 0, 0, 0),
    ("Z +20cm", 0, 0, 20),
]

ROTATION_SWEEP = [
    ("centre", 0, 0, 0),
    ("YAW +20", 20, 0, 0),
    ("centre", 0, 0, 0),
    ("PITCH +20", 0, 20, 0),
    ("centre", 0, 0, 0),
    ("ROLL +20", 0, 0, 20),
]


def packet(x=0.0, y=0.0, z=0.0, yaw=0.0, pitch=0.0, roll=0.0):
    return struct.pack("<6d", x, y, z, yaw, pitch, roll)


def stream(sock, address, data, seconds):
    """Hold a pose for a while. The mod treats data as stale after ~500ms, so
    a pose has to be sent continuously rather than once."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        sock.sendto(data, address)
        time.sleep(1.0 / RATE_HZ)


class PoseStream:
    """The same hold, on a background thread, so the caller can drive the game
    while a pose is held. Used by the capture scripts, which have to take
    screenshots between poses rather than block on each one.
    """

    def __init__(self, address):
        self._address = address
        self._current = packet()
        self._stop = threading.Event()

    def set(self, x=0.0, y=0.0, z=0.0, yaw=0.0, pitch=0.0, roll=0.0):
        self._current = packet(x, y, z, yaw, pitch, roll)

    def start(self):
        threading.Thread(target=self._run, daemon=True).start()

    def stop(self):
        self._stop.set()

    def _run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        while not self._stop.is_set():
            sock.sendto(self._current, self._address)
            time.sleep(1.0 / RATE_HZ)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=4242)
    ap.add_argument("--x", type=float, default=0.0, help="centimetres")
    ap.add_argument("--y", type=float, default=0.0, help="centimetres")
    ap.add_argument("--z", type=float, default=0.0, help="centimetres")
    ap.add_argument("--yaw", type=float, default=0.0, help="degrees")
    ap.add_argument("--pitch", type=float, default=0.0, help="degrees")
    ap.add_argument("--roll", type=float, default=0.0, help="degrees")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--sweep", choices=["position", "rotation"])
    ap.add_argument("--hold", type=float, default=3.0, help="seconds per sweep phase")
    ap.add_argument("--settle", type=float, default=3.0,
                    help="seconds of centred data before a sweep, so smoothing settles")
    opts = ap.parse_args()

    address = (opts.host, opts.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    if not opts.sweep:
        print(f"holding x={opts.x} y={opts.y} z={opts.z} "
              f"yaw={opts.yaw} pitch={opts.pitch} roll={opts.roll} "
              f"for {opts.seconds}s", flush=True)
        stream(sock, address, packet(opts.x, opts.y, opts.z,
                                     opts.yaw, opts.pitch, opts.roll), opts.seconds)
        return

    # A centred hold first, so the smoothing has settled before the first
    # offset and each phase is measured from neutral.
    print(f"settling at centre for {opts.settle}s", flush=True)
    stream(sock, address, packet(), opts.settle)

    sweep = POSITION_SWEEP if opts.sweep == "position" else ROTATION_SWEEP
    for name, a, b, c in sweep:
        print(f"PHASE {name}", flush=True)
        data = packet(a, b, c) if opts.sweep == "position" else packet(0, 0, 0, a, b, c)
        stream(sock, address, data, opts.hold)
    print("done", flush=True)


if __name__ == "__main__":
    main()
