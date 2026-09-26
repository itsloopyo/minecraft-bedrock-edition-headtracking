#pragma once

#include "cameraunlock/data/tracking_pose.h"

namespace mcht::tracking {

// The conversion from the tracker's rotation convention to Bedrock's, handed to the tracking
// processor as its sensitivity settings. Bedrock's post-view transform applies pitch and roll
// opposite to the OpenTrack convention, so both are negated here; yaw and every scale are
// identity. There is no setting for any of it: the tracker shapes the pose.
inline cameraunlock::SensitivitySettings TrackerToBedrockRotation() {
    cameraunlock::SensitivitySettings settings;
    settings.invert_pitch = true;
    settings.invert_roll = true;
    return settings;
}

}  // namespace mcht::tracking
