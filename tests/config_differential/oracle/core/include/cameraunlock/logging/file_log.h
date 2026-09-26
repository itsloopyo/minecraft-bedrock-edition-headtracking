#pragma once

#include <string>

namespace cameraunlock::logging {

// Process-wide log file. One per mod: open it next to the game EXE (or the
// mod DLL) during bootstrap, write timestamped lines from any thread, close
// on shutdown. Lines are flushed through to disk immediately so the log
// survives a hard process kill - that is what makes it usable as the crash
// reporter's output channel.

// Name Open() rotates the outgoing generation to: "HeadTracking.log" becomes
// "HeadTracking.prev.log". Exposed for tests; callers do not need it.
std::wstring PreviousGenerationPath(const std::wstring& filename);

void Open(const std::wstring& filename);
void Close();

// printf-style. Timestamped, mutex-serialized, flushed per line.
void Line(const char* fmt, ...);

// Lock-free, exception-handler-safe write. Use ONLY from inside a
// vectored / unhandled exception handler. Bypasses the normal mutex so
// a thread holding the log lock when it faulted does not deadlock the
// crash report, and uses WriteFile directly (no CRT locks, no heap).
void EmergencyLine(const char* fmt, ...);

}  // namespace cameraunlock::logging
