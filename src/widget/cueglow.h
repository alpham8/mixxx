#pragma once

namespace mixxx {
namespace cueglow {

constexpr double kFadeInSeconds = 2.0;
constexpr double kHoldSeconds = 1.0;
constexpr double kFadeOutSeconds = 3.0;
constexpr float kMaxAlpha = 0.75f;

// Returns raw intensity [0.0, 1.0] based on temporal distance from a cue point.
// Negative distSeconds means the playhead is approaching the cue (before it),
// positive means the playhead has passed the cue.
// Multiply the result by kMaxAlpha to get the final overlay alpha.
inline float calcIntensity(double distSeconds) {
    if (distSeconds < -kFadeInSeconds) {
        return 0.0f;
    } else if (distSeconds < 0.0) {
        return static_cast<float>(
                1.0 + distSeconds / kFadeInSeconds);
    } else if (distSeconds < kHoldSeconds) {
        return 1.0f;
    } else if (distSeconds < kHoldSeconds + kFadeOutSeconds) {
        return static_cast<float>(
                1.0 - (distSeconds - kHoldSeconds) / kFadeOutSeconds);
    }
    return 0.0f;
}

} // namespace cueglow
} // namespace mixxx
