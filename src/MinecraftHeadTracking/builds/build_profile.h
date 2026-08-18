#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace mcht::builds {

// What this mod still pins to a specific Bedrock build: struct field offsets,
// vtable indices, and the two crosshair addresses that have no name to find
// them by. Call sites read ActiveProfile().Offsets rather than ever naming a
// literal, so a layout change is answered by appending a profile instead of
// editing code.
//
// The camera code addresses are NOT here any more. They move on every single
// patch, because any code edit shifts everything downstream of it, and they are
// recoverable from the running image by name and by the ECS type hash the
// camera functions carry - see code_resolver.h. What is left is the half that
// only moves when a class actually gains or loses a member, which is rare.
//
// RVAs are relative to Minecraft.Windows.exe's module base. They are derived
// from a memory dump of the running game (scripts/dump_running_exe.py) because
// the on-disk EXE is unreadable under Microsoft Store licensing.
struct OffsetTable {
    struct CameraGroup {
        // UIControl::mSize (float x, y). The cursor renderer centres with
        // x = (screen - mSize.x) / 2 while the rect it builds carries a
        // hardcoded 16x16, so recovering the screen size needs mSize, not the
        // rect's own width.
        std::uint32_t UiControlSize;
    } Camera;

    // Within LevelRendererPlayer, CameraSetup's `this`.
    struct RendererGroup {
        std::uint32_t ClientInstance;
    } Renderer;

    // Within MinecraftCamera::CameraComponent (sizeof 0x120). Cross-checked
    // against LeviLamina's MIT headers for 1.26.x.
    struct CameraComponentGroup {
        // The camera pose. Rewritten every frame by the camera systems, and
        // read by non-render consumers, so head tracking must not go here.
        std::uint32_t Orientation;

        // Read fresh each frame to build the projection, so they are also the
        // values the reticle projection must use.
        std::uint32_t AspectRatio;
        std::uint32_t FieldOfView;

        // Applied to the view matrix as V_final = PostViewTransform * V, so it
        // acts in view space: the doctrine's headRot * gameViewMatrix, in a
        // slot the engine already provides. Render-only.
        //
        // An ECS system resets it from an identity matrix and may then apply
        // its own rotations, but it early-outs on some frames, so a write here
        // must compose against the value found and restore it afterwards
        // rather than accumulate.
        std::uint32_t PostViewTransform;
    } CameraComponent;

    // What the mod needs to answer "could head tracking give an unfair
    // advantage right now". Head tracking decouples looking from aiming, which
    // is a PvP advantage, so it switches off when PvP is enabled and another
    // player could be fought.
    //
    // Vtable entries are BYTE offsets into the vtable; the rest are member
    // offsets.
    struct SessionGroup {
        std::uint32_t ClientInstanceGetLevel;        // vtable: nullptr outside a world
        std::uint32_t ClientInstanceGetLocalPlayer;  // vtable: nullptr until StartGamePacket
        // vtable: isMultiPlayerClient. True for a remote server or Realm, and
        // FALSE for the host of a LAN world, which is why the player count and
        // not this is what catches a hosted session with guests in it.
        std::uint32_t ClientInstanceIsMultiPlayer;
        std::uint32_t LevelGetGameRules;             // vtable
        std::uint32_t LevelPlayerList;               // Level member: the tab list
        std::uint32_t PlayerListSize;                // std::unordered_map _Mysize
        std::uint32_t GameRulesBegin;                // vector _Myfirst
        std::uint32_t GameRulesEnd;                  // vector _Mylast
        std::uint32_t GameRuleStride;                // sizeof(GameRule)

        // Byte offsets from the rule vector's first element. The value byte is
        // what the game's own player-damage gate compares against zero.
        std::uint32_t PvpValueByte;
        std::uint32_t PvpVariantTag;   // must read 1 (bool) or the value is not trustworthy
        std::uint32_t PvpRuleIndex;    // only to sanity-check the vector is long enough

        // LocalPlayer vtable: displays a client-side-only chat line. Sends no
        // packet and is invisible to other players.
        std::uint32_t LocalPlayerDisplayMessage;
    } Session;

    // Offsets within mce::Camera, reached as ScreenContext+0x18 and handed to
    // CameraSetup as its second argument. The matrix stacks are MSVC
    // std::deque<Matrix4> with block size 1, so the live matrix is
    // map[(off + size - 1) & (mapsize - 1)].
    struct CameraStructGroup {
        std::uint32_t ViewStackMap;      // deque _Map
        std::uint32_t ViewStackMapSize;  // deque _Mapsize
        std::uint32_t ViewStackOffset;   // deque _Myoff
        std::uint32_t ViewStackSize;     // deque _Mysize
        std::uint32_t ViewStackDirty;    // recompute flag for the derived cache
    } CameraStruct;
};

struct BuildProfile {
    // "store-win64-YYYYMMDD" - surfaces in the log so a bug report says
    // exactly which profile activated.
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

// A profile can be landed the moment a patch is spotted, carrying only its
// fingerprint, so the mod recognises the build and says so instead of
// reporting it as unknown. It stays dormant until the layout it cannot work
// without is filled in.
//
// The session offsets are required, not optional. Without them the mod cannot
// tell whether PvP is live, and its failure mode would be head tracking
// silently left enabled in a fight - worse than not running at all.
inline bool ProfileIsComplete(const BuildProfile& profile) {
    return profile.Offsets.Renderer.ClientInstance != 0
        && profile.Offsets.CameraComponent.PostViewTransform != 0
        && profile.Offsets.Session.LevelGetGameRules != 0
        && profile.Offsets.Session.ClientInstanceGetLocalPlayer != 0;
}

}  // namespace mcht::builds
