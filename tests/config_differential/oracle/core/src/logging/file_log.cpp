#include <cameraunlock/logging/file_log.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cameraunlock::logging {

namespace {

// HANDLE rather than FILE* so EmergencyLine can write from inside an
// exception handler without going through the CRT (locks, heap, TLS).
// FILE_SHARE_READ so external tools can tail the log while we hold write
// access.
//
// One atomic rather than a handle plus an open flag: EmergencyLine reads it
// without the mutex (a faulted thread may already hold that), and two unordered
// variables let it pick up a handle value Close() had already given back to the
// OS. Win32 recycles handle values, so the game reopening a file in that window
// can receive the same numeric handle and take our crash report into the
// player's save.
constexpr intptr_t kNoHandle = -1;  // INVALID_HANDLE_VALUE, as a constant initializer
std::atomic<intptr_t> g_handle{kNoHandle};
std::mutex g_mutex;

// One rotation per process, not per Open(). A mod that honours a "log to file"
// setting closes the log and can reopen it later in the same run; rotating a
// second time would file the run still in progress away as the previous
// generation, losing the launch the player actually wants to send.
bool g_rotated = false;

HANDLE CurrentHandle() {
    return reinterpret_cast<HANDLE>(g_handle.load(std::memory_order_acquire));
}

// No FlushFileBuffers here: Line() is called from render/hook threads where a
// physical-disk flush is a multi-millisecond stall. OS-buffered writes survive
// a process crash (the page cache outlives the process); only kernel
// crash/power loss can lose them, which EmergencyLine covers for the one path
// where that durability matters.
void WriteTimestampedLocked(const char* msg, size_t len) {
    const HANDLE handle = CurrentHandle();
    if (handle == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char prefix[32];
    const int n = std::snprintf(prefix, sizeof(prefix),
        "[%02d:%02d:%02d.%03d] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    DWORD written = 0;
    if (n > 0) WriteFile(handle, prefix, static_cast<DWORD>(n), &written, nullptr);
    WriteFile(handle, msg, static_cast<DWORD>(len), &written, nullptr);
    WriteFile(handle, "\r\n", 2, &written, nullptr);
}

}  // namespace

// Rotated name for the outgoing generation: "HeadTracking.log" becomes
// "HeadTracking.prev.log", so the kept copy is still a .log a player can open.
// The separator check keeps a dot in a directory name (a real shape in game
// install paths) from being mistaken for the file extension.
std::wstring PreviousGenerationPath(const std::wstring& filename) {
    const size_t dot = filename.find_last_of(L'.');
    const size_t sep = filename.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) {
        return filename + L".prev";
    }
    return filename.substr(0, dot) + L".prev" + filename.substr(dot);
}

void Open(const std::wstring& filename) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (CurrentHandle() != INVALID_HANDLE_VALUE) return;

    // Keep exactly one generation. CREATE_ALWAYS below truncates, and
    // EmergencyLine writes the crash report into this same file, so without
    // this the report is destroyed the moment the player relaunches to
    // reproduce the crash - which is the first thing they do.
    bool rotationFailed = false;
    DWORD rotationError = 0;
    if (!g_rotated) {
        g_rotated = true;
        // Only a rename over an existing log can fail in a way worth reporting;
        // a first-ever launch has nothing to rotate and must stay quiet.
        const bool hadPriorLog =
            GetFileAttributesW(filename.c_str()) != INVALID_FILE_ATTRIBUTES;
        // Named, not a temporary in the call: a temporary's destructor would run
        // between MoveFileExW and GetLastError and the deallocation can overwrite
        // the thread's last-error value.
        const std::wstring previous = PreviousGenerationPath(filename);
        const bool rotated =
            MoveFileExW(filename.c_str(), previous.c_str(),
                        MOVEFILE_REPLACE_EXISTING) != FALSE;
        rotationError = GetLastError();
        rotationFailed = hadPriorLog && !rotated;
    }

    const HANDLE handle = CreateFileW(
        filename.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    g_handle.store(reinterpret_cast<intptr_t>(handle), std::memory_order_release);

    // Said here rather than dropped: CREATE_ALWAYS has just truncated the file,
    // so a failed rename means the previous session is gone and whatever sits in
    // .prev.log is older than the reader will assume. Written through the locked
    // helper because Line() would take the mutex this function already holds.
    if (rotationFailed && handle != INVALID_HANDLE_VALUE) {
        char msg[192];
        const int n = std::snprintf(msg, sizeof(msg),
            "WARN: could not rotate the previous log (error %lu); any .prev.log "
            "beside this one is from an older session",
            static_cast<unsigned long>(rotationError));
        if (n > 0) {
            const size_t len = (static_cast<size_t>(n) >= sizeof(msg))
                ? sizeof(msg) - 1
                : static_cast<size_t>(n);
            WriteTimestampedLocked(msg, len);
        }
    }
}

void Close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Publish the closed state before the handle value goes back to the OS, so
    // no reader can still be holding it once it can be reissued.
    const auto old = g_handle.exchange(kNoHandle, std::memory_order_acq_rel);
    if (reinterpret_cast<HANDLE>(old) != INVALID_HANDLE_VALUE) {
        CloseHandle(reinterpret_cast<HANDLE>(old));
    }
}

void Line(const char* fmt, ...) {
    if (CurrentHandle() == INVALID_HANDLE_VALUE) return;
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return;
    const size_t len = (static_cast<size_t>(n) >= sizeof(buf))
        ? sizeof(buf) - 1
        : static_cast<size_t>(n);
    std::lock_guard<std::mutex> lock(g_mutex);
    WriteTimestampedLocked(buf, len);
}

void EmergencyLine(const char* fmt, ...) {
    // No mutex: a faulted thread may already hold it. The single atomic is what
    // makes that safe - see the note on g_handle.
    const HANDLE h = CurrentHandle();
    if (h == INVALID_HANDLE_VALUE) return;

    // Deliberately small. This runs on the crash path, and EXCEPTION_STACK_OVERFLOW
    // leaves roughly one page of stack: a 2KB buffer plus the vsnprintf frame
    // costs the report we are here to write.
    char buf[512];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return;
    const size_t msg_len = (static_cast<size_t>(n) >= sizeof(buf))
        ? sizeof(buf) - 1
        : static_cast<size_t>(n);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char prefix[32];
    const int p = std::snprintf(prefix, sizeof(prefix),
        "[%02d:%02d:%02d.%03d] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    DWORD written = 0;
    if (p > 0) WriteFile(h, prefix, static_cast<DWORD>(p), &written, nullptr);
    WriteFile(h, buf, static_cast<DWORD>(msg_len), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    FlushFileBuffers(h);
}

}  // namespace cameraunlock::logging
