// The config differential test (convert-a-mod-to-the-canonical-config, section 5). Every input
// is read two ways:
//
//   oracle  v1.1.2, the newest published build: its Bootstrap and ReadSettings with the core
//           sources they compiled at its pin 1fd2956, and its hotkey registration
//           (oracle_adapter.h)
//   import  the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, on every input: whether the file was usable, every
// field both read (floats bit for bit), the [Discovery] branch and its duration, the startup
// state, and which actions every key press fires under every set of held modifiers. The
// differences it may find are kComparison1Differences below; there are none.
//
// Inputs: no file, an empty file, the first-run output of every published build (no build
// shipped a config or a launcher seed, so a player's file started as one of these), and core's
// corpus over v1.1.2's first-run output.

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/testing/ini_mutations.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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

}  // namespace

int main() {
    try {
        Scratch scratch;

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

        const std::vector<Input> inputs = Inputs(firstRuns);
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (v1.1.2) against the import:\n");
        for (const char* d : kComparison1Differences) std::printf("  recorded difference: %s\n", d);
        int discovery = 0;
        for (const Input& input : inputs) {
            if (Comparison1(scratch, input).config.discovery_enabled) ++discovery;
        }
        std::printf("  %d inputs take the [Discovery] branch\n", discovery);
        Check(discovery > 0, "no input takes the [Discovery] branch");
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
