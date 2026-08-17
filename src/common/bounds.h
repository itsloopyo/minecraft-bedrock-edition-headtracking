#pragma once

#include <cwchar>

// Range checks for the numbers that reach this mod from outside it: the INI
// file and the launcher's command line. Header-only and free of Windows types
// so the test binary can exercise them without linking the mod.
namespace mcht::bounds {

// OpenTrack's own range. 0 would bind an ephemeral port the tracker can never
// find, and anything above 65535 truncates into an unrelated port; both look
// from the game exactly like a tracker that is not sending.
constexpr int kMinTrackerPort = 1024;
constexpr int kMaxTrackerPort = 65535;

inline bool ValidTrackerPort(int port) {
    return port >= kMinTrackerPort && port <= kMaxTrackerPort;
}

// Discovery's duration is multiplied by 1000 into an int. An unbounded value
// overflows that multiply, which is undefined behaviour, and a negative one
// ends the run before its first phase without saying why.
constexpr int kMinDiscoverySeconds = 1;
constexpr int kMaxDiscoverySeconds = 3600;

inline int ClampDiscoverySeconds(int seconds) {
    if (seconds < kMinDiscoverySeconds) {
        return kMinDiscoverySeconds;
    }
    return seconds > kMaxDiscoverySeconds ? kMaxDiscoverySeconds : seconds;
}

// The launcher's --wait. It is multiplied by 1000 into a 32-bit millisecond
// count and added to a tick count, so an unbounded value turns "wait for the
// game" into a wait no user will outlive.
constexpr unsigned long kMinWaitSeconds = 1;
constexpr unsigned long kMaxWaitSeconds = 3600;

// False for anything that is not a whole number in range, including a value
// with trailing text. wcstol saturates rather than reporting overflow and
// returns 0 for text it cannot parse at all, so neither can be told from a
// legitimate value without checking the end pointer.
inline bool ParseWaitSeconds(const wchar_t* text, unsigned long& out) {
    if (text == nullptr || *text == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (end == nullptr || *end != L'\0') {
        return false;
    }
    if (value < static_cast<long>(kMinWaitSeconds) || value > static_cast<long>(kMaxWaitSeconds)) {
        return false;
    }
    out = static_cast<unsigned long>(value);
    return true;
}

}  // namespace mcht::bounds
