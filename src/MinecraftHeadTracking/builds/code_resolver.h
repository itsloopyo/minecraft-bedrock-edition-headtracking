#pragma once

#include <cstdint>

namespace mcht::builds {

// The camera code addresses, recovered from the running image at load time.
//
// These used to be pinned per build, which meant every Minecraft patch moved
// them and left the mod dormant until someone rederived them by hand. They are
// found instead by what they ARE - the name Bedrock bakes into its own assert
// strings, and the ECS type hash the camera functions carry - so a patch that
// only moves code needs no new build profile.
//
// Struct field offsets and vtable indices are NOT here. Those still live in the
// build profile, because they cannot be recovered this way and, unlike code
// addresses, they only move when a class actually gains or loses a member.
struct ResolvedCode {
    // InGamePlayScreen::_renderLevelPrep. The anchor: found by name, and the
    // other two are found relative to it or by the same type hash it leads to.
    std::uint32_t RenderLevelPrep = 0;

    // LevelRendererPlayer::setupCamera - the hook target.
    std::uint32_t CameraSetup = 0;

    // IClientInstance* -> MinecraftCamera::CameraComponent*, or null.
    std::uint32_t GetRenderCameraComponent = 0;

    // HudCursorRenderer::render, and the UI blit it draws the crosshair
    // through. Found together, by the hardcoded 16x16 the renderer packs into
    // the rect and by that rect being the blit's fourth argument.
    std::uint32_t HudCursorRender = 0;
    std::uint32_t UiBlit = 0;

    // The two the camera path cannot work without. RenderLevelPrep is only a
    // stepping stone, and the crosshair pair is best-effort: without it the
    // crosshair stays centred, which is a cosmetic loss rather than a reason to
    // refuse to track.
    bool Complete() const { return CameraSetup != 0 && GetRenderCameraComponent != 0; }
};

// Recover the camera addresses from the running image, logging each step and
// what it found. False means something was missing or ambiguous, which leaves
// the mod dormant rather than hooking an address that merely looked plausible.
// clientInstanceSlot is a vtable byte offset known to belong to IClientInstance
// (the profile's Session.ClientInstanceGetLevel). It is how the component
// getter is told apart from a function reaching the same accessor through a
// different interface.
bool ResolveCode(ResolvedCode& out, std::uint32_t clientInstanceSlot);

}  // namespace mcht::builds
