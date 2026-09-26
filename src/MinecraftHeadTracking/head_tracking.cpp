#include "head_tracking.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "axis_signs.h"
#include "camera_hook.h"
#include "cameraunlock/data/position_data.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/processing/pose_interpolator.h"
#include "cameraunlock/processing/position_interpolator.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/processing/tracking_processor.h"
#include "cameraunlock/tracking/tracking_mode.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "frame_timing.h"
#include "held_pose.h"
#include "pose_composition.h"

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

// The two configured values, kept so the locality log can name the one that is
// actually in effect. Which of them applies is never decided here: the
// processors are told the connection and select it themselves.
float g_localSmoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
float g_remoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

// The last locality the log reported. Only the render thread touches these, and
// the line is gated on a change: it is reporting a switch, not a frame.
bool g_remoteConnection = false;
bool g_remoteConnectionKnown = false;

// This mod wires the processors by hand rather than through HeadTrackingSession,
// so there is no kHasRemoteConnection guard to assert: the call below is a
// direct one, and a receiver that stopped exposing IsRemoteConnection() would
// fail to compile rather than compile the selection silently away.
void ApplyConnectionLocality() {
    const bool isRemote = g_receiver.IsRemoteConnection();
    g_processor.SetIsRemoteConnection(isRemote);
    g_positionProcessor.SetIsRemoteConnection(isRemote);

    if (g_remoteConnectionKnown && isRemote == g_remoteConnection) {
        return;
    }
    g_remoteConnection = isRemote;
    g_remoteConnectionKnown = true;
    cameraunlock::logging::Line(
        "Tracker source is %s; smoothing=%.2f.", isRemote ? "a remote device" : "on this machine",
        cameraunlock::math::GetEffectiveSmoothing(g_localSmoothing, g_remoteSmoothing, isRemote));
}

// ---------------------------------------------------------------------------
// What the hotkeys switch.
// ---------------------------------------------------------------------------

cameraunlock::input::HotkeyPoller g_hotkeys;

// The one reader and writer of CameraUnlock.ini, owned by Bootstrap and saved
// through from the hotkey thread.
cameraunlock::config::ConfigOwner<mcht::config::Config>* g_owner = nullptr;

std::atomic<bool> g_enabled{true};

// What the tracking mode hotkey cycles through, in this order.
using cameraunlock::TrackingMode;
std::atomic<TrackingMode> g_trackingMode{TrackingMode::RotationAndPosition};

// Yaw about the world's up axis rather than the camera's. Default, because up
// wants to be a constant: with the mouse pointed at your feet, turning your
// head should still pan across the floor rather than spin the view.
std::atomic<bool> g_worldSpaceYaw{true};

TrackingMode NextTrackingMode(TrackingMode mode) {
    switch (mode) {
        case TrackingMode::RotationAndPosition:
            return TrackingMode::RotationOnly;
        case TrackingMode::RotationOnly:
            return TrackingMode::PositionOnly;
        case TrackingMode::PositionOnly:
            break;
    }
    return TrackingMode::RotationAndPosition;
}

const char* TrackingModeName(TrackingMode mode) {
    switch (mode) {
        case TrackingMode::RotationOnly:
            return "rotation only";
        case TrackingMode::PositionOnly:
            return "position only";
        case TrackingMode::RotationAndPosition:
            break;
    }
    return "rotation and position";
}

const char* YawModeName(bool worldSpace) {
    return worldSpace ? "world-locked (horizon)" : "camera-local";
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

    // Re-read every frame rather than once at startup. A player who swaps a
    // local OpenTrack instance for a phone on WiFi has to get the other
    // parameter without restarting the game.
    ApplyConnectionLocality();

    const float delta = g_frameClock.Advance(now);

    // GetPosition already returns metres: the packet parser converts
    // OpenTrack's centimetres on the way in. Scaling again here made a 10cm
    // head movement 0.0001m, which is what left the mod at 3DOF.
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    const bool havePosition = g_receiver.GetPosition(px, py, pz);
    const cameraunlock::PositionData raw(px, py, pz);

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

    const TrackingMode mode = g_trackingMode.load(std::memory_order_relaxed);
    const cameraunlock::math::Vec3 offset =
        ProcessPositionOffset(mode, havePosition, raw, pose, delta);

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

void ApplySettings(const mcht::config::Config& config) {
    g_processor.SetSensitivity(TrackerToBedrockRotation());

    // One pair for rotation and position alike, pushed to both processors.
    // Neither this function nor the render path picks between them: they hand
    // both values over and let the connection decide, so the choice cannot
    // drift between the two pipelines.
    g_localSmoothing = config.local_smoothing;
    g_remoteSmoothing = config.remote_smoothing;
    g_processor.SetLocalSmoothing(g_localSmoothing);
    g_processor.SetRemoteSmoothing(g_remoteSmoothing);

    // The limits and the smoothing copies come from the config; the position
    // sensitivities and inversions stay at the identity PositionSettings starts
    // with, since no row sets them.
    g_positionProcessor.SetSettings(config.position);

    // Off, because we cannot feed it a rotation it can use. The pivot
    // compensation subtracts the translation artifact of a head rotating about
    // a pivot in front of the tracker, so it needs the rotation in the
    // tracker's own frame. The pose handed to Process has already been through
    // the pitch and roll negation, so the artifact would be added rather than
    // removed: about 0.15m of spurious pitch-correlated offset at 30 degrees,
    // against a 0.20m Y limit. That is large enough to have been read as an
    // axis sign during testing.
    g_positionProcessor.SetTrackerPivotForward(0.0f);

    g_enabled.store(config.enable_on_startup);
    // The table never loads a pair that names no mode: it reads both rows as
    // their defaults instead.
    g_trackingMode.store(
        cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled).value());
    g_worldSpaceYaw.store(config.world_space_yaw);
}

// A save that did not happen has already reached the log through the status
// sink, and the session keeps the state the toggle applied. A save that did can
// carry a line too, naming a row that stopped following Defaults.ini.
void LogSave(const cameraunlock::config::ConfigSaveResult& saved) {
    for (const std::string& line : saved.log) {
        cameraunlock::logging::Line("%s", line.c_str());
    }
    if (saved.status != cameraunlock::config::ConfigSaveStatus::Saved) {
        cameraunlock::logging::Line("The change applies for this session only.");
    }
}

// The table has already refused a list that does not parse, so one here is a
// bug rather than a typo in the file.
std::vector<cameraunlock::input::KeyBinding> ParseKeys(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    }
    return parsed.bindings;
}

struct KeyLists {
    std::vector<cameraunlock::input::KeyBinding> toggle;
    std::vector<cameraunlock::input::KeyBinding> cycleMode;
    std::vector<cameraunlock::input::KeyBinding> yawMode;
};

// The actions run on the hotkey poller's thread. Each toggle applies its new
// state first, then saves it. End is the exception: it changes the session
// only, so EnableOnStartup decides the next start.
void RegisterHotkeys(const KeyLists& keys) {
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
        const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(next);
        LogSave(g_owner->Save([channels](mcht::config::Config& c) {
            c.rotation_enabled = channels.rotation_enabled;
            c.position_enabled = channels.position_enabled;
        }));
    };
    const auto toggleYawMode = [] {
        const bool now = !g_worldSpaceYaw.load(std::memory_order_relaxed);
        g_worldSpaceYaw.store(now, std::memory_order_relaxed);
        cameraunlock::logging::Line("Yaw mode: %s.", YawModeName(now));
        LogSave(g_owner->Save([now](mcht::config::Config& c) { c.world_space_yaw = now; }));
    };

    // One registration per key: a binding without modifiers stays quiet while
    // Ctrl and Shift are both held, so Ctrl+Shift with a key reaches only a
    // binding that names it, and one press never fires an action twice.
    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(g_hotkeys, keys.toggle, toggle);
    RegisterKeyBindings(g_hotkeys, keys.cycleMode, cycleMode);
    RegisterKeyBindings(g_hotkeys, keys.yawMode, toggleYawMode);

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

bool Start(const mcht::config::Config& config,
           cameraunlock::config::ConfigOwner<mcht::config::Config>& owner) {
    // Parsed before anything is installed, so a list that does not parse
    // leaves the game untouched.
    const KeyLists keys{ParseKeys(config.toggle_key_name), ParseKeys(config.cycle_tracking_mode_key_name),
                        ParseKeys(config.yaw_mode_key_name)};
    g_owner = &owner;
    ApplySettings(config);

    if (!mcht::camera::Install(&ProvidePose)) {
        return false;
    }

    StartReceiver(config.udp_port);

    RegisterHotkeys(keys);
    cameraunlock::logging::Line(
        "Controls: toggle tracking [%s], cycle tracking mode [%s], toggle yaw mode [%s].",
        config.toggle_key_name.c_str(), config.cycle_tracking_mode_key_name.c_str(),
        config.yaw_mode_key_name.c_str());
    cameraunlock::logging::Line("Yaw mode: %s.", YawModeName(g_worldSpaceYaw.load()));
    return true;
}

}  // namespace mcht::tracking
