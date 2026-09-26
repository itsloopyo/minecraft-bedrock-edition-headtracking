#pragma once

// The oracle: v1.1.2, the newest published build, as it read MinecraftHeadTracking.ini and
// registered its hotkeys, compiled from oracle/ with the core sources it included at its pin
// (1fd2956). Two libraries build it, each with its namespaces renamed at compile time so it links
// beside the current core. This header names no core or mod type, so the test includes it
// without the renaming.

#include <array>
#include <string>
#include <vector>

namespace mcht_oracle_view {

// v1.1.2's mcht::tracking::Settings, flattened.
struct OracleSettings {
    float yaw_sensitivity, pitch_sensitivity, roll_sensitivity;
    bool invert_yaw, invert_pitch, invert_roll;
    float local_smoothing, remote_smoothing;
    float position_sensitivity_x, position_sensitivity_y, position_sensitivity_z;
    float position_limit_x, position_limit_y, position_limit_y_down, position_limit_z, position_limit_z_back;
    bool position_invert_x, position_invert_y, position_invert_z;
    bool enable_on_startup, position_enabled, world_space_yaw;
    int yaw_mode_key;
    int port;
};

struct OracleResult {
    // Bootstrap took the [Discovery] branch, and the duration it handed InstallCalibration.
    bool discovery;
    int discovery_seconds;
    // What Start ran on: ReadSettings(path).value_or(Settings{}). On the discovery branch
    // Bootstrap never called Start, and this is what ReadSettings gives for the same file.
    OracleSettings settings;
};

// v1.1.2's Bootstrap, from dllmain.cpp verbatim, with `folder` (ending in a backslash) as the
// mod's folder and the camera reported recovered. It writes MinecraftHeadTracking.ini there
// when there is none, as that build did, and its log beside it.
OracleResult RunOracle(const std::wstring& folder);

// Which actions a key press fires, for every key a binding can name (0x01-0xFE) under every
// set of held modifiers. Entry (vk - kFirstKey) * kHeldStates + held counts the toggle, cycle
// and yaw mode actions fired, in that order. held: 1 Ctrl, 2 Shift, 4 Alt.
constexpr int kFirstKey = 0x01;
constexpr int kLastKey = 0xFE;
constexpr int kHeldStates = 8;
using FireTable = std::vector<std::array<int, 3>>;

// v1.1.2's RegisterHotkeys with `yawModeKey`, pressing each key under each held set.
FireTable OracleFires(int yawModeKey);

}  // namespace mcht_oracle_view
