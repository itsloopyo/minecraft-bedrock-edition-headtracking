#pragma once

// The reader of MinecraftHeadTracking.ini as v1.1.2, the last build before the canonical
// format, read it, frozen so a player updating from any older build is converted exactly as
// that build read the file. Nothing in this folder is ever edited.
//
// Three things differ from the code it was taken from (src/MinecraftHeadTracking/
// tracking_settings.cpp and dllmain.cpp at v1.1.2): it fills this frozen copy of that build's
// Settings and defaults rather than the runtime type, it never writes the file (v1.1.2 created a
// missing file with its defaults before reading it, so a missing file reads here as those
// defaults), and the reads dllmain.cpp made outside the reader, the byte order mark check and
// [Discovery], are part of it. The core helpers it called that core does not freeze are copied
// beside it, with the values they had at core 1fd2956: SanitizeFinite, the default smoothing
// pair, PositionSettings::Default() and the tracker port bounds.

#include "cameraunlock/config/legacy_import.h"

#include <string>
#include <vector>

namespace mcht::legacy {

enum class ReadStatus {
    Read,
    // No file at the path. Config holds the defaults.
    Absent,
};

// v1.1.2's mcht::tracking::Settings, flattened, with its defaults, and the two [Discovery]
// values Bootstrap read.
struct Config {
    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = true;
    bool invert_roll = true;

    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float position_limit_x = 0.30f;
    float position_limit_y = 0.20f;
    // Never read from the file: PositionSettings::Default() set it and nothing changed it.
    float position_limit_y_down = 0.20f;
    float position_limit_z = 0.40f;
    float position_limit_z_back = 0.10f;
    bool position_invert_x = false;
    bool position_invert_y = false;
    bool position_invert_z = false;

    bool enable_on_startup = true;
    bool position_enabled = true;
    bool world_space_yaw = true;
    int yaw_mode_key = 0x22;  // VK_NEXT, Page Down.
    int port = 4242;

    bool discovery_enabled = false;
    int discovery_seconds = 40;

    // Call on a default-constructed Config, with the path and its ANSI form as AnsiPath
    // gives it: v1.1.2 checked the byte order mark through the one and read through the other.
    ReadStatus Read(const std::wstring& path, const std::string& ansiPath);
};

// The wide path in the ANSI code page, as v1.1.2's Bootstrap converted it before handing it
// to GetPrivateProfileStringA: WideCharToMultiByte with CP_ACP and its default best-fit
// mapping, logging a warning when the result does not round-trip.
std::string AnsiPath(const std::wstring& text);

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}  // namespace mcht::legacy
