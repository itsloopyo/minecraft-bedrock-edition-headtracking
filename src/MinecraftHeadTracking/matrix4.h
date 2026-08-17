#pragma once

#include <cmath>

// Row-major storage, row-vector convention: a point is transformed as v * M,
// so a transform applied later in the chain multiplies on the right.
//
// Note that "row-major storage with row vectors" and "column-major storage
// with column vectors" are the SAME system - identical bytes, identical
// translation slot, identical argument order - so observations like "the
// engine computes viewProj as view * proj" hold under both and settle nothing.
// The only real alternative is the transposed family, and two in-game results
// rule it out: under it a yaw would swing the scene about the world origin
// rather than the camera, and a translation written to [12..14] would land in
// the projective row and warp perspective instead of producing parallax.
// Neither was observed; the measured parallax was clean and affine.
namespace mcht::math {

constexpr float kPi = 3.14159265358979323846f;

// AxisRotation's first argument. Named because a bare 0, 1 or 2 at a call site
// says nothing about which rotation it builds, and the three are otherwise
// interchangeable by eye.
constexpr int kAxisYaw = 0;
constexpr int kAxisPitch = 1;
constexpr int kAxisRoll = 2;

constexpr int kMatrixFloats = 16;
constexpr int kMatrixOrder = 4;

inline void Identity(float* out) {
    for (int i = 0; i < kMatrixFloats; ++i) {
        // The diagonal of a row-major 4x4 is every fifth element.
        out[i] = (i % (kMatrixOrder + 1) == 0) ? 1.0f : 0.0f;
    }
}

inline void Multiply(const float* a, const float* b, float* out) {
    for (int row = 0; row < kMatrixOrder; ++row) {
        for (int col = 0; col < kMatrixOrder; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < kMatrixOrder; ++k) {
                sum += a[row * kMatrixOrder + k] * b[k * kMatrixOrder + col];
            }
            out[row * kMatrixOrder + col] = sum;
        }
    }
}

// axis: kAxisYaw (about view up), kAxisPitch (about view right), kAxisRoll
// (about the view axis).
inline void AxisRotation(int axis, float degrees, float* out) {
    const float r = degrees * kPi / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    Identity(out);
    if (axis == kAxisYaw) {
        out[0] = c;  out[2] = -s;
        out[8] = s;  out[10] = c;
    } else if (axis == kAxisPitch) {
        out[5] = c;  out[6] = s;
        out[9] = -s; out[10] = c;
    } else {
        out[0] = c;  out[1] = s;
        out[4] = -s; out[5] = c;
    }
}

// Rotation about an arbitrary unit axis. AxisRotation(0, degrees) is this with
// the axis (0, 1, 0), which is why world-space yaw collapses onto camera-local
// yaw exactly when the game camera is level rather than merely close to it.
inline void AxisAngleRotation(const float axis[3], float degrees, float* out) {
    const float r = degrees * kPi / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    const float t = 1.0f - c;
    const float x = axis[0];
    const float y = axis[1];
    const float z = axis[2];
    Identity(out);
    out[0] = c + x * x * t;
    out[1] = x * y * t + z * s;
    out[2] = x * z * t - y * s;
    out[4] = y * x * t - z * s;
    out[5] = c + y * y * t;
    out[6] = y * z * t + x * s;
    out[8] = z * x * t + y * s;
    out[9] = z * y * t - x * s;
    out[10] = c + z * z * t;
}

// Translation lives in the last row. Composed into the camera's post-view
// transform it acts along the camera's own axes, which is what a 6DOF head
// position offset needs.
inline void Translation(float x, float y, float z, float* out) {
    Identity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

}  // namespace mcht::math
