#pragma once

#include "rendergraph/geometrynode.h"
#include "util/class.h"
#include "waveform/renderers/waveformrendererabstract.h"

class QDomNode;
class SkinContext;

namespace allshader {
class WaveformRenderBeatMatchMarker;
} // namespace allshader

/// Serato-style beat-match markers ("Zapfen"): at every beat a coloured bar is
/// drawn at the waveform edge facing the gap between the two stacked decks. The
/// bar's colour comes from the audio's frequency content at that beat (bass =
/// red, mid = green, high = blue) and its width is proportional to the bass
/// energy. Aligning the colour/width patterns of the two decks gives precise
/// visual beatmatching.
class allshader::WaveformRenderBeatMatchMarker final
        : public QObject,
          public ::WaveformRendererAbstract,
          public rendergraph::GeometryNode {
    Q_OBJECT
  public:
    enum class Edge {
        None,
        Top,
        Bottom,
    };

    explicit WaveformRenderBeatMatchMarker(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);

    // Pure virtual from WaveformRendererAbstract, not used
    void draw(QPainter* painter, QPaintEvent* event) override final;

    void setup(const QDomNode& node, const SkinContext& skinContext) override;

    // Virtuals for rendergraph::Node
    void preprocess() override;

  public slots:
    void setBeatsPerBar(int beatsPerBar) {
        m_beatsPerBar = beatsPerBar;
    }

  private:
    bool preprocessInner();

    int m_beatsPerBar{4};
    Edge m_edge{Edge::None};
    bool m_isSlipRenderer;

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderBeatMatchMarker)
};
