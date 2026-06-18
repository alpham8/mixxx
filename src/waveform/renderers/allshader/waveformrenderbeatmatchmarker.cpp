#include "waveform/renderers/allshader/waveformrenderbeatmatchmarker.h"

#include <QDomNode>
#include <QVector2D>
#include <QVector3D>
#include <algorithm>
#include <cmath>

#include "moc_waveformrenderbeatmatchmarker.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/rgbmaterial.h"
#include "rendergraph/vertexupdaters/rgbvertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "util/math.h"
#include "waveform/renderers/waveformbeatmatchlane.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"

using namespace rendergraph;

namespace {
// Cone height (logical pixels). Must match mixxx::kBeatMatchLaneHeight so the
// cones fit exactly in the band the signal renderer reserves for them. Every
// cone is drawn at this full height - only the colour varies with the audio.
constexpr float kBandHeight = mixxx::kBeatMatchLaneHeight;
// Mild brightness boost for the cone colour; kept low so deep bass stays a rich
// red instead of washing to a saturated orange/yellow wall - clamped to 1.0.
constexpr float kColourGain = 1.3f;
// Horizontal spacing between cone centres (logical pixels). At default zoom a
// Serato-style strip has one prominent cone per ~10 px - wide enough to read as
// a proper triangle, not a line.
constexpr float kConeStepPx = 10.0f;
// Fraction of each step the cone base fills (the rest is gap). 0.70 gives a
// ~7 px base with ~3 px gap, which is clearly triangular at this step size.
constexpr float kConeFillFraction = 0.70f;
// Scan this fraction of the visible span past each edge so the row is already
// built before it scrolls into view.
constexpr double kRangeMarginFraction = 0.15;

constexpr int kVerticesPerCone = 3; // one triangular Zapfen
} // namespace

namespace allshader {

WaveformRenderBeatMatchMarker::WaveformRenderBeatMatchMarker(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<RGBMaterial>(0);
    setUsePreprocess(true);
}

void WaveformRenderBeatMatchMarker::setup(const QDomNode& node, const SkinContext& skinContext) {
    const QString edge = skinContext.selectString(
            node, QStringLiteral("BeatMatchEdge")).trimmed().toLower();
    if (edge == QStringLiteral("top")) {
        m_edge = Edge::Top;
    } else if (edge == QStringLiteral("bottom")) {
        m_edge = Edge::Bottom;
    } else {
        m_edge = Edge::None;
    }
}

void WaveformRenderBeatMatchMarker::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderBeatMatchMarker::preprocess() {
    if (!preprocessInner()) {
        if (geometry().vertexCount() != 0) {
            geometry().allocate(0);
            markDirtyGeometry();
        }
    }
}

bool WaveformRenderBeatMatchMarker::preprocessInner() {
    // The cones live in their own thin lane widget (skin sets "BeatMatchLane").
    // Never draw them inside the normal waveform - there they would overlap the
    // full-height, centred signal.
    if (!m_waveformRenderer->isBeatMatchLane() || m_edge == Edge::None) {
        return false;
    }

    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();
    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    const auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                               : ::WaveformRendererAbstract::Play;

    // The strip only shows for analysed tracks (beat-match context).
    if (!trackInfo->getBeats()) {
        return false;
    }

    ConstWaveformPointer waveform = trackInfo->getWaveform();
    if (!waveform) {
        return false;
    }
    const int dataSize = waveform->getDataSize();
    if (dataSize <= 0) {
        return false;
    }

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    // Scan a little past both visible edges (clamped to the track) so teeth are
    // already built before their beat scrolls into view.
    const double margin =
            (lastDisplayedPosition - firstDisplayedPosition) * kRangeMarginFraction;
    const double startFraction = std::max(firstDisplayedPosition - margin, 0.0);
    const double endFraction = std::min(lastDisplayedPosition + margin, 1.0);

    // All geometry is in logical pixels (getBreadth/getLength and the sample->x
    // transform are logical); the device pixel ratio is applied by the
    // projection, so it must not be folded into the coordinates here.
    const float breadth = m_waveformRenderer->getBreadth();
    const float bandHeight = kBandHeight;

    const bool top = (m_edge == Edge::Top);
    // Cones grow from the inner edge toward the waveform centre. The signal
    // renderer reserves a matching band, so the cones sit in their own space
    // with a gap to the waveform - no mask needed here.
    const float baseY = top ? 0.f : breadth;

    // Map a track fraction to its x in renderer-world (logical) pixels.
    const auto worldX = [&](double frac) {
        return static_cast<float>(
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        frac * trackSamples, positionType));
    };

    // Anchor each cone to an absolute waveform-data bucket (multiples of
    // indexStep counted from index 0), not to a viewport-relative subdivision.
    // Each bucket always covers the same audio, so the cones stay glued to the
    // waveform as it scrolls instead of crawling across it and flickering. The
    // bucket width is chosen so the cones sit roughly kConeStepPx apart on screen.
    const double visibleFraction = lastDisplayedPosition - firstDisplayedPosition;
    const int lengthPx = std::max(1, m_waveformRenderer->getLength());
    const double indexStep = std::max(
            1.0, visibleFraction * dataSize * kConeStepPx / lengthPx);

    const int idxStart = std::clamp(
            static_cast<int>(startFraction * dataSize), 0, dataSize - 1);
    const int idxEnd = std::clamp(
            static_cast<int>(endFraction * dataSize), 0, dataSize - 1);
    const int firstBucket = static_cast<int>(std::floor(idxStart / indexStep));
    const int lastBucket = static_cast<int>(std::floor(idxEnd / indexStep));
    const int numCones = lastBucket - firstBucket + 1;

    // One triangular Zapfen per cone.
    geometry().allocate(kVerticesPerCone * numCones);
    RGBVertexUpdater updater{geometry().vertexDataAs<Geometry::RGBColoredPoint2D>()};

    const float halfWidth = kConeStepPx * kConeFillFraction * 0.5f;
    for (int b = firstBucket; b <= lastBucket; ++b) {
        const int idxFrom = std::clamp(
                static_cast<int>(b * indexStep), 0, dataSize - 1);
        const int idxTo = std::clamp(
                static_cast<int>((b + 1) * indexStep) - 1, idxFrom, dataSize - 1);

        // Peak over the data points this bucket covers (bass = red, mid = green,
        // high = blue), so the cone stays steady instead of popping on a single
        // sample as the waveform scrolls.
        uchar low = 0;
        uchar mid = 0;
        uchar high = 0;
        for (int i = idxFrom; i <= idxTo; ++i) {
            low = math_max(low, waveform->getLow(i));
            mid = math_max(mid, waveform->getMid(i));
            high = math_max(high, waveform->getHigh(i));
        }

        // Snap each cone vertex to a whole device pixel (like the beat renderer)
        // so the row does not shimmer with sub-pixel coverage while scrolling.
        const double centreFrac = (b + 0.5) * indexStep / dataSize;
        const float x = static_cast<float>(qRound(worldX(centreFrac)));

        // Every cone is the same full-band-height triangle - only the colour
        // (bass = red, mid = green, high = blue) varies with the audio. The row
        // reads as an even Serato-style strip instead of a second waveform that
        // follows the amplitude with uneven peaks.
        const QVector3D color{
                std::min(1.f, static_cast<float>(low) / 255.f * kColourGain),
                std::min(1.f, static_cast<float>(mid) / 255.f * kColourGain),
                std::min(1.f, static_cast<float>(high) / 255.f * kColourGain)};
        const float apex = top ? bandHeight : breadth - bandHeight;

        // One triangular Zapfen: base on the lane floor, apex pointing inward.
        updater.addTriangle({x - halfWidth, baseY},
                {x + halfWidth, baseY},
                {x, apex},
                color);
    }

    DEBUG_ASSERT(kVerticesPerCone * numCones == updater.index());

    markDirtyGeometry();
    return true;
}

} // namespace allshader
