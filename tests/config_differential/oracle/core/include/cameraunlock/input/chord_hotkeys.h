#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

namespace cameraunlock::input {

// True while the Ctrl+Shift chord modifier is held. Every mod registers
// Ctrl+Shift+<letter> chord bindings (T/Y/U/G/H/J cluster) alongside its
// nav-cluster keys; this is the shared modifier check.
inline bool IsChordHeld() {
    return ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        && ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
}

// Wrap an action so it fires only when the chord is NOT held. Used for
// nav-cluster bindings so the chord path is the sole trigger for
// Ctrl+Shift+<nav> combos.
template <typename F>
std::function<void()> NavGuarded(F action) {
    return [action]() { if (!IsChordHeld()) action(); };
}

// Wrap an action so it fires only while the chord IS held. Used for the
// Ctrl+Shift+<letter> chord bindings.
template <typename F>
std::function<void()> ChordGuarded(F action) {
    return [action]() { if (IsChordHeld()) action(); };
}

} // namespace cameraunlock::input
