#include "waveform/renderers/allshader/waveformrendercuetriangle.h"

#include <QDomNode>
#include <QImage>

#include "rendergraph/context.h"
#include "rendergraph/geometry.h"
#include "rendergraph/geometrynode.h"
#include "rendergraph/material/texturematerial.h"
#include "rendergraph/texture.h"
#include "rendergraph/vertexupdaters/texturedvertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/cue.h"
#include "track/track.h"
#include "util/color/rgbcolor.h"
#include "waveform/renderers/waveformwidgetrenderer.h"

using namespace rendergraph;

namespace {
// Width of the vertical cue line (logical pixels). Serato marks cues with a thin
// full-height line in the cue colour rather than triangles at the edges.
constexpr float kLineWidth = 2.0f;
} // namespace

namespace allshader {

WaveformRenderCueTriangle::WaveformRenderCueTriangle(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    setUsePreprocess(true);
}

void WaveformRenderCueTriangle::setup(
        const QDomNode& node, const SkinContext& skinContext) {
    Q_UNUSED(node);
    Q_UNUSED(skinContext);
}

void WaveformRenderCueTriangle::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderCueTriangle::removeAllChildNodes() {
    while (firstChild()) {
        detachChildNode(firstChild());
    }
}

void WaveformRenderCueTriangle::preprocess() {
    if (!preprocessInner()) {
        removeAllChildNodes();
    }
}

bool WaveformRenderCueTriangle::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const float waveformHeight = static_cast<float>(
            m_waveformRenderer->getHeight());

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const QList<CuePointer> cuePoints = trackInfo->getCuePoints();
    if (cuePoints.isEmpty()) {
        return false;
    }

    removeAllChildNodes();

    rendergraph::Context* pContext = m_waveformRenderer->getContext();

    for (const auto& pCue : cuePoints) {
        if (pCue->getType() != mixxx::CueType::HotCue) {
            continue;
        }
        const auto position = pCue->getPosition();
        if (!position.isValid()) {
            continue;
        }

        const double samplePos = position.toEngineSamplePos();
        const double normalizedPos = samplePos / trackSamples;
        if (normalizedPos < firstDisplayedPosition ||
                normalizedPos > lastDisplayedPosition) {
            continue;
        }

        double xPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                samplePos, positionType);
        xPoint = qRound(xPoint * devicePixelRatio) / devicePixelRatio;

        const QColor cueColor = mixxx::RgbColor::toQColor(pCue->getColor());

        // A thin vertical line in the cue colour spanning the full height
        // (Serato style), instead of triangles at the top and bottom edges.
        QImage image(2, 2, QImage::Format_ARGB32_Premultiplied);
        image.fill(cueColor);
        image.setDevicePixelRatio(devicePixelRatio);

        auto pNode = std::make_unique<GeometryNode>();
        pNode->initForRectangles<TextureMaterial>(1);
        dynamic_cast<TextureMaterial&>(pNode->material())
                .setTexture(std::make_unique<Texture>(pContext, image));
        pNode->markDirtyMaterial();

        const float x1 = static_cast<float>(xPoint) - kLineWidth / 2.0f;
        TexturedVertexUpdater updater{
                pNode->geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
        updater.addRectangle(
                {x1, 0.f}, {x1 + kLineWidth, waveformHeight}, {0.f, 0.f}, {1.f, 1.f});
        pNode->markDirtyGeometry();
        appendChildNode(std::move(pNode));
    }

    return true;
}

} // namespace allshader
