"""Report the running game's PE fingerprint and match it against the registry.

The first thing to run when a user reports the "staying dormant" log line, and
the first step of a rederive after a Minecraft update.

Unlike the other mods in this catalogue this reads the fingerprint from the
RUNNING process, not from the EXE on disk: Microsoft Store licensing makes
Minecraft.Windows.exe unreadable from disk. Launch Minecraft first.
"""

import datetime
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import ctypes  # noqa: E402  (after sys.path fix-up)

from proc_probe import (  # noqa: E402
    COFF_HEADER_SIZE,
    COFF_TIMESTAMP,
    E_LFANEW,
    OPT_CHECKSUM,
    OPT_SIZE_OF_IMAGE,
    PROCESS_QUERY_INFORMATION,
    PROCESS_VM_READ,
    find_pid,
    k32,
    main_module,
    read_range,
)

PROFILE_SOURCE = Path(__file__).parent.parent / "src/MinecraftHeadTracking/builds/store_offsets.cpp"


def known_profiles():
    """(name, timestamp, size, checksum) for every profile in store_offsets.cpp."""
    if not PROFILE_SOURCE.exists():
        return []
    text = PROFILE_SOURCE.read_text(encoding="utf-8")
    pattern = re.compile(
        r'"(?P<name>[^"]+)",\s*\{\s*(?P<ts>0x[0-9A-Fa-f]+),\s*'
        r"(?P<size>0x[0-9A-Fa-f]+),\s*(?P<sum>0x[0-9A-Fa-f]+)\s*\}"
    )
    return [
        (m.group("name"), int(m.group("ts"), 16), int(m.group("size"), 16), int(m.group("sum"), 16))
        for m in pattern.finditer(text)
    ]


def main():
    pid = find_pid("Minecraft.Windows")
    handle = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        raise OSError(ctypes.get_last_error(), f"OpenProcess({pid}) failed")

    try:
        base, _ = main_module(handle)
        headers = read_range(handle, base, 0x1000)
        (e_lfanew,) = struct.unpack_from("<I", headers, E_LFANEW)
        coff = e_lfanew + 4
        (timestamp,) = struct.unpack_from("<I", headers, coff + COFF_TIMESTAMP)
        opt = coff + COFF_HEADER_SIZE
        (size_of_image,) = struct.unpack_from("<I", headers, opt + OPT_SIZE_OF_IMAGE)
        (checksum,) = struct.unpack_from("<I", headers, opt + OPT_CHECKSUM)
    finally:
        k32.CloseHandle(handle)

    print(f"Running Minecraft.Windows.exe (pid {pid})")
    print(f"  TimeDateStamp = 0x{timestamp:08X}")
    print(f"  SizeOfImage   = 0x{size_of_image:08X}")
    print(f"  CheckSum      = 0x{checksum:08X}")

    profiles = known_profiles()
    for name, ts, size, checksum_known in profiles:
        if (ts, size, checksum_known) == (timestamp, size_of_image, checksum):
            print(f"\nMatches build profile {name}. No rederive needed.")
            return 0

    stamp = datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc).strftime("%Y%m%d")
    print(f"\nNo profile matches ({len(profiles)} known). Paste this into store_offsets.cpp -")
    print("the function inside the anonymous namespace, the profile after it:\n")
    # Every offset left at zero on purpose: the profile is complete enough for
    # the mod to recognise the build and say so, and ProfileIsComplete keeps it
    # dormant until the addresses are derived. Fields are assigned by name when
    # they are filled in, never positionally - see store_offsets.cpp.
    print(f"constexpr OffsetTable Offsets_{stamp}() {{")
    print("    OffsetTable t{};")
    print("    // TODO: derive this build's addresses.")
    print("    // Until CameraSetup, GetRenderCameraComponent, LevelGetGameRules and")
    print("    // ClientInstanceGetLocalPlayer are set, the mod stays dormant on this build.")
    print("    return t;")
    print("}")
    print("")
    print(f"extern const BuildProfile kStoreProfile_{stamp} = {{")
    print(f'    "store-win64-{stamp}",')
    print(f"    {{0x{timestamp:08X}, 0x{size_of_image:08X}, 0x{checksum:08X}}},")
    print(f"    Offsets_{stamp}(),")
    print("};")
    print("\nThen add it to the TOP of kKnownProfiles in build_registry.cpp,")
    print("leaving every existing profile in place, and rederive the camera RVAs.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
