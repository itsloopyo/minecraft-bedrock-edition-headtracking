#include "tracking_settings.h"

#include <windows.h>

#include "common/bounds.h"
#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/math/finite_utils.h"
#include "cameraunlock/protocol/udp_receiver.h"

namespace mcht::tracking {

// The header spells these two out so it does not have to pull in windows.h or
// the receiver. Asserted here so the literals cannot drift from what they mean.
static_assert(kDefaultYawModeKey == VK_NEXT, "the default yaw-mode key must be Page Down");
static_assert(kDefaultTrackerPort == cameraunlock::UdpReceiver::kDefaultPort,
              "the default port must be the receiver's own default");

namespace {

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

// Every reader below takes its fallback from the Settings it is filling, which
// arrives carrying this mod's defaults. Spelling a literal here instead is how
// the ini's stated default and the no-ini default drift apart - a bug this mod
// has already shipped once, with pitch and roll inversion.
float ReadClamped(const cameraunlock::IniReader& config, const char* section, const char* key,
                  float fallback, float lo, float hi) {
    const float raw = config.ReadFloat(section, key, fallback);
    const float value = cameraunlock::math::SanitizeFinite(raw, fallback, lo, hi);
    // Reported, not just substituted. The header promises as much, and without
    // it a value that does not apply reads exactly like one that does. The
    // comparison catches NaN too, which is never equal to itself.
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

// The single Smoothing key is gone from both sections it used to appear in.
// Said out loud, once per section that still has it: dropping it in silence
// leaves the player's tuned value reverting for no stated reason, with the dead
// line still sitting in their ini arguing that it should have worked.
//
// Deliberately NOT migrated into the new keys. The old value carried a hidden
// 0.15 floor, so the number in an existing ini does not mean what it used to:
// copying it across would hand a local player smoothing they never chose, and
// copying it into only one of the two would be a guess about which connection
// they were on.
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
        section, static_cast<double>(cameraunlock::math::kDefaultLocalSmoothing),
        static_cast<double>(cameraunlock::math::kDefaultRemoteSmoothing),
        static_cast<double>(cameraunlock::math::kDefaultRemoteSmoothing));
}

void ReadTracking(const cameraunlock::IniReader& config, Settings& settings) {
    cameraunlock::SensitivitySettings& sensitivity = settings.Sensitivity;
    sensitivity.yaw = ReadClamped(config, "Tracking", "YawSensitivity", sensitivity.yaw,
                                  kMinSensitivity, kMaxSensitivity);
    sensitivity.pitch = ReadClamped(config, "Tracking", "PitchSensitivity", sensitivity.pitch,
                                    kMinSensitivity, kMaxSensitivity);
    sensitivity.roll = ReadClamped(config, "Tracking", "RollSensitivity", sensitivity.roll,
                                   kMinSensitivity, kMaxSensitivity);
    // Pitch and roll default to inverted: the post-view transform applies them
    // opposite to the OpenTrack convention on this engine.
    sensitivity.invert_yaw = config.ReadBool("Tracking", "InvertYaw", sensitivity.invert_yaw);
    sensitivity.invert_pitch = config.ReadBool("Tracking", "InvertPitch", sensitivity.invert_pitch);
    sensitivity.invert_roll = config.ReadBool("Tracking", "InvertRoll", sensitivity.invert_roll);

    // Each takes its own default as the fallback. Sharing one would turn a
    // malformed RemoteSmoothing into the LOCAL default, handing a phone player
    // zero smoothing on raw network jitter and calling it their setting.
    settings.LocalSmoothing = ReadClamped(config, "Tracking", "LocalSmoothing",
                                          settings.LocalSmoothing, kMinSmoothing, kMaxSmoothing);
    settings.RemoteSmoothing = ReadClamped(config, "Tracking", "RemoteSmoothing",
                                           settings.RemoteSmoothing, kMinSmoothing, kMaxSmoothing);
    WarnRetiredSmoothingKey(config, "Tracking");
    settings.EnableOnStartup =
        config.ReadBool("Tracking", "EnableOnStartup", settings.EnableOnStartup);
    settings.WorldSpaceYaw = config.ReadBool("Tracking", "WorldSpaceYaw", settings.WorldSpaceYaw);
}

void ReadPosition(const cameraunlock::IniReader& config, Settings& settings) {
    cameraunlock::PositionSettings& position = settings.Position;
    position.sensitivity_x =
        ReadClamped(config, "Position", "SensitivityX", position.sensitivity_x,
                    kMinPositionSensitivity, kMaxPositionSensitivity);
    position.sensitivity_y =
        ReadClamped(config, "Position", "SensitivityY", position.sensitivity_y,
                    kMinPositionSensitivity, kMaxPositionSensitivity);
    position.sensitivity_z =
        ReadClamped(config, "Position", "SensitivityZ", position.sensitivity_z,
                    kMinPositionSensitivity, kMaxPositionSensitivity);
    position.limit_x = ReadClamped(config, "Position", "LimitX", position.limit_x,
                                   kMinPositionLimitMetres, kMaxPositionLimitMetres);
    position.limit_y = ReadClamped(config, "Position", "LimitY", position.limit_y,
                                   kMinPositionLimitMetres, kMaxPositionLimitMetres);
    position.limit_z = ReadClamped(config, "Position", "LimitZ", position.limit_z,
                                   kMinPositionLimitMetres, kMaxPositionLimitMetres);
    position.limit_z_back = ReadClamped(config, "Position", "LimitZBack", position.limit_z_back,
                                        kMinPositionLimitMetres, kMaxPositionLimitMetres);
    position.invert_x = config.ReadBool("Position", "InvertX", position.invert_x);
    position.invert_y = config.ReadBool("Position", "InvertY", position.invert_y);
    position.invert_z = config.ReadBool("Position", "InvertZ", position.invert_z);

    // Position has no smoothing key of its own any more: the two [Tracking]
    // values cover rotation and position together, so the two cannot be tuned
    // into disagreeing about how much lag the same head movement has.
    WarnRetiredSmoothingKey(config, "Position");

    settings.PositionEnabled = config.ReadBool("Position", "Enabled", settings.PositionEnabled);
}

void ReadHotkeys(const cameraunlock::IniReader& config, Settings& settings) {
    const int yawModeKey = config.ReadHex("Hotkeys", "YawModeKey", settings.YawModeKey);
    if (yawModeKey > 0 && yawModeKey <= kMaxVirtualKey) {
        settings.YawModeKey = yawModeKey;
        return;
    }
    cameraunlock::logging::Line(
        "[Hotkeys] YawModeKey 0x%X is not a virtual key code; using Page Down.", yawModeKey);
}

void ReadPort(const cameraunlock::IniReader& config, Settings& settings) {
    const int fallback = settings.Port;
    int port = fallback;
    if (config.ReadIntInRange("Tracking", "Port", port, mcht::bounds::kMinTrackerPort,
                              mcht::bounds::kMaxTrackerPort, fallback)) {
        settings.Port = port;
        return;
    }
    // Out of range does not fail loudly anywhere downstream: the cast to
    // uint16_t silently binds a different port from the one asked for, or an
    // ephemeral one for 0, and the tracker then sends into nothing. That is
    // indistinguishable in game from a tracker that is not running.
    cameraunlock::logging::Line("[Tracking] Port %d is outside %d-%d; using %d.", port,
                                mcht::bounds::kMinTrackerPort, mcht::bounds::kMaxTrackerPort,
                                fallback);
}

}  // namespace

std::optional<Settings> ReadSettings(const std::string& configPath) {
    cameraunlock::IniReader config;
    if (!config.Open(configPath)) {
        cameraunlock::logging::Line("No config at %s; using defaults.", configPath.c_str());
        return std::nullopt;
    }

    Settings settings;
    ReadTracking(config, settings);
    ReadPosition(config, settings);
    ReadHotkeys(config, settings);
    ReadPort(config, settings);
    return settings;
}

}  // namespace mcht::tracking
