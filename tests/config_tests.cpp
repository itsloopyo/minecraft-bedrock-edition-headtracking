// CameraUnlock.ini and what the conversion moved into code: the committed file is the table's
// fresh render, the defaults v1.1.2 ran on map to the defaults, a first start creates the
// committed file, the toggles save only their own lines and leave Defaults.ini and
// MinecraftHeadTracking.ini alone, End's row cannot be saved, and the import finds
// MinecraftHeadTracking.ini by the ANSI path v1.1.2 opened it by.
//
// `--render-config <path>` writes the committed file instead (pixi run render-config).

#include "axis_signs.h"
#include "config.h"
#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/config_table.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mcht::config;

namespace {

namespace cfg = cameraunlock::config;
namespace fs = std::filesystem;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The file a first start creates.
std::string Rendered() {
    return cfg::RenderCanonicalFresh(MakeConfigTable(), {kConfigDisplayName});
}

std::string Committed() {
    return ReadBytes(fs::path(MCHT_COMMITTED_CONFIG));
}

void TestCommittedConfigIsRendered() {
    Check(Committed() == Rendered(), "config/MinecraftHeadTracking.ini is the table's fresh render (pixi run render-config)");
}

// A scratch mod folder, and a Defaults.ini of its own beside it that the first load creates.
struct Scratch {
    fs::path root;
    fs::path mod;
    fs::path defaults;

    explicit Scratch(const std::wstring& modFolder) {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        root = fs::path(temp) / (L"mcht-config-tests-" + std::to_wstring(GetCurrentProcessId()));
        mod = root / modFolder;
        fs::remove_all(mod);
        fs::create_directories(mod);
        fs::create_directories(root / L"user");
        defaults = root / L"user" / L"CameraUnlock" / L"Defaults.ini";
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    cfg::ConfigOwnerOptions<Config> Options() const {
        return MakeConfigOwnerOptions(mod.wstring() + L"\\", cfg::DefaultsFile::At(defaults.wstring()));
    }
    fs::path ConfigPath() const { return mod / kConfigFileName; }
    fs::path LegacyPath() const { return mod / kLegacyConfigFileName; }
};

std::vector<std::string> Listing(const fs::path& dir) {
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

std::string AllValues(const Config& c) {
    return cfg::RenderCanonical(MakeConfigTable(), c, {kConfigDisplayName});
}

// With neither file there, the first start creates CameraUnlock.ini as the committed file and
// Defaults.ini with the built-in values, and no MinecraftHeadTracking.ini.
void TestFirstStartCreatesTheCommittedFile() {
    const Scratch s(L"first-start");
    const auto loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "a first start with no file is Created");
    Check(ReadBytes(s.ConfigPath()) == Committed(), "a first start creates config/MinecraftHeadTracking.ini's bytes");
    Check(Listing(s.mod) == std::vector<std::string>{kConfigFileName}, "a first start creates CameraUnlock.ini and nothing else");
    Check(fs::exists(s.defaults), "a first start creates Defaults.ini");
    Check(AllValues(loaded.config) == AllValues(MakeConfigTable().defaults()), "a first start runs on the built-in values");
    Check(loaded.config.world_space_yaw, "yaw starts world-locked");
    Check(!loaded.config.run_discovery && loaded.config.discovery_seconds == 40, "discovery starts off, at 40 seconds");
}

// A fresh install and an upgrade from v1.1.2's defaults start the same: the map of the frozen
// defaults holds every row at the table's default, drops nothing, and every pose-shaping value
// is the shipped one, which TrackerToBedrockRotation now applies.
void TestLegacyDefaultsMapToTheDefaults() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring missing = (fs::path(temp) / L"mcht-no-such-folder" / kLegacyConfigFileName).wstring();
    const auto table = MakeConfigTable();
    Config mapped = table.defaults();
    const cfg::ImportResult result =
        MakeLegacyImport().run(cfg::LegacyInput{missing, mcht::legacy::AnsiPath(missing), false}, mapped);
    Check(result.status == cfg::ImportStatus::Absent, "no file imports as Absent");
    Check(result.dropped.empty(), "the old defaults drop nothing");
    Check(result.pose_shaping.size() == 12, "every sensitivity and inversion is recorded");
    for (const cfg::PoseShapingValue& value : result.pose_shaping) {
        Check(value.folded, "[" + value.section + "] " + value.key + " at its shipped value is folded");
    }
    Check(AllValues(mapped) == AllValues(table.defaults()), "the old defaults map to the defaults");
    Check(mapped.toggle_key_name == "End, Ctrl+Shift+Y" && mapped.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G" &&
              mapped.yaw_mode_key_name == "PageDown, Ctrl+Shift+H",
          "the old hotkeys and their chords become the fleet's key lists");

    const mcht::legacy::Config shipped;
    const cameraunlock::SensitivitySettings boundary = mcht::tracking::TrackerToBedrockRotation();
    Check(boundary.invert_pitch == shipped.invert_pitch && boundary.invert_roll == shipped.invert_roll &&
              boundary.invert_yaw == shipped.invert_yaw,
          "the axis conversion is the inversion v1.1.2 shipped");
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A save changes the lines of its rows and no other byte, writes a value over default, and
// touches neither Defaults.ini nor MinecraftHeadTracking.ini; the yaw mode and the tracking mode
// persist, and End's row cannot be saved at all.
void TestTogglesSave() {
    const Scratch s(L"save");
    const std::string committed = Committed();
    const std::string legacyBytes = "[Tracking]\r\nEnableOnStartup=0\r\n";
    WriteBytes(s.ConfigPath(), committed);
    WriteBytes(s.LegacyPath(), legacyBytes);

    {
        cfg::ConfigOwner<Config> owner(s.Options());
        const auto loaded = owner.Load();
        Check(loaded.status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as canonical");
        Check(loaded.config.enable_on_startup, "MinecraftHeadTracking.ini is not read while CameraUnlock.ini exists");
        Check(Contains(loaded.log, "is left as it was and is not read"),
              "the log says MinecraftHeadTracking.ini is not read while CameraUnlock.ini exists");
        const std::string defaultsBefore = ReadBytes(s.defaults);

        const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
        Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw mode saves");
        Check(Contains(yaw.log, "WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini"),
              "the yaw save says WorldSpaceYaw stopped following Defaults.ini");
        const std::string afterYaw = ReadBytes(s.ConfigPath());
        Check(ChangedLines(committed, afterYaw) == std::vector<std::string>{"WorldSpaceYaw=false"},
              "saving the yaw mode writes its value over default and changes nothing else");

        const auto rotationOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
        Check(owner.Save([rotationOnly](Config& c) {
                  c.rotation_enabled = rotationOnly.rotation_enabled;
                  c.position_enabled = rotationOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
              "the tracking mode saves");
        const std::string afterRotationOnly = ReadBytes(s.ConfigPath());
        Check(ChangedLines(afterYaw, afterRotationOnly) == std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"},
              "saving rotation only writes the mode pair over default and changes nothing else");

        const auto positionOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
        Check(owner.Save([positionOnly](Config& c) {
                  c.rotation_enabled = positionOnly.rotation_enabled;
                  c.position_enabled = positionOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
              "the third tracking mode saves");
        const std::string afterPositionOnly = ReadBytes(s.ConfigPath());
        Check(ChangedLines(afterRotationOnly, afterPositionOnly) ==
                  std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"},
              "saving position only changes the mode pair and nothing else");

        Check(owner.Save([](Config&) {}).status == cfg::ConfigSaveStatus::Saved, "an empty save succeeds");
        Check(ReadBytes(s.ConfigPath()) == afterPositionOnly, "an empty save writes nothing");

        bool refused = false;
        try {
            owner.Save([](Config& c) { c.enable_on_startup = false; });
        } catch (const std::logic_error&) {
            refused = true;
        }
        Check(refused, "EnableOnStartup is not Writable, so the End toggle cannot persist");
        Check(ReadBytes(s.ConfigPath()) == afterPositionOnly, "a refused save writes nothing");

        Check(ReadBytes(s.defaults) == defaultsBefore, "saving leaves Defaults.ini as it was");
        Check(ReadBytes(s.LegacyPath()) == legacyBytes, "saving leaves MinecraftHeadTracking.ini as it was");
    }

    const auto again = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical && again.diagnostics.empty() && !again.config.world_space_yaw &&
              !again.config.rotation_enabled && again.config.position_enabled && again.config.enable_on_startup,
          "the saved yaw and tracking mode come back at the next start");
    Check((Listing(s.mod) == std::vector<std::string>{kConfigFileName, kLegacyConfigFileName}),
          "the mod folder holds CameraUnlock.ini and MinecraftHeadTracking.ini and nothing else");
}

// v1.1.2 opened its file by the ANSI path it built with best-fit mapping. In a folder whose
// name the ANSI code page cannot hold, that path names another file or none, so the import has
// to look where v1.1.2 looked, or a player there would get settings v1.1.2 never ran on.
void TestAFolderTheCodepageCannotNameImportsAsThePublishedBuildReadIt() {
    const Scratch s(L"mcht-\x4E2D");
    WriteBytes(s.LegacyPath(), "[Tracking]\r\nPort=5000\r\n");
    const std::string ansi = mcht::legacy::AnsiPath(s.LegacyPath().wstring());
    const bool ansiFindsTheFile = GetFileAttributesA(ansi.c_str()) != INVALID_FILE_ATTRIBUTES;
    const auto loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Migrated, "the file is not Migrated");
    Check(loaded.config.udp_port == (ansiFindsTheFile ? 5000 : 4242),
          ansiFindsTheFile ? "the file was not read through its ANSI path"
                           : "a file v1.1.2 could not find did not import as its defaults");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            WriteBytes(argv[2], Rendered());
            return 0;
        }
        if (argc != 1) {
            std::printf("usage: %s [--render-config <path>]\n", argv[0]);
            return 2;
        }

        TestCommittedConfigIsRendered();
        TestLegacyDefaultsMapToTheDefaults();
        TestFirstStartCreatesTheCommittedFile();
        TestTogglesSave();
        TestAFolderTheCodepageCannotNameImportsAsThePublishedBuildReadIt();
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config tests: all passed\n");
        return 0;
    }
    std::printf("config tests: %d failure(s)\n", g_failures);
    return 1;
}
