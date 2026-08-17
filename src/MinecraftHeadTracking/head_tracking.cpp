#include "head_tracking.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "camera_hook.h"
#include "cameraunlock/data/position_data.h"
#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/processing/pose_interpolator.h"
#include "cameraunlock/processing/position_interpolator.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/processing/tracking_processor.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "frame_timing.h"
#include "held_pose.h"
#include "pose_composition.h"
#include "tracking_settings.h"

namespace mcht::tracking {
namespace {

// ---------------------------------------------------------------------------
// The pipeline, from the wire to a pose.
// ---------------------------------------------------------------------------

cameraunlock::UdpReceiver g_receiver;
cameraunlock::TrackingProcessor g_processor;
cameraunlock::PositionProcessor g_positionProcessor;

// Between the receiver and the processors. The camera hook runs several
// hundred times a second against a tracker sending at 60Hz or less, so
// without this the same sample is reused across many frames and the motion
// shows visible flat spots on a high-refresh display.
cameraunlock::PoseInterpolator g_poseInterpolator;
cameraunlock::PositionInterpolator g_positionInterpolator;
std::int64_t g_lastSampleAt = 0;

FrameClock g_frameClock;
HeldPose g_held;

// ---------------------------------------------------------------------------
// What the hotkeys switch.
// ---------------------------------------------------------------------------

cameraunlock::input::HotkeyPoller g_hotkeys;

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_recenterRequested{false};

// What Page Up / Ctrl+Shift+G cycles through, in that order.
enum class TrackingMode { Both, RotationOnly, PositionOnly };
std::atomic<TrackingMode> g_trackingMode{TrackingMode::Both};

// Yaw about the world's up axis rather than the camera's. Default, because up
// wants to be a constant: with the mouse pointed at your feet, turning your
// head should still pan across the floor rather than spin the view.
std::atomic<bool> g_worldSpaceYaw{true};
int g_yawModeKey = kDefaultYawModeKey;

TrackingMode NextTrackingMode(TrackingMode mode) {
    switch (mode) {
        case TrackingMode::Both:
            return TrackingMode::RotationOnly;
        case TrackingMode::RotationOnly:
            return TrackingMode::PositionOnly;
        case TrackingMode::PositionOnly:
            break;
    }
    return TrackingMode::Both;
}

const char* TrackingModeName(TrackingMode mode) {
    switch (mode) {
        case TrackingMode::RotationOnly:
            return "rotation only";
        case TrackingMode::PositionOnly:
            return "position only";
        case TrackingMode::Both:
            break;
    }
    return "rotation and position";
}

const char* YawModeName(bool worldSpace) {
    return worldSpace ? "world-locked (horizon)" : "camera-local";
}

// ---------------------------------------------------------------------------
// Recentring.
// ---------------------------------------------------------------------------

// Recenter once per session, after the tracker has settled. Never re-armed
// after a gap: a phone tracker stops sending when it loses the face, and
// recentring on resume captures whatever pose the user happens to hold.
int g_freshFrames = 0;
bool g_autoRecentered = false;
constexpr int kStabilizationFrames = 10;

void RecenterAll(bool havePosition, const cameraunlock::PositionData& raw) {
    g_processor.Recenter();
    if (havePosition) {
        g_positionProcessor.SetCenter(raw);
    }
}

// A recenter the tracker app signalled is not the same operation as the
// hotkey. The app subtracts the pose at its own end, so everything that
// arrives from here on is already measured from the new centre and the mod's
// own centre has to drop to nothing. Anything that latches a sample instead -
// Recenter(), which folds the smoothed pose in, or centring on the pose that
// rides the request - subtracts the drift the app has already removed and
// parks the view mirrored about centre by however far off axis the user was
// when they pressed.
//
// Latching the pose that arrives WITH the request looks correct and is not:
// the app arms the signal before its own zeroing reaches the wire, so the
// first packets of the burst still carry the pre-press pose. Whether the
// request is consumed on one of those or on a zeroed one is a race, which is
// what made the failure look intermittent.
void RecenterToTrackerOrigin() {
    g_processor.RecenterTo(0.0f, 0.0f, 0.0f);
    g_poseInterpolator.Reset();
    g_positionProcessor.Reset();
    g_positionInterpolator.Reset();
}

// Recentring has to move the position origin too. Without it the tracker's
// absolute position is treated as an offset from the sensor origin, which the
// box limits then clamp, so leaning does nothing.
//
// Both flags are consumed every frame rather than short-circuited, so a
// request that loses the tie is dropped rather than firing a second recentre
// on the frame after.
void ConsumeRecenterRequests(bool havePosition, const cameraunlock::PositionData& raw) {
    const bool hotkeyRecenter = g_recenterRequested.exchange(false, std::memory_order_relaxed);
    const bool remoteRecenter = g_receiver.TryConsumeRecenterRequest();
    if (remoteRecenter) {
        RecenterToTrackerOrigin();
        // The tracker app has centred, so the one automatic recentre this
        // session has nothing left to do; letting it fire later would fold the
        // app's centre back in.
        g_autoRecentered = true;
        cameraunlock::logging::Line("Recentered by tracker app.");
    } else if (hotkeyRecenter) {
        RecenterAll(havePosition, raw);
    }
}

void MaybeAutoRecenter(bool havePosition, const cameraunlock::PositionData& raw) {
    if (g_autoRecentered || ++g_freshFrames < kStabilizationFrames) {
        return;
    }
    RecenterAll(havePosition, raw);
    g_autoRecentered = true;
    cameraunlock::logging::Line("Auto-recentered on first tracking data.");
}

// ---------------------------------------------------------------------------
// Turning the state into the frame's pose.
// ---------------------------------------------------------------------------

void BuildHeldPose(mcht::camera::Pose& out) {
    mcht::camera::BuildPose(g_held.Yaw, g_held.Pitch, g_held.Roll, g_held.Offset,
                            g_worldSpaceYaw.load(std::memory_order_relaxed), out);
}

// How long switching tracking off takes to ease the held pose out, instead of
// cutting, so the view returns to neutral without a jump.
constexpr float kDisableEaseSeconds = 0.25f;

// Tracking is off: decay whatever was last applied towards neutral rather than
// cutting it. False once there is nothing left to show.
bool EaseOutHeldPose(std::chrono::steady_clock::time_point now, mcht::camera::Pose& out) {
    if (!g_held.Valid) {
        return false;
    }
    const float decayed = g_frameClock.Advance(now) / kDisableEaseSeconds;
    g_held.Decay(decayed >= 1.0f ? 0.0f : 1.0f - decayed);
    if (g_held.IsSettled()) {
        g_held.Valid = false;
        return false;
    }
    BuildHeldPose(out);
    return true;
}

// No fresh data: hold, do not snap. Smoothing blends back naturally when data
// resumes. The frame clock is deliberately not advanced, so the delta the next
// real sample is processed with spans the whole gap.
bool HoldLastPose(mcht::camera::Pose& out) {
    if (!g_held.Valid) {
        return false;
    }
    BuildHeldPose(out);
    return true;
}

cameraunlock::math::Vec3 ProcessPositionOffset(TrackingMode mode, bool havePosition,
                                               const cameraunlock::PositionData& raw,
                                               const cameraunlock::TrackingPose& pose,
                                               float delta) {
    if (mode == TrackingMode::RotationOnly || !havePosition) {
        return {};
    }
    const cameraunlock::math::Quat4 rotationQuat =
        cameraunlock::math::Quat4::FromYawPitchRoll(pose.yaw, pose.pitch, pose.roll);
    const cameraunlock::PositionData smoothRaw = g_positionInterpolator.Update(raw, delta);
    return g_positionProcessor.Process(smoothRaw, rotationQuat, delta);
}

// Fast enough to watch a pose problem develop, slow enough that a session's
// log stays readable.
constexpr std::uint64_t kPoseLogIntervalMs = 1000;
std::uint64_t g_lastPoseLogAt = 0;

void LogPoseAtInterval(const cameraunlock::PositionData& raw,
                       const cameraunlock::math::Vec3& offset,
                       const cameraunlock::TrackingPose& pose) {
    const std::uint64_t nowMs = GetTickCount64();
    if (nowMs - g_lastPoseLogAt < kPoseLogIntervalMs) {
        return;
    }
    g_lastPoseLogAt = nowMs;
    cameraunlock::logging::Line(
        "[pose] raw pos %.3f %.3f %.3f | offset %.3f %.3f %.3f | ypr %.1f %.1f %.1f", raw.x, raw.y,
        raw.z, offset.x, offset.y, offset.z, pose.yaw, pose.pitch, pose.roll);
}

// Called on the render thread once per camera setup.
bool ProvidePose(mcht::camera::Pose& out) {
    const auto now = std::chrono::steady_clock::now();

    if (!g_enabled.load(std::memory_order_relaxed)) {
        return EaseOutHeldPose(now, out);
    }

    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    if (!g_receiver.GetRotation(yaw, pitch, roll) || !g_receiver.IsReceiving()) {
        return HoldLastPose(out);
    }

    const float delta = g_frameClock.Advance(now);

    // Read every frame, not once at startup: swapping a local OpenTrack
    // instance for a phone on WiFi has to pick up the other smoothing
    // parameter without restarting the game.
    const bool isRemoteConnection = g_receiver.IsRemoteConnection();
    g_processor.SetIsRemoteConnection(isRemoteConnection);
    g_positionProcessor.SetIsRemoteConnection(isRemoteConnection);

    // GetPosition already returns metres: the packet parser converts
    // OpenTrack's centimetres on the way in. Scaling again here made a 10cm
    // head movement 0.0001m, which is what left the mod at 3DOF.
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    const bool havePosition = g_receiver.GetPosition(px, py, pz);
    const cameraunlock::PositionData raw(px, py, pz);

    ConsumeRecenterRequests(havePosition, raw);

    // A sample is "new" when the receiver's timestamp moves; the hook runs far
    // faster than packets arrive, so most frames are interpolated rather than
    // fed a fresh sample.
    const std::int64_t sampleAt = g_receiver.GetLastReceiveTimestamp();
    const bool freshSample = sampleAt != g_lastSampleAt;
    g_lastSampleAt = sampleAt;

    const cameraunlock::InterpolatedPose smooth =
        g_poseInterpolator.Update(yaw, pitch, roll, freshSample, delta);

    const cameraunlock::TrackingPose pose =
        g_processor.Process(smooth.yaw, smooth.pitch, smooth.roll, delta);

    MaybeAutoRecenter(havePosition, raw);

    const TrackingMode mode = g_trackingMode.load(std::memory_order_relaxed);
    const cameraunlock::math::Vec3 offset =
        ProcessPositionOffset(mode, havePosition, raw, pose, delta);

    LogPoseAtInterval(raw, offset, pose);

    // The processor keeps running while rotation is suppressed, so its
    // smoothing state is current the moment the cycle brings rotation back.
    const bool rotationActive = mode != TrackingMode::PositionOnly;
    g_held.Set(rotationActive ? pose.yaw : 0.0f, rotationActive ? pose.pitch : 0.0f,
               rotationActive ? pose.roll : 0.0f, offset);

    BuildHeldPose(out);
    return true;
}

// ---------------------------------------------------------------------------
// Startup.
// ---------------------------------------------------------------------------

void ApplySettings(const Settings& settings) {
    g_processor.SetSensitivity(settings.Sensitivity);
    // Both smoothing values go in; the per-frame connection locality decides
    // which one each processor uses.
    g_processor.SetLocalSmoothing(settings.LocalSmoothing);
    g_processor.SetRemoteSmoothing(settings.RemoteSmoothing);

    // The position processor takes its pair through PositionSettings; only the
    // connection flag is runtime state on the processor itself.
    cameraunlock::PositionSettings position = settings.Position;
    position.local_smoothing = settings.LocalSmoothing;
    position.remote_smoothing = settings.RemoteSmoothing;
    g_positionProcessor.SetSettings(position);

    // Off, because we cannot feed it a rotation it can use. The pivot
    // compensation subtracts the translation artifact of a head rotating about
    // a pivot in front of the tracker, so it needs the rotation in the
    // tracker's own frame. The pose handed to Process has already been through
    // sensitivity and inversion, so the artifact would be added rather than
    // removed: about 0.15m of spurious pitch-correlated offset at 30 degrees,
    // against a 0.20m Y limit. That is large enough to have been read as an
    // axis sign during testing.
    g_positionProcessor.SetTrackerPivotForward(0.0f);

    g_enabled.store(settings.EnableOnStartup);
    g_trackingMode.store(settings.PositionEnabled ? TrackingMode::Both
                                                  : TrackingMode::RotationOnly);
    g_worldSpaceYaw.store(settings.WorldSpaceYaw);
    g_yawModeKey = settings.YawModeKey;
}

void RegisterHotkeys() {
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    const auto recenter = [] {
        g_recenterRequested.store(true, std::memory_order_relaxed);
        cameraunlock::logging::Line("Recentered.");
    };
    const auto toggle = [] {
        const bool now = !g_enabled.load(std::memory_order_relaxed);
        g_enabled.store(now, std::memory_order_relaxed);
        cameraunlock::logging::Line("Head tracking %s.", now ? "enabled" : "disabled");
    };
    const auto cycleMode = [] {
        const TrackingMode next =
            NextTrackingMode(g_trackingMode.load(std::memory_order_relaxed));
        g_trackingMode.store(next, std::memory_order_relaxed);
        cameraunlock::logging::Line("Tracking mode: %s.", TrackingModeName(next));
    };
    const auto toggleYawMode = [] {
        const bool now = !g_worldSpaceYaw.load(std::memory_order_relaxed);
        g_worldSpaceYaw.store(now, std::memory_order_relaxed);
        cameraunlock::logging::Line("Yaw mode: %s.", YawModeName(now));
    };

    g_hotkeys.AddHotkey(VK_HOME, NavGuarded(recenter));
    g_hotkeys.AddHotkey(VK_END, NavGuarded(toggle));
    g_hotkeys.AddHotkey(VK_PRIOR, NavGuarded(cycleMode));
    g_hotkeys.AddHotkey(g_yawModeKey, NavGuarded(toggleYawMode));

    // Chord alternatives for keyboards without a nav cluster.
    g_hotkeys.AddHotkey('T', ChordGuarded(recenter));
    g_hotkeys.AddHotkey('Y', ChordGuarded(toggle));
    g_hotkeys.AddHotkey('G', ChordGuarded(cycleMode));
    g_hotkeys.AddHotkey('H', ChordGuarded(toggleYawMode));

    g_hotkeys.Start();
}

void StartReceiver(int port) {
    g_receiver.SetLog([](const std::string& message) {
        cameraunlock::logging::Line("%s", message.c_str());
    });
    if (g_receiver.Start(static_cast<std::uint16_t>(port))) {
        cameraunlock::logging::Line("Listening for OpenTrack data on UDP %d.", port);
        return;
    }
    // The receiver has already logged the bind failure and keeps retrying on
    // its own thread, so nothing here is torn down: closing whatever holds the
    // port brings tracking up without restarting the game.
    cameraunlock::logging::Line(
        "The camera hook is installed and waiting; tracking starts by itself once UDP %d "
        "is free.", port);
}

}  // namespace

bool Start(const std::string& configPath) {
    // Applied whether or not the ini opened. Skipping it left the processors on
    // their own constructed defaults, which are a third behaviour belonging to
    // nobody: PositionProcessor arrives with tracker-pivot compensation on, so
    // the 0.15m pitch-correlated position artifact ApplySettings exists to
    // disable came back, and pitch and roll inversion arrives off, the opposite
    // of the ini this mod writes, so nodding and leaning went the wrong way.
    const Settings settings = ReadSettings(configPath).value_or(Settings{});
    ApplySettings(settings);

    if (!mcht::camera::Install(&ProvidePose)) {
        return false;
    }

    StartReceiver(settings.Port);

    RegisterHotkeys();
    cameraunlock::logging::Line(
        "Controls: Home / Ctrl+Shift+T recenter, End / Ctrl+Shift+Y toggle tracking, "
        "PageUp / Ctrl+Shift+G cycle tracking mode, PageDown / Ctrl+Shift+H toggle yaw mode.");
    cameraunlock::logging::Line("Yaw mode: %s.", YawModeName(g_worldSpaceYaw.load()));
    return true;
}

}  // namespace mcht::tracking
