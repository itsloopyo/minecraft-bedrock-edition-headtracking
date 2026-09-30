#pragma once

#include "build_profile.h"
#include "code_resolver.h"
#include "layout_resolver.h"

namespace mcht::builds {

// Result of preparing the running Minecraft.Windows.exe for hooking.
enum class SelectResult {
    Matched,     // A profile's fingerprint matched this exact build.
    Unresolved,  // No verified session layout or camera resolution failed.
    ReadFailed,  // Could not read the module's PE headers.
};

// Fingerprint the running game, recover the camera from the image, and pick
// the fairness gate's layout. Runs before a single hook is installed.
//
// Only Matched may install hooks. All other results leave the game unmodified.
SelectResult SelectProfile();

// The fairness gate's layout. Valid after SelectProfile() returned Matched.
const BuildProfile& ActiveProfile();

// The camera addresses recovered from the running image. Valid only after
// SelectProfile() returned Matched.
const ResolvedCode& ActiveCode();

// The camera's struct layout, read off setupCamera's own code. Valid only
// after SelectProfile() returned Matched.
const ResolvedLayout& ActiveLayout();

}  // namespace mcht::builds
