#pragma once

namespace mixxx {

/// Height (logical pixels) of the beat-match cone lane reserved at the inner
/// edge of a waveform. The signal renderer squeezes its waveform out of this
/// band on the configured edge (see WaveformRendererSignalBase::setup reading
/// "BeatMatchEdge") so the cones drawn by WaveformRenderBeatMatchMarker get
/// their own space instead of the waveform being clipped where they sit.
constexpr float kBeatMatchLaneHeight = 24.0f;

/// Extra gap (logical pixels) the signal renderer leaves between the waveform
/// and the cone lane. Zero: the whole lane (band + gap) is reserved out of the
/// deck height (only ~110 px in 2-deck mode, half that with 4 decks), so the
/// cones get the full band height while the waveform keeps its size. The
/// waveform rarely fills its band, so there is still visible space above the
/// cones in practice.
constexpr float kBeatMatchLaneGap = 0.0f;

} // namespace mixxx
