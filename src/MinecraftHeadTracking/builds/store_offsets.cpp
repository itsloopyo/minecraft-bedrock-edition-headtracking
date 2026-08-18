// Microsoft Store / Xbox builds of Minecraft: Bedrock Edition.
//
// Append-only. A game patch gets a NEW kStoreProfile_YYYYMMDD below and a new
// entry at the top of kKnownProfiles; existing profiles are never edited or
// removed, so a player who has held back on an older patch keeps working from
// the same mod binary as one who updated today.
//
// What lives here is only what cannot be recovered from the running image:
// struct field offsets and vtable indices. Every code address is resolved at
// load time instead (see code_resolver.h), which is why a patch that moves code
// and leaves the layout alone needs nothing here but a fingerprint.
//
// Every value is assigned by name. Positional braces would be shorter, but a
// table of two dozen bare hex values has no way to say which field a value
// landed in: one stray comma silently routes an offset into the neighbouring
// slot and the mod reads the wrong field. Naming each one also means a field
// added to OffsetTable defaults to 0 in the profiles that have not derived it
// yet, which the dormancy contract already handles.

#include "build_profile.h"

namespace mcht::builds {
namespace {

// The 1.26.x layout, shared by every build below that has been verified to
// carry it. Sharing is deliberate rather than lazy: these are class layouts,
// and asserting that two builds have the same one is a claim worth making in
// one place. A build that diverges gets its own function, and the profiles that
// already point here keep the layout they were verified against.
//
// The CameraComponent offsets are confirmed against setupCamera's own code in
// each build: it reads the quaternion at +0x30, the aspect and field of view at
// +0x4C and +0x50, and takes the address of the post-view transform at +0x5C.
// The 0x120 element stride the component getter computes is sizeof of this
// class, which is the same claim from the other direction.
//
// The vtable slot indices do NOT sit at a constant offset from any published
// table, so each one is derived from the binary.
constexpr OffsetTable Layout_1_26() {
    OffsetTable t{};

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

// Minecraft for Windows 1.26.4201.0, EXE built 2026-08-06.
extern const BuildProfile kStoreProfile_20260806 = {
    "store-win64-20260806",
    {0x6A750E92, 0x1286D000, 0x125529D1},
    Layout_1_26(),
};

// Minecraft for Windows 1.26.4403.0, EXE built 2026-08-12.
//
// Every code address moved - setupCamera 0x031AC390 -> 0x031ACA40, the
// crosshair renderer 0x05A898B0 -> 0x05A8C020, and so on - and not one of them
// needed writing down, because the resolver finds them from the running image.
// The layout is unchanged. That is what a profile looks like now: a
// fingerprint and a layout it was verified against.
extern const BuildProfile kStoreProfile_20260812 = {
    "store-win64-20260812",
    {0x6A7CA63A, 0x12888000, 0x1256B1FE},
    Layout_1_26(),
};

}  // namespace mcht::builds
