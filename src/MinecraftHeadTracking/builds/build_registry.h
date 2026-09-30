#pragma once

#include "build_profile.h"
#include "code_resolver.h"
#include "layout_resolver.h"

namespace mcht::builds {

// Result of preparing the running Minecraft.Windows.exe for hooking.
enum class SelectResult {
    Matched,     // A profile's fingerprint matched this exact build.
    Resolved,    // Camera resolved; session access still requires runtime validation.
    Unresolved,  // Camera resolution failed.
    ReadFailed,  // Could not read the module's PE headers.
};

// Fingerprint the running game, recover the camera from the image, and pick
// an optional exact-build profile. Runs before a single hook is installed.
//
// Matched and Resolved may install camera hooks. Both validate session access
// against the running implementation before tracking is allowed.
SelectResult SelectProfile();

// Optional exact-build metadata; unresolved fields are zero on unknown builds.
// Valid after SelectProfile() returned Matched or Resolved.
const BuildProfile& ActiveProfile();

bool KnownBuild();

// The camera addresses recovered from the running image. Valid only after
// SelectProfile() returned Matched or Resolved.
const ResolvedCode& ActiveCode();

// The camera's struct layout, read off setupCamera's own code. Valid only
// after SelectProfile() returned Matched or Resolved.
const ResolvedLayout& ActiveLayout();

}  // namespace mcht::builds
