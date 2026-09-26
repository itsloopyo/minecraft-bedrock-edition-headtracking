#pragma once

// Stands in for core's HotkeyPoller in the hotkey oracle library only, so v1.1.2's hotkey
// registration runs with no polling thread and the test sees what it registered. Compiled with
// that library's renaming, so everything here lives in mcht_oracle_hk_core and cannot collide
// with the real poller the current core links.
//
// The GetAsyncKeyState below is declared in the namespace chord_hotkeys.h's guards are defined
// in, so their unqualified calls find it before ::GetAsyncKeyState. That is how the test holds
// modifiers without a keyboard.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <vector>

namespace cameraunlock::input {

using HotkeyCallback = std::function<void()>;

struct FakeRegistration {
    int vk;
    HotkeyCallback callback;
};

// Every registration any poller made since the test last cleared it, in order.
inline std::vector<FakeRegistration>& FakeRegistrations() {
    static std::vector<FakeRegistration> registrations;
    return registrations;
}

// The modifiers the test holds: 1 Ctrl, 2 Shift, 4 Alt.
inline int& FakeHeld() {
    static int held = 0;
    return held;
}

inline SHORT GetAsyncKeyState(int vk) {
    const int held = FakeHeld();
    const bool down = (vk == VK_CONTROL && (held & 1) != 0) || (vk == VK_SHIFT && (held & 2) != 0) ||
                      (vk == VK_MENU && (held & 4) != 0);
    return down ? static_cast<SHORT>(0x8000) : static_cast<SHORT>(0);
}

class HotkeyPoller {
public:
    int AddHotkey(int vkCode, HotkeyCallback callback) {
        FakeRegistrations().push_back({vkCode, std::move(callback)});
        return static_cast<int>(FakeRegistrations().size());
    }
};

}  // namespace cameraunlock::input
