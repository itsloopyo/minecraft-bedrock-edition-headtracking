#include "camera_hook.h"

#include <windows.h>

#include <MinHook.h>

#include <cstdint>
#include <cstring>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "cameraunlock/logging/file_log.h"
#include "common/memory_probe.h"
#include "matrix4.h"
#include "menu_state.h"
#include "pose_composition.h"
#include "session_policy.h"

namespace mcht::camera {
namespace {

using mcht::memory::AccessViolationFilter;
using mcht::memory::IsReadable;
using mcht::memory::IsWritable;

using CameraSetupFn = void(__fastcall*)(void* self, void* camera, float alpha);
using GetCameraComponentFn = void*(__fastcall*)(void* clientInstance);
using UiBlitFn = void(__fastcall*)(void* renderer, void* screenContext, void* texture,
                                   int* rect, void* material);
using HudCursorRenderFn = void(__fastcall*)(void* self, void* uiContext, void* clientInstance,
                                            void* owner, int pass);

// ---------------------------------------------------------------------------
// What Install resolves: the trampolines, the game's own getter, and the
// address table the fingerprint selected.
// ---------------------------------------------------------------------------

PoseProvider g_provider = nullptr;

CameraSetupFn g_originalCameraSetup = nullptr;
GetCameraComponentFn g_getCameraComponent = nullptr;
UiBlitFn g_originalUiBlit = nullptr;
HudCursorRenderFn g_originalHudCursorRender = nullptr;

// Resolved once by Install, from the profile the fingerprint selected. Held as
// a pointer rather than copied field by field so a new offset cannot reach the
// table without also reaching the hook.
const mcht::builds::OffsetTable* g_offsets = nullptr;

// The post-view transform, and so also the buffer the frame's saved copy needs.
constexpr std::size_t kTransformFloats = 16;
constexpr std::size_t kTransformBytes = kTransformFloats * sizeof(float);

// ---------------------------------------------------------------------------
// What the camera hook hands to the crosshair hook, once per frame. The two
// run at different points in the frame, so this is the only channel between
// them.
// ---------------------------------------------------------------------------

// The transform applied this frame. The reticle projects through this exact
// matrix rather than recomputing from angles, which is what keeps the crosshair
// on the aim point under any composition.
float g_headMatrix[kTransformFloats] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// CameraComponent+0x50 is the vertical field of view in RADIANS, not degrees.
float g_fovRadians = 1.15f;
float g_aspect = 16.0f / 9.0f;

bool g_inCursorRender = false;

// The cursor control's size, captured from the owner the renderer is handed.
// 16x16 is the vanilla crosshair, used until the first real render.
constexpr float kDefaultCursorPixels = 16.0f;
float g_cursorSize[2] = {kDefaultCursorPixels, kDefaultCursorPixels};

// A control larger than this is not a crosshair, so the read landed somewhere
// that is not UIControl::mSize.
constexpr float kMaxPlausibleCursorPixels = 4096.0f;

// Where along the clean aim ray the crosshair is projected.
//
// This is an approximation and the only one in the reticle path. Projecting a
// point rather than a direction is what makes the 6DOF offset reach the
// crosshair at all, but the exact answer needs the distance to whatever is
// actually being aimed at, which would mean a raycast the mod does not have.
// Block reach is the distance that matters for aiming at blocks, and the error
// falls to zero as the target gets further away and as the head offset returns
// to centre.
constexpr float kAimDistanceMetres = 5.0f;

// The getter is an address this mod worked out, not one it was given, so the
// call into it gets a fault boundary like every other reach into game memory.
//
// This is not defensive decoration. An earlier resolver picked a different
// component accessor - one taking a registry rather than an IClientInstance -
// and because this call was unguarded it took the whole game down a second
// after tracking went active, twice. A wrong address should cost a frame of
// tracking, not the player's session. Its own function because __try cannot
// share a frame with anything that needs unwinding.
void* CallCameraComponentGetter(void* clientInstance) {
    __try {
        return g_getCameraComponent(clientInstance);
    } __except (AccessViolationFilter(GetExceptionCode())) {
        return nullptr;
    }
}

void* CameraComponentOf(void* self) {
    const auto bytes = static_cast<unsigned char*>(self);
    const auto slot = bytes + g_offsets->Renderer.ClientInstance;
    // IsReadable, not IsBadReadPtr: the latter probes by touching the memory,
    // so a hit on a guard page consumes it, and on a thread stack that breaks
    // the growth mechanism for the rest of the process. On the render path, a
    // few hundred calls a second, that is not a probe worth keeping.
    if (!IsReadable(slot, sizeof(void*))) {
        return nullptr;
    }
    void* const clientInstance = *reinterpret_cast<void* const*>(slot);
    if (clientInstance == nullptr) {
        return nullptr;
    }
    return CallCameraComponentGetter(clientInstance);
}

// Split out so the fault boundaries are explicit and each one has exactly one
// meaning. Both are free of C++ objects, which __try requires.
bool ReadCameraState(const unsigned char* fields, float orientationOut[4]) {
    const auto& component = g_offsets->CameraComponent;
    float aspect = 0.0f;
    float fov = 0.0f;
    __try {
        aspect = *reinterpret_cast<const float*>(fields + component.AspectRatio);
        fov = *reinterpret_cast<const float*>(fields + component.FieldOfView);
        std::memcpy(orientationOut, fields + component.Orientation, sizeof(float) * 4);
    } __except (AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
    // Committed only once they are believable, and the frame is skipped
    // otherwise. A readable field is not the same as the right field, and these
    // two are divided by: the crosshair's screen offset comes back non-finite
    // from a zero or NaN field of view and reaches a float-to-int conversion.
    if (!PlausibleProjection(fov, aspect)) {
        return false;
    }
    g_aspect = aspect;
    g_fovRadians = fov;
    return true;
}

// The world's up axis expressed in view space, which is the axis world-space
// yaw has to turn about.
//
// The game builds its view rotation as the inverse of mOrientation, so world
// up lands in column 1 of that quaternion's expansion and nothing else of the
// matrix is needed. The component order and the expansion are read off
// setupCamera's own code rather than assumed - it loads x, y, z, w from +0x30
// upwards and builds the row-vector rotation from them.
//
// False when the result is not a unit vector, which is the only check the raw
// floats get: this is game memory being read on the render path, and yawing
// about a garbage axis would shear the whole frame.
bool WorldUpInViewSpace(const float quaternion[4], float out[3]) {
    const float x = quaternion[0];
    const float y = quaternion[1];
    const float z = quaternion[2];
    const float w = quaternion[3];
    out[0] = 2.0f * (x * y + w * z);
    out[1] = 1.0f - 2.0f * (x * x + z * z);
    out[2] = 2.0f * (y * z - w * x);
    const float lengthSq = out[0] * out[0] + out[1] * out[1] + out[2] * out[2];
    return lengthSq > 0.99f && lengthSq < 1.01f;
}

// Taking the copy is its own fault boundary because the restore at the end of
// the frame writes these bytes straight back into the camera. A save that
// faulted part way would leave the buffer holding stack garbage, and restoring
// that is worse than never having tracked the frame at all: the engine system
// that resets this field early-outs on some frames, so the garbage would
// persist.
bool SaveTransform(const float* transform, float* savedOut) {
    __try {
        std::memcpy(savedOut, transform, kTransformBytes);
        return true;
    } __except (AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
}

// The only write into the camera, used both to install the frame's transform
// and to put the original bytes back afterwards. Split from the composition so
// the fault boundary covers exactly the game-memory access and nothing else:
// the matrices themselves are built in local buffers, where nothing can fault.
bool WriteTransform(float* transform, const float* bytes) {
    __try {
        std::memcpy(transform, bytes, kTransformBytes);
        return true;
    } __except (AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
}

// deltaOut receives the transform from the clean view to the tracked one,
// which is what the crosshair projects through. It is only written once the
// camera has actually taken the transform, so a fault leaves whatever the
// caller seeded it with rather than offsetting the crosshair by a pose the
// view never received.
bool ComposeInto(float* transform, const mcht::camera::Pose& pose, const float* worldUp,
                 const float* saved, float* deltaOut) {
    float composed[kTransformFloats];
    float delta[kTransformFloats];
    if (pose.WorldSpaceYaw) {
        ComposeWorldYaw(pose, worldUp, saved, composed, delta);
    } else {
        ComposeCameraLocal(pose, saved, composed, delta);
    }

    if (!WriteTransform(transform, composed)) {
        return false;
    }
    std::memcpy(deltaOut, delta, kTransformBytes);
    return true;
}

// Composes the frame's pose into the camera's post-view transform and records
// the clean-to-tracked delta for the crosshair. Returns the field it wrote so
// the caller can put the original bytes back after the game has rendered, or
// null when nothing was written and there is nothing to undo.
float* ApplyPoseToCamera(void* self, const mcht::camera::Pose& pose, float* savedOut) {
    void* const component = CameraComponentOf(self);
    if (component == nullptr) {
        return nullptr;
    }

    const auto fields = static_cast<unsigned char*>(component);
    float* const postView =
        reinterpret_cast<float*>(fields + g_offsets->CameraComponent.PostViewTransform);
    float orientation[4];
    float worldUp[3] = {};

    // The camera's own fields are read before the write, so a fault reading
    // them cannot happen after the component has been modified. Anything that
    // fails here leaves the component untouched, so there is nothing to undo.
    //
    // The orientation is the one input world-space yaw cannot do without.
    // Skipping the frame beats yawing about an axis we do not trust.
    if (!IsWritable(postView, kTransformBytes) || !ReadCameraState(fields, orientation) ||
        (pose.WorldSpaceYaw && !WorldUpInViewSpace(orientation, worldUp)) ||
        !SaveTransform(postView, savedOut)) {
        return nullptr;
    }

    // Seeded, because a compose that faults part way leaves the camera
    // untracked for the frame and so wants the crosshair back at screen centre
    // rather than offset by a pose that never reached the camera.
    float delta[kTransformFloats];
    mcht::math::Identity(delta);
    // The return is deliberately ignored, and postView is handed back either
    // way. If the write faulted part way the restore must still run: putting
    // the saved bytes back is safe whatever happened, and skipping it is not.
    // Head rotation left composed in would be permanent, because the engine
    // system that resets this field early-outs on some frames and the next
    // frame would then compose on top of our own output.
    ComposeInto(postView, pose, worldUp, savedOut, delta);
    std::memcpy(g_headMatrix, delta, sizeof(g_headMatrix));
    return postView;
}

void __fastcall DetourCameraSetup(void* self, void* camera, float alpha) {
    mcht::camera::Pose pose = {};
    // Checked here rather than inside the pose provider, so it covers every
    // provider. This is the only place in the mod that hooks the camera, which
    // is what makes that guarantee true rather than aspirational.
    const bool active = mcht::session::TrackingAllowed(self) && !mcht::ui::MenuIsOpen() &&
                        g_provider != nullptr && g_provider(pose);

    // Identity unless a transform actually reaches the camera this frame. The
    // crosshair projects through this matrix, so every path that skips the
    // camera write has to leave it here: offsetting the crosshair by a pose the
    // view never received unglues it from the aim point.
    mcht::math::Identity(g_headMatrix);

    float saved[kTransformFloats];
    float* const transform = active ? ApplyPoseToCamera(self, pose, saved) : nullptr;

    g_originalCameraSetup(self, camera, alpha);

    // A camera system rewrites this field on some frames and skips it on
    // others, so the value found is always put back rather than accumulated.
    // The result is deliberately ignored: there is nothing left to do about a
    // restore that faults, and not attempting it is the worse answer.
    if (transform != nullptr) {
        WriteTransform(transform, saved);
    }
}

// The rect the cursor renderer built, as {x, y, width, height}.
constexpr std::size_t kRectInts = 4;

// Moves the crosshair rect to where the clean aim projects into the tracked
// view. Its own fault boundary, and free of C++ objects, which __try requires.
void OffsetCursorRect(int* rect) {
    __try {
        // The renderer centred this rect as x = (screen - mSize.x) / 2, using
        // the control's size. The rect's own width is a hardcoded 16 and is
        // NOT what it centred with, so recovering the screen size from rect[2]
        // is only right when the control happens to be 16 wide.
        const float width = static_cast<float>(2 * rect[0]) + g_cursorSize[0];
        const float height = static_cast<float>(2 * rect[1]) + g_cursorSize[1];

        // A miss leaves the rect exactly where the game put it, which is the
        // right answer when the aim point is behind the camera: pushing it
        // through the divide would mirror the crosshair to the opposite side
        // of the screen.
        AimNdc ndc;
        if (ProjectAimPoint(g_headMatrix, kAimDistanceMetres, g_fovRadians, g_aspect, ndc)) {
            rect[0] += static_cast<int>(ndc.x * width * 0.5f);
            rect[1] += static_cast<int>(-ndc.y * height * 0.5f);
        }
    } __except (AccessViolationFilter(GetExceptionCode())) {
    }
}

void __fastcall DetourUiBlit(void* renderer, void* screenContext, void* texture, int* rect,
                             void* material) {
    // Shared with a cubemap path, so only the call the cursor renderer makes
    // may be touched.
    if (g_inCursorRender && rect != nullptr && IsWritable(rect, kRectInts * sizeof(int))) {
        OffsetCursorRect(rect);
    }
    g_originalUiBlit(renderer, screenContext, texture, rect, material);
}

// Separate because a function holding a C++ object cannot also hold __try.
void CaptureCursorSize(const void* owner) {
    const std::uint32_t sizeOffset = g_offsets->Camera.UiControlSize;
    if (owner == nullptr || sizeOffset == 0) {
        return;
    }
    const auto sizeField = static_cast<const unsigned char*>(owner) + sizeOffset;
    __try {
        const float x = *reinterpret_cast<const float*>(sizeField);
        const float y = *reinterpret_cast<const float*>(sizeField + sizeof(float));
        if (x > 0.0f && y > 0.0f && x < kMaxPlausibleCursorPixels &&
            y < kMaxPlausibleCursorPixels) {
            g_cursorSize[0] = x;
            g_cursorSize[1] = y;
        }
    } __except (AccessViolationFilter(GetExceptionCode())) {
    }
}

void __fastcall DetourHudCursorRender(void* self, void* uiContext, void* clientInstance,
                                      void* owner, int pass) {
    CaptureCursorSize(owner);
    g_inCursorRender = true;
    // Cleared through a guard so a throw out of the renderer cannot latch it.
    // Latched true, every later call to the shared blit, including the cubemap
    // path, would get the crosshair offset and the whole HUD would shift.
    struct Guard {
        ~Guard() { g_inCursorRender = false; }
    } guard;
    g_originalHudCursorRender(self, uiContext, clientInstance, owner, pass);
}

// Best-effort: the crosshair staying put while the view moves is a cosmetic
// loss, not a reason to leave the camera unhooked.
void InstallCrosshairHooks(unsigned char* base, const mcht::builds::ResolvedCode& code) {
    if (code.HudCursorRender == 0 || code.UiBlit == 0) {
        cameraunlock::logging::Line(
            "Could not find the crosshair renderer; it will stay centred while the view moves.");
        return;
    }
    void* const cursorTarget = base + code.HudCursorRender;
    void* const blitTarget = base + code.UiBlit;
    const bool hooked =
        MH_CreateHook(cursorTarget, reinterpret_cast<void*>(&DetourHudCursorRender),
                      reinterpret_cast<void**>(&g_originalHudCursorRender)) == MH_OK &&
        MH_CreateHook(blitTarget, reinterpret_cast<void*>(&DetourUiBlit),
                      reinterpret_cast<void**>(&g_originalUiBlit)) == MH_OK &&
        MH_EnableHook(cursorTarget) == MH_OK && MH_EnableHook(blitTarget) == MH_OK;
    cameraunlock::logging::Line(
        hooked ? "Crosshair follows the aim point."
               : "Could not hook the crosshair; it will stay centred while the view moves.");
}

}  // namespace

bool Install(PoseProvider provider) {
    const mcht::builds::OffsetTable& offsets = mcht::builds::ActiveProfile().Offsets;
    // Recovered from the running image rather than pinned to this build, so a
    // Minecraft patch that only moves code does not reach here at all.
    const mcht::builds::ResolvedCode& code = mcht::builds::ActiveCode();
    if (!code.Complete()) {
        cameraunlock::logging::Line(
            "The camera addresses were not recovered; staying dormant.");
        return false;
    }

    g_provider = provider;
    g_offsets = &offsets;

    const auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    g_getCameraComponent = reinterpret_cast<GetCameraComponentFn>(
        base + code.GetRenderCameraComponent);

    if (MH_Initialize() != MH_OK) {
        cameraunlock::logging::Line("MinHook failed to initialise; staying dormant.");
        return false;
    }

    void* const setupTarget = base + code.CameraSetup;
    if (MH_CreateHook(setupTarget, reinterpret_cast<void*>(&DetourCameraSetup),
                      reinterpret_cast<void**>(&g_originalCameraSetup)) != MH_OK ||
        MH_EnableHook(setupTarget) != MH_OK) {
        cameraunlock::logging::Line("Could not hook the camera setup at %p; staying dormant.",
                                    setupTarget);
        return false;
    }
    cameraunlock::logging::Line("Camera hook installed at %p.", setupTarget);

    InstallCrosshairHooks(base, code);
    return true;
}

}  // namespace mcht::camera
