#pragma once

#include "camera_hook.h"
#include "cameraunlock/math/vec3.h"
#include "matrix4.h"

// How head movement becomes a matrix, start to finish, with no state and no
// game memory involved.
//
// BuildPose turns processed tracker angles into a Pose; the two Compose
// functions turn that Pose plus the camera's own view transform into the
// bytes the hook writes. Keeping the whole chain pure is what makes the
// composition order testable: every comment below is about an ordering that is
// identical on a single axis and wrong on a combined pose, which is precisely
// the class of mistake that only a test catches.
namespace mcht::camera {

// Maps the tracker's axes onto the camera's.
//
// X was derived by measurement (synthetic packets holding one axis at a time,
// scene displacement measured off the rendered frame). Y and Z were corrected
// afterwards against a real tracker: the synthetic tests could only assume an
// OpenTrack axis convention, and a live tracker settles it. In-game behaviour
// wins over the bench measurement here.
//
// [Position] InvertX/InvertY/InvertZ flip these further without a rebuild, for
// trackers that disagree.
constexpr float kPositionScaleX = -1.0f;
constexpr float kPositionScaleY = 1.0f;
constexpr float kPositionScaleZ = 1.0f;

// Builds the frame's Pose from angles in degrees and a position offset in
// metres.
//
// In world-space mode the yaw is not composed here at all. It has to turn
// about the world's up axis, and where that axis lies in view space depends on
// where the mouse has the camera pointed, which only the hook can see. It
// travels alongside the matrices as an angle instead.
inline void BuildPose(float yaw, float pitch, float roll,
                      const cameraunlock::math::Vec3& offset, bool worldSpaceYaw, Pose& pose) {
    pose.WorldSpaceYaw = worldSpaceYaw;
    pose.WorldYawDegrees = worldSpaceYaw ? yaw : 0.0f;

    // The catalogue's order is yaw outermost, roll innermost (Quat4 builds the
    // same thing as qy*qx*qz, and TrackingProcessor round-trips the pose
    // through exactly that, so these angles are YXZ by definition).
    //
    // Row vectors reverse the factors: a transform applied later multiplies on
    // the right, so yaw outermost means yaw LAST. Writing it in the column
    // order instead composes ZXY, which is identical on any single axis and
    // wrong on every combined pose - yaw plus pitch picks up a parasitic roll
    // of atan2(sin p * sin y, cos p), 16 degrees at 30/30.
    float yawM[mcht::math::kMatrixFloats];
    float pitchM[mcht::math::kMatrixFloats];
    float rollM[mcht::math::kMatrixFloats];
    float rollPitch[mcht::math::kMatrixFloats];
    mcht::math::AxisRotation(mcht::math::kAxisYaw, worldSpaceYaw ? 0.0f : yaw, yawM);
    mcht::math::AxisRotation(mcht::math::kAxisPitch, pitch, pitchM);
    mcht::math::AxisRotation(mcht::math::kAxisRoll, roll, rollM);
    mcht::math::Multiply(rollM, pitchM, rollPitch);
    mcht::math::Multiply(rollPitch, yawM, pose.Rotation);

    // The hook applies this before the rotation, so the offset follows the
    // body rather than the head-rotated view. Negated because this translates
    // the VIEW, not the camera: shifting the world one way moves the viewpoint
    // the other.
    mcht::math::Translation(-offset.x * kPositionScaleX, -offset.y * kPositionScaleY,
                            -offset.z * kPositionScaleZ, pose.Offset);
}

// `saved` is the camera's own post-view transform as found this frame.
// `transformOut` receives what replaces it; `deltaOut` receives the transform
// from the clean view to the tracked one, which is what the crosshair projects
// through. All three must be distinct buffers: Multiply writes its output as it
// reads its inputs, so overlapping any two of them corrupts the result.

// Camera-local yaw: the whole head movement applies outermost, in view space,
// which is the doctrine's headRot * gameViewMatrix written for row vectors.
inline void ComposeCameraLocal(const Pose& pose, const float* saved, float* transformOut,
                               float* deltaOut) {
    // The head movement IS the clean-to-tracked delta in this mode, so it is
    // built straight into the caller's buffer rather than copied there after.
    mcht::math::Multiply(pose.Offset, pose.Rotation, deltaOut);
    mcht::math::Multiply(saved, deltaOut, transformOut);
}

// World-space yaw: offset * worldYaw * saved * rotation.
//
// The yaw goes in AHEAD of the game's own view transform, so it turns the
// world before the camera sees it - which is what makes it a turn about the
// world's up axis rather than the camera's. Pitch and roll stay behind it and
// so stay camera-local. That is the catalogue's decomposed form,
// worldYaw * baseRotation * localPitchRoll, written for row vectors.
//
// The offset goes in ahead of the yaw for two reasons at once: leaning then
// moves you along your body's axes rather than your head's, and the yaw turns
// about where your head actually is rather than about where it would be
// without the lean.
inline void ComposeWorldYaw(const Pose& pose, const float* worldUp, const float* saved,
                            float* transformOut, float* deltaOut) {
    float yawM[mcht::math::kMatrixFloats];
    mcht::math::AxisAngleRotation(worldUp, pose.WorldYawDegrees, yawM);
    float offsetYaw[mcht::math::kMatrixFloats];
    mcht::math::Multiply(pose.Offset, yawM, offsetYaw);
    float withView[mcht::math::kMatrixFloats];
    mcht::math::Multiply(offsetYaw, saved, withView);
    mcht::math::Multiply(withView, pose.Rotation, transformOut);

    // Exactly the clean-to-tracked delta whenever the engine left its own
    // post-view transform at identity, which is all but the frames it is
    // shaking the camera on. Dividing that transform back out per frame to
    // cover those too would mean inverting it, for a correction smaller than
    // the assumed aim distance the crosshair is already projected at.
    mcht::math::Multiply(offsetYaw, pose.Rotation, deltaOut);
}

}  // namespace mcht::camera
