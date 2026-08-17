#pragma once

#include <cmath>

#include "cameraunlock/math/vec3.h"

namespace mcht::tracking {

// Below these the pose is indistinguishable from neutral, so the ease-out is
// over. Both are needed: in position-only mode the rotation is already zero,
// so a rotation-only test would end the ease on its first frame and cut the
// lean back instead of easing it.
constexpr float kSettledDegrees = 0.01f;
constexpr float kSettledMetres = 0.0005f;

// The last pose actually applied.
//
// A tracker that stops sending holds this rather than snapping the view to
// centre: phone trackers drop out whenever they lose the face, and a snap to
// neutral every time is far worse than freezing where you were looking.
//
// Switching tracking off decays it instead, so the view returns to neutral
// without a jump. The fairness gate deliberately does not go through here: if
// PvP starts, tracking stops on the same frame.
struct HeldPose {
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float Roll = 0.0f;
    cameraunlock::math::Vec3 Offset;
    bool Valid = false;

    void Set(float yaw, float pitch, float roll, const cameraunlock::math::Vec3& offset) {
        Yaw = yaw;
        Pitch = pitch;
        Roll = roll;
        Offset = offset;
        Valid = true;
    }

    // `keep` is the fraction of the pose surviving this frame, so 1 holds and
    // 0 cuts straight to neutral.
    void Decay(float keep) {
        Yaw *= keep;
        Pitch *= keep;
        Roll *= keep;
        Offset = Offset * keep;
    }

    bool IsSettled() const {
        return std::fabs(Yaw) < kSettledDegrees && std::fabs(Pitch) < kSettledDegrees &&
               std::fabs(Roll) < kSettledDegrees && std::fabs(Offset.x) < kSettledMetres &&
               std::fabs(Offset.y) < kSettledMetres && std::fabs(Offset.z) < kSettledMetres;
    }
};

}  // namespace mcht::tracking
