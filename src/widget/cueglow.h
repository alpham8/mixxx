#pragma once

namespace mixxx {
namespace cueglow {

constexpr double kDrainBeats = 16.0;
constexpr float kMaxAlpha = 0.75f;

// Returns raw intensity [0.0, 1.0] based on beat distance past a cue point.
// distBeats < 0 means the playhead has not yet reached the cue (no glow).
// distBeats >= 0 means the playhead has passed the cue — intensity drains
// linearly from 1.0 down to 0.0 over kDrainBeats beats.
// Multiply the result by kMaxAlpha to get the final overlay alpha.
inline float calcIntensity(double distBeats) {
    if (distBeats < 0.0) {
        return 0.0f;
    }
    if (distBeats >= kDrainBeats) {
        return 0.0f;
    }
    return static_cast<float>(1.0 - distBeats / kDrainBeats);
}

} // namespace cueglow
} // namespace mixxx
