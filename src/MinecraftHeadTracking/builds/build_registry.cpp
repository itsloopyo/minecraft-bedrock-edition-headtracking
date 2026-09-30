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
ResolvedCode g_code;
ResolvedLayout g_layout;

void LogMismatch(const PeFingerprint& running) {
    const BuildProfile& primary = *kKnownProfiles[0];
    switch (cameraunlock::memory::ClassifyMismatch(running, primary.Fingerprint)) {
        case FingerprintMismatch::Newer:
            cameraunlock::logging::Line(
                "  Your Minecraft is newer than any build this mod was tested on. Check for a mod update.");
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
    g_active = nullptr;
    g_code = {};
    g_layout = {};
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

    if (recognised != nullptr && matched == nullptr) {
        cameraunlock::logging::Line(
            "Build %s is recognised but names none of the PvP gate's offsets yet.",
            recognised->Name);
    } else if (matched == nullptr) {
        cameraunlock::logging::Line("No build profile matches this Minecraft.");
        LogMismatch(running);
    }

    // A recovered camera does not validate the session vtable. Calling a stale
    // slot can write through an argument the caller never supplied.
    if (matched == nullptr) {
        cameraunlock::logging::Line(
            "No verified session layout for this build. Head tracking is disabled; the game runs unmodified.");
        return SelectResult::Unresolved;
    }

    if (!RecoverCamera(*matched)) {
        cameraunlock::logging::Line(
            "The camera could not be recovered from this build. Staying dormant; the game runs "
            "unmodified.");
        return SelectResult::Unresolved;
    }

    g_active = matched;
    cameraunlock::logging::Line("Activated build profile %s", matched->Name);
    return SelectResult::Matched;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

const ResolvedCode& ActiveCode() {
    return g_code;
}

const ResolvedLayout& ActiveLayout() {
    return g_layout;
}

}  // namespace mcht::builds
