#pragma once

#include <QColor>

#include "rendergraph/node.h"
#include "util/class.h"
#include "waveform/renderers/waveformrendererabstract.h"

class QDomNode;
class SkinContext;

namespace allshader {
class WaveformRenderCueTriangle;
} // namespace allshader

class allshader::WaveformRenderCueTriangle final
        : public ::WaveformRendererAbstract,
          public rendergraph::Node {
  public:
    explicit WaveformRenderCueTriangle(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);

    void draw(QPainter* painter, QPaintEvent* event) override final;

    void setup(const QDomNode& node, const SkinContext& skinContext) override;

    void preprocess() override;

  private:
    bool preprocessInner();
    void removeAllChildNodes();

    bool m_isSlipRenderer;

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderCueTriangle);
};
