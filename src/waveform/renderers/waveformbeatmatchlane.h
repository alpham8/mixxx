#pragma once

namespace mixxx {

/// Height (logical pixels) of the beat-match cones. They are drawn by
/// WaveformRenderBeatMatchMarker into a dedicated thin lane widget placed
/// between the two stacked decks (the skin sets "BeatMatchLane" and
/// "BeatMatchEdge"), so the waveforms themselves stay full-height and centred.
constexpr float kBeatMatchLaneHeight = 24.0f;

} // namespace mixxx
