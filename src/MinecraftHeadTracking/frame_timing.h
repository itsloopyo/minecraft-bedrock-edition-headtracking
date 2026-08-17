#pragma once

#include <chrono>

namespace mcht::tracking {

// Stand-in delta for the first frame and for any measurement that cannot be
// believed. 60Hz is the honest guess: it is close enough that one frame of
// smoothing at the wrong rate is invisible.
constexpr float kFallbackFrameSeconds = 1.0f / 60.0f;

// Above this, the gap is a stall - alt-tab, a loading screen, a breakpoint -
// rather than a frame. Feeding it to the smoothing would collapse the filter
// to the raw sample and snap the view on the frame the game comes back.
constexpr float kMaxPlausibleFrameSeconds = 0.25f;

// Seconds since the previous frame, or the fallback when the measurement is
// not plausible. Zero and negative are rejected too: the steady clock can
// report either across a resume, and a zero delta divides through the
// smoothing.
inline float NormalizeFrameDelta(float measuredSeconds) {
    if (measuredSeconds <= 0.0f || measuredSeconds > kMaxPlausibleFrameSeconds) {
        return kFallbackFrameSeconds;
    }
    return measuredSeconds;
}

// The render thread's own frame clock.
//
// Advance is the only way to read it, because every caller that wants the
// delta also owns the frame it measures. The one path that must NOT advance -
// holding a pose while no data arrives - simply does not call it, so the delta
// the next real sample is processed with spans the whole gap.
struct FrameClock {
    std::chrono::steady_clock::time_point Last{};
    bool Started = false;

    float Advance(std::chrono::steady_clock::time_point now) {
        const float seconds =
            Started ? NormalizeFrameDelta(std::chrono::duration<float>(now - Last).count())
                    : kFallbackFrameSeconds;
        Last = now;
        Started = true;
        return seconds;
    }
};

}  // namespace mcht::tracking
