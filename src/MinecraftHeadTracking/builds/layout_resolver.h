#pragma once

#include <cstdint>

namespace mcht::builds {

// The camera's struct layout, recovered from setupCamera's own code at load
// time rather than pinned per build.
//
// setupCamera is the one function that reads every field the mod touches: it
// loads the client instance out of the renderer, walks the ECS registry to the
// component, reads the orientation quaternion and the two projection inputs,
// and takes the address of the post-view transform. Every offset below is the
// displacement the game itself uses, so the mod reads the same bytes the
// renderer does by construction.
//
// What is NOT here is the fairness gate's layout. Those offsets are reached
// through virtual dispatch and member walks that setupCamera never performs,
// so there is no code to read them off; they stay in the build profile.
struct ResolvedLayout {
    // Within LevelRendererPlayer: the IClientInstance the component getter
    // takes.
    std::uint32_t ClientInstance = 0;

    // Within MinecraftCamera::CameraComponent.
    std::uint32_t Orientation = 0;        // the camera pose quaternion
    std::uint32_t AspectRatio = 0;        // both are read fresh each frame to
    std::uint32_t FieldOfView = 0;        // build the projection
    std::uint32_t PostViewTransform = 0;  // the render-only slot tracking writes

    bool Complete() const {
        return ClientInstance != 0 && Orientation != 0 && AspectRatio != 0 &&
               FieldOfView != 0 && PostViewTransform != 0;
    }
};

// Recover the layout from the setupCamera the code resolver found, logging
// each offset and what evidence carried it. False means a field was missing or
// its evidence was ambiguous, which leaves the mod dormant rather than writing
// a head transform into a field that merely looked like the right one.
bool ResolveLayout(std::uint32_t cameraSetupRva, ResolvedLayout& out);

}  // namespace mcht::builds
