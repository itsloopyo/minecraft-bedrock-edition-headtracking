#pragma once

#include "cameraunlock/math/angle_utils.h"

#include <cmath>

namespace cameraunlock {
namespace math {

/// Default smoothing for connections originating on the machine running the mod
/// (loopback / same-host sender). Must match DefaultLocalSmoothing in C#
/// SmoothingUtils.cs. Zero: a same-machine tracker is already stable, so
/// smoothing only buys latency.
constexpr double kDefaultLocalSmoothing = 0.0;

/// Default smoothing for connections from a remote network device. Must match
/// DefaultRemoteSmoothing in C# SmoothingUtils.cs. 0.15 maps to speed 42.5, a
/// flat 23.5 ms time constant at every frame rate; only the per-frame factor
/// varies with dt (0.51 at 60fps, 0.16 at 240fps). That covers the jitter a
/// WiFi/phone tracker adds over the network.
constexpr double kDefaultRemoteSmoothing = 0.15;

/// Maximum interpolation speed, used at smoothing=0. This is the frame
/// interpolation floor: fast enough to be responsive, slow enough to hide
/// discrete tracker sample boundaries at high refresh rates. Speed 50 is a flat
/// 20 ms time constant (1/50) and so ~20 ms of average lag, identical at every
/// frame rate; only the per-frame factor varies with dt (0.81 at 30fps, 0.57 at
/// 60fps, 0.19 at 240fps).
constexpr double kFrameInterpolationSpeed = 50.0;

/// Minimum speed at maximum user smoothing (smoothing=1). Speed 0.1 is a flat
/// 10 second time constant (1/0.1) at every frame rate. Quote the time constant
/// rather than a settling time: 5 seconds reaches only ~39% convergence.
constexpr double kMaxSmoothingSpeed = 0.1;

/// Linear interpolation.
inline double Lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Calculates the smoothing interpolation factor for the current frame.
/// Uses frame-rate independent exponential smoothing: t = 1 - exp(-speed * dt).
/// The speed is clamped to [kMaxSmoothingSpeed, kFrameInterpolationSpeed], so the
/// factor is always strictly between 0 and 1 and frame interpolation stays active
/// whatever the smoothing input is. There is deliberately no snap branch at low
/// smoothing: interpolation is gated on receiving data, never on the smoothing
/// value, and with the local default at 0 a snap would leave every local user with
/// raw stepped output on a high-refresh display.
/// @param smoothing Smoothing factor 0-1. 0=frame interpolation only, 1=very slow.
/// @param delta_time Time since last frame in seconds.
/// @return Interpolation factor to use with Lerp/Slerp, always in (0, 1).
inline double CalculateSmoothingFactor(double smoothing, double delta_time) {
    double smoothing_speed =
        kFrameInterpolationSpeed - (kFrameInterpolationSpeed - kMaxSmoothingSpeed) * smoothing;
    if (smoothing_speed > kFrameInterpolationSpeed) smoothing_speed = kFrameInterpolationSpeed;
    if (smoothing_speed < kMaxSmoothingSpeed) smoothing_speed = kMaxSmoothingSpeed;
    return 1.0 - std::exp(-smoothing_speed * delta_time);
}

inline float CalculateSmoothingFactor(float smoothing, float delta_time) {
    constexpr float kSpeedMax = static_cast<float>(kFrameInterpolationSpeed);
    constexpr float kSpeedMin = static_cast<float>(kMaxSmoothingSpeed);
    float smoothing_speed = kSpeedMax - (kSpeedMax - kSpeedMin) * smoothing;
    if (smoothing_speed > kSpeedMax) smoothing_speed = kSpeedMax;
    if (smoothing_speed < kSpeedMin) smoothing_speed = kSpeedMin;
    return 1.0f - std::exp(-smoothing_speed * delta_time);
}

/// Applies smoothing to a single value.
inline double Smooth(double current, double target, double smoothing, double delta_time) {
    double t = CalculateSmoothingFactor(smoothing, delta_time);
    return current + (target - current) * t;
}

inline float Smooth(float current, float target, float smoothing, float delta_time) {
    float t = CalculateSmoothingFactor(smoothing, delta_time);
    return current + (target - current) * t;
}

/// Smooths an angle in DEGREES, taking the shortest path around the +/-180 seam.
/// Smooth() lerps the raw scalar difference, so smoothing 179.5 toward -179.5 - a 1
/// degree head movement across the seam - travels -359 degrees and swings the camera
/// the long way round. Matches SmoothingUtils.SmoothAngle in C#.
inline float SmoothAngle(float current, float target, float smoothing, float delta_time) {
    float t = CalculateSmoothingFactor(smoothing, delta_time);
    return NormalizeAngle(current + ShortestAngleDelta(current, target) * t);
}

/// Selects the smoothing value for the current connection. This is the only path
/// by which a smoothing value reaches a processor - no caller picks it itself.
/// @param local_smoothing Smoothing configured for same-machine (loopback) senders.
/// @param remote_smoothing Smoothing configured for remote network senders.
/// @param is_remote_connection True when the packet source is a remote device.
/// @return The configured value for this connection, unmodified.
inline double GetEffectiveSmoothing(double local_smoothing, double remote_smoothing,
                                    bool is_remote_connection) {
    return is_remote_connection ? remote_smoothing : local_smoothing;
}

/// Exponentially smoothed scalar that snaps to the first sample instead of
/// blending up from zero. Replaces the per-call-site "static value + init
/// flag + Lerp" boilerplate around screen-space projection values.
class SmoothedFloat {
public:
    /// Feed the next raw sample and return the smoothed value.
    float Update(float target, float smoothing, float delta_time) {
        if (!m_initialized) {
            m_value = target;
            m_initialized = true;
            return m_value;
        }
        m_value = Smooth(m_value, target, smoothing, delta_time);
        return m_value;
    }

    float Get() const { return m_value; }
    bool IsInitialized() const { return m_initialized; }

    /// Forget the current value; the next Update snaps to its sample.
    void Reset() {
        m_value = 0.0f;
        m_initialized = false;
    }

private:
    float m_value = 0.0f;
    bool m_initialized = false;
};

}  // namespace math
}  // namespace cameraunlock
