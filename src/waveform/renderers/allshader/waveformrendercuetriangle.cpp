#include "waveform/renderers/allshader/waveformrendercuetriangle.h"

#include <QDomNode>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

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
constexpr float kTriangleSize = 8.0f;
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

    const float triSize = kTriangleSize * devicePixelRatio;
    const int imgSize = static_cast<int>(triSize + 2.0f);

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

        // Top triangle (pointing down ▼)
        {
            QImage image(imgSize, imgSize, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            image.setDevicePixelRatio(devicePixelRatio);

            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(cueColor);

            QPainterPath triangle;
            const float cx = imgSize / 2.0f;
            triangle.moveTo(cx - triSize / 2.0f, 0.0f);
            triangle.lineTo(cx + triSize / 2.0f, 0.0f);
            triangle.lineTo(cx, triSize);
            triangle.closeSubpath();
            painter.fillPath(triangle, cueColor);
            painter.end();

            auto pNode = std::make_unique<GeometryNode>();
            pNode->initForRectangles<TextureMaterial>(1);
            dynamic_cast<TextureMaterial&>(pNode->material())
                    .setTexture(std::make_unique<Texture>(pContext, image));
            pNode->markDirtyMaterial();

            const float labelW = static_cast<float>(imgSize) / devicePixelRatio;
            const float labelH = static_cast<float>(imgSize) / devicePixelRatio;
            const float x1 = static_cast<float>(xPoint) - labelW / 2.0f;
            const float y1 = 0.0f;

            TexturedVertexUpdater updater{
                    pNode->geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
            updater.addRectangle(
                    {x1, y1}, {x1 + labelW, y1 + labelH}, {0.f, 0.f}, {1.f, 1.f});
            pNode->markDirtyGeometry();
            appendChildNode(std::move(pNode));
        }

        // Bottom triangle (pointing up ▲)
        {
            QImage image(imgSize, imgSize, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            image.setDevicePixelRatio(devicePixelRatio);

            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(cueColor);

            QPainterPath triangle;
            const float cx = imgSize / 2.0f;
            triangle.moveTo(cx - triSize / 2.0f, triSize);
            triangle.lineTo(cx + triSize / 2.0f, triSize);
            triangle.lineTo(cx, 0.0f);
            triangle.closeSubpath();
            painter.fillPath(triangle, cueColor);
            painter.end();

            auto pNode = std::make_unique<GeometryNode>();
            pNode->initForRectangles<TextureMaterial>(1);
            dynamic_cast<TextureMaterial&>(pNode->material())
                    .setTexture(std::make_unique<Texture>(pContext, image));
            pNode->markDirtyMaterial();

            const float labelW = static_cast<float>(imgSize) / devicePixelRatio;
            const float labelH = static_cast<float>(imgSize) / devicePixelRatio;
            const float x1 = static_cast<float>(xPoint) - labelW / 2.0f;
            const float y1 = waveformHeight - labelH;

            TexturedVertexUpdater updater{
                    pNode->geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
            updater.addRectangle(
                    {x1, y1}, {x1 + labelW, y1 + labelH}, {0.f, 0.f}, {1.f, 1.f});
            pNode->markDirtyGeometry();
            appendChildNode(std::move(pNode));
        }
    }

    return true;
}

} // namespace allshader
