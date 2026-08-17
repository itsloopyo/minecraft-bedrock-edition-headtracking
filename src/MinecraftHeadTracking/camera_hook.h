#pragma once

namespace mcht::camera {

// The head movement for one frame. All 4x4, row-major, row-vector.
//
// The three parts are kept apart rather than handed over pre-multiplied
// because world-space yaw has to be threaded between them, and where it goes
// is what makes it a world rotation rather than a camera-local one.
struct Pose {
    // The 6DOF position offset, as a translation.
    float Offset[16];

    // The camera-local part of the rotation: pitch and roll, plus yaw when
    // yaw is camera-local.
    float Rotation[16];

    // Yaw about the world's up axis instead of the camera's, so that pointing
    // the mouse at the floor and then turning your head still pans across the
    // floor rather than spinning the view.
    //
    // Carried as an angle rather than a matrix because the axis it turns about
    // is the world's up axis seen from view space, which needs the game
    // camera's own orientation - something only the hook can read.
    bool WorldSpaceYaw;
    float WorldYawDegrees;
};

// Fills the pose to apply this frame. Return false to leave the camera alone,
// which is what tracking-disabled and no-data both do.
//
// Called on the render path, once per camera setup, so it must not block.
using PoseProvider = bool (*)(Pose& pose);

// Hooks LevelRendererPlayer::setupCamera and the crosshair renderer.
//
// The rotation goes into MinecraftCamera::CameraComponent::mPostViewTransform
// before the game builds its view matrix, and is restored afterwards. That
// slot is applied in view space and is read only by the renderer, so the
// game's aim, raycast and movement never observe head movement - aim
// decoupling needs no save/restore sandwich of its own.
//
// The crosshair is moved to where the clean aim projects into the rotated
// view, so it sits where the mouse is pointing rather than at screen centre.
//
// Returns false and installs nothing if the active profile lacks the
// addresses, leaving the game exactly vanilla.
bool Install(PoseProvider provider);

}  // namespace mcht::camera
