// Tests for the parts of this mod that can run without a game attached: the
// range checks on everything that reaches it from an ini file or a command
// line, the render thread's frame clock, the pose and view composition the
// camera hook and the crosshair share, the crosshair projection itself, the
// held-pose ease-out, and the pinned per-build addresses.
//
// Same shape as cameraunlock-core/cpp/tests: a plain main, a Check helper, no
// framework.

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

#include "aim_projection.h"
#include "builds/build_profile.h"
#include "common/bounds.h"
#include "frame_timing.h"
#include "held_pose.h"
#include "matrix4.h"
#include "pose_composition.h"
#include "tracking_settings.h"

namespace mcht::builds {
extern const BuildProfile kStoreProfile_20260806;
extern const BuildProfile kStoreProfile_20260812;
}

namespace {

int g_failures = 0;

void Check(bool condition, const char* name) {
    if (condition) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool NearEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

bool MatrixNearEqual(const float* a, const float* b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) {
        if (!NearEqual(a[i], b[i], eps)) {
            return false;
        }
    }
    return true;
}

void TestTrackerPort() {
    using namespace mcht::bounds;
    Check(ValidTrackerPort(4242), "port: the OpenTrack default is accepted");
    Check(ValidTrackerPort(kMinTrackerPort) && ValidTrackerPort(kMaxTrackerPort),
          "port: both ends of the range are accepted");
    // 0 binds an ephemeral port the tracker can never find; 65536 and 70000
    // truncate through uint16_t into 0 and 4464.
    Check(!ValidTrackerPort(0), "port: 0 is rejected rather than bound as ephemeral");
    Check(!ValidTrackerPort(65536) && !ValidTrackerPort(70000),
          "port: values that would truncate through uint16_t are rejected");
    Check(!ValidTrackerPort(-1), "port: negative is rejected");
    Check(!ValidTrackerPort(80), "port: privileged ports are rejected");
}

void TestDiscoverySeconds() {
    using namespace mcht::bounds;
    Check(ClampDiscoverySeconds(40) == 40, "discovery: an in-range duration is untouched");
    // The caller multiplies the result by 1000 into an int. Anything above
    // 2147483 overflows that, which is undefined behaviour.
    Check(ClampDiscoverySeconds(2147483647) == kMaxDiscoverySeconds,
          "discovery: a duration that would overflow the x1000 is clamped");
    Check(static_cast<long long>(ClampDiscoverySeconds(2147483647)) * 1000 < 2147483647LL,
          "discovery: the clamped duration times 1000 still fits an int");
    Check(ClampDiscoverySeconds(-1) == kMinDiscoverySeconds,
          "discovery: a negative duration is clamped up, not run as zero-length");
    Check(ClampDiscoverySeconds(0) == kMinDiscoverySeconds, "discovery: zero is clamped up");
}

void TestWaitSeconds() {
    using namespace mcht::bounds;
    unsigned long seconds = 0;

    Check(ParseWaitSeconds(L"120", seconds) && seconds == 120,
          "--wait: a plain number parses");
    Check(ParseWaitSeconds(L"1", seconds) && seconds == 1, "--wait: the minimum parses");
    Check(ParseWaitSeconds(L"3600", seconds) && seconds == kMaxWaitSeconds,
          "--wait: the maximum parses");

    // -1 through the old static_cast<DWORD> became 4294967295, and the deadline
    // GetTickCount64() + that * 1000 lands about 136 years out: the launcher
    // waits forever instead of reporting that the game did not start.
    Check(!ParseWaitSeconds(L"-1", seconds), "--wait: negative is rejected, not wrapped");
    Check(!ParseWaitSeconds(L"0", seconds), "--wait: zero is rejected");
    Check(!ParseWaitSeconds(L"99999", seconds), "--wait: above the maximum is rejected");
    Check(!ParseWaitSeconds(L"4294967296", seconds),
          "--wait: a value past 32 bits is rejected rather than saturated");
    Check(!ParseWaitSeconds(L"abc", seconds), "--wait: non-numeric is rejected, not read as 0");
    Check(!ParseWaitSeconds(L"12abc", seconds), "--wait: trailing text is rejected");
    Check(!ParseWaitSeconds(L"", seconds), "--wait: empty is rejected");
    Check(!ParseWaitSeconds(nullptr, seconds), "--wait: null is rejected");

    seconds = 4242;
    ParseWaitSeconds(L"nonsense", seconds);
    Check(seconds == 4242, "--wait: a rejected value leaves the caller's default alone");
}

void TestFrameDelta() {
    using namespace mcht::tracking;

    Check(NearEqual(NormalizeFrameDelta(1.0f / 120.0f), 1.0f / 120.0f),
          "frame delta: a plausible measurement is passed through");
    Check(NearEqual(NormalizeFrameDelta(kMaxPlausibleFrameSeconds), kMaxPlausibleFrameSeconds),
          "frame delta: the top of the plausible range is still accepted");

    // A stall - alt-tab, a loading screen, a breakpoint - is not a frame.
    // Feeding it to the smoothing collapses the filter to the raw sample and
    // snaps the view on the frame the game comes back.
    Check(NearEqual(NormalizeFrameDelta(5.0f), kFallbackFrameSeconds),
          "frame delta: a stall falls back rather than snapping the smoothing");
    Check(NearEqual(NormalizeFrameDelta(0.0f), kFallbackFrameSeconds),
          "frame delta: zero falls back rather than dividing through the smoothing");
    Check(NearEqual(NormalizeFrameDelta(-0.5f), kFallbackFrameSeconds),
          "frame delta: a backwards clock falls back");
}

void TestHeldPose() {
    using namespace mcht::tracking;
    using cameraunlock::math::Vec3;

    HeldPose held;
    Check(!held.Valid, "held pose: nothing is held before the first sample");

    held.Set(10.0f, -6.0f, 3.0f, Vec3(0.1f, -0.2f, 0.05f));
    Check(held.Valid && !held.IsSettled(),
          "held pose: a real pose is held and is not mistaken for neutral");

    held.Decay(0.5f);
    Check(NearEqual(held.Yaw, 5.0f) && NearEqual(held.Pitch, -3.0f) &&
              NearEqual(held.Roll, 1.5f) && NearEqual(held.Offset.x, 0.05f) &&
              NearEqual(held.Offset.y, -0.1f) && NearEqual(held.Offset.z, 0.025f),
          "held pose: decay scales rotation and offset by the same fraction");

    held.Decay(0.0f);
    Check(held.IsSettled(), "held pose: a fully decayed pose reads as settled");

    // In position-only mode the held rotation is already zero, so a
    // rotation-only settle test would end the ease on its first frame and cut
    // the lean back instead of easing it.
    HeldPose leaning;
    leaning.Set(0.0f, 0.0f, 0.0f, Vec3(0.0f, 0.0f, 0.2f));
    Check(!leaning.IsSettled(),
          "held pose: a lean with no rotation is not settled, so position-only eases out");

    HeldPose tiny;
    tiny.Set(0.0f, 0.0f, 0.0f, Vec3(0.0f, 0.0f, kSettledMetres * 0.5f));
    Check(tiny.IsSettled(), "held pose: an imperceptible lean reads as settled");
}

// What applies when there is no readable ini. This is not a cosmetic default:
// Start() hands these to the processors, whose OWN constructed defaults differ,
// so anything wrong here is a mod that behaves differently for the user whose
// config file could not be opened.
void TestSettingsDefaults() {
    using namespace mcht::tracking;

    const Settings defaults;

    // Bedrock's post-view transform runs pitch and roll opposite to the
    // OpenTrack convention. The ini this mod writes says so; these are the
    // values that apply when that ini cannot be read, and the two used to
    // disagree - so an unreadable ini nodded and leaned the wrong way.
    Check(defaults.Sensitivity.invert_pitch,
          "settings: pitch is inverted by default, matching the ini the mod writes");
    Check(defaults.Sensitivity.invert_roll,
          "settings: roll is inverted by default, matching the ini the mod writes");
    Check(!defaults.Sensitivity.invert_yaw, "settings: yaw is not inverted by default");

    Check(NearEqual(defaults.Sensitivity.yaw, 1.0f) && NearEqual(defaults.Sensitivity.pitch, 1.0f) &&
              NearEqual(defaults.Sensitivity.roll, 1.0f),
          "settings: every sensitivity defaults to 1.0");
    Check(defaults.Port == kDefaultTrackerPort && mcht::bounds::ValidTrackerPort(defaults.Port),
          "settings: the default port is the OpenTrack one and is in range");
    Check(defaults.YawModeKey == kDefaultYawModeKey,
          "settings: the default yaw-mode key is Page Down");
    Check(defaults.EnableOnStartup && defaults.PositionEnabled && defaults.WorldSpaceYaw,
          "settings: tracking, position and world-locked yaw are all on by default");
    Check(NearEqual(defaults.Position.limit_z, 0.40f) &&
              NearEqual(defaults.Position.limit_z_back, 0.10f),
          "settings: the Z limits stay asymmetric, more forward travel than back");

    // These are now the single source for three things at once: the values the
    // mod writes into a fresh ini, the fallbacks the reader passes for keys
    // that are absent, and what applies with no ini at all. Pinned here so a
    // change to the shared library's PositionSettings::Default cannot move this
    // mod's behaviour without a test saying so.
    // Zero, and it stays zero. There is no floor underneath it any more, so a
    // player on this machine gets the tracker's own latency and nothing added.
    Check(NearEqual(defaults.LocalSmoothing, 0.0f),
          "settings: a tracker on this machine gets no smoothing by default, and no floor");
    Check(NearEqual(defaults.RemoteSmoothing, 0.15f),
          "settings: a tracker on the network gets 0.15 by default, for the jitter it adds");
    Check(!NearEqual(defaults.LocalSmoothing, defaults.RemoteSmoothing),
          "settings: the two defaults differ, so the connection check decides something");
    Check(NearEqual(defaults.Position.sensitivity_x, 1.0f) &&
              NearEqual(defaults.Position.sensitivity_y, 1.0f) &&
              NearEqual(defaults.Position.sensitivity_z, 1.0f),
          "settings: every position sensitivity defaults to 1.0");
    Check(NearEqual(defaults.Position.limit_x, 0.30f) &&
              NearEqual(defaults.Position.limit_y, 0.20f),
          "settings: the X and Y position limits are the catalogue defaults");
    // Position carries the same pair as rotation rather than a knob of its own,
    // so the two pipelines cannot be tuned into disagreeing about how much lag
    // one head movement has.
    Check(NearEqual(defaults.Position.local_smoothing, defaults.LocalSmoothing) &&
              NearEqual(defaults.Position.remote_smoothing, defaults.RemoteSmoothing),
          "settings: position smoothing is the same pair rotation uses, not a separate setting");
    Check(!defaults.Position.invert_x && !defaults.Position.invert_y && !defaults.Position.invert_z,
          "settings: no position axis is inverted by default");
}

// The frame clock is the render thread's only measure of elapsed time, and the
// hold path depends on NOT advancing it: a tracker that stops sending must be
// processed with a delta spanning the whole gap when it comes back.
void TestFrameClock() {
    using namespace mcht::tracking;
    using namespace std::chrono;

    const steady_clock::time_point start{};

    FrameClock clock;
    Check(NearEqual(clock.Advance(start), kFallbackFrameSeconds),
          "frame clock: the first frame reports the fallback, not a delta from epoch");

    const auto later = start + milliseconds(10);
    Check(NearEqual(clock.Advance(later), 0.010f, 1e-3f),
          "frame clock: a subsequent frame reports the measured interval");

    // A stall goes through NormalizeFrameDelta rather than reaching the
    // smoothing.
    Check(NearEqual(clock.Advance(later + seconds(3)), kFallbackFrameSeconds),
          "frame clock: a stall falls back rather than snapping the smoothing");

    // Not advancing is what holding a pose does, so the gap accumulates.
    FrameClock held;
    held.Advance(start);
    Check(NearEqual(held.Advance(start + milliseconds(200)), 0.200f, 1e-3f),
          "frame clock: skipping frames leaves the next delta spanning the whole gap");
}

// The composition the camera hook applies, exercised without a game attached.
// Every ordering below is identical on a single axis and wrong on a combined
// pose, which is exactly why it is pinned here rather than eyeballed in game.
void TestPoseComposition() {
    using namespace mcht::camera;
    using namespace mcht::math;
    using cameraunlock::math::Vec3;

    Pose pose = {};
    BuildPose(20.0f, -10.0f, 5.0f, Vec3(0.1f, 0.2f, 0.3f), false, pose);

    Check(!pose.WorldSpaceYaw && NearEqual(pose.WorldYawDegrees, 0.0f),
          "pose: camera-local mode carries no world yaw angle");

    // The tracker-to-camera axis mapping, and the negation that makes the
    // offset translate the VIEW rather than the camera. A sign flip here sends
    // leaning the wrong way and is invisible in any single-axis check.
    Check(NearEqual(pose.Offset[12], 0.1f) && NearEqual(pose.Offset[13], -0.2f) &&
              NearEqual(pose.Offset[14], -0.3f),
          "pose: the position offset maps X inverted, Y and Z negated, into the last row");

    float yawM[16];
    float pitchM[16];
    float rollM[16];
    float rollPitch[16];
    float expected[16];
    AxisRotation(kAxisYaw, 20.0f, yawM);
    AxisRotation(kAxisPitch, -10.0f, pitchM);
    AxisRotation(kAxisRoll, 5.0f, rollM);
    Multiply(rollM, pitchM, rollPitch);
    Multiply(rollPitch, yawM, expected);
    Check(MatrixNearEqual(pose.Rotation, expected),
          "pose: rotation composes roll then pitch then yaw, so yaw applies last");

    // The same claim stated as an observable: in the correct order a pure
    // yaw+pitch leaves view right in the horizontal plane. The reversed order
    // picks up about 16 degrees of parasitic roll at 30/30.
    Pose combined = {};
    BuildPose(30.0f, 30.0f, 0.0f, Vec3(), false, combined);
    Check(NearEqual(combined.Rotation[1], 0.0f),
          "pose: a combined yaw and pitch introduces no parasitic roll");

    // World-space mode hands the yaw over as an angle instead, because the axis
    // it turns about is only knowable inside the hook.
    Pose world = {};
    BuildPose(40.0f, 0.0f, 0.0f, Vec3(), true, world);
    float identity[16];
    Identity(identity);
    Check(world.WorldSpaceYaw && NearEqual(world.WorldYawDegrees, 40.0f) &&
              MatrixNearEqual(world.Rotation, identity),
          "pose: world-space mode leaves yaw out of the rotation and carries it as an angle");
}

// transformOut is what the camera receives; deltaOut is what the crosshair
// projects through. The two are different matrices, and confusing them unglues
// the crosshair from the aim point.
void TestViewComposition() {
    using namespace mcht::camera;
    using namespace mcht::math;
    using cameraunlock::math::Vec3;

    float identity[16];
    Identity(identity);

    Pose pose = {};
    BuildPose(15.0f, 8.0f, 0.0f, Vec3(0.1f, 0.0f, 0.0f), false, pose);

    float head[16];
    Multiply(pose.Offset, pose.Rotation, head);

    float transform[16];
    float delta[16];
    ComposeCameraLocal(pose, identity, transform, delta);
    Check(MatrixNearEqual(transform, head) && MatrixNearEqual(delta, head),
          "compose: against an untouched camera the transform and the delta are the same");

    // The delta must NOT carry the game's own post-view transform: it is the
    // clean-to-tracked step, and folding the camera's shake into it would move
    // the crosshair off the aim point every time the game shook the view.
    float saved[16];
    AxisRotation(kAxisPitch, 22.0f, saved);
    float withSaved[16];
    float deltaWithSaved[16];
    ComposeCameraLocal(pose, saved, withSaved, deltaWithSaved);
    float expected[16];
    Multiply(saved, head, expected);
    Check(MatrixNearEqual(withSaved, expected),
          "compose: camera-local applies the head movement outermost, saved * head");
    Check(MatrixNearEqual(deltaWithSaved, head),
          "compose: the crosshair delta ignores the camera's own transform");

    // The 6DOF offset has to survive into the delta, or leaning moves the view
    // and leaves the crosshair behind. Isolated from rotation, because the
    // offset composes ahead of it and so arrives at the delta rotated.
    Pose leanOnly = {};
    BuildPose(0.0f, 0.0f, 0.0f, Vec3(0.1f, 0.0f, 0.0f), false, leanOnly);
    float leanTransform[16];
    float leanDelta[16];
    ComposeCameraLocal(leanOnly, saved, leanTransform, leanDelta);
    Check(NearEqual(leanDelta[12], 0.1f) && NearEqual(leanDelta[13], 0.0f) &&
              NearEqual(leanDelta[14], 0.0f),
          "compose: a lean reaches the crosshair delta through the translation row");

    // World-space yaw goes AHEAD of the game's view transform, which is what
    // makes it a turn about the world's up axis rather than the camera's.
    const float up[3] = {0.0f, 1.0f, 0.0f};
    Pose worldPose = {};
    BuildPose(15.0f, 0.0f, 0.0f, Vec3(), true, worldPose);

    float worldTransform[16];
    float worldDelta[16];
    ComposeWorldYaw(worldPose, up, saved, worldTransform, worldDelta);

    float worldYaw[16];
    AxisAngleRotation(up, 15.0f, worldYaw);
    float ahead[16];
    float expectedWorld[16];
    Multiply(worldYaw, saved, ahead);
    Multiply(ahead, worldPose.Rotation, expectedWorld);
    Check(MatrixNearEqual(worldTransform, expectedWorld),
          "compose: world yaw multiplies in ahead of the camera's own view transform");

    float expectedWorldDelta[16];
    Multiply(worldYaw, worldPose.Rotation, expectedWorldDelta);
    Check(MatrixNearEqual(worldDelta, expectedWorldDelta),
          "compose: the world-yaw crosshair delta also drops the camera's own transform");

    // On a level camera the two yaw modes must agree exactly, or toggling yaw
    // mode while looking at the horizon would visibly jump the view.
    Pose localYaw = {};
    BuildPose(15.0f, 0.0f, 0.0f, Vec3(), false, localYaw);
    float localTransform[16];
    float localDelta[16];
    ComposeCameraLocal(localYaw, identity, localTransform, localDelta);

    float worldLevel[16];
    float worldLevelDelta[16];
    ComposeWorldYaw(worldPose, up, identity, worldLevel, worldLevelDelta);
    Check(MatrixNearEqual(localTransform, worldLevel),
          "compose: on a level camera world-space yaw and camera-local yaw agree");
}

// Deliberately not the values the shipped ini defaults to - the projection
// must not depend on them.
constexpr float kTestFovRadians = 1.2f;
constexpr float kTestAspect = 16.0f / 9.0f;
constexpr float kTestDistance = 5.0f;

void TestAimProjection() {
    using namespace mcht::camera;
    using namespace mcht::math;

    const float tanV = std::tan(kTestFovRadians * 0.5f);
    const float tanH = tanV * kTestAspect;

    AimNdc ndc{};

    float identity[16];
    Identity(identity);
    Check(ProjectAimPoint(identity, kTestDistance, kTestFovRadians, kTestAspect, ndc) &&
              NearEqual(ndc.x, 0.0f) && NearEqual(ndc.y, 0.0f),
          "aim: an untracked frame leaves the crosshair at screen centre");

    // Litmus: pure yaw moves the crosshair horizontally only, by the tangent
    // of the angle over the half-FOV tangent.
    float yawM[16];
    AxisRotation(kAxisYaw, 15.0f, yawM);
    Check(ProjectAimPoint(yawM, kTestDistance, kTestFovRadians, kTestAspect, ndc) &&
              NearEqual(ndc.x, -std::tan(15.0f * kPi / 180.0f) / tanH) && NearEqual(ndc.y, 0.0f),
          "aim: pure yaw moves the crosshair horizontally by tan(yaw)/tan(fovH/2)");

    // Litmus: pure pitch moves it vertically only.
    float pitchM[16];
    AxisRotation(kAxisPitch, 12.0f, pitchM);
    Check(ProjectAimPoint(pitchM, kTestDistance, kTestFovRadians, kTestAspect, ndc) &&
              NearEqual(ndc.x, 0.0f) && NearEqual(ndc.y, std::tan(12.0f * kPi / 180.0f) / tanV),
          "aim: pure pitch moves the crosshair vertically by tan(pitch)/tan(fovV/2)");

    // Litmus: pure roll spins the view about the aim ray, so the aim point does
    // not move and the crosshair must stay at centre.
    float rollM[16];
    AxisRotation(kAxisRoll, 25.0f, rollM);
    Check(ProjectAimPoint(rollM, kTestDistance, kTestFovRadians, kTestAspect, ndc) &&
              NearEqual(ndc.x, 0.0f) && NearEqual(ndc.y, 0.0f),
          "aim: pure roll leaves the crosshair at centre");

    // The 6DOF offset rides in the matrix's last row. Projecting the aim
    // direction alone rather than a point would drop it, and leaning would move
    // the view while leaving the crosshair behind.
    float lean[16];
    Translation(0.1f, 0.0f, 0.0f, lean);
    Check(ProjectAimPoint(lean, kTestDistance, kTestFovRadians, kTestAspect, ndc) &&
              NearEqual(ndc.x, (0.1f / kTestDistance) / tanH) && NearEqual(ndc.y, 0.0f),
          "aim: a lateral lean reaches the crosshair through the translation row");

    // A wider frustum makes the same world offset a smaller fraction of the
    // screen, and aspect reaches the horizontal axis only.
    AimNdc wide{};
    Check(ProjectAimPoint(pitchM, kTestDistance, kTestFovRadians, kTestAspect * 2.0f, wide) &&
              NearEqual(wide.y, std::tan(12.0f * kPi / 180.0f) / tanV),
          "aim: aspect leaves the vertical offset alone");
    Check(ProjectAimPoint(yawM, kTestDistance, kTestFovRadians, kTestAspect * 2.0f, wide) &&
              NearEqual(wide.x, -std::tan(15.0f * kPi / 180.0f) / (tanH * 2.0f)),
          "aim: doubling the aspect halves the horizontal offset");

    // Behind the camera. Pushing it through the divide would mirror the
    // crosshair to the opposite side, which is worse than leaving it put.
    float behind[16];
    Translation(0.0f, 0.0f, kTestDistance * 2.0f, behind);
    Check(!ProjectAimPoint(behind, kTestDistance, kTestFovRadians, kTestAspect, ndc),
          "aim: an aim point behind the camera is refused rather than mirrored");

    float onCamera[16];
    Translation(0.0f, 0.0f, kTestDistance, onCamera);
    Check(!ProjectAimPoint(onCamera, kTestDistance, kTestFovRadians, kTestAspect, ndc),
          "aim: an aim point on the camera plane is refused rather than divided by zero");
}

// The frustum the crosshair divides by is read out of game memory every frame,
// and nothing upstream sanitizes it. These are the values that must never reach
// ProjectAimPoint, because the offset it returns for them is non-finite or
// enormous and the caller converts it to int.
void TestProjectionInputs() {
    using namespace mcht::camera;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    Check(PlausibleProjection(1.15f, 16.0f / 9.0f), "camera fields: a real frustum is accepted");
    Check(PlausibleProjection(0.5f, 1.0f) && PlausibleProjection(2.0f, 3.5f),
          "camera fields: the range covers narrow and very wide fields of view");

    Check(!PlausibleProjection(0.0f, 16.0f / 9.0f),
          "camera fields: a zero field of view is refused, not divided by");
    Check(!PlausibleProjection(nan, 16.0f / 9.0f),
          "camera fields: a NaN field of view is refused");
    Check(!PlausibleProjection(inf, 16.0f / 9.0f),
          "camera fields: an infinite field of view is refused");
    Check(!PlausibleProjection(-1.0f, 16.0f / 9.0f),
          "camera fields: a negative field of view is refused");
    Check(!PlausibleProjection(mcht::math::kPi, 16.0f / 9.0f) && kMaxFovRadians < mcht::math::kPi,
          "camera fields: a field of view at pi, where tan diverges, is refused");

    Check(!PlausibleProjection(1.15f, 0.0f), "camera fields: a zero aspect ratio is refused");
    Check(!PlausibleProjection(1.15f, nan), "camera fields: a NaN aspect ratio is refused");
    Check(!PlausibleProjection(1.15f, 1e9f), "camera fields: an absurd aspect ratio is refused");

    // Why the guard is not decorative: pushed through anyway, a zero field of
    // view divides by tan(0) and the crosshair offset the caller casts to int
    // is not a number.
    AimNdc ndc{};
    float yawM[16];
    mcht::math::AxisRotation(mcht::math::kAxisYaw, 15.0f, yawM);
    const bool projected = ProjectAimPoint(yawM, kTestDistance, 0.0f, kTestAspect, ndc);
    Check(projected && !std::isfinite(ndc.x),
          "camera fields: an unguarded zero field of view really does yield a non-finite offset");
}

void TestMatrixComposition() {
    using namespace mcht::math;

    float identity[16];
    Identity(identity);
    Check(NearEqual(identity[0], 1.0f) && NearEqual(identity[5], 1.0f) &&
              NearEqual(identity[10], 1.0f) && NearEqual(identity[15], 1.0f) &&
              NearEqual(identity[1], 0.0f) && NearEqual(identity[12], 0.0f),
          "matrix: Identity sets the diagonal and nothing else");

    float rotated[16];
    AxisRotation(kAxisYaw, 30.0f, rotated);
    float product[16];
    Multiply(rotated, identity, product);
    Check(MatrixNearEqual(product, rotated), "matrix: multiplying by identity is a no-op");

    // The 6DOF offset rides in the last row. If it ever moved to the projective
    // column it would warp perspective instead of producing parallax.
    float translation[16];
    Translation(0.1f, -0.2f, 0.3f, translation);
    Check(NearEqual(translation[12], 0.1f) && NearEqual(translation[13], -0.2f) &&
              NearEqual(translation[14], 0.3f) && NearEqual(translation[15], 1.0f),
          "matrix: translation lands in the last row");

    // AxisAngleRotation about world up must collapse exactly onto AxisRotation's
    // yaw. World-space yaw takes the first path and camera-local yaw the second,
    // so a divergence here is the two yaw modes disagreeing on a level camera.
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float aboutUp[16];
    float aboutYaw[16];
    AxisAngleRotation(up, 37.0f, aboutUp);
    AxisRotation(kAxisYaw, 37.0f, aboutYaw);
    Check(MatrixNearEqual(aboutUp, aboutYaw),
          "matrix: axis-angle about world up equals the yaw rotation");

    // Rotations stay orthonormal, so nothing composed from them can shear or
    // scale the frame.
    for (int axis = 0; axis < 3; ++axis) {
        float m[16];
        AxisRotation(axis, 47.0f, m);
        bool orthonormal = true;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                float dot = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    dot += m[r * 4 + k] * m[c * 4 + k];
                }
                orthonormal = orthonormal && NearEqual(dot, r == c ? 1.0f : 0.0f);
            }
        }
        Check(orthonormal, axis == 0   ? "matrix: yaw rotation is orthonormal"
                           : axis == 1 ? "matrix: pitch rotation is orthonormal"
                                       : "matrix: roll rotation is orthonormal");
    }

    // Row-vector convention: BuildPose composes roll*pitch*yaw so that yaw is
    // applied last. Written in the column order instead it becomes yaw*pitch*
    // roll, which is identical on any single axis and wrong on every combined
    // pose, so it is worth pinning that the two really do differ.
    float yawM[16];
    float pitchM[16];
    float rollM[16];
    AxisRotation(kAxisYaw, 30.0f, yawM);
    AxisRotation(kAxisPitch, 30.0f, pitchM);
    AxisRotation(kAxisRoll, 20.0f, rollM);
    float rp[16];
    float ypr[16];
    Multiply(rollM, pitchM, rp);
    Multiply(rp, yawM, ypr);
    float yp[16];
    float reversed[16];
    Multiply(yawM, pitchM, yp);
    Multiply(yp, rollM, reversed);
    Check(!MatrixNearEqual(ypr, reversed, 1e-3f),
          "matrix: the YXZ order and its reverse differ on a combined pose");

    // A pure yaw+pitch must leave no roll: in the correct order view right
    // (row 0) stays in the horizontal plane, so its y component is zero. The
    // reversed order picks up about 16 degrees of parasitic roll at 30/30.
    float noRoll[16];
    float flatRoll[16];
    float rpFlat[16];
    AxisRotation(kAxisRoll, 0.0f, flatRoll);
    Multiply(flatRoll, pitchM, rpFlat);
    Multiply(rpFlat, yawM, noRoll);
    Check(NearEqual(noRoll[1], 0.0f), "matrix: yaw+pitch introduces no roll");

    float ypOnly[16];
    Multiply(yawM, pitchM, ypOnly);
    Check(!NearEqual(ypOnly[1], 0.0f, 1e-2f),
          "matrix: the reversed order does introduce parasitic roll");
}

// Pins every offset the mod dereferences on the builds it knows. These are
// derived by hand from a memory dump and are not recoverable from anything else
// in the tree, so a silent edit is unrecoverable too: the mod would read the
// wrong field on a build it claims to support.
//
// The camera code addresses are deliberately absent. They are recovered from
// the running image at load time, so there is nothing here to pin and nothing
// for a game patch to invalidate.
void TestBuildProfile() {
    const mcht::builds::BuildProfile& p = mcht::builds::kStoreProfile_20260806;
    const mcht::builds::OffsetTable& o = p.Offsets;

    Check(p.Fingerprint.TimeDateStamp == 0x6A750E92 && p.Fingerprint.SizeOfImage == 0x1286D000 &&
              p.Fingerprint.CheckSum == 0x125529D1,
          "profile 20260806: the routing fingerprint is unchanged");

    Check(o.Camera.UiControlSize == 0x48,
          "profile 20260806: the UI control size offset is unchanged");

    Check(o.Renderer.ClientInstance == 0x1168,
          "profile 20260806: the renderer's client-instance offset is unchanged");

    Check(o.CameraComponent.Orientation == 0x30 && o.CameraComponent.AspectRatio == 0x4C &&
              o.CameraComponent.FieldOfView == 0x50 &&
              o.CameraComponent.PostViewTransform == 0x5C,
          "profile 20260806: camera-component member offsets are unchanged");

    Check(o.Session.ClientInstanceGetLevel == 0x538 &&
              o.Session.ClientInstanceGetLocalPlayer == 0x0F8 &&
              o.Session.ClientInstanceIsMultiPlayer == 0x560 &&
              o.Session.LevelGetGameRules == 0xAB0 && o.Session.LevelPlayerList == 0x4E0 &&
              o.Session.PlayerListSize == 0x10 && o.Session.GameRulesBegin == 0x18 &&
              o.Session.GameRulesEnd == 0x20 && o.Session.GameRuleStride == 0x118 &&
              o.Session.PvpValueByte == 0x106C && o.Session.PvpVariantTag == 0x1070 &&
              o.Session.PvpRuleIndex == 15 && o.Session.LocalPlayerDisplayMessage == 0x630,
          "profile 20260806: the fairness gate's offsets are unchanged");

    Check(o.CameraStruct.ViewStackMap == 0x08 && o.CameraStruct.ViewStackMapSize == 0x10 &&
              o.CameraStruct.ViewStackOffset == 0x18 && o.CameraStruct.ViewStackSize == 0x20 &&
              o.CameraStruct.ViewStackDirty == 0x38,
          "profile 20260806: mce::Camera view-stack offsets are unchanged");

    // The dormancy contract: a profile missing anything the mod cannot work
    // without must not report itself ready to hook.
    Check(mcht::builds::ProfileIsComplete(p),
          "profile 20260806: carries every offset the mod refuses to run without");

    mcht::builds::BuildProfile placeholder = p;
    placeholder.Offsets.Session.LevelGetGameRules = 0;
    Check(!mcht::builds::ProfileIsComplete(placeholder),
          "profile: a fingerprint-only placeholder is reported incomplete, so it stays dormant");

    // The 1.26.4403 patch. It moved every camera address and changed no layout,
    // which is the case the resolver exists to absorb: the profile is a
    // fingerprint and nothing else, and the two builds share one layout.
    const mcht::builds::BuildProfile& next = mcht::builds::kStoreProfile_20260812;

    Check(next.Fingerprint.TimeDateStamp == 0x6A7CA63A &&
              next.Fingerprint.SizeOfImage == 0x12888000 &&
              next.Fingerprint.CheckSum == 0x1256B1FE,
          "profile 20260812: the routing fingerprint is unchanged");

    Check(!next.Fingerprint.Matches(p.Fingerprint),
          "profile 20260812: routes separately from the build before it");

    Check(next.Offsets.Renderer.ClientInstance == o.Renderer.ClientInstance &&
              next.Offsets.CameraComponent.PostViewTransform ==
                  o.CameraComponent.PostViewTransform &&
              next.Offsets.Session.LevelGetGameRules == o.Session.LevelGetGameRules &&
              next.Offsets.Session.PvpValueByte == o.Session.PvpValueByte,
          "profile 20260812: shares the 1.26 layout with the build before it");

    Check(mcht::builds::ProfileIsComplete(next),
          "profile 20260812: carries every offset the mod refuses to run without");
}

}  // namespace

int main() {
    std::cout << "MinecraftHeadTracking Tests\n";
    std::cout << "===========================\n";

    std::cout << "Config bounds:\n";
    TestTrackerPort();
    TestDiscoverySeconds();
    TestWaitSeconds();

    std::cout << "Frame timing:\n";
    TestFrameDelta();
    TestFrameClock();

    std::cout << "Held pose:\n";
    TestHeldPose();

    std::cout << "Settings defaults:\n";
    TestSettingsDefaults();

    std::cout << "Matrix composition:\n";
    TestMatrixComposition();

    std::cout << "Pose and view composition:\n";
    TestPoseComposition();
    TestViewComposition();

    std::cout << "Crosshair projection:\n";
    TestAimProjection();
    TestProjectionInputs();

    std::cout << "Build profiles:\n";
    TestBuildProfile();

    if (g_failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED\n";
    return 1;
}
