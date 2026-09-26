// The config differential test (convert-a-mod-to-the-canonical-config, section 5). Every input
// is read three ways:
//
//   oracle     v1.1.2, the newest published build: its Bootstrap and ReadSettings with the core
//              sources they compiled at its pin 1fd2956, and its hotkey registration
//              (oracle_adapter.h)
//   import     the frozen reader in src/legacy_config/
//   migration  the config owner in a folder holding only MinecraftHeadTracking.ini, the legacy
//              file, importing it into a new CameraUnlock.ini, then the canonical reader and
//              table on that file
//
// Comparison 1, oracle against import, on every input: whether the file was usable, every
// field both read (floats bit for bit), the [Discovery] branch and its duration, the startup
// state, and which actions every key press fires under every set of held modifiers. The
// differences it may find are kComparison1Differences below; there are none.
//
// Comparison 2, import against migration, is the proof for the migration: the settings the mod
// starts on and the actions every key press fires are the import's, apart from the one approved
// change this map applies. A sensitivity or inversion the player set away from its shipped value
// is dropped (pose_shaping), and the import must record each one as dropped; the shipped values,
// pitch and roll inverted, are what TrackerToBedrockRotation applies now. v1.1.2 clamped every
// number it read into a range the canonical rows hold, and refused no file, so no input is
// deferred or refused.
//
// Comparison 2 runs twice, once over a Defaults.ini at the built-in values and once over one a
// player changed, since the migration writes default exactly where the imported value equals
// what Defaults.ini gives. After every load MinecraftHeadTracking.ini keeps its bytes, its write
// time and its attributes, Defaults.ini is never written, and the folder holds the legacy file
// and CameraUnlock.ini and nothing else. The next load reads CameraUnlock.ini, imports nothing
// and writes nothing, and a read-only legacy file imports as a writable one does.
//
// The distinct migrated files are written beside the executable, in a folder named migrated,
// for lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the first-run output of every published build (no build
// shipped a config or a launcher seed, so a player's file started as one of these), and core's
// corpus over v1.1.2's first-run output.

#include "axis_signs.h"
#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// v1.1.2 is the build the frozen reader was taken from, and nothing in src/ that reads the file
// changed between v1.1.2 and the conversion, so comparison 1 finds nothing.
const char* const kComparison1Differences[] = {
    "none: no commit between v1.1.2 (ccc3ba5) and the conversion changed how the file is read",
};

constexpr const char* kFileName = "MinecraftHeadTracking.ini";

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

using Files = std::vector<std::pair<std::string, std::string>>;

Files List(const fs::path& dir) {
    Files files;
    for (const auto& e : fs::directory_iterator(dir)) files.emplace_back(e.path().filename().string(), ReadBytes(e.path()));
    std::sort(files.begin(), files.end());
    return files;
}

void SetReadOnly(const fs::path& path, bool readOnly) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    const DWORD next = readOnly ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (!SetFileAttributesW(path.c_str(), next)) throw std::runtime_error("could not set attributes on " + path.string());
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

// One folder per reading under a root of this process's own, emptied before each input so the
// test never holds more than one input's files.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("mcht-config-differential-" + std::to_string(GetCurrentProcessId()));
        Remove(root_);
        fs::create_directories(root_);
    }
    ~Scratch() {
        try {
            Remove(root_);
        } catch (const std::exception& e) {
            std::printf("  note: %s\n", e.what());
        }
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    // root/leaf, created empty.
    fs::path Clean(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        Remove(dir);
        fs::create_directories(dir);
        return dir;
    }

private:
    // Read-only files included, which remove_all will not delete.
    static void Remove(const fs::path& dir) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(dir, ec);
        if (ec) throw std::runtime_error("could not empty " + dir.string() + ": " + ec.message());
    }

    fs::path root_;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

std::wstring Folder(const fs::path& dir) { return dir.wstring() + L"\\"; }

// Startup state as v1.1.2's ApplySettings derived it: enabled from EnableOnStartup, both
// channels when PositionEnabled and rotation only otherwise, the yaw mode from WorldSpaceYaw.
struct Startup {
    bool enabled;
    int mode;  // 0 rotation and position, 1 rotation only
    bool world_space_yaw;
    bool operator==(const Startup& o) const {
        return enabled == o.enabled && mode == o.mode && world_space_yaw == o.world_space_yaw;
    }
};

Startup StartupOf(const mcht_oracle_view::OracleSettings& s) {
    return {s.enable_on_startup, s.position_enabled ? 0 : 1, s.world_space_yaw};
}

Startup StartupOf(const mcht::legacy::Config& c) {
    return {c.enable_on_startup, c.position_enabled ? 0 : 1, c.world_space_yaw};
}

// Every field the import reads, against the oracle's field of the same name.
std::vector<std::string> FieldDifferences(const mcht_oracle_view::OracleSettings& o, const mcht::legacy::Config& i) {
    std::vector<std::string> d;
    auto b = [&d](const char* n, bool x, bool y) { if (x != y) d.push_back(n); };
    auto f = [&d](const char* n, float x, float y) { if (!SameBits(x, y)) d.push_back(n); };
    auto n = [&d](const char* name, int x, int y) { if (x != y) d.push_back(name); };
    f("yaw_sensitivity", o.yaw_sensitivity, i.yaw_sensitivity);
    f("pitch_sensitivity", o.pitch_sensitivity, i.pitch_sensitivity);
    f("roll_sensitivity", o.roll_sensitivity, i.roll_sensitivity);
    b("invert_yaw", o.invert_yaw, i.invert_yaw);
    b("invert_pitch", o.invert_pitch, i.invert_pitch);
    b("invert_roll", o.invert_roll, i.invert_roll);
    f("local_smoothing", o.local_smoothing, i.local_smoothing);
    f("remote_smoothing", o.remote_smoothing, i.remote_smoothing);
    f("position_sensitivity_x", o.position_sensitivity_x, i.position_sensitivity_x);
    f("position_sensitivity_y", o.position_sensitivity_y, i.position_sensitivity_y);
    f("position_sensitivity_z", o.position_sensitivity_z, i.position_sensitivity_z);
    f("position_limit_x", o.position_limit_x, i.position_limit_x);
    f("position_limit_y", o.position_limit_y, i.position_limit_y);
    f("position_limit_y_down", o.position_limit_y_down, i.position_limit_y_down);
    f("position_limit_z", o.position_limit_z, i.position_limit_z);
    f("position_limit_z_back", o.position_limit_z_back, i.position_limit_z_back);
    b("position_invert_x", o.position_invert_x, i.position_invert_x);
    b("position_invert_y", o.position_invert_y, i.position_invert_y);
    b("position_invert_z", o.position_invert_z, i.position_invert_z);
    b("enable_on_startup", o.enable_on_startup, i.enable_on_startup);
    b("position_enabled", o.position_enabled, i.position_enabled);
    b("world_space_yaw", o.world_space_yaw, i.world_space_yaw);
    n("yaw_mode_key", o.yaw_mode_key, i.yaw_mode_key);
    n("port", o.port, i.port);
    return d;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// The first press whose fired actions differ, for the failure message.
std::string FirstFireDifference(const mcht_oracle_view::FireTable& expected, const mcht_oracle_view::FireTable& got) {
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        if (expected[i] != got[i]) {
            char text[160];
            std::snprintf(text, sizeof text, "key 0x%02X held %d fires %d/%d/%d, not %d/%d/%d",
                          static_cast<int>(i / mcht_oracle_view::kHeldStates) + mcht_oracle_view::kFirstKey,
                          static_cast<int>(i % mcht_oracle_view::kHeldStates), got[i][0], got[i][1], got[i][2],
                          expected[i][0], expected[i][1], expected[i][2]);
            return text;
        }
    }
    return expected.size() == got.size() ? "none" : "the tables differ in size";
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    // The Ctrl+Shift+H chord was hard-coded, not a switch in the file, so it folds into the
    // list from no row.
    MutationKey yawModeKey = plain("Hotkeys", "YawModeKey", "0x70", {"0xFF"});
    yawModeKey.hotkey = true;
    return {
        plain("Discovery", "Enabled", "1"),
        plain("Discovery", "DurationSeconds", "60", {"0", "3601"}),
        plain("Tracking", "YawSensitivity", "0.5", {"0.001", "11"}),
        plain("Tracking", "PitchSensitivity", "0.5", {"0.001", "11"}),
        plain("Tracking", "RollSensitivity", "0.5", {"0.001", "11"}),
        plain("Tracking", "InvertYaw", "1"),
        plain("Tracking", "InvertPitch", "0"),
        plain("Tracking", "InvertRoll", "0"),
        plain("Tracking", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Tracking", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Tracking", "Smoothing", "0.3"),
        plain("Tracking", "EnableOnStartup", "0"),
        plain("Tracking", "WorldSpaceYaw", "0"),
        plain("Position", "SensitivityX", "0.5", {"-1", "6"}),
        plain("Position", "SensitivityY", "0.5", {"-1", "6"}),
        plain("Position", "SensitivityZ", "0.5", {"-1", "6"}),
        plain("Position", "LimitX", "0.4", {"0.001", "0.6"}),
        plain("Position", "LimitY", "0.3", {"0.001", "0.6"}),
        plain("Position", "LimitZ", "0.45", {"0.001", "0.6"}),
        plain("Position", "LimitZBack", "0.2", {"0.001", "0.6"}),
        plain("Position", "InvertX", "1"),
        plain("Position", "InvertY", "1"),
        plain("Position", "InvertZ", "1"),
        plain("Position", "Smoothing", "0.3"),
        plain("Position", "Enabled", "0"),
        yawModeKey,
        plain("Tracking", "Port", "4243", {"1023", "65536"}),
    };
}

struct ImportRun {
    mcht::legacy::Config config;
    mcht::legacy::ReadStatus status = mcht::legacy::ReadStatus::Read;
};

// The import on a read-only copy of the input, which must leave its folder as it found it.
ImportRun RunImport(Scratch& scratch, const Input& input) {
    const fs::path dir = scratch.Clean("import");
    const fs::path file = Place(dir, input);
    if (input.bytes) SetReadOnly(file, true);
    const Files before = List(dir);
    ImportRun run;
    run.status = run.config.Read(file.wstring(), mcht::legacy::AnsiPath(file.wstring()));
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const fs::path odir = scratch.Clean("oracle");
    Place(odir, input);
    const mcht_oracle_view::OracleResult oracle = mcht_oracle_view::RunOracle(Folder(odir));
    const ImportRun import = RunImport(scratch, input);

    Check(input.bytes.has_value() == (import.status == mcht::legacy::ReadStatus::Read),
          input.name + ": the import's status does not match whether there is a file");
    Check(oracle.discovery == import.config.discovery_enabled, input.name + ": the [Discovery] branch differs");
    if (oracle.discovery) {
        Check(oracle.discovery_seconds == import.config.discovery_seconds,
              input.name + ": the discovery duration differs");
    }
    const std::vector<std::string> fields = FieldDifferences(oracle.settings, import.config);
    Check(fields.empty(), input.name + ": fields differ: " + Join(fields));
    Check(StartupOf(oracle.settings) == StartupOf(import.config), input.name + ": startup state differs");
    const mcht_oracle_view::FireTable oracleFires = mcht_oracle_view::OracleFires(oracle.settings.yaw_mode_key);
    const mcht_oracle_view::FireTable importFires = mcht_oracle_view::OracleFires(import.config.yaw_mode_key);
    Check(oracleFires == importFires,
          input.name + ": hotkeys fire differently: " + FirstFireDifference(oracleFires, importFires));
    return import;
}

std::vector<Input> Inputs(const std::vector<std::pair<std::string, std::string>>& firstRuns) {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    for (const auto& [name, bytes] : firstRuns) inputs.push_back({name, bytes});
    for (auto& m : GenerateIniMutations(firstRuns.back().second, mcht::legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + m.name, std::move(m.bytes)});
    }
    return inputs;
}

// ---------------------------------------------------------------------------
// Comparison 2
// ---------------------------------------------------------------------------

namespace cfg = cameraunlock::config;
using cfg::ConfigLoadStatus;
using cfg::DropRule;
using cfg::DroppedValue;
using cfg::ImportResult;
using cfg::ImportStatus;
using mcht::config::Config;

cameraunlock::input::KeyModifiers g_currentHeld = cameraunlock::input::KeyModifiers::kNone;

cameraunlock::input::KeyModifiers CurrentHeld() { return g_currentHeld; }

cameraunlock::input::KeyModifiers ModifiersOf(int held) {
    using cameraunlock::input::KeyModifiers;
    KeyModifiers m = KeyModifiers::kNone;
    if ((held & 1) != 0) m = m | KeyModifiers::kCtrl;
    if ((held & 2) != 0) m = m | KeyModifiers::kShift;
    if ((held & 4) != 0) m = m | KeyModifiers::kAlt;
    return m;
}

// OracleFires' table for the current build. Its Start parses each key list and hands it to
// RegisterKeyBindings, which puts one detail::GuardKey callback per distinct key on the poller,
// holding that key's bindings in list order. The same callbacks are built here with the held
// modifiers read from the test rather than the keyboard, since the poller keeps its callbacks
// to itself.
mcht_oracle_view::FireTable CurrentFires(const Config& m) {
    using mcht_oracle_view::kFirstKey;
    using mcht_oracle_view::kHeldStates;
    using mcht_oracle_view::kLastKey;
    std::array<int, 3> fired{};
    std::vector<std::pair<int, std::function<void()>>> registered;
    const std::string* lists[3] = {&m.toggle_key_name, &m.cycle_tracking_mode_key_name, &m.yaw_mode_key_name};
    for (int action = 0; action < 3; ++action) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*lists[action]);
        if (!parsed.ok()) throw std::logic_error("migrated hotkey list '" + *lists[action] + "' does not parse");
        std::vector<int> keys;
        std::vector<std::vector<cameraunlock::input::KeyModifiers>> modifiers;
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            const auto at = std::find(keys.begin(), keys.end(), b.vk);
            if (at == keys.end()) {
                keys.push_back(b.vk);
                modifiers.push_back({b.modifiers});
            } else {
                modifiers[static_cast<std::size_t>(at - keys.begin())].push_back(b.modifiers);
            }
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            registered.emplace_back(keys[i], cameraunlock::input::detail::GuardKey(
                                                 std::move(modifiers[i]), [&fired, action] { ++fired[action]; },
                                                 &CurrentHeld));
        }
    }

    mcht_oracle_view::FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_currentHeld = ModifiersOf(held);
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
    return table;
}

// A file as the test holds it to: its bytes, its last write time and its attributes.
struct FileStamp {
    std::string bytes;
    FILETIME written{};
    DWORD attributes = 0;

    bool operator==(const FileStamp& o) const {
        return bytes == o.bytes && CompareFileTime(&written, &o.written) == 0 && attributes == o.attributes;
    }
};

FileStamp Stamp(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot stat " + path.string());
    }
    FileStamp s;
    s.bytes = ReadBytes(path);
    s.written = data.ftLastWriteTime;
    s.attributes = data.dwFileAttributes;
    return s;
}

// Where Defaults.ini is for each run of comparison 2: at the built-in values, which the first
// load creates, and with the values a player changed, written from it.
fs::path g_builtinDefaults;
fs::path g_alteredDefaults;

cfg::ConfigOwnerOptions<Config> OwnerOptions(const fs::path& dir, const fs::path& defaults) {
    return mcht::config::MakeConfigOwnerOptions(Folder(dir), cfg::DefaultsFile::At(defaults.wstring()));
}

// The import with its map, for the values it records.
ImportResult RunMappedImport(Scratch& scratch, const Input& input) {
    const fs::path file = Place(scratch.Clean("mapped"), input);
    Config out = mcht::config::MakeConfigTable().defaults();
    return mcht::config::MakeLegacyImport().run(
        cfg::LegacyInput{file.wstring(), mcht::legacy::AnsiPath(file.wstring()), false}, out);
}

bool DroppedAsPoseShaping(const std::vector<DroppedValue>& dropped, const char* section, const char* key) {
    for (const DroppedValue& d : dropped) {
        if (d.rule == DropRule::PoseShaping && d.section == section && d.key == key) return true;
    }
    return false;
}

struct Tally {
    std::string committed;
    std::set<std::string> migrated;
    struct Run {
        int created = 0;
        int imported = 0;
        // Migrated files holding at least one default row.
        int with_default_rows = 0;
        // Migrated files holding a value on at least one row, which the committed file never does.
        int with_values = 0;
    } builtin, altered;
    int with_pose_shaping_dropped = 0;
};

// Every sensitivity and inversion the frozen reader read is listed in its place, folded where it
// holds the value v1.1.2 shipped and dropped as PoseShaping where it does not, and nothing is
// dropped by any other rule.
void CheckDrops(const std::string& name, const mcht::legacy::Config& l, const ImportResult& imported, Tally& tally) {
    const mcht::legacy::Config shipped;
    struct Read {
        const char* section;
        const char* key;
        bool atShipped;
    };
    const Read reads[] = {
        {"Tracking", "YawSensitivity", SameBits(l.yaw_sensitivity, shipped.yaw_sensitivity)},
        {"Tracking", "PitchSensitivity", SameBits(l.pitch_sensitivity, shipped.pitch_sensitivity)},
        {"Tracking", "RollSensitivity", SameBits(l.roll_sensitivity, shipped.roll_sensitivity)},
        {"Tracking", "InvertYaw", l.invert_yaw == shipped.invert_yaw},
        {"Tracking", "InvertPitch", l.invert_pitch == shipped.invert_pitch},
        {"Tracking", "InvertRoll", l.invert_roll == shipped.invert_roll},
        {"Position", "SensitivityX", SameBits(l.position_sensitivity_x, shipped.position_sensitivity_x)},
        {"Position", "SensitivityY", SameBits(l.position_sensitivity_y, shipped.position_sensitivity_y)},
        {"Position", "SensitivityZ", SameBits(l.position_sensitivity_z, shipped.position_sensitivity_z)},
        {"Position", "InvertX", l.position_invert_x == shipped.position_invert_x},
        {"Position", "InvertY", l.position_invert_y == shipped.position_invert_y},
        {"Position", "InvertZ", l.position_invert_z == shipped.position_invert_z},
    };
    Check(imported.pose_shaping.size() == std::size(reads),
          name + ": the import lists " + std::to_string(imported.pose_shaping.size()) + " pose-shaping values, not 12");
    if (imported.pose_shaping.size() != std::size(reads)) return;
    bool anyDropped = false;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = imported.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        Check(v.section == reads[k].section && v.key == reads[k].key, name + ": " + label + " is not listed in its place");
        Check(v.folded == reads[k].atShipped, name + ": " + label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = DroppedAsPoseShaping(imported.dropped, reads[k].section, reads[k].key);
        Check(listed != reads[k].atShipped,
              name + ": " + label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        if (!reads[k].atShipped) anyDropped = true;
    }
    if (anyDropped) ++tally.with_pose_shaping_dropped;
    for (const DroppedValue& d : imported.dropped) {
        Check(d.rule == DropRule::PoseShaping,
              name + ": the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
}

// The settings the mod starts on after the migration against the ones v1.1.2 started on from the
// frozen reader's values, with the approved change applied: the rotation conversion is
// TrackerToBedrockRotation, and the position sensitivities and inversions stay at the identity
// v1.1.2 shipped (CheckDrops holds the import to recording every value it leaves out).
std::vector<std::string> StartupDifferences(const mcht::legacy::Config& l, const Config& m) {
    std::vector<std::string> d;
    if (m.enable_on_startup != l.enable_on_startup) d.push_back("EnableOnStartup");
    if (m.udp_port != l.port) d.push_back("UdpPort");
    if (m.world_space_yaw != l.world_space_yaw) d.push_back("WorldSpaceYaw");
    const auto mode = cameraunlock::DecodeTrackingMode(m.rotation_enabled, m.position_enabled);
    if (!mode || *mode != (l.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                              : cameraunlock::TrackingMode::RotationOnly)) {
        d.push_back("tracking mode");
    }
    if (!SameBits(m.local_smoothing, l.local_smoothing) || !SameBits(m.position.local_smoothing, l.local_smoothing)) {
        d.push_back("LocalSmoothing");
    }
    if (!SameBits(m.remote_smoothing, l.remote_smoothing) || !SameBits(m.position.remote_smoothing, l.remote_smoothing)) {
        d.push_back("RemoteSmoothing");
    }
    if (!SameBits(m.position.limit_x, l.position_limit_x)) d.push_back("PositionLimitX");
    if (!SameBits(m.position.limit_y, l.position_limit_y)) d.push_back("PositionLimitY");
    if (!SameBits(m.position.limit_y_down, l.position_limit_y_down)) d.push_back("PositionLimitYDown");
    if (!SameBits(m.position.limit_z, l.position_limit_z)) d.push_back("PositionLimitZ");
    if (!SameBits(m.position.limit_z_back, l.position_limit_z_back)) d.push_back("PositionLimitZBack");
    const mcht::legacy::Config shipped;
    if (!SameBits(m.position.sensitivity_x, shipped.position_sensitivity_x) ||
        !SameBits(m.position.sensitivity_y, shipped.position_sensitivity_y) ||
        !SameBits(m.position.sensitivity_z, shipped.position_sensitivity_z) ||
        m.position.invert_x != shipped.position_invert_x || m.position.invert_y != shipped.position_invert_y ||
        m.position.invert_z != shipped.position_invert_z) {
        d.push_back("position sensitivity or inversion");
    }
    if (m.run_discovery != l.discovery_enabled) d.push_back("RunDiscovery");
    if (m.discovery_seconds != l.discovery_seconds) d.push_back("DurationSeconds");
    const mcht_oracle_view::FireTable before = mcht_oracle_view::OracleFires(l.yaw_mode_key);
    const mcht_oracle_view::FireTable after = CurrentFires(m);
    if (before != after) d.push_back("hotkeys: " + FirstFireDifference(before, after));
    return d;
}

// Every field the table binds, as the canonical renderer writes it, so two Configs compare whole.
std::string AllValues(const Config& c) {
    return cfg::RenderCanonical(mcht::config::MakeConfigTable(), c, {mcht::config::kConfigDisplayName});
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void Comparison2(Scratch& scratch, const Input& input, const ImportRun& import, const ImportResult* mapped,
                 const fs::path& defaults, Tally& tally) {
    const bool builtin = defaults == g_builtinDefaults;
    Tally::Run& run = builtin ? tally.builtin : tally.altered;
    const std::string name =
        input.name + (builtin ? " (Defaults.ini at the built-in values)" : " (Defaults.ini changed)");

    const fs::path dir = scratch.Clean("migration");
    const fs::path config = dir / mcht::config::kConfigFileName;
    const fs::path legacyFile = Place(dir, input);
    const FileStamp defaultsBefore = Stamp(defaults);
    FileStamp legacyBefore;
    if (input.bytes) legacyBefore = Stamp(legacyFile);

    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(OwnerOptions(dir, defaults)).Load();
    const Files after = List(dir);
    Check(Stamp(defaults) == defaultsBefore, name + ": the load wrote Defaults.ini");

    if (!input.bytes) {
        // A fresh install, which follows Defaults.ini.
        ++run.created;
        Check(loaded.status == ConfigLoadStatus::Created, name + ": no file is not Created");
        Check(after == Files{{mcht::config::kConfigFileName, tally.committed}},
              name + ": the folder does not hold CameraUnlock.ini as config/MinecraftHeadTracking.ini and nothing else");
        if (builtin) {
            const std::vector<std::string> d = StartupDifferences(import.config, loaded.config);
            Check(d.empty(), name + ": comparison 2: " + Join(d));
        }
        return;
    }

    Check(Stamp(legacyFile) == legacyBefore,
          name + ": MinecraftHeadTracking.ini did not keep its bytes, write time and attributes");
    if (builtin) CheckDrops(name, import.config, *mapped, tally);

    {
        const std::vector<std::string> d = StartupDifferences(import.config, loaded.config);
        Check(d.empty(), name + ": comparison 2: " + Join(d));
    }

    ++run.imported;
    Check(loaded.status == ConfigLoadStatus::Migrated,
          name + ": the migration is " + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
    if (loaded.status != ConfigLoadStatus::Migrated) return;
    Check(after.size() == 2 && after[0].first == mcht::config::kConfigFileName && after[1].first == kFileName &&
              after[1].second == *input.bytes,
          name + ": the folder does not hold MinecraftHeadTracking.ini and CameraUnlock.ini and nothing else");
    Check(Contains(loaded.log, "created from"), name + ": the log does not say where CameraUnlock.ini came from");
    const std::string migrated = ReadBytes(config);
    tally.migrated.insert(migrated);
    if (migrated.find("=default\r\n") != std::string::npos) ++run.with_default_rows;
    if (migrated != tally.committed) ++run.with_values;

    // The next launch reads CameraUnlock.ini over the same Defaults.ini, with nothing to report,
    // to the same settings, does not import, and writes neither file.
    {
        const cfg::ConfigLoadResult<Config> reread = cfg::ConfigOwner<Config>(OwnerOptions(dir, defaults)).Load();
        Check(reread.status == ConfigLoadStatus::Canonical && reread.diagnostics.empty(),
              name + ": the next launch does not read CameraUnlock.ini cleanly");
        Check(AllValues(reread.config) == AllValues(loaded.config), name + ": the next launch runs on other settings");
        Check(!Contains(reread.log, "created from"), name + ": the next launch imports again");
        Check(Contains(reread.log, "is left as it was and is not read"),
              name + ": the next launch does not say MinecraftHeadTracking.ini is not read");
        Check(List(dir) == after && Stamp(legacyFile) == legacyBefore && Stamp(defaults) == defaultsBefore,
              name + ": the next launch changed a file");
    }

    // A read-only MinecraftHeadTracking.ini imports as a writable one does and keeps its
    // attribute, bytes and write time.
    if (builtin) {
        const fs::path roDir = scratch.Clean("read-only");
        const fs::path roLegacy = Place(roDir, input);
        SetReadOnly(roLegacy, true);
        const FileStamp roBefore = Stamp(roLegacy);
        const cfg::ConfigLoadResult<Config> fromReadOnly = cfg::ConfigOwner<Config>(OwnerOptions(roDir, defaults)).Load();
        Check(fromReadOnly.status == ConfigLoadStatus::Migrated && AllValues(fromReadOnly.config) == AllValues(loaded.config) &&
                  ReadBytes(roDir / mcht::config::kConfigFileName) == migrated,
              name + ": a read-only MinecraftHeadTracking.ini does not import as a writable one does");
        Check(Stamp(roLegacy) == roBefore && (roBefore.attributes & FILE_ATTRIBUTE_READONLY) != 0,
              name + ": a read-only MinecraftHeadTracking.ini did not keep its attribute, bytes and write time");
    }
}

// Defaults.ini as a player may have changed it, from the one the owner created: every value this
// game takes from it differs from the built-in one, each set to the corpus's alternate for the
// legacy key it comes from where there is one, so a corpus input holding that alternate migrates
// as default.
void WriteAlteredDefaults() {
    std::string text = ReadBytes(g_builtinDefaults);
    const std::pair<const char*, const char*> changes[] = {
        {"UdpPort=4242", "UdpPort=4243"},
        {"EnableOnStartup=true", "EnableOnStartup=false"},
        {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
        {"PositionEnabled=true", "PositionEnabled=false"},
        {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
        {"RemoteSmoothing=0.15", "RemoteSmoothing=0.3"},
        {"PositionLimitX=0.3", "PositionLimitX=0.4"},
        {"PositionLimitY=0.2", "PositionLimitY=0.3"},
        {"PositionLimitYDown=0.2", "PositionLimitYDown=0.3"},
        {"PositionLimitZ=0.4", "PositionLimitZ=0.45"},
        {"PositionLimitZBack=0.1", "PositionLimitZBack=0.2"},
        {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F2, Ctrl+Shift+Y"},
        {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F3, Ctrl+Shift+G"},
        {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F1, Ctrl+Shift+H"},
    };
    for (const auto& [from, to] : changes) {
        const std::string line = std::string("\r\n") + from + "\r\n";
        const size_t at = text.find(line);
        if (at == std::string::npos) throw std::runtime_error(std::string("the created Defaults.ini has no line ") + from);
        text.replace(at + 2, std::strlen(from), to);
    }
    fs::create_directories(g_alteredDefaults.parent_path());
    WriteBytes(g_alteredDefaults, text);
}

}  // namespace

int main() {
    try {
        Scratch scratch;
        Tally tally;
        tally.committed = ReadBytes(fs::path(MCHT_COMMITTED_CONFIG));

        // Each Defaults.ini sits outside the mod folder, in a user folder of its own whose
        // parent exists, as the owner requires before it creates the file.
        g_builtinDefaults = scratch.Clean("user-builtin") / "CameraUnlock" / "Defaults.ini";
        g_alteredDefaults = scratch.Clean("user-altered") / "CameraUnlock" / "Defaults.ini";
        {
            const fs::path dir = scratch.Clean("first-load");
            Check(cfg::ConfigOwner<Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status == ConfigLoadStatus::Created,
                  "the first load is not Created");
            Check(fs::exists(g_builtinDefaults), "the first load did not create Defaults.ini");
        }
        WriteAlteredDefaults();

        // The first-run output of every published build, extracted once from each and
        // committed as test data. The last is what v1.1.0 and v1.1.2 write too, and so what
        // the oracle still writes for a missing file.
        const fs::path data(MCHT_DIFFERENTIAL_DATA);
        std::vector<std::pair<std::string, std::string>> firstRuns;
        for (const char* file : {"v0.1.0-first-run.ini", "v1.0.0-first-run.ini", "v1.0.1-first-run.ini"}) {
            firstRuns.emplace_back(std::string("first-run output ") + file, ReadBytes(data / file));
        }
        {
            const fs::path dir = scratch.Clean("first-run");
            mcht_oracle_view::RunOracle(Folder(dir));
            Check(ReadBytes(dir / kFileName) == firstRuns.back().second,
                  "the oracle's first-run output differs from data/v1.0.1-first-run.ini");
        }

        // Fresh equals upgrade: over Defaults.ini at the built-in values, v1.1.2's first-run
        // output imports into a CameraUnlock.ini that is the committed file, which is what a
        // fresh install creates.
        {
            const fs::path dir = scratch.Clean("fresh-equals-upgrade");
            WriteBytes(dir / kFileName, firstRuns.back().second);
            Check(cfg::ConfigOwner<Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status == ConfigLoadStatus::Migrated &&
                      ReadBytes(dir / mcht::config::kConfigFileName) == tally.committed,
                  "v1.1.2's first-run output does not import into the committed file");
        }

        const std::vector<Input> inputs = Inputs(firstRuns);
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (v1.1.2) against the import:\n");
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        int discovery = 0;
        for (const Input& input : inputs) {
            const ImportRun import = Comparison1(scratch, input);
            if (import.config.discovery_enabled) ++discovery;
            std::optional<ImportResult> mapped;
            if (input.bytes) {
                mapped = RunMappedImport(scratch, input);
                Check(mapped->status == ImportStatus::Imported, input.name + ": the mapped import is not Imported");
            }
            for (const fs::path& defaults : {g_builtinDefaults, g_alteredDefaults}) {
                Comparison2(scratch, input, import, mapped ? &*mapped : nullptr, defaults, tally);
            }
        }
        std::printf("  %d inputs take the [Discovery] branch\n", discovery);
        Check(discovery > 0, "no input takes the [Discovery] branch");

        std::printf("comparison 2, the import against the migration, %zu distinct files:\n", tally.migrated.size());
        for (const auto& [over, run] : {std::pair<const char*, const Tally::Run*>{"at the built-in values", &tally.builtin},
                                        std::pair<const char*, const Tally::Run*>{"changed", &tally.altered}}) {
            std::printf("  over Defaults.ini %s: %d created, %d imported (%d holding a default row, %d a value)\n",
                        over, run->created, run->imported, run->with_default_rows, run->with_values);
            Check(run->with_default_rows > 0, std::string("no import writes default over ") + over);
            Check(run->with_values > 0, std::string("no import writes a value over ") + over);
        }
        std::printf("  %d with a changed sensitivity or inversion dropped (pose_shaping)\n",
                    tally.with_pose_shaping_dropped);
        Check(tally.with_pose_shaping_dropped > 0, "no input drops a changed pose-shaping value");
        Check(tally.migrated.count(tally.committed) == 1, "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path lintDir = fs::path(exe).parent_path() / "migrated";
        fs::remove_all(lintDir);
        fs::create_directories(lintDir);
        int n = 0;
        for (const std::string& file : tally.migrated) WriteBytes(lintDir / (std::to_string(n++) + ".ini"), file);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
