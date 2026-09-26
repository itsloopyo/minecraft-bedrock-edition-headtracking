#pragma once

#include <cmath>

namespace cameraunlock {
namespace math {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

/// Normalizes an angle to the range -180 to +180 degrees.
/// Optimized fast path for typical head tracking angles.
inline double NormalizeAngle(double angle) {
    // Fast path for typical head tracking angles
    if (angle >= -180.0 && angle <= 180.0) {
        return angle;
    }

    // Wrap to -180..180 range
    angle = std::fmod(angle, 360.0);
    if (angle > 180.0) {
        angle -= 360.0;
    } else if (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

inline float NormalizeAngle(float angle) {
    if (angle >= -180.0f && angle <= 180.0f) {
        return angle;
    }
    angle = std::fmod(angle, 360.0f);
    if (angle > 180.0f) {
        angle -= 360.0f;
    } else if (angle < -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

/// Calculates the shortest angular distance from one angle to another.
inline double ShortestAngleDelta(double from, double to) {
    return NormalizeAngle(to - from);
}

/// Float overload, matching AngleUtils.ShortestAngleDelta(float, float) in C#. Without
/// it the float call sites promote to double and narrow back, which is both slower and
/// a source of last-bit divergence from the C# port.
inline float ShortestAngleDelta(float from, float to) {
    return NormalizeAngle(to - from);
}

/// Clamps a value between min and max.
template <typename T>
constexpr T Clamp(T value, T min_val, T max_val) {
    return value < min_val ? min_val : (value > max_val ? max_val : value);
}

/// Converts degrees to radians.
inline double ToRadians(double degrees) {
    return degrees * kDegToRad;
}

/// Converts radians to degrees.
inline double ToDegrees(double radians) {
    return radians * kRadToDeg;
}

}  // namespace math
}  // namespace cameraunlock
