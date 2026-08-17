#pragma once

#include <cstddef>

#include "build_profile.h"

namespace mcht::builds {

// Result of matching the running Minecraft.Windows.exe against the registry.
enum class SelectResult {
    Matched,     // Fingerprint matched a profile that carries every address we need.
    Incomplete,  // Fingerprint matched, but this build's addresses are not derived yet.
    Unknown,     // No profile matched.
    ReadFailed,  // Could not read the module's PE headers.
};

// Append-only. Newest build first: the top entry is the diagnostic primary
// that words the "newer than / older than" line when nothing matches.
extern const BuildProfile* const kKnownProfiles[];
extern const std::size_t kKnownProfileCount;

// Fingerprint the running game and pick a profile. Runs before a single hook
// is installed. Anything but Matched leaves the mod fully dormant: no hooks,
// no writes, no pattern scans, so an unrecognised build runs exactly vanilla.
SelectResult SelectProfile();

// Valid after SelectProfile() returned Matched or Incomplete. On Incomplete
// only the addresses that are actually derived are populated, and nothing but
// the discovery tooling may act on it.
const BuildProfile& ActiveProfile();

}  // namespace mcht::builds
