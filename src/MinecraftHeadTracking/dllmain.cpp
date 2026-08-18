#include <windows.h>

#include <string>

#include "builds/build_registry.h"
#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"
#include "common/module_path.h"
#include "discovery.h"
#include "head_tracking.h"
#include "tracking_settings.h"

namespace {

HMODULE g_self = nullptr;

// Written into the ini and used when the ini does not answer, so the two
// cannot drift into a file whose stated default is not the one that applies.
constexpr int kDefaultDiscoverySeconds = 40;

// A path in the encoding the ini APIs actually decode.
//
// IniReader and IniWriter take a narrow path and hand it to
// GetPrivateProfileStringA and fopen, both of which decode it with the process
// ANSI code page. Encoding it as UTF-8 sent every path holding a non-ASCII
// character - any localised Windows user name, since these files live under
// %LOCALAPPDATA% - to a different file name, so the ini was written and read
// somewhere nothing else looked and every setting silently reverted.
//
// A character the code page cannot represent is substituted rather than
// reported, so the result is round-tripped: a path that does not come back
// identical does not name the file we asked for, and saying so beats a
// mystery "using defaults" line.
std::string AnsiPath(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);

    const int back = MultiByteToWideChar(CP_ACP, 0, out.c_str(), size, nullptr, 0);
    std::wstring verify(static_cast<std::size_t>(back > 0 ? back : 0), L'\0');
    if (back <= 0 ||
        MultiByteToWideChar(CP_ACP, 0, out.c_str(), size, verify.data(), back) <= 0 ||
        verify != text) {
        cameraunlock::logging::Line(
            "WARNING: %S contains characters this system's ANSI code page cannot represent, so "
            "the settings file cannot be read or written there and built-in defaults apply.",
            text.c_str());
    }
    return out;
}

// Written once, so the discovery switch is discoverable without reading the
// source. Never overwrites an existing file.
//
// Every value comes from a default-constructed Settings - the same object that
// applies when there is no readable ini - so the file cannot state one default
// while the no-file path applies another. That drift has already shipped once
// here, as an ini promising inverted pitch beside a no-ini path that nodded
// the wrong way.
void WriteDefaultConfig(const std::wstring& path, const std::string& ansiPath) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    cameraunlock::IniWriter writer;
    if (!writer.Open(ansiPath)) {
        cameraunlock::logging::Line("Could not create %S; built-in defaults apply.", path.c_str());
        return;
    }
    const mcht::tracking::Settings defaults;

    writer.WriteComment(" MinecraftHeadTracking");
    writer.WriteBlankLine();
    writer.WriteSection("Tracking");
    writer.WriteComment(" UDP port the tracker sends OpenTrack packets to.");
    writer.WriteInt("Port", defaults.Port);
    writer.WriteBool("EnableOnStartup", defaults.EnableOnStartup);
    writer.WriteComment(" Smoothing, 0.0 (none) to 1.0 (heavy). Which of the two applies is");
    writer.WriteComment(" decided per connection from where the packets come from, so both");
    writer.WriteComment(" can be set and left alone. Nothing is applied on top of these: 0.0");
    writer.WriteComment(" means none. Both cover head rotation and head position alike.");
    writer.WriteComment(" LocalSmoothing: the tracker runs on this PC (loopback). Already");
    writer.WriteComment(" steady, so smoothing here only costs latency.");
    writer.WriteDouble("LocalSmoothing", defaults.LocalSmoothing);
    writer.WriteComment(" RemoteSmoothing: the tracker is a phone or another PC on the");
    writer.WriteComment(" network. Covers the jitter the network adds.");
    writer.WriteDouble("RemoteSmoothing", defaults.RemoteSmoothing);
    writer.WriteDouble("YawSensitivity", defaults.Sensitivity.yaw);
    writer.WriteDouble("PitchSensitivity", defaults.Sensitivity.pitch);
    writer.WriteDouble("RollSensitivity", defaults.Sensitivity.roll);
    writer.WriteComment(" Pitch and roll are inverted by default: Bedrock's post-view transform");
    writer.WriteComment(" runs them opposite to the OpenTrack convention, so leaving these off");
    writer.WriteComment(" makes leaning and nodding go the wrong way.");
    writer.WriteBool("InvertYaw", defaults.Sensitivity.invert_yaw);
    writer.WriteBool("InvertPitch", defaults.Sensitivity.invert_pitch);
    writer.WriteBool("InvertRoll", defaults.Sensitivity.invert_roll);
    writer.WriteComment(" true keeps yaw horizon-locked: turning your head yaws about the world's");
    writer.WriteComment(" up axis whatever the mouse has the camera pointed at. false yaws about");
    writer.WriteComment(" the camera's own up axis instead, which leans and rolls the view when");
    writer.WriteComment(" you are looking at the floor or the sky.");
    writer.WriteBool("WorldSpaceYaw", defaults.WorldSpaceYaw);
    writer.WriteBlankLine();
    writer.WriteSection("Hotkeys");
    writer.WriteComment(" Toggles the two yaw modes in game. 0x22 is Page Down; Ctrl+Shift+H does");
    writer.WriteComment(" the same thing and is not configurable.");
    writer.WriteHex("YawModeKey", defaults.YawModeKey);
    writer.WriteBlankLine();
    writer.WriteSection("Position");
    writer.WriteComment(" 6DOF: leaning and moving your head shifts the viewpoint.");
    writer.WriteBool("Enabled", defaults.PositionEnabled);
    writer.WriteDouble("SensitivityX", defaults.Position.sensitivity_x);
    writer.WriteDouble("SensitivityY", defaults.Position.sensitivity_y);
    writer.WriteDouble("SensitivityZ", defaults.Position.sensitivity_z);
    writer.WriteComment(" Flip an axis if your tracker's convention disagrees with the defaults.");
    writer.WriteBool("InvertX", defaults.Position.invert_x);
    writer.WriteBool("InvertY", defaults.Position.invert_y);
    writer.WriteBool("InvertZ", defaults.Position.invert_z);
    writer.WriteBlankLine();
    writer.WriteSection("Discovery");
    writer.WriteComment(" Developer tool. Drives the camera through one axis at a time and names");
    writer.WriteComment(" each phase in the log, which is how the axis mapping is measured for a");
    writer.WriteComment(" new Minecraft build. Head tracking input is ignored while it runs, and");
    writer.WriteComment(" it obeys the same PvP rules as head tracking. Needs you in a world.");
    writer.WriteBool("Enabled", false);
    writer.WriteInt("DurationSeconds", kDefaultDiscoverySeconds);
}

// Windows INI parsing is byte-oriented and a UTF-8 BOM ends up glued to the
// first section header, so every setting silently reverts to its default.
// Notepad writes that BOM by default, so say so rather than ignoring the file.
void WarnIfByteOrderMarked(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    unsigned char bom[3] = {0, 0, 0};
    DWORD read = 0;
    ReadFile(file, bom, sizeof(bom), &read, nullptr);
    CloseHandle(file);

    if (read == sizeof(bom) && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        cameraunlock::logging::Line(
            "WARNING: MinecraftHeadTracking.ini starts with a UTF-8 byte order mark, so Windows "
            "cannot read any setting in it and all defaults apply. Re-save it as plain ANSI or "
            "UTF-8 without BOM, or delete it and let it be recreated.");
    }
}

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

DWORD WINAPI Bootstrap(LPVOID) {
    // The log and ini live beside the mod DLL, not beside the game EXE.
    // Bedrock's install directory is under C:\Program Files\WindowsApps and is
    // not writable, and a packaged app's working directory is not somewhere a
    // user would find a file either, so the mod's own deployment folder is the
    // only sensible home for them.
    const std::wstring directory = mcht::paths::DirectoryOfModule(g_self);
    cameraunlock::logging::Open(directory + L"MinecraftHeadTracking.log");
    LogHostEnvironment();

    // Nothing may touch game memory until a profile is confirmed. An
    // unrecognised build leaves the game running exactly vanilla.
    const mcht::builds::SelectResult result = mcht::builds::SelectProfile();
    // Incomplete and Unresolved still write the ini below, because the build IS
    // recognised and the file is what a user edits before the next launch. Only
    // Matched goes on to install a hook.
    const bool recognised = result == mcht::builds::SelectResult::Matched ||
                            result == mcht::builds::SelectResult::Incomplete ||
                            result == mcht::builds::SelectResult::Unresolved;
    if (!recognised) {
        cameraunlock::logging::Line("Dormant. No hooks installed.");
        return 0;
    }

    const std::wstring configPath = directory + L"MinecraftHeadTracking.ini";
    // Converted once: writing the file, probing it here and reading it in
    // Start must all name the same bytes, and the conversion is where that can
    // stop being true.
    const std::string configPathAnsi = AnsiPath(configPath);
    WriteDefaultConfig(configPath, configPathAnsi);
    WarnIfByteOrderMarked(configPath);

    cameraunlock::IniReader config;
    if (!config.Open(configPathAnsi)) {
        cameraunlock::logging::Line("Could not open %S; using defaults.", configPath.c_str());
    }
    // Checked before discovery, so an underived build stays dormant whatever
    // the ini says. Discovery drives the camera through the ordinary hook, so
    // it needs the same addresses head tracking does.
    if (result != mcht::builds::SelectResult::Matched) {
        cameraunlock::logging::Line(
            "Dormant. No hooks installed.");
        return 0;
    }

    if (config.ReadBool("Discovery", "Enabled", false)) {
        cameraunlock::logging::Line("Discovery mode is enabled in MinecraftHeadTracking.ini.");
        mcht::discovery::InstallCalibration(
            config.ReadInt("Discovery", "DurationSeconds", kDefaultDiscoverySeconds));
        return 0;
    }

    mcht::tracking::Start(configPathAnsi);
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
