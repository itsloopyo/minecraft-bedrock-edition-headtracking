#include "build_registry.h"

#include <windows.h>

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "code_resolver.h"
#include "layout_resolver.h"

namespace mcht::builds {

using cameraunlock::memory::FingerprintMismatch;
using cameraunlock::memory::PeFingerprint;

namespace {

const BuildProfile* g_active = nullptr;
bool g_sessionVerified = false;
ResolvedCode g_code;
ResolvedLayout g_layout;

// The profile an unrecognised build takes its fairness offsets from: the
// newest one that actually names them.
const BuildProfile* NewestCompleteProfile() {
    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        if (ProfileIsComplete(*kKnownProfiles[i])) {
            return kKnownProfiles[i];
        }
    }
    return nullptr;
}

void LogMismatch(const PeFingerprint& running) {
    const BuildProfile& primary = *kKnownProfiles[0];
    switch (cameraunlock::memory::ClassifyMismatch(running, primary.Fingerprint)) {
        case FingerprintMismatch::Newer:
            cameraunlock::logging::Line(
                "  Your Minecraft is newer than any build this mod was tested on.");
            break;
        case FingerprintMismatch::Older:
            cameraunlock::logging::Line(
                "  Your Minecraft is older than any build this mod was tested on. Let the "
                "Microsoft Store finish updating the game.");
            break;
        case FingerprintMismatch::Differs:
            cameraunlock::logging::Line(
                "  Your Minecraft has the expected build date but a different size or "
                "checksum, so it is a repacked or modified executable.");
            break;
    }
}

// What a new profile needs, already in the shape it goes in. Only the
// fingerprint is left to fill in: everything about the camera was recovered
// above and belongs in no profile.
void LogProfileStub(const PeFingerprint& running, const BuildProfile& adopted) {
    cameraunlock::logging::Line(
        "  Running on addresses recovered from this build. To record it as tested, append to "
        "store_offsets.cpp:");
    cameraunlock::logging::Line("    extern const BuildProfile kStoreProfile_YYYYMMDD = {");
    cameraunlock::logging::Line("        \"store-win64-YYYYMMDD\",");
    cameraunlock::logging::Line("        {0x%08X, 0x%08X, 0x%08X},", running.TimeDateStamp,
                                running.SizeOfImage, running.CheckSum);
    cameraunlock::logging::Line("        Layout_1_26(),");
    cameraunlock::logging::Line("    };");
    cameraunlock::logging::Line(
        "  Confirm the PvP gate still reads this build's game rules before shipping it - those "
        "offsets are the ones carried over from %s.", adopted.Name);
}

// The camera, recovered from the running image. Both halves come from the
// image itself, so a patch that moves or reshapes the camera is answered here
// rather than by appending anything.
bool RecoverCamera(const BuildProfile& sessionLayout) {
    if (!ResolveCode(g_code, sessionLayout.Offsets.Session.ClientInstanceGetLevel)) {
        return false;
    }
    return ResolveLayout(g_code.CameraSetup, g_layout);
}

}  // namespace

SelectResult SelectProfile() {
    HMODULE module = GetModuleHandleW(nullptr);
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(module, running)) {
        cameraunlock::logging::Line("Could not read Minecraft.Windows.exe PE headers. Staying dormant.");
        return SelectResult::ReadFailed;
    }

    cameraunlock::logging::Line("Running build fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
                                running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    const BuildProfile* matched = nullptr;
    const BuildProfile* recognised = nullptr;
    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile& profile = *kKnownProfiles[i];
        const bool matches = running.Matches(profile.Fingerprint);
        cameraunlock::logging::Line("  compared against %s (0x%08X/0x%08X/0x%08X): %s",
                                    profile.Name, profile.Fingerprint.TimeDateStamp,
                                    profile.Fingerprint.SizeOfImage, profile.Fingerprint.CheckSum,
                                    matches ? "MATCH" : "no");
        if (!matches) {
            continue;
        }
        recognised = &profile;
        if (ProfileIsComplete(profile)) {
            matched = &profile;
        }
        break;
    }

    // An unrecognised build is no longer a reason to stay dormant. Everything
    // about the camera is recovered from the image below, and the fairness
    // offsets are the only thing a profile still carries, so what an unknown
    // build costs is that those are the previous build's rather than this
    // one's - not that the mod cannot run.
    const BuildProfile* const session = matched != nullptr ? matched : NewestCompleteProfile();
    if (session == nullptr) {
        cameraunlock::logging::Line(
            "No build profile names the PvP gate's offsets. Staying dormant; the game runs "
            "unmodified.");
        return SelectResult::Unresolved;
    }

    if (recognised != nullptr && matched == nullptr) {
        cameraunlock::logging::Line(
            "Build %s is recognised but names none of the PvP gate's offsets yet.",
            recognised->Name);
    } else if (matched == nullptr) {
        cameraunlock::logging::Line("No build profile matches this Minecraft.");
        LogMismatch(running);
    }

    g_active = session;
    g_sessionVerified = matched != nullptr;

    if (!RecoverCamera(*session)) {
        cameraunlock::logging::Line(
            "The camera could not be recovered from this build. Staying dormant; the game runs "
            "unmodified.");
        g_active = nullptr;
        return SelectResult::Unresolved;
    }

    if (matched != nullptr) {
        cameraunlock::logging::Line("Activated build profile %s", matched->Name);
        return SelectResult::Matched;
    }

    LogProfileStub(running, *session);
    cameraunlock::logging::Line(
        "The PvP gate is using %s's offsets, which are not verified for this build. It refuses "
        "to allow tracking unless every read still checks out, and the in-game notice is left "
        "unsent because that call cannot be checked first.", session->Name);
    return SelectResult::Adopted;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

bool SessionLayoutVerified() {
    return g_sessionVerified;
}

const ResolvedCode& ActiveCode() {
    return g_code;
}

const ResolvedLayout& ActiveLayout() {
    return g_layout;
}

}  // namespace mcht::builds
