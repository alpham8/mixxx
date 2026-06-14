#include "waveform/renderers/allshader/waveformrenderbeatmatchmarker.h"

#include <QDomNode>
#include <QVector2D>
#include <QVector3D>
#include <algorithm>

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
// Height of the marker bar at the waveform's inner edge (logical pixels). Two
// decks placed back-to-back make a central lane twice this height.
constexpr float kBandHeight = 10.0f;
// Marker half-width = base + bass-energy * scale (logical pixels).
// The base is wide enough that every beat shows a clearly visible tooth; the
// bass term makes kick-heavy (down)beats noticeably fatter, like Serato.
constexpr float kMinHalfWidth = 1.5f;
constexpr float kBassHalfWidthScale = 6.0f;
// Dark backing of the marker bar so the teeth sit in their own lane instead of
// overlapping the coloured audio.
constexpr float kBarBackingGrey = 0.10f;

constexpr int kVerticesPerTooth = 3;     // one triangle per beat
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

    int numBeats = 0;
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        numBeats++;
    }
    if (numBeats == 0) {
        geometry().allocate(0);
        markDirtyGeometry();
        return true;
    }

    // One dark backing rectangle (the lane) plus one tooth per beat.
    geometry().allocate(kVerticesPerRectangle + numBeats * kVerticesPerTooth);
    RGBVertexUpdater updater{geometry().vertexDataAs<Geometry::RGBColoredPoint2D>()};

    // The marker bar sits in a band at the waveform's inner edge. Each beat is
    // a triangular "tooth" (Serato-style Zapfen): the wide base sits against the
    // waveform body and the apex points into the lane between the two waveforms,
    // giving a precise per-beat alignment point.
    const bool top = (m_edge == Edge::Top);
    const float bandTop = top ? 0.f : breadth - bandHeight;
    const float bandBottom = top ? bandHeight : breadth;
    const float baseY = top ? bandHeight : breadth - bandHeight;
    const float apexY = top ? 0.f : breadth;

    // Dark lane behind the teeth so they sit in their own narrow bar instead of
    // overlapping the coloured audio.
    updater.addRectangle({0.f, bandTop},
            {length, bandBottom},
            {kBarBackingGrey, kBarBackingGrey, kBarBackingGrey});

    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        const double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPosition, positionType);
        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;
        const float x = static_cast<float>(xBeatPoint);

        // Sample the analysed waveform around the beat for the frequency
        // content (colour) and the bass energy (bar width).
        const double frac = beatPosition / trackSamples;
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

        // Colour from the frequency bands (bass = red, mid = green, high =
        // blue), normalised so the dominant band is full intensity.
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
            // Silent beat: still draw a visible neutral-grey tooth so the
            // beat is never missing from the row.
            red = 0.5f;
            green = 0.5f;
            blue = 0.5f;
        }

        const float halfWidth =
                (kMinHalfWidth + (static_cast<float>(maxLow) / 255.f) * kBassHalfWidthScale) *
                devicePixelRatio;

        updater.addTriangle({x - halfWidth, baseY},
                {x + halfWidth, baseY},
                {x, apexY},
                {red, green, blue});
    }

    DEBUG_ASSERT(kVerticesPerRectangle + numBeats * kVerticesPerTooth == updater.index());

    markDirtyGeometry();
    return true;
}

} // namespace allshader
