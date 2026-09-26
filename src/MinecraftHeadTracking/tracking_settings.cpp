#include "tracking_settings.h"

#include <windows.h>

#include "cameraunlock/protocol/udp_receiver.h"

namespace mcht::tracking {

// The header spells these two out so it does not have to pull in windows.h or
// the receiver. Asserted here so the literals cannot drift from what they mean.
static_assert(kDefaultYawModeKey == VK_NEXT, "the default yaw-mode key must be Page Down");
static_assert(kDefaultTrackerPort == cameraunlock::UdpReceiver::kDefaultPort,
              "the default port must be the receiver's own default");

Settings FromLegacy(const legacy::Config& config) {
    Settings settings;
    settings.Sensitivity.yaw = config.yaw_sensitivity;
    settings.Sensitivity.pitch = config.pitch_sensitivity;
    settings.Sensitivity.roll = config.roll_sensitivity;
    settings.Sensitivity.invert_yaw = config.invert_yaw;
    settings.Sensitivity.invert_pitch = config.invert_pitch;
    settings.Sensitivity.invert_roll = config.invert_roll;
    settings.LocalSmoothing = config.local_smoothing;
    settings.RemoteSmoothing = config.remote_smoothing;
    settings.Position.sensitivity_x = config.position_sensitivity_x;
    settings.Position.sensitivity_y = config.position_sensitivity_y;
    settings.Position.sensitivity_z = config.position_sensitivity_z;
    settings.Position.limit_x = config.position_limit_x;
    settings.Position.limit_y = config.position_limit_y;
    settings.Position.limit_y_down = config.position_limit_y_down;
    settings.Position.limit_z = config.position_limit_z;
    settings.Position.limit_z_back = config.position_limit_z_back;
    settings.Position.invert_x = config.position_invert_x;
    settings.Position.invert_y = config.position_invert_y;
    settings.Position.invert_z = config.position_invert_z;
    settings.EnableOnStartup = config.enable_on_startup;
    settings.PositionEnabled = config.position_enabled;
    settings.WorldSpaceYaw = config.world_space_yaw;
    settings.YawModeKey = config.yaw_mode_key;
    settings.Port = config.port;
    return settings;
}

}  // namespace mcht::tracking
