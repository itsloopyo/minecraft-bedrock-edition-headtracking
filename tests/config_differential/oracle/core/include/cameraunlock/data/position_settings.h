#pragma once

#include "cameraunlock/math/smoothing_utils.h"

namespace cameraunlock {

/// Settings for positional tracking: per-axis sensitivity, limits, smoothing, and
/// inversion. Port of CameraUnlock.Core.Data.PositionSettings (C#). The connection
/// flag that selects between the two smoothing values is NOT here: it is runtime
/// state, not a user setting, so it lives on PositionProcessor as
/// is_remote_connection and a settings rebuild never has to re-supply it.
struct PositionSettings {
    float sensitivity_x = 1.0f;
    float sensitivity_y = 1.0f;
    float sensitivity_z = 1.0f;
    float limit_x = 0.30f;
    /// Maximum Y displacement upward.
    float limit_y = 0.20f;
    /// Maximum Y displacement DOWNWARD. Restricts how far below eye height the camera
    /// can travel. Was missing entirely here while C# has always had it, so an
    /// asymmetric vertical limit simply could not be expressed on this side and a
    /// ported config silently widened the downward budget into player-body clipping.
    float limit_y_down = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;
    float local_smoothing = static_cast<float>(math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(math::kDefaultRemoteSmoothing);
    bool invert_x = false;
    bool invert_y = false;
    bool invert_z = false;

    PositionSettings() = default;

    /// The full asymmetric form, mirroring the single C# constructor. Use Symmetric()
    /// for the symmetric case.
    ///
    /// C# is immune to a stale positional call because it has no implicit bool->float
    /// conversion, so the old 9-float shape is simply a compile error there. C++ is NOT:
    /// a stale (9 floats + 3 bools) call binds here with the first bool converted into
    /// remote_smoothing, silently producing limit_z_back = 0 (backward lean disabled),
    /// remote_smoothing = 0, and invert_z landing in invert_y - all at /W4 with no
    /// diagnostic. The deleted overloads below exist purely to turn that into an error;
    /// they are a better match for a bool argument than the float parameter is, so
    /// overload resolution picks them.
    PositionSettings(float sens_x, float sens_y, float sens_z,
                     float lim_x, float lim_y, float lim_y_down, float lim_z, float lim_z_back,
                     float local_smooth, float remote_smooth,
                     bool inv_x = false, bool inv_y = false, bool inv_z = false)
        : sensitivity_x(sens_x), sensitivity_y(sens_y), sensitivity_z(sens_z)
        , limit_x(lim_x), limit_y(lim_y), limit_y_down(lim_y_down)
        , limit_z(lim_z), limit_z_back(lim_z_back)
        , local_smoothing(local_smooth), remote_smoothing(remote_smooth)
        , invert_x(inv_x), invert_y(inv_y), invert_z(inv_z) {}

    /// Poison overloads for the pre-limit_y_down argument shape. Never defined: any call
    /// that matches one is a compile error naming this line, which is the diagnostic the
    /// arity change would otherwise not produce. Convert the call to Symmetric().
    PositionSettings(float, float, float, float, float, float, float, float, float,
                     bool) = delete;
    PositionSettings(float, float, float, float, float, float, float, float, float,
                     bool, bool) = delete;
    PositionSettings(float, float, float, float, float, float, float, float, float,
                     bool, bool, bool) = delete;

    /// Symmetric vertical limits: lim_y is mirrored into limit_y_down.
    static PositionSettings Symmetric(float sens_x, float sens_y, float sens_z,
                                      float lim_x, float lim_y, float lim_z, float lim_z_back,
                                      float local_smooth, float remote_smooth,
                                      bool inv_x = false, bool inv_y = false, bool inv_z = false) {
        return PositionSettings(sens_x, sens_y, sens_z, lim_x, lim_y, lim_y, lim_z, lim_z_back,
                                local_smooth, remote_smooth, inv_x, inv_y, inv_z);
    }

    static PositionSettings Default() {
        return Symmetric(1.0f, 1.0f, 1.0f, 0.30f, 0.20f, 0.40f, 0.10f,
                         static_cast<float>(math::kDefaultLocalSmoothing),
                         static_cast<float>(math::kDefaultRemoteSmoothing));
    }
};

}  // namespace cameraunlock
