#include "waveform/renderers/allshader/waveformrenderbeatmatchmarker.h"

#include <QDomNode>
#include <QVector2D>
#include <QVector3D>
#include <algorithm>
#include <vector>

#include "moc_waveformrenderbeatmatchmarker.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/rgbmaterial.h"
#include "rendergraph/vertexupdaters/rgbvertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "util/math.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"

using namespace rendergraph;

namespace {
// Length of a tooth, measured from the waveform's inner edge into the lane
// (logical pixels). Two decks back-to-back make a central lane twice this.
constexpr float kBandHeight = 16.0f;
// A tooth is also drawn on each half-beat for a denser grid. It has the same
// length as the beat teeth so the whole row reads as one uniform, easily
// readable grid (Serato style) instead of an alternating long/short pattern.
constexpr float kHalfBeatLengthScale = 1.0f;
// Marker half-width = base + bass-energy * scale (logical pixels). A modest
// bass term keeps kick-heavy beats a little fatter without making the row look
// irregular.
constexpr float kMinHalfWidth = 1.5f;
constexpr float kBassHalfWidthScale = 4.0f;
// Dark backing of the marker bar so the teeth sit in their own lane instead of
// overlapping the coloured audio.
constexpr float kBarBackingGrey = 0.10f;

constexpr int kVerticesPerTooth = 3;     // one triangle per tooth
constexpr int kVerticesPerRectangle = 6; // the dark bar backing (2 triangles)
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
    if (m_edge == Edge::None) {
        return false;
    }

    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();
    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    const auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                               : ::WaveformRendererAbstract::Play;

    const mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
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

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);
    if (!startPosition.isValid() || !endPosition.isValid()) {
        return false;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const float breadth = m_waveformRenderer->getBreadth();
    const float length = m_waveformRenderer->getLength() * devicePixelRatio;
    const float bandHeight = kBandHeight * devicePixelRatio;

    std::vector<double> beatPositions;
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        beatPositions.push_back(it->toEngineSamplePos());
    }
    const int numBeats = static_cast<int>(beatPositions.size());
    if (numBeats == 0) {
        geometry().allocate(0);
        markDirtyGeometry();
        return true;
    }

    // A full tooth on every beat plus a shorter one on every half-beat between
    // consecutive beats, for a denser Serato-like grid.
    const int numTeeth = numBeats + std::max(numBeats - 1, 0);

    // One dark backing rectangle (the lane) plus the teeth.
    geometry().allocate(kVerticesPerRectangle + numTeeth * kVerticesPerTooth);
    RGBVertexUpdater updater{geometry().vertexDataAs<Geometry::RGBColoredPoint2D>()};

    // The marker bar sits in a band at the waveform's inner edge. Each tooth is a
    // triangle (Serato-style Zapfen): the wide base sits against the waveform
    // body and the apex points into the lane between the two waveforms.
    const bool top = (m_edge == Edge::Top);
    const float bandTop = top ? 0.f : breadth - bandHeight;
    const float bandBottom = top ? bandHeight : breadth;
    // Serato orientation: the wide base sits at the centre-lane edge (where the
    // two decks meet) and the tip points outward toward this deck's waveform.
    const float baseY = top ? 0.f : breadth;

    // Dark lane behind the teeth so they sit in their own bar instead of
    // overlapping the coloured audio.
    updater.addRectangle({0.f, bandTop},
            {length, bandBottom},
            {kBarBackingGrey, kBarBackingGrey, kBarBackingGrey});

    // Draw one tooth at an engine-sample position. lengthScale 1.0 is a full
    // beat, a smaller value a half-beat. Colour comes from the frequency bands
    // (bass = red, mid = green, high = blue, normalised) and the width from the
    // bass energy, both sampled from the analysed waveform at that position.
    const auto emitTooth = [&](double position, float lengthScale) {
        double xPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                position, positionType);
        xPoint = qRound(xPoint * devicePixelRatio) / devicePixelRatio;
        const float x = static_cast<float>(xPoint);

        const double frac = position / trackSamples;
        const int centre = std::clamp(
                static_cast<int>(frac * dataSize), 0, dataSize - 1);
        constexpr int kWindow = 8;
        uchar maxLow = 0;
        uchar maxMid = 0;
        uchar maxHigh = 0;
        const int from = std::max(centre - kWindow, 0);
        const int to = std::min(centre + kWindow, dataSize - 1);
        for (int i = from; i <= to; ++i) {
            maxLow = math_max(maxLow, waveform->getLow(i));
            maxMid = math_max(maxMid, waveform->getMid(i));
            maxHigh = math_max(maxHigh, waveform->getHigh(i));
        }

        float red = static_cast<float>(maxLow);
        float green = static_cast<float>(maxMid);
        float blue = static_cast<float>(maxHigh);
        const float maxComponent = math_max3(red, green, blue);
        if (maxComponent > 0.f) {
            const float norm = 1.f / maxComponent;
            red *= norm;
            green *= norm;
            blue *= norm;
        } else {
            // Silent position: still draw a visible neutral-grey tooth.
            red = 0.5f;
            green = 0.5f;
            blue = 0.5f;
        }

        const float halfWidth =
                (kMinHalfWidth + (static_cast<float>(maxLow) / 255.f) * kBassHalfWidthScale) *
                devicePixelRatio;
        const float apex = top ? bandHeight * lengthScale
                               : breadth - bandHeight * lengthScale;

        updater.addTriangle({x - halfWidth, baseY},
                {x + halfWidth, baseY},
                {x, apex},
                {red, green, blue});
    };

    for (int i = 0; i < numBeats; ++i) {
        emitTooth(beatPositions[i], 1.0f);
        if (i + 1 < numBeats) {
            const double midPosition = 0.5 * (beatPositions[i] + beatPositions[i + 1]);
            emitTooth(midPosition, kHalfBeatLengthScale);
        }
    }

    DEBUG_ASSERT(kVerticesPerRectangle + numTeeth * kVerticesPerTooth == updater.index());

    markDirtyGeometry();
    return true;
}

} // namespace allshader
