#include "build_registry.h"

#include <windows.h>

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "code_resolver.h"

namespace mcht::builds {

using cameraunlock::memory::FingerprintMismatch;
using cameraunlock::memory::PeFingerprint;

extern const BuildProfile kStoreProfile_20260812;
extern const BuildProfile kStoreProfile_20260806;

// Newest first. kKnownProfiles[0] is the diagnostic primary.
const BuildProfile* const kKnownProfiles[] = {
    &kStoreProfile_20260812,
    &kStoreProfile_20260806,
};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {

const BuildProfile* g_active = nullptr;
ResolvedCode g_code;

void LogMismatch(const PeFingerprint& running) {
    const BuildProfile& primary = *kKnownProfiles[0];
    switch (cameraunlock::memory::ClassifyMismatch(running, primary.Fingerprint)) {
        case FingerprintMismatch::Newer:
            cameraunlock::logging::Line(
                "  Your Minecraft is newer than any build this mod knows about. "
                "Check the releases page for an updated mod.");
            break;
        case FingerprintMismatch::Older:
            cameraunlock::logging::Line(
                "  Your Minecraft is older than any build this mod knows about. "
                "Let the Microsoft Store finish updating the game.");
            break;
        case FingerprintMismatch::Differs:
            cameraunlock::logging::Line(
                "  Your Minecraft has the expected build date but a different size or "
                "checksum. This mod does not engage on a modified executable.");
            break;
    }
}

// What an unrecognised build needs from us: everything the new profile has to
// say, already in the shape it goes in.
//
// The camera addresses are recovered even here, and deliberately so. The scan
// only READS this process's own image and writes nothing but these log lines,
// so the game still runs exactly vanilla - the dormancy contract is about not
// engaging, and nothing here engages. What it buys is that answering a patch
// stops being a reverse-engineering session: the addresses that used to need
// rederiving are printed, and what is left to confirm is the layout.
void LogProfileStub(const PeFingerprint& running) {
    ResolvedCode code;
    // The newest profile's layout, only to word the diagnosis. Nothing acts on
    // what this resolves; it is printed and thrown away.
    const bool resolved =
        ResolveCode(code, kKnownProfiles[0]->Offsets.Session.ClientInstanceGetLevel);

    cameraunlock::logging::Line("To add support for this build, append to store_offsets.cpp:");
    cameraunlock::logging::Line("    extern const BuildProfile kStoreProfile_YYYYMMDD = {");
    cameraunlock::logging::Line("        \"store-win64-YYYYMMDD\",");
    cameraunlock::logging::Line("        {0x%08X, 0x%08X, 0x%08X},", running.TimeDateStamp,
                                running.SizeOfImage, running.CheckSum);
    cameraunlock::logging::Line("        Layout_1_26(),");
    cameraunlock::logging::Line("    };");
    if (resolved) {
        cameraunlock::logging::Line(
            "  The camera addresses need no entry - they were recovered here as "
            "setupCamera=0x%08X, getCameraComponent=0x%08X. Confirm the struct layout is "
            "unchanged before shipping the profile.",
            code.CameraSetup, code.GetRenderCameraComponent);
    } else {
        cameraunlock::logging::Line(
            "  The camera addresses could NOT be recovered from this build, so it needs a "
            "rederive rather than just a profile.");
    }
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

    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile& profile = *kKnownProfiles[i];
        const bool matched = running.Matches(profile.Fingerprint);
        cameraunlock::logging::Line("  compared against %s (0x%08X/0x%08X/0x%08X): %s",
                                    profile.Name, profile.Fingerprint.TimeDateStamp,
                                    profile.Fingerprint.SizeOfImage, profile.Fingerprint.CheckSum,
                                    matched ? "MATCH" : "no");
        if (!matched) {
            continue;
        }
        // Recorded even when incomplete so the discovery tooling can reach the
        // offsets that *are* derived. Nothing else may act on it.
        g_active = &profile;
        if (!ProfileIsComplete(profile)) {
            cameraunlock::logging::Line(
                "Build %s is recognised but its struct layout is not derived yet. "
                "Staying dormant; the game runs unmodified.", profile.Name);
            return SelectResult::Incomplete;
        }

        // The layout came from the profile; the addresses come from the image.
        // A patch that only moved code is answered entirely by this call.
        if (!ResolveCode(g_code, profile.Offsets.Session.ClientInstanceGetLevel)) {
            cameraunlock::logging::Line(
                "Build %s is recognised but its camera addresses could not be recovered from "
                "the running image. Staying dormant; the game runs unmodified.", profile.Name);
            return SelectResult::Unresolved;
        }

        cameraunlock::logging::Line("Activated build profile %s", profile.Name);
        return SelectResult::Matched;
    }

    cameraunlock::logging::Line("No build profile matches this Minecraft. Staying dormant; the game runs unmodified.");
    LogMismatch(running);
    LogProfileStub(running);
    return SelectResult::Unknown;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

const ResolvedCode& ActiveCode() {
    return g_code;
}

}  // namespace mcht::builds
