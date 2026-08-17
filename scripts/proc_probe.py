"""Read the running game's module header.

Bedrock's on-disk EXE cannot be opened, so `check_fingerprint.py` identifies the
build from the loaded module's PE header instead of from a file. This module is
deliberately limited to locating the process and reading a header-sized range;
it does not reconstruct the image.
"""

import ctypes
import ctypes.wintypes as wt

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
LIST_MODULES_ALL = 0x03

# PE header field offsets, from the PE32+ layout. Named because a bare 0x38
# says nothing about which field it reaches.
E_LFANEW = 0x3C  # In the DOS header: offset of the PE signature.
COFF_TIMESTAMP = 0x04  # From the COFF header's start.
COFF_HEADER_SIZE = 0x14  # COFF header start -> optional header start.
OPT_SIZE_OF_IMAGE = 0x38  # The two below are from the optional header's start.
OPT_CHECKSUM = 0x40

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class MODULEINFO(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", ctypes.c_void_p),
        ("SizeOfImage", wt.DWORD),
        ("EntryPoint", ctypes.c_void_p),
    ]


# ctypes defaults every unprototyped argument to C int, which truncates the
# 64-bit handles and module bases these calls traffic in.
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenProcess.restype = wt.HANDLE
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.ReadProcessMemory.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
psapi.GetModuleBaseNameW.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_wchar_p, wt.DWORD]
psapi.EnumProcessModulesEx.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    wt.DWORD,
    ctypes.POINTER(wt.DWORD),
    wt.DWORD,
]
psapi.GetModuleInformation.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wt.DWORD]


def find_pid(image_name):
    """Return the single PID whose image name matches, erroring on 0 or >1."""
    count = 4096
    arr = (wt.DWORD * count)()
    needed = wt.DWORD()
    if not psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(needed)):
        raise OSError(ctypes.get_last_error(), "EnumProcesses failed")

    wanted = image_name.lower()
    if not wanted.endswith(".exe"):
        wanted += ".exe"

    matches = []
    for i in range(needed.value // ctypes.sizeof(wt.DWORD)):
        pid = arr[i]
        h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
        if not h:
            continue
        try:
            buf = ctypes.create_unicode_buffer(1024)
            if psapi.GetModuleBaseNameW(h, None, buf, 1024) and buf.value.lower() == wanted:
                matches.append(pid)
        finally:
            k32.CloseHandle(h)

    if not matches:
        raise SystemExit(f"No running process named {image_name!r}. Launch the game first.")
    if len(matches) > 1:
        raise SystemExit(f"Ambiguous: {len(matches)} processes named {image_name!r} ({matches}).")
    return matches[0]


def main_module(handle):
    """Base address and image size of the process's first (main) module."""
    needed = wt.DWORD()
    mods = (ctypes.c_void_p * 1024)()
    if not psapi.EnumProcessModulesEx(
        handle, ctypes.byref(mods), ctypes.sizeof(mods), ctypes.byref(needed), LIST_MODULES_ALL
    ):
        raise OSError(ctypes.get_last_error(), "EnumProcessModulesEx failed")
    info = MODULEINFO()
    if not psapi.GetModuleInformation(handle, mods[0], ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError(ctypes.get_last_error(), "GetModuleInformation failed")
    return mods[0], info.SizeOfImage


def read_range(handle, address, size):
    """Read `size` bytes at `address`, or raise if the read is refused."""
    buf = (ctypes.c_char * size)()
    got = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(
        handle, ctypes.c_void_p(address), buf, ctypes.c_size_t(size), ctypes.byref(got)
    )
    if not ok or got.value != size:
        raise OSError(
            ctypes.get_last_error(),
            f"ReadProcessMemory({address:#x}, {size:#x}) read {got.value:#x}",
        )
    return bytes(buf)
