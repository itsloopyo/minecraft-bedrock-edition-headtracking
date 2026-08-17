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

    # drift off centre, then press CENTER the way the Headcam apps do
    python scripts/send_opentrack.py --sweep recenter --hold 3
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

# Headcam's trailer, appended after the pose for a short burst of packets when
# the user presses CENTER in the app: magic, version, recenter counter. The
# apps only ever send it on the UDP path and only in that burst, because plain
# OpenTrack on Windows rejects datagrams over 48 bytes.
TRAILER_MAGIC = b"HCAM"
TRAILER_VERSION = 1
RECENTER_BURST_SECONDS = 0.5

# How long the app keeps sending the pre-press pose after arming the trailer.
# Measured off a real phone: the mod saw the request alongside a stale sample.
RECENTER_LAG_SECONDS = 0.1

# How far off centre the recenter test drifts before pressing CENTER, on both
# an angle and an axis so the rotation and position centres are each covered.
# Large enough that the failure this test exists for - the view parking
# mirrored on the far side instead of centring - is unmistakable in the log.
RECENTER_DRIFT_YAW = 25.0
RECENTER_DRIFT_X = 15.0


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


def recenter_sequence(sock, address, hold):
    """What the phone does when the user presses CENTER: it arms the trailer,
    and its own zeroing reaches the wire a frame or two later, so the burst
    opens on the pre-press pose and only then drops to zero. That lag is the
    whole point of this sequence. A mod that centres on whichever sample the
    request happens to ride subtracts the drift a second time and parks the
    view mirrored, and it does so only when it loses the race - which is what
    made the bug look intermittent."""
    drift = packet(x=RECENTER_DRIFT_X, yaw=RECENTER_DRIFT_YAW)
    print(f"PHASE drift X +{RECENTER_DRIFT_X:g}cm YAW +{RECENTER_DRIFT_YAW:g}", flush=True)
    stream(sock, address, drift, hold)

    trailer = TRAILER_MAGIC + bytes([TRAILER_VERSION, 1])
    print("PHASE app CENTER pressed, app output still lagging", flush=True)
    stream(sock, address, drift + trailer, RECENTER_LAG_SECONDS)

    print("PHASE app output zeroed, burst continues", flush=True)
    stream(sock, address, packet() + trailer, RECENTER_BURST_SECONDS - RECENTER_LAG_SECONDS)

    print("PHASE centred (expect the view back at neutral)", flush=True)
    stream(sock, address, packet(), hold)


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
    ap.add_argument("--sweep", choices=["position", "rotation", "recenter"])
    ap.add_argument("--hold", type=float, default=3.0, help="seconds per sweep phase")
    ap.add_argument("--settle", type=float, default=3.0,
                    help="seconds of centred data before a sweep, so the mod auto-recentres")
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

    # A centred hold first: the mod auto-recentres after a few frames of data,
    # and that has to happen at the origin or every later offset is measured
    # from the wrong place.
    print(f"settling at centre for {opts.settle}s", flush=True)
    stream(sock, address, packet(), opts.settle)

    if opts.sweep == "recenter":
        recenter_sequence(sock, address, opts.hold)
        print("done", flush=True)
        return

    sweep = POSITION_SWEEP if opts.sweep == "position" else ROTATION_SWEEP
    for name, a, b, c in sweep:
        print(f"PHASE {name}", flush=True)
        data = packet(a, b, c) if opts.sweep == "position" else packet(0, 0, 0, a, b, c)
        stream(sock, address, data, opts.hold)
    print("done", flush=True)


if __name__ == "__main__":
    main()
