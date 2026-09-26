#pragma once

#include <algorithm>
#include <cmath>

namespace cameraunlock {
namespace math {

// Clamp a value into [lo, hi], replacing non-finite input with fallback first.
// std::clamp does NOT sanitize NaN: clamp(NaN, lo, hi) returns NaN because
// both `NaN < lo` and `hi < NaN` are false. strtod-based INI parsing accepts
// "nan"/"inf" and overflows large literals like 1e400 to +inf, so a malformed
// or corrupted config value would otherwise pass straight through range
// validation and poison downstream sin/cos/view-matrix math. Use this for
// every float that crosses a config/user boundary.
inline float SanitizeFinite(float value, float fallback, float lo, float hi) {
    return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
}

}  // namespace math
}  // namespace cameraunlock
