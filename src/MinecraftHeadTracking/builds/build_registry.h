#pragma once

#include <cstddef>

#include "build_profile.h"
#include "code_resolver.h"

namespace mcht::builds {

// Result of preparing the running Minecraft.Windows.exe for hooking.
enum class SelectResult {
    Matched,     // Layout profile matched and the camera addresses were recovered.
    Incomplete,  // Fingerprint matched, but this build's layout is not derived yet.
    Unresolved,  // Layout matched, but the camera addresses could not be recovered.
    Unknown,     // No profile matched.
    ReadFailed,  // Could not read the module's PE headers.
};

// Append-only. Newest build first: the top entry is the diagnostic primary
// that words the "newer than / older than" line when nothing matches.
extern const BuildProfile* const kKnownProfiles[];
extern const std::size_t kKnownProfileCount;

// Fingerprint the running game, pick its layout profile, and recover the camera
// addresses from the image. Runs before a single hook is installed. Anything
// but Matched leaves the mod fully dormant: no hooks and no writes, so an
// unrecognised build runs exactly vanilla.
SelectResult SelectProfile();

// Valid after SelectProfile() returned Matched or Incomplete. On Incomplete
// only the offsets that are actually derived are populated, and nothing but
// the discovery tooling may act on it.
const BuildProfile& ActiveProfile();

// The camera addresses recovered from the running image. Valid only after
// SelectProfile() returned Matched.
const ResolvedCode& ActiveCode();

}  // namespace mcht::builds
