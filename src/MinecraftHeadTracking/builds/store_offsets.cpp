// Microsoft Store / Xbox builds of Minecraft: Bedrock Edition.
//
// Append-only. A game patch gets a NEW kStoreProfile_YYYYMMDD below and a new
// entry at the top of kKnownProfiles; existing profiles are never edited or
// removed, so a player who has held back on an older patch keeps working from
// the same mod binary as one who updated today.
//
// Every address is assigned by name. Positional braces would be shorter, but a
// table of two dozen bare hex values has no way to say which field a value
// landed in: one stray comma silently routes an RVA into the neighbouring slot
// and the mod hooks the wrong function. Naming each one also means a field
// added to OffsetTable defaults to 0 in the profiles that have not derived it
// yet, which the dormancy contract already handles.

#include "build_profile.h"

namespace mcht::builds {
namespace {

// Minecraft for Windows 1.26.4201.0, EXE built 2026-08-06.
//
// PreRenderUpdate is derived and confirmed: the function at this RVA
// references both the assert descriptor naming it
// "virtual void __cdecl LevelRendererPlayer::preRenderUpdate(ScreenContext &,
// LevelRenderPreRenderUpdateParameters &)" and the profiler zone string
// "Player - Pre render update".
//
// CameraSetup is derived from the call at InGamePlayScreen::_renderLevelPrep
// +0x30D. Its body reads the ECS CameraComponent (type hash 0x4F6047C7) at
// +0x30 as a quaternion, expands it to a 3x3 with zero translation, inverts it
// through the cofactor inverse at 0x005815A0 into the view stack, then applies
// the component's +0x5C pre-transform through the row-major multiply at
// 0x0058BE30.
//
// The CameraStruct offsets are read straight out of that function's deque
// access at 0x031AC3F0..0x031AC40C.
//
// Every value below is verified against the dumped image rather than carried
// over from a reference: the vtable slot indices in particular do NOT sit at a
// constant offset from any published table, so each one is derived from the
// binary. See docs/reverse-engineering.md.
constexpr OffsetTable Offsets_20260806() {
    OffsetTable t{};

    t.Camera.PreRenderUpdate = 0x0314C7D0;
    t.Camera.RenderLevelPrep = 0x004F2E10;
    t.Camera.CameraRender = 0x03169F70;
    t.Camera.CameraSetup = 0x031AC390;
    t.Camera.GetRenderCameraComponent = 0x03023340;
    t.Camera.HudCursorRender = 0x05A898B0;
    t.Camera.UiBlit = 0x0186F4B0;
    t.Camera.UiControlSize = 0x48;

    t.Renderer.ClientInstance = 0x1168;

    t.CameraComponent.Orientation = 0x30;
    t.CameraComponent.AspectRatio = 0x4C;
    t.CameraComponent.FieldOfView = 0x50;
    t.CameraComponent.PostViewTransform = 0x5C;

    t.Session.ClientInstanceGetLevel = 0x538;
    t.Session.ClientInstanceGetLocalPlayer = 0x0F8;
    t.Session.ClientInstanceIsMultiPlayer = 0x560;
    t.Session.LevelGetGameRules = 0xAB0;
    t.Session.LevelPlayerList = 0x4E0;
    t.Session.PlayerListSize = 0x10;
    t.Session.GameRulesBegin = 0x18;
    t.Session.GameRulesEnd = 0x20;
    t.Session.GameRuleStride = 0x118;
    t.Session.PvpValueByte = 0x106C;
    t.Session.PvpVariantTag = 0x1070;
    t.Session.PvpRuleIndex = 15;
    t.Session.LocalPlayerDisplayMessage = 0x630;

    t.CameraStruct.ViewStackMap = 0x08;
    t.CameraStruct.ViewStackMapSize = 0x10;
    t.CameraStruct.ViewStackOffset = 0x18;
    t.CameraStruct.ViewStackSize = 0x20;
    t.CameraStruct.ViewStackDirty = 0x38;

    return t;
}

}  // namespace

extern const BuildProfile kStoreProfile_20260806 = {
    "store-win64-20260806",
    {0x6A750E92, 0x1286D000, 0x125529D1},
    Offsets_20260806(),
};

}  // namespace mcht::builds
