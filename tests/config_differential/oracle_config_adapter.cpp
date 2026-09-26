// Compiled into the config oracle library only, with `cameraunlock` and `mcht` renamed, so the
// dllmain.cpp included here is v1.1.2's (oracle/src/MinecraftHeadTracking/dllmain.cpp) and the
// core under it is 1fd2956's. Its Bootstrap is in an unnamed namespace, which is why the file
// is included rather than linked.
#include "MinecraftHeadTracking/dllmain.cpp"

#include "oracle_adapter.h"

namespace {

bool g_discovery = false;
int g_discoverySeconds = 0;
std::string g_startPath;

mcht_oracle_view::OracleSettings View(const mcht::tracking::Settings& s) {
    mcht_oracle_view::OracleSettings o{};
    o.yaw_sensitivity = s.Sensitivity.yaw;
    o.pitch_sensitivity = s.Sensitivity.pitch;
    o.roll_sensitivity = s.Sensitivity.roll;
    o.invert_yaw = s.Sensitivity.invert_yaw;
    o.invert_pitch = s.Sensitivity.invert_pitch;
    o.invert_roll = s.Sensitivity.invert_roll;
    o.local_smoothing = s.LocalSmoothing;
    o.remote_smoothing = s.RemoteSmoothing;
    o.position_sensitivity_x = s.Position.sensitivity_x;
    o.position_sensitivity_y = s.Position.sensitivity_y;
    o.position_sensitivity_z = s.Position.sensitivity_z;
    o.position_limit_x = s.Position.limit_x;
    o.position_limit_y = s.Position.limit_y;
    o.position_limit_y_down = s.Position.limit_y_down;
    o.position_limit_z = s.Position.limit_z;
    o.position_limit_z_back = s.Position.limit_z_back;
    o.position_invert_x = s.Position.invert_x;
    o.position_invert_y = s.Position.invert_y;
    o.position_invert_z = s.Position.invert_z;
    o.enable_on_startup = s.EnableOnStartup;
    o.position_enabled = s.PositionEnabled;
    o.world_space_yaw = s.WorldSpaceYaw;
    o.yaw_mode_key = s.YawModeKey;
    o.port = s.Port;
    return o;
}

}  // namespace

namespace mcht::builds {
SelectResult SelectProfile() { return SelectResult::Matched; }
}  // namespace mcht::builds

namespace mcht::discovery {
bool InstallCalibration(int durationSeconds) {
    g_discovery = true;
    g_discoverySeconds = durationSeconds;
    return true;
}
}  // namespace mcht::discovery

namespace mcht::tracking {
// v1.1.2's head_tracking.cpp Start read its settings on its first line (line 338) and handed
// them to ApplySettings; the rest of it is the game.
bool Start(const std::string& configPath) {
    g_startPath = configPath;
    return true;
}
}  // namespace mcht::tracking

namespace mcht_oracle_view {

OracleResult RunOracle(const std::wstring& folder) {
    g_discovery = false;
    g_discoverySeconds = 0;
    g_startPath.clear();
    mcht::paths::OracleModuleDirectory() = folder;
    Bootstrap(nullptr);
    cameraunlock::logging::Close();

    OracleResult result{};
    result.discovery = g_discovery;
    result.discovery_seconds = g_discoverySeconds;
    const std::string path = g_discovery ? AnsiPath(folder + L"MinecraftHeadTracking.ini") : g_startPath;
    result.settings = View(mcht::tracking::ReadSettings(path).value_or(mcht::tracking::Settings{}));
    return result;
}

}  // namespace mcht_oracle_view
