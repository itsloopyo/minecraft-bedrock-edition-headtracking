#pragma once

#include <cmath>

namespace mcht::camera {

// Where the crosshair belongs, in normalised device coordinates: x and y each
// run -1 to +1 across the viewport, x rightwards and y upwards.
struct AimNdc {
    float x;
    float y;
};

// The frustum ProjectAimPoint divides by comes out of game memory on the render
// path, and unlike the ini nothing upstream has sanitized it. A field of view
// of zero, of NaN, or of pi makes tan(fov/2) zero, NaN or unbounded, and the
// offset that reaches the crosshair rect is then converted to int - which for
// a non-finite or out-of-range value is undefined behaviour rather than merely
// a wrong picture. So the two are checked before they are used, at the one
// place that reads them.
//
// The bounds are deliberately far wider than any real camera: this is here to
// catch a field that is not the field the profile says it is, not to police a
// setting.
constexpr float kMinFovRadians = 0.01f;
constexpr float kMaxFovRadians = 3.13f;  // Just inside pi, where tan diverges.
constexpr float kMinAspect = 0.1f;
constexpr float kMaxAspect = 10.0f;

// Written as a pair of open-interval tests so NaN fails both, which a
// clamp-style check would not.
inline bool PlausibleProjection(float fovRadiansVertical, float aspect) {
    return fovRadiansVertical > kMinFovRadians && fovRadiansVertical < kMaxFovRadians &&
           aspect > kMinAspect && aspect < kMaxAspect;
}

// Projects the clean aim point through the head transform the camera was
// actually given this frame, which is what keeps the crosshair on the aim
// point under any composition. Deriving it from the yaw/pitch/roll angles
// instead would only agree with the view while the composition it assumed
// happened to match the one the hook applies.
//
// `head` is the clean-to-tracked delta, 4x4 row-major with row vectors, so its
// last row carries the 6DOF translation. Taking the rotation alone would drop
// that: with position tracking on, leaning would move the view and leave the
// crosshair behind.
//
// View forward is -Z on this engine (the projection's [2][3] is -1), so the
// aim point is a point that distance ahead along -Z, and a visible one has
// negative view z.
//
// False when the aim point is behind the camera, which an extreme head turn
// can reach. Pushing it through the divide would mirror the crosshair to the
// opposite side of the screen, so the caller leaves it where the game put it.
inline bool ProjectAimPoint(const float head[16], float distanceMetres, float fovRadiansVertical,
                            float aspect, AimNdc& out) {
    const float x = -distanceMetres * head[8] + head[12];
    const float y = -distanceMetres * head[9] + head[13];
    const float z = -distanceMetres * head[10] + head[14];

    // Not >= 0: a point on or fractionally in front of the near plane divides
    // into an offset large enough to throw the crosshair off screen.
    if (z >= -1e-4f) {
        return false;
    }

    const float tanVertical = std::tan(fovRadiansVertical * 0.5f);
    const float tanHorizontal = tanVertical * aspect;
    out.x = -(x / z) / tanHorizontal;
    out.y = -(y / z) / tanVertical;
    return true;
}

}  // namespace mcht::camera
