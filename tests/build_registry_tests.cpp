#include <windows.h>

#include <cstdio>
#include <stdexcept>

#include "builds/build_registry.h"

namespace {

mcht::builds::BuildProfile profiles[2] = {};
int codeCalls = 0;
int layoutCalls = 0;
bool codeSucceeds = true;
bool layoutSucceeds = true;

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RejectWithoutResolving(const cameraunlock::memory::PeFingerprint& fingerprint) {
    profiles[0].Fingerprint = fingerprint;
    codeCalls = layoutCalls = 0;
    Check(mcht::builds::SelectProfile() == mcht::builds::SelectResult::Unresolved,
          "Unverified build was accepted");
    Check(codeCalls == 0 && layoutCalls == 0, "Unverified build reached camera resolution");
    Check(!mcht::builds::ActiveCode().Complete(), "Unverified build retained camera addresses");
}

}  // namespace

namespace mcht::builds {

const BuildProfile* const kKnownProfiles[] = {&profiles[0], &profiles[1]};
extern const std::size_t kKnownProfileCount = 2;

bool ResolveCode(ResolvedCode& out, std::uint32_t slot) {
    ++codeCalls;
    Check(slot == 0x538, "Wrong profile used for code resolution");
    out.CameraSetup = 1;
    out.GetRenderCameraComponent = 2;
    return codeSucceeds;
}

bool ResolveLayout(std::uint32_t cameraSetup, ResolvedLayout& out) {
    ++layoutCalls;
    Check(cameraSetup == 1, "Wrong camera passed to layout resolution");
    out = {1, 2, 3, 4, 5};
    return layoutSucceeds;
}

}  // namespace mcht::builds

int main() {
    using namespace mcht::builds;
    cameraunlock::memory::PeFingerprint running{};
    Check(cameraunlock::memory::ReadPeFingerprint(GetModuleHandleW(nullptr), running),
          "Could not read test executable fingerprint");
    profiles[0].Name = "test-newest";
    profiles[0].Offsets.Session.ClientInstanceGetLevel = 0x538;
    profiles[0].Offsets.Session.ClientInstanceGetLocalPlayer = 0xF8;
    profiles[0].Offsets.Session.LevelGetGameRules = 0xAB0;
    profiles[1] = profiles[0];
    profiles[1].Name = "test-older";

    RejectWithoutResolving({0x6AB54E37, 0x12C01000, 0x128CD801});
    auto mismatch = running;
    ++mismatch.TimeDateStamp;
    RejectWithoutResolving(mismatch);
    mismatch = running;
    --mismatch.TimeDateStamp;
    RejectWithoutResolving(mismatch);
    mismatch = running;
    ++mismatch.SizeOfImage;
    RejectWithoutResolving(mismatch);
    mismatch = running;
    ++mismatch.CheckSum;
    RejectWithoutResolving(mismatch);

    profiles[0].Offsets = {};
    RejectWithoutResolving(running);
    profiles[0].Offsets = profiles[1].Offsets;
    profiles[0].Fingerprint = running;
    Check(SelectProfile() == SelectResult::Matched, "Known build was rejected");
    Check(&ActiveProfile() == &profiles[0], "Wrong active profile");
    Check(codeCalls == 1 && layoutCalls == 1, "Known build did not resolve its camera");

    profiles[1].Fingerprint = running;
    profiles[0].Fingerprint.CheckSum ^= 1;
    Check(SelectProfile() == SelectResult::Matched, "Older known build was rejected");
    Check(&ActiveProfile() == &profiles[1], "Newest profile replaced an older exact match");

    codeSucceeds = false;
    layoutCalls = 0;
    Check(SelectProfile() == SelectResult::Unresolved, "Failed code resolution was accepted");
    Check(layoutCalls == 0, "Layout resolved after code resolution failed");
    codeSucceeds = true;
    layoutSucceeds = false;
    Check(SelectProfile() == SelectResult::Unresolved, "Failed layout resolution was accepted");

    std::puts("Build registry tests passed.");
}
