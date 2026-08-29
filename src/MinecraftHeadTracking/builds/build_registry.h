#pragma once

#include "build_profile.h"
#include "code_resolver.h"
#include "layout_resolver.h"

namespace mcht::builds {

// Result of preparing the running Minecraft.Windows.exe for hooking.
enum class SelectResult {
    Matched,     // A profile's fingerprint matched this exact build.
    Adopted,     // No profile matched, but the camera was recovered anyway.
    Unresolved,  // The camera could not be recovered from this image.
    ReadFailed,  // Could not read the module's PE headers.
};

// Fingerprint the running game, recover the camera from the image, and pick
// the fairness gate's layout. Runs before a single hook is installed.
//
// Matched and Adopted both go on to hook; the rest leave the mod fully
// dormant, so a build the camera cannot be recovered from runs exactly vanilla.
SelectResult SelectProfile();

// The fairness gate's layout. Valid after SelectProfile() returned Matched or
// Adopted. On Adopted these offsets come from the newest profile rather than
// from one verified against the running build - see SessionLayoutVerified.
const BuildProfile& ActiveProfile();

// False when the fairness offsets were carried over from an older build rather
// than verified against this one. The gate still refuses to allow tracking
// unless every read validates; what this gates is the calls that cannot be
// validated before they are made.
bool SessionLayoutVerified();

// The camera addresses recovered from the running image. Valid only after
// SelectProfile() returned Matched or Adopted.
const ResolvedCode& ActiveCode();

// The camera's struct layout, read off setupCamera's own code. Valid only
// after SelectProfile() returned Matched or Adopted.
const ResolvedLayout& ActiveLayout();

}  // namespace mcht::builds
