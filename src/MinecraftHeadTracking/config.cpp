#include "config.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "axis_signs.h"
#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"
#include "legacy_config/legacy_config.h"

namespace mcht::config {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::KeyModifiers;

// A hotkey v1.1.2 registered as a key and a Ctrl+Shift chord on a letter, as one list: the key,
// when it is bound, then the chord.
std::string KeyList(const std::string& key, char letter) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(key);
    if (!parsed.ok()) throw std::logic_error("hotkey '" + key + "' does not parse: " + parsed.error);
    parsed.bindings.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, letter});
    return cameraunlock::input::FormatKeyBindings(parsed.bindings);
}

cfg::ImportResult Import(const cfg::LegacyInput& input, Config& out) {
    // v1.1.2 opened the file by the ANSI path it built itself, with best-fit mapping, not by the
    // one the owner derives, so the import builds it the same way.
    legacy::Config c;
    const legacy::ReadStatus read = c.Read(input.path, legacy::AnsiPath(input.path));

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;

    out.udp_port = c.port;
    out.enable_on_startup = c.enable_on_startup;
    out.world_space_yaw = c.world_space_yaw;

    // [Position] Enabled chose only the startup mode: the cycle key reached every mode either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                           : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    out.position.limit_x = c.position_limit_x;
    out.position.limit_y = c.position_limit_y;
    out.position.limit_y_down = c.position_limit_y_down;
    out.position.limit_z = c.position_limit_z;
    out.position.limit_z_back = c.position_limit_z_back;

    out.run_discovery = c.discovery_enabled;
    out.discovery_seconds = c.discovery_seconds;

    // Every sensitivity and the position inversions shipped at identity. Pitch and roll shipped
    // inverted, which was the axis conversion to Bedrock's post-view transform, and
    // TrackerToBedrockRotation now applies it. A value the player changed is dropped.
    const legacy::Config shipped;
    const cameraunlock::SensitivitySettings boundary = tracking::TrackerToBedrockRotation();
    cfg::LegacyPoseShaping(c.yaw_sensitivity, boundary.yaw, "Tracking", "YawSensitivity", shaping, dropped);
    cfg::LegacyPoseShaping(c.pitch_sensitivity, boundary.pitch, "Tracking", "PitchSensitivity", shaping, dropped);
    cfg::LegacyPoseShaping(c.roll_sensitivity, boundary.roll, "Tracking", "RollSensitivity", shaping, dropped);
    cfg::LegacyPoseShaping(c.invert_yaw, boundary.invert_yaw, "Tracking", "InvertYaw", shaping, dropped);
    cfg::LegacyPoseShaping(c.invert_pitch, boundary.invert_pitch, "Tracking", "InvertPitch", shaping, dropped);
    cfg::LegacyPoseShaping(c.invert_roll, boundary.invert_roll, "Tracking", "InvertRoll", shaping, dropped);
    cfg::LegacyPoseShaping(c.position_sensitivity_x, shipped.position_sensitivity_x, "Position", "SensitivityX",
                           shaping, dropped);
    cfg::LegacyPoseShaping(c.position_sensitivity_y, shipped.position_sensitivity_y, "Position", "SensitivityY",
                           shaping, dropped);
    cfg::LegacyPoseShaping(c.position_sensitivity_z, shipped.position_sensitivity_z, "Position", "SensitivityZ",
                           shaping, dropped);
    cfg::LegacyPoseShaping(c.position_invert_x, shipped.position_invert_x, "Position", "InvertX", shaping, dropped);
    cfg::LegacyPoseShaping(c.position_invert_y, shipped.position_invert_y, "Position", "InvertY", shaping, dropped);
    cfg::LegacyPoseShaping(c.position_invert_z, shipped.position_invert_z, "Position", "InvertZ", shaping, dropped);

    // v1.1.2 bound End and Page Up in code, and every one of its three actions to a Ctrl+Shift
    // letter; only the yaw mode's key was read from the file.
    out.toggle_key_name = KeyList("End", 'Y');
    out.cycle_tracking_mode_key_name = KeyList("PageUp", 'G');
    out.yaw_mode_key_name =
        KeyList(cfg::LegacyVirtualKeyToBindings(c.yaw_mode_key, "Hotkeys", "YawModeKey", dropped), 'H');

    return read == legacy::ReadStatus::Absent ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
                                              : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

}  // namespace

cfg::ConfigTable<Config> MakeConfigTable() {
    using cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled, Concept::PositionLimitX,
         Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
         Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Local("Discovery", "RunDiscovery", &Config::run_discovery, cfg::BoolCodec(),
                "Developer tool. true: instead of head tracking, drive the camera through one axis at a\n"
                "time and name each phase in MinecraftHeadTracking.log, to measure the axis mapping of a\n"
                "new Minecraft build. It obeys the same PvP rules as head tracking. Needs you in a world.")
        .Local("Discovery", "DurationSeconds", &Config::discovery_seconds, cfg::IntCodec<int>(),
               "How long each discovery run lasts, in seconds. A value below 1 runs for 1, and one above\n"
               "3600 for 3600.");
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cfg::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder, cfg::DefaultsFile defaults) {
    const auto wide = [](const char* name) { return std::wstring(name, name + std::char_traits<char>::length(name)); };
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + wide(kConfigFileName);
    options.legacy_path = folder + wide(kLegacyConfigFileName);
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace mcht::config
