#pragma once

#include <cstddef>
#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace mcht::builds {

// What this mod still pins to a specific Bedrock build: the fairness gate's
// struct offsets and vtable indices, and the cursor control's size. Call sites
// read ActiveProfile().Offsets rather than ever naming a literal, so a layout
// change is answered by appending a profile instead of editing code.
//
// Neither the camera's code addresses nor its struct layout are here any more.
// The addresses are found by name and by the ECS type hash the camera
// functions carry (code_resolver.h); the layout is read off setupCamera's own
// code (layout_resolver.h). Both are recovered from the running image at load
// time, so a Minecraft patch that moves the camera needs nothing appended.
//
// What is left is what no code in the image reads in a form this can follow:
// the fairness gate reaches its values through virtual dispatch and member
// walks that no single function performs.
//
// Those offsets are derived from a memory dump of the running game
// (scratch/dump_running_exe.py) because the on-disk EXE is unreadable under
// Microsoft Store licensing.
struct OffsetTable {
    struct CameraGroup {
        // UIControl::mSize (float x, y). The cursor renderer centres with
        // x = (screen - mSize.x) / 2 while the rect it builds carries a
        // hardcoded 16x16, so recovering the screen size needs mSize, not the
        // rect's own width.
        std::uint32_t UiControlSize;
    } Camera;

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
};

struct BuildProfile {
    // "store-win64-YYYYMMDD" - surfaces in the log so a bug report says
    // exactly which profile activated.
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

// Whether a profile can serve as the fairness gate's layout. A profile can be
// landed the moment a patch is spotted, carrying only its fingerprint, to
// record that the build was seen; one in that state names no offsets and so
// cannot be the source of them.
//
// The session offsets are required, not optional. Without them the mod cannot
// tell whether PvP is live, and its failure mode would be head tracking
// silently left enabled in a fight - worse than not running at all.
inline bool ProfileIsComplete(const BuildProfile& profile) {
    return profile.Offsets.Session.LevelGetGameRules != 0
        && profile.Offsets.Session.ClientInstanceGetLocalPlayer != 0;
}

// Append-only, newest build first. The top entry is the diagnostic primary
// that words the "newer than / older than" line, and the profile an
// unrecognised build takes its fairness offsets from. Declared beside the
// profiles rather than beside the selection logic so that answering a patch is
// one edit in one file.
extern const BuildProfile* const kKnownProfiles[];
extern const std::size_t kKnownProfileCount;

}  // namespace mcht::builds
