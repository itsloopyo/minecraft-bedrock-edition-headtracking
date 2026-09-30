#include "build_registry.h"

#include <windows.h>

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "code_resolver.h"
#include "layout_resolver.h"

namespace mcht::builds {

using cameraunlock::memory::PeFingerprint;

namespace {

const BuildProfile* g_active = nullptr;
ResolvedCode g_code;
ResolvedLayout g_layout;
BuildProfile g_resolved = {"runtime-validated", {}, {}};

// The camera, recovered from the running image. Both halves come from the
// image itself, so a patch that moves or reshapes the camera is answered here
// rather than by appending anything.
bool RecoverCamera() {
    if (!ResolveCode(g_code)) {
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
        cameraunlock::logging::Line("Validating compatibility against the running game.");
    }

    if (!RecoverCamera()) {
        cameraunlock::logging::Line(
            "The camera could not be recovered from this build. Staying dormant; the game runs "
            "unmodified.");
        return SelectResult::Unresolved;
    }

    g_resolved.Fingerprint = running;
    g_active = matched ? matched : &g_resolved;
    cameraunlock::logging::Line("Camera resolved for %s. Session access will be validated before tracking.",
                                g_active->Name);
    return matched ? SelectResult::Matched : SelectResult::Resolved;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

bool KnownBuild() {
    return g_active != &g_resolved;
}

const ResolvedCode& ActiveCode() {
    return g_code;
}

const ResolvedLayout& ActiveLayout() {
    return g_layout;
}

}  // namespace mcht::builds
