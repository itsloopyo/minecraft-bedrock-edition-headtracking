#pragma once

namespace mcht::discovery {

// Developer tool, off unless MinecraftHeadTracking.ini turns it on.
//
// Drives the camera with a synthetic pose that holds one axis at a time -
// yaw, pitch, roll, then X, Y, Z - separated by neutral gaps, naming each
// phase in the log. A screenshot taken mid-phase therefore shows that axis
// alone, which is what makes the axis mapping and signs measurable rather
// than guessed. scripts/calibrate_capture.py automates the capture.
//
// It runs through the ordinary camera hook rather than installing its own, so
// it inherits the fairness gate and the reticle compensation. Nothing here can
// modify the camera on a session where head tracking itself would be refused.
//
// Needs the player in a world; the camera setup does not run in menus.
bool InstallCalibration(int durationSeconds);

}  // namespace mcht::discovery
