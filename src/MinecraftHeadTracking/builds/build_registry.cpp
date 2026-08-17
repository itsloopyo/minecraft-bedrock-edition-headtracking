#include "build_registry.h"

#include <windows.h>

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"

namespace mcht::builds {

using cameraunlock::memory::FingerprintMismatch;
using cameraunlock::memory::PeFingerprint;

extern const BuildProfile kStoreProfile_20260806;

// Newest first. kKnownProfiles[0] is the diagnostic primary.
const BuildProfile* const kKnownProfiles[] = {
    &kStoreProfile_20260806,
};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {

const BuildProfile* g_active = nullptr;

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
        // addresses that *are* derived. Nothing else may act on it.
        g_active = &profile;
        if (!ProfileIsComplete(profile)) {
            cameraunlock::logging::Line(
                "Build %s is recognised but its camera addresses are not derived yet. "
                "Staying dormant; the game runs unmodified.", profile.Name);
            return SelectResult::Incomplete;
        }
        cameraunlock::logging::Line("Activated build profile %s", profile.Name);
        return SelectResult::Matched;
    }

    cameraunlock::logging::Line("No build profile matches this Minecraft. Staying dormant; the game runs unmodified.");
    LogMismatch(running);
    return SelectResult::Unknown;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

}  // namespace mcht::builds
