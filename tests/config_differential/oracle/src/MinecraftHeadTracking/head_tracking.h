#pragma once

#include <string>

namespace mcht::tracking {

// Starts the OpenTrack UDP receiver, the processing pipeline and the hotkeys,
// then installs the camera hook. Configuration is read from the ini beside the
// mod DLL.
//
// Returns false if the camera hook could not be installed, in which case
// nothing is running and the game is untouched.
bool Start(const std::string& configPath);

}  // namespace mcht::tracking
