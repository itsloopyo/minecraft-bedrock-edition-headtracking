// Compiled into the hotkey oracle library only, with `cameraunlock` renamed, so chord_hotkeys.h
// here is core 1fd2956's (oracle/core/include) and the poller is oracle_hk_fake's. The poller
// comes first so the fake GetAsyncKeyState is declared before IsChordHeld looks the name up.
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/chord_hotkeys.h"

#include "oracle_adapter.h"

namespace mcht_oracle_view {

FireTable OracleFires(int yawModeKey) {
    namespace input = cameraunlock::input;
    std::array<int, 3> fired{};
    input::FakeRegistrations().clear();

    // v1.1.2's head_tracking.cpp RegisterHotkeys (lines 280-310), restated: that file is the
    // whole tracking runtime, and its registration is these six calls on one poller, with the
    // three actions and g_yawModeKey, which ApplySettings set from Settings::YawModeKey.
    {
        using input::ChordGuarded;
        using input::NavGuarded;
        const auto toggle = [&fired] { ++fired[0]; };
        const auto cycleMode = [&fired] { ++fired[1]; };
        const auto toggleYawMode = [&fired] { ++fired[2]; };
        input::HotkeyPoller hotkeys;
        hotkeys.AddHotkey(VK_END, NavGuarded(toggle));
        hotkeys.AddHotkey(VK_PRIOR, NavGuarded(cycleMode));
        hotkeys.AddHotkey(yawModeKey, NavGuarded(toggleYawMode));
        hotkeys.AddHotkey('Y', ChordGuarded(toggle));
        hotkeys.AddHotkey('G', ChordGuarded(cycleMode));
        hotkeys.AddHotkey('H', ChordGuarded(toggleYawMode));
    }
    const std::vector<input::FakeRegistration> registered = input::FakeRegistrations();

    // The published poller's Poll: a callback runs when its key goes down.
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            input::FakeHeld() = held;
            for (const input::FakeRegistration& r : registered) {
                if (r.vk == vk && r.callback) r.callback();
            }
            table.push_back(fired);
        }
    }
    input::FakeHeld() = 0;
    return table;
}

}  // namespace mcht_oracle_view
