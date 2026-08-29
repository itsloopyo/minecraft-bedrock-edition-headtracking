// Microsoft Store / Xbox builds of Minecraft: Bedrock Edition.
//
// Append-only. A game patch gets a NEW kStoreProfile_YYYYMMDD below and a new
// entry at the top of kKnownProfiles; existing profiles are never edited or
// removed, so a player who has held back on an older patch keeps working from
// the same mod binary as one who updated today.
//
// What lives here is only what cannot be recovered from the running image: the
// fairness gate's offsets and vtable indices. Every camera address is resolved
// at load time (code_resolver.h) and so is the camera's struct layout
// (layout_resolver.h), which is why a patch that moves or reshapes the camera
// needs nothing here at all.
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

// The 1.26.x fairness layout, shared by every build below that has been
// verified to carry it. Sharing is deliberate rather than lazy: these are class
// layouts, and asserting that two builds have the same one is a claim worth
// making in one place. A build that diverges gets its own function, and the
// profiles that already point here keep the layout they were verified against.
//
// The vtable slot indices do NOT sit at a constant offset from any published
// table, so each one is derived from the binary.
constexpr OffsetTable Layout_1_26() {
    OffsetTable t{};

    t.Camera.UiControlSize = 0x48;

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

// Minecraft for Windows 1.26.4501.0, EXE built 2026-08-29.
//
// The first build the mod ran on before it was written down here: the camera
// addresses and the camera layout were both recovered from the running image,
// so all this profile adds is that the fairness layout was confirmed in game
// on this build. setupCamera is byte-identical to the 20260812 build apart
// from one rip displacement.
extern const BuildProfile kStoreProfile_20260829 = {
    "store-win64-20260829",
    {0x6A8378BA, 0x12888000, 0x12568F46},
    Layout_1_26(),
};

const BuildProfile* const kKnownProfiles[] = {
    &kStoreProfile_20260829,
    &kStoreProfile_20260812,
    &kStoreProfile_20260806,
};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

}  // namespace mcht::builds
