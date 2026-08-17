#pragma once

#include <optional>
#include <string>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/data/tracking_pose.h"

namespace mcht::tracking {

// Spelled out rather than pulled from windows.h and the receiver, so this
// header stays free of both. tracking_settings.cpp static_asserts each against
// the thing it stands for.
constexpr int kDefaultYawModeKey = 0x22;   // VK_NEXT, Page Down.
constexpr int kDefaultTrackerPort = 4242;  // The OpenTrack default.

// Bedrock's post-view transform applies pitch and roll opposite to the
// OpenTrack convention, so inverted is what "not configured" has to mean here.
// Declared once and read by all three places that need it - the ini this mod
// writes, the fallback the reader passes for a key that is absent, and the
// Settings below that stand in when there is no readable ini at all - so a
// file cannot state one default while the no-file path applies another.
constexpr bool kDefaultInvertYaw = false;
constexpr bool kDefaultInvertPitch = true;
constexpr bool kDefaultInvertRoll = true;

// Everything the ini decides, as a value. Parsing it is kept apart from
// applying it: the parse touches nothing but the file, so the runtime state it
// eventually configures cannot leak into the decision of what to read.
struct Settings {
    cameraunlock::SensitivitySettings Sensitivity{1.0f, 1.0f, 1.0f, kDefaultInvertYaw,
                                                  kDefaultInvertPitch, kDefaultInvertRoll};
    // Which of the two applies is decided per connection from the packet
    // source address, and covers rotation and position alike.
    float LocalSmoothing = 0.0f;
    float RemoteSmoothing = 0.15f;
    cameraunlock::PositionSettings Position = cameraunlock::PositionSettings::Default();
    bool EnableOnStartup = true;
    bool PositionEnabled = true;
    bool WorldSpaceYaw = true;
    int YawModeKey = kDefaultYawModeKey;
    int Port = kDefaultTrackerPort;
};

// Reads the ini in one pass, or nothing when there is no ini to read.
//
// The absent case is a Settings{} the caller must still apply, not a licence to
// apply nothing. The processors' own constructed defaults are NOT this mod's
// defaults - their pitch and roll inversion is off where this engine needs it
// on, and PositionProcessor arrives with tracker-pivot compensation enabled
// where this mod requires it off - so leaving them alone is a third behaviour
// belonging to nobody.
//
// Out-of-range values are clamped or replaced and reported, never passed on.
// Every float here crosses a user boundary and ends up in a matrix the mod
// copies into the game, and INI parsing is strtod, which happily returns nan
// and inf. A NaN reaches the frustum planes and the chunk sort comparator - a
// strict-weak-ordering violation, which is an out-of-bounds write inside
// std::sort rather than merely a wrong picture.
std::optional<Settings> ReadSettings(const std::string& configPath);

}  // namespace mcht::tracking
