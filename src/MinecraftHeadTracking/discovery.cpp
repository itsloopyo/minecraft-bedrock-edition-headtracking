#include "discovery.h"

#include <windows.h>

#include <cstdint>

#include "camera_hook.h"
#include "common/bounds.h"
#include "cameraunlock/logging/file_log.h"
#include "matrix4.h"

namespace mcht::discovery {
namespace {

// Long enough to take a screenshot in, short enough to sit through the whole
// sweep.
constexpr int kPhaseMs = 3000;

enum class Driven { Neutral, Yaw, Pitch, Roll, X, Y, Z };

struct Phase {
    const char* Name;
    Driven Axis;
    float Amount;  // Degrees for a rotation, metres for a translation.
};

// Hold one axis at a large constant offset at a time, separated by neutral
// gaps. Which engine axis each tracker axis maps to, and with which sign, is
// not something to guess: the view matrix is reached through an inverse and a
// pre-transform, either of which can flip a sense.
constexpr Phase kPhases[] = {
    {"neutral", Driven::Neutral, 0.0f}, {"YAW +25", Driven::Yaw, 25.0f},
    {"neutral", Driven::Neutral, 0.0f}, {"PITCH +25", Driven::Pitch, 25.0f},
    {"neutral", Driven::Neutral, 0.0f}, {"ROLL +25", Driven::Roll, 25.0f},
    {"neutral", Driven::Neutral, 0.0f}, {"X +0.5", Driven::X, 0.5f},
    {"neutral", Driven::Neutral, 0.0f}, {"Y +0.5", Driven::Y, 0.5f},
    {"neutral", Driven::Neutral, 0.0f}, {"Z +0.5", Driven::Z, 0.5f},
};
constexpr int kPhaseCount = static_cast<int>(sizeof(kPhases) / sizeof(kPhases[0]));

// Camera-local throughout: each phase is meant to drive exactly one engine
// axis, and world-space yaw would mix the game camera's pitch into it.
void DrivePhase(const Phase& phase, mcht::camera::Pose& pose) {
    pose.WorldSpaceYaw = false;
    pose.WorldYawDegrees = 0.0f;
    mcht::math::Identity(pose.Offset);
    mcht::math::Identity(pose.Rotation);

    switch (phase.Axis) {
        case Driven::Yaw:
            mcht::math::AxisRotation(mcht::math::kAxisYaw, phase.Amount, pose.Rotation);
            break;
        case Driven::Pitch:
            mcht::math::AxisRotation(mcht::math::kAxisPitch, phase.Amount, pose.Rotation);
            break;
        case Driven::Roll:
            mcht::math::AxisRotation(mcht::math::kAxisRoll, phase.Amount, pose.Rotation);
            break;
        case Driven::X:
            mcht::math::Translation(phase.Amount, 0.0f, 0.0f, pose.Offset);
            break;
        case Driven::Y:
            mcht::math::Translation(0.0f, phase.Amount, 0.0f, pose.Offset);
            break;
        case Driven::Z:
            mcht::math::Translation(0.0f, 0.0f, phase.Amount, pose.Offset);
            break;
        case Driven::Neutral:
            break;
    }
}

std::uint64_t g_startedAt = 0;
int g_durationMs = 0;
int g_phase = -1;
bool g_finished = false;

bool Provide(mcht::camera::Pose& pose) {
    const std::uint64_t now = GetTickCount64();
    if (g_startedAt == 0) {
        g_startedAt = now;
        cameraunlock::logging::Line(
            "[calib] camera reached; %ds of phases follow. Stand still and watch.",
            g_durationMs / 1000);
    }

    const int elapsed = static_cast<int>(now - g_startedAt);
    if (elapsed > g_durationMs) {
        if (!g_finished) {
            g_finished = true;
            cameraunlock::logging::Line("[calib] finished; the camera is untouched again.");
        }
        return false;
    }

    const int index = (elapsed / kPhaseMs) % kPhaseCount;
    const Phase& phase = kPhases[index];
    if (index != g_phase) {
        g_phase = index;
        cameraunlock::logging::Line("[calib] phase=%s", phase.Name);
    }

    if (phase.Axis == Driven::Neutral) {
        return false;
    }
    DrivePhase(phase, pose);
    return true;
}

}  // namespace

bool InstallCalibration(int durationSeconds) {
    // Straight off the ini. Unclamped, the x1000 overflows on a large value
    // and a negative one ends the run before its first phase.
    g_durationMs = mcht::bounds::ClampDiscoverySeconds(durationSeconds) * 1000;
    if (!mcht::camera::Install(&Provide)) {
        return false;
    }
    cameraunlock::logging::Line(
        "Calibration mode: the camera will be driven through one axis at a time. "
        "Head tracking input is ignored while this runs.");
    return true;
}

}  // namespace mcht::discovery
