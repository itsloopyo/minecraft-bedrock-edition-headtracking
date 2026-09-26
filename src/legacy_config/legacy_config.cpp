#include "legacy_config/legacy_config.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace mcht::legacy {

namespace {

// cameraunlock::math::kDefaultLocalSmoothing and kDefaultRemoteSmoothing at core 1fd2956.
constexpr double kDefaultLocalSmoothing = 0.0;
constexpr double kDefaultRemoteSmoothing = 0.15;

// mcht::bounds::kMinTrackerPort and kMaxTrackerPort at v1.1.2.
constexpr int kMinTrackerPort = 1024;
constexpr int kMaxTrackerPort = 65535;

// The highest virtual key code Windows defines. Anything past it would be
// registered and then never fire, which looks exactly like the toggle being
// broken.
constexpr int kMaxVirtualKey = 0xFE;

// Accepted ranges for the numbers the ini can set. Deliberately wider than any
// useful setting: they are here so a typo or a NaN cannot reach a matrix the
// mod copies into the game, not to police a preference.
constexpr float kMinSensitivity = 0.01f;
constexpr float kMaxSensitivity = 10.0f;
constexpr float kMinPositionSensitivity = 0.0f;
constexpr float kMaxPositionSensitivity = 5.0f;
constexpr float kMinPositionLimitMetres = 0.01f;
constexpr float kMaxPositionLimitMetres = 0.5f;
constexpr float kMinSmoothing = 0.0f;
constexpr float kMaxSmoothing = 1.0f;

// cameraunlock::math::SanitizeFinite at core 1fd2956.
float SanitizeFinite(float value, float fallback, float lo, float hi) {
    return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
}

float ReadClamped(const cameraunlock::IniReader& config, const char* section, const char* key,
                  float fallback, float lo, float hi) {
    const float raw = config.ReadFloat(section, key, fallback);
    const float value = SanitizeFinite(raw, fallback, lo, hi);
    // The comparison catches NaN too, which is never equal to itself.
    if (!(value == raw)) {
        cameraunlock::logging::Line(
            "[%s] %s is not a usable value; using %.3f. Every float here is validated for "
            "finiteness and range, which is not the same as being given a minimum.",
            section, key, value);
    }
    return value;
}

// IniReader has no key-presence query, so an absent key is read as an empty
// string. A key present but empty carried no setting either, so conflating the
// two costs nothing.
bool HasKey(const cameraunlock::IniReader& config, const char* section, const char* key) {
    return !config.ReadString(section, key, "").empty();
}

// The single Smoothing key was retired before v1.1.2 and is not migrated: the
// old value carried a hidden 0.15 floor, so the number in an existing ini does
// not mean what it used to.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& config, const char* section) {
    if (!HasKey(config, section, "Smoothing")) {
        return;
    }
    cameraunlock::logging::Line(
        "[%s] Smoothing has been retired and is IGNORED. Smoothing is now two keys in "
        "[Tracking]: LocalSmoothing (default %.2f, applies to a tracker on this machine) and "
        "RemoteSmoothing (default %.2f, applies to a tracker on the network). The old value is "
        "not migrated because the semantics changed - it carried a hidden %.2f floor that no "
        "longer exists. Set the two new keys.",
        section, kDefaultLocalSmoothing, kDefaultRemoteSmoothing, kDefaultRemoteSmoothing);
}

void ReadTracking(const cameraunlock::IniReader& config, Config& c) {
    c.yaw_sensitivity = ReadClamped(config, "Tracking", "YawSensitivity", c.yaw_sensitivity,
                                    kMinSensitivity, kMaxSensitivity);
    c.pitch_sensitivity = ReadClamped(config, "Tracking", "PitchSensitivity", c.pitch_sensitivity,
                                      kMinSensitivity, kMaxSensitivity);
    c.roll_sensitivity = ReadClamped(config, "Tracking", "RollSensitivity", c.roll_sensitivity,
                                     kMinSensitivity, kMaxSensitivity);
    c.invert_yaw = config.ReadBool("Tracking", "InvertYaw", c.invert_yaw);
    c.invert_pitch = config.ReadBool("Tracking", "InvertPitch", c.invert_pitch);
    c.invert_roll = config.ReadBool("Tracking", "InvertRoll", c.invert_roll);

    c.local_smoothing = ReadClamped(config, "Tracking", "LocalSmoothing", c.local_smoothing,
                                    kMinSmoothing, kMaxSmoothing);
    c.remote_smoothing = ReadClamped(config, "Tracking", "RemoteSmoothing", c.remote_smoothing,
                                     kMinSmoothing, kMaxSmoothing);
    WarnRetiredSmoothingKey(config, "Tracking");
    c.enable_on_startup = config.ReadBool("Tracking", "EnableOnStartup", c.enable_on_startup);
    c.world_space_yaw = config.ReadBool("Tracking", "WorldSpaceYaw", c.world_space_yaw);
}

void ReadPosition(const cameraunlock::IniReader& config, Config& c) {
    c.position_sensitivity_x = ReadClamped(config, "Position", "SensitivityX", c.position_sensitivity_x,
                                           kMinPositionSensitivity, kMaxPositionSensitivity);
    c.position_sensitivity_y = ReadClamped(config, "Position", "SensitivityY", c.position_sensitivity_y,
                                           kMinPositionSensitivity, kMaxPositionSensitivity);
    c.position_sensitivity_z = ReadClamped(config, "Position", "SensitivityZ", c.position_sensitivity_z,
                                           kMinPositionSensitivity, kMaxPositionSensitivity);
    c.position_limit_x = ReadClamped(config, "Position", "LimitX", c.position_limit_x,
                                     kMinPositionLimitMetres, kMaxPositionLimitMetres);
    c.position_limit_y = ReadClamped(config, "Position", "LimitY", c.position_limit_y,
                                     kMinPositionLimitMetres, kMaxPositionLimitMetres);
    c.position_limit_z = ReadClamped(config, "Position", "LimitZ", c.position_limit_z,
                                     kMinPositionLimitMetres, kMaxPositionLimitMetres);
    c.position_limit_z_back = ReadClamped(config, "Position", "LimitZBack", c.position_limit_z_back,
                                          kMinPositionLimitMetres, kMaxPositionLimitMetres);
    c.position_invert_x = config.ReadBool("Position", "InvertX", c.position_invert_x);
    c.position_invert_y = config.ReadBool("Position", "InvertY", c.position_invert_y);
    c.position_invert_z = config.ReadBool("Position", "InvertZ", c.position_invert_z);

    WarnRetiredSmoothingKey(config, "Position");

    c.position_enabled = config.ReadBool("Position", "Enabled", c.position_enabled);
}

void ReadHotkeys(const cameraunlock::IniReader& config, Config& c) {
    const int yawModeKey = config.ReadHex("Hotkeys", "YawModeKey", c.yaw_mode_key);
    if (yawModeKey > 0 && yawModeKey <= kMaxVirtualKey) {
        c.yaw_mode_key = yawModeKey;
        return;
    }
    cameraunlock::logging::Line(
        "[Hotkeys] YawModeKey 0x%X is not a virtual key code; using Page Down.", yawModeKey);
}

void ReadPort(const cameraunlock::IniReader& config, Config& c) {
    const int fallback = c.port;
    int port = fallback;
    if (config.ReadIntInRange("Tracking", "Port", port, kMinTrackerPort, kMaxTrackerPort, fallback)) {
        c.port = port;
        return;
    }
    cameraunlock::logging::Line("[Tracking] Port %d is outside %d-%d; using %d.", port,
                                kMinTrackerPort, kMaxTrackerPort, fallback);
}

// Windows INI parsing is byte-oriented and a UTF-8 BOM ends up glued to the
// first section header, so every setting in that section silently reverts to
// its default. v1.1.2's Bootstrap said so before it read anything.
void WarnIfByteOrderMarked(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    unsigned char bom[3] = {0, 0, 0};
    DWORD read = 0;
    ReadFile(file, bom, sizeof(bom), &read, nullptr);
    CloseHandle(file);

    if (read == sizeof(bom) && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        cameraunlock::logging::Line(
            "WARNING: MinecraftHeadTracking.ini starts with a UTF-8 byte order mark, so Windows "
            "cannot read any setting in it and all defaults apply. Re-save it as plain ANSI or "
            "UTF-8 without BOM, or delete it and let it be recreated.");
    }
}

void ReadDiscovery(const cameraunlock::IniReader& config, Config& c) {
    c.discovery_enabled = config.ReadBool("Discovery", "Enabled", c.discovery_enabled);
    c.discovery_seconds = config.ReadInt("Discovery", "DurationSeconds", c.discovery_seconds);
}

}  // namespace

std::string AnsiPath(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);

    const int back = MultiByteToWideChar(CP_ACP, 0, out.c_str(), size, nullptr, 0);
    std::wstring verify(static_cast<std::size_t>(back > 0 ? back : 0), L'\0');
    if (back <= 0 ||
        MultiByteToWideChar(CP_ACP, 0, out.c_str(), size, verify.data(), back) <= 0 ||
        verify != text) {
        cameraunlock::logging::Line(
            "WARNING: %S contains characters this system's ANSI code page cannot represent, so "
            "the settings file cannot be read or written there and built-in defaults apply.",
            text.c_str());
    }
    return out;
}

ReadStatus Config::Read(const std::wstring& path, const std::string& ansiPath) {
    WarnIfByteOrderMarked(path);

    cameraunlock::IniReader config;
    if (!config.Open(ansiPath)) {
        cameraunlock::logging::Line("No config at %s; using defaults.", ansiPath.c_str());
        return ReadStatus::Absent;
    }

    ReadDiscovery(config, *this);
    ReadTracking(config, *this);
    ReadPosition(config, *this);
    ReadHotkeys(config, *this);
    ReadPort(config, *this);
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"Discovery", "Enabled"},
        {"Discovery", "DurationSeconds"},
        {"Tracking", "YawSensitivity"},
        {"Tracking", "PitchSensitivity"},
        {"Tracking", "RollSensitivity"},
        {"Tracking", "InvertYaw"},
        {"Tracking", "InvertPitch"},
        {"Tracking", "InvertRoll"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"Tracking", "Smoothing"},
        {"Tracking", "EnableOnStartup"},
        {"Tracking", "WorldSpaceYaw"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "Smoothing"},
        {"Position", "Enabled"},
        {"Hotkeys", "YawModeKey"},
        {"Tracking", "Port"},
    };
}

}  // namespace mcht::legacy
