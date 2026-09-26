#include <windows.h>

#include <exception>
#include <string>
#include <utility>

#include "builds/build_registry.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/logging/file_log.h"
#include "common/module_path.h"
#include "config.h"
#include "discovery.h"
#include "head_tracking.h"

namespace {

HMODULE g_self = nullptr;

// What a bug report needs before anything else: which mod build is running,
// in which process, against which image.
void LogHostEnvironment() {
    cameraunlock::logging::Line("MinecraftHeadTracking %s (%s) attached to pid %lu",
                                MCHT_MOD_VERSION, MCHT_GIT_SHA, GetCurrentProcessId());

    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        cameraunlock::logging::Line("Host executable: %S", exePath);
    }
    cameraunlock::logging::Line("Module base: 0x%p", static_cast<void*>(GetModuleHandleW(nullptr)));
}

// The one reader and writer of CameraUnlock.ini. Never destroyed: the hotkey
// thread saves through it for as long as the game runs.
cameraunlock::config::ConfigOwner<mcht::config::Config>* g_configOwner = nullptr;

void BootstrapBody() {
    // The log and settings live beside the mod DLL, not beside the game EXE.
    // Bedrock's install directory is under C:\Program Files\WindowsApps and is
    // not writable, and a packaged app's working directory is not somewhere a
    // user would find a file either, so the mod's own deployment folder is the
    // only sensible home for them.
    const std::wstring directory = mcht::paths::DirectoryOfModule(g_self);
    cameraunlock::logging::Open(directory + L"MinecraftHeadTracking.log");
    LogHostEnvironment();

    // Nothing may touch game memory until the camera has been recovered from
    // this image. A build it cannot be recovered from leaves the game running
    // exactly vanilla.
    const mcht::builds::SelectResult result = mcht::builds::SelectProfile();
    if (result != mcht::builds::SelectResult::Matched &&
        result != mcht::builds::SelectResult::Adopted) {
        cameraunlock::logging::Line("Dormant. No hooks installed.");
        return;
    }

    // One game process opens this folder's settings: the launcher refuses to
    // inject while more than one Minecraft.Windows.exe runs.
    cameraunlock::config::ConfigOwnerOptions<mcht::config::Config> options =
        mcht::config::MakeConfigOwnerOptions(directory, cameraunlock::config::DefaultsFile::PerUser());
    options.status_sink = [](const std::string& message) {
        cameraunlock::logging::Line("%s", message.c_str());
    };
    g_configOwner = new cameraunlock::config::ConfigOwner<mcht::config::Config>(std::move(options));

    const cameraunlock::config::ConfigLoadResult<mcht::config::Config> loaded = g_configOwner->Load();
    for (const std::string& line : loaded.log) {
        cameraunlock::logging::Line("%s", line.c_str());
    }
    cameraunlock::logging::Line("Settings: %s.", cameraunlock::config::ConfigLoadStatusName(loaded.status));
    const mcht::config::Config& config = loaded.config;

    if (config.run_discovery) {
        cameraunlock::logging::Line("Discovery mode is enabled in CameraUnlock.ini.");
        mcht::discovery::InstallCalibration(config.discovery_seconds);
        return;
    }

    mcht::tracking::Start(config, *g_configOwner);
}

// A thread procedure has no handler above it, so an exception escaping here
// would be std::terminate, taking the game down with the log stopping
// mid-startup. The config owner refuses a table it cannot render, the key
// lists are parsed, and the receiver and the hotkey poller each start a
// thread, all of which throw on failure.
DWORD WINAPI Bootstrap(LPVOID) {
    try {
        BootstrapBody();
    } catch (const std::exception& e) {
        cameraunlock::logging::Line("ERROR: head tracking did not start: %s", e.what());
        return 1;
    }
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        // Loader lock: do the real work on our own thread.
        const HANDLE thread = CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        cameraunlock::logging::Close();
    }
    return TRUE;
}
