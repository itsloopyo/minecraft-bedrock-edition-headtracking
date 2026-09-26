#pragma once

#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

namespace mcht::config {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside kConfigFileName. Imported once
// while kConfigFileName is absent, and never written.
constexpr const char* kLegacyConfigFileName = "MinecraftHeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Minecraft: Bedrock Edition";

// Core's config with this mod's own rows.
struct Config : cameraunlock::HeadTrackingConfig {
    // Developer tool: drive the camera through one axis at a time instead of head tracking,
    // for measuring the axis mapping of a new Minecraft build.
    bool run_discovery = false;
    int discovery_seconds = 40;
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// MinecraftHeadTracking.ini as v1.1.2 and every build before it read it (src/legacy_config/),
// mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing backslash): the settings in
// CameraUnlock.ini, imported once from MinecraftHeadTracking.ini. The mod passes
// DefaultsFile::PerUser() and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(
    const std::wstring& folder, cameraunlock::config::DefaultsFile defaults);

}  // namespace mcht::config
