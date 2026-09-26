#pragma once

#include "config.h"

namespace mcht::tracking {

// Starts the OpenTrack UDP receiver, the processing pipeline and the hotkeys,
// then installs the camera hook, configured from `config`. The tracking mode
// and yaw mode hotkeys save their new state through `owner`, which must
// outlive the game.
//
// Returns false if the camera hook could not be installed, in which case
// nothing is running and the game is untouched.
bool Start(const mcht::config::Config& config,
           cameraunlock::config::ConfigOwner<mcht::config::Config>& owner);

}  // namespace mcht::tracking
