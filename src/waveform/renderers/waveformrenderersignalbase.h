#pragma once

#include <memory>

#include "skin/legacy/skincontext.h"
#include "util/span.h"
#include "util/types.h"
#include "waveform/waveform.h"
#include "waveformrendererabstract.h"

class ControlProxy;
class WaveformSignalColors;

class WaveformRendererSignalBase : public QObject, public WaveformRendererAbstract {
    Q_OBJECT
  public:
    enum class Option {
        None = 0b0,
        SplitStereoSignal = 0b1,
        HighDetail = 0b10,
        AllOptionsCombined = SplitStereoSignal | HighDetail,
    };
    Q_DECLARE_FLAGS(Options, Option)

    explicit WaveformRendererSignalBase(
            WaveformWidgetRenderer* waveformWidgetRenderer, Options options);
    virtual ~WaveformRendererSignalBase();

    virtual bool init();
    virtual void setup(const QDomNode& node, const SkinContext& context);

    virtual bool onInit() {
        return true;
    }
    virtual void onSetup(const QDomNode& node) = 0;

  public slots:
    void setAllChannelVisualGain(double gain) {
        m_allChannelVisualGain = static_cast<CSAMPLE_GAIN>(gain);
    }
    void setLowVisualGain(double gain) {
        m_lowVisualGain = static_cast<CSAMPLE_GAIN>(gain);
    }
    void setMidVisualGain(double gain) {
        m_midVisualGain = static_cast<CSAMPLE_GAIN>(gain);
    }
    void setHighVisualGain(double gain) {
        m_highVisualGain = static_cast<CSAMPLE_GAIN>(gain);
    }
    void setNormalizeWaveform(bool normalize) {
        m_normalizeWaveform = normalize;
    }
    virtual void setOptions(WaveformRendererSignalBase::Options) {
    }

  protected:
    void getGains(float* pAllGain,
            float* pLowGain,
            float* pMidGain,
            float* highGain);
    float getTrackPeak();

    // Vertical centre and half-height of the band the waveform is drawn in,
    // after reserving the beat-match cone lane on its inner edge. Insets and
    // breadth (getBreadth) are both logical pixels - the renderer geometry works
    // in logical coordinates and the device pixel ratio is applied by the
    // projection - so no scaling is needed. Without a lane these reduce to
    // breadth/2, i.e. the full centred waveform.
    float waveformBandCenter(float breadth) const {
        return (m_laneInsetTop + breadth - m_laneInsetBottom) * 0.5f;
    }
    float waveformBandHalfHeight(float breadth) const {
        return (breadth - (m_laneInsetTop + m_laneInsetBottom)) * 0.5f;
    }

  protected:
    std::unique_ptr<ControlProxy> m_pEQEnabled;
    std::unique_ptr<ControlProxy> m_pLowFilterControlObject;
    std::unique_ptr<ControlProxy> m_pMidFilterControlObject;
    std::unique_ptr<ControlProxy> m_pHighFilterControlObject;
    std::unique_ptr<ControlProxy> m_pLowKillControlObject;
    std::unique_ptr<ControlProxy> m_pMidKillControlObject;
    std::unique_ptr<ControlProxy> m_pHighKillControlObject;

    Qt::Alignment m_alignment;
    Qt::Orientation m_orientation;

    // Reserved beat-match cone lane (logical px) squeezed out of the waveform on
    // the inner edge so the cones get their own space. Both 0 unless the skin
    // sets "BeatMatchEdge" (top/bottom). See waveformbeatmatchlane.h.
    float m_laneInsetTop{0.f};
    float m_laneInsetBottom{0.f};

    CSAMPLE_GAIN m_allChannelVisualGain;
    CSAMPLE_GAIN m_lowVisualGain;
    CSAMPLE_GAIN m_midVisualGain;
    CSAMPLE_GAIN m_highVisualGain;

    bool m_normalizeWaveform;
    ConstWaveformPointer m_pCachedPeakWaveform;
    float m_cachedTrackPeak;

    float m_axesColor_r, m_axesColor_g, m_axesColor_b, m_axesColor_a;
    float m_signalColor_r, m_signalColor_g, m_signalColor_b;
    float m_signalColor_h, m_signalColor_s, m_signalColor_v;
    float m_lowColor_r, m_lowColor_g, m_lowColor_b;
    float m_midColor_r, m_midColor_g, m_midColor_b;
    float m_highColor_r, m_highColor_g, m_highColor_b;
    float m_rgbLowColor_r, m_rgbLowColor_g, m_rgbLowColor_b;
    float m_rgbMidColor_r, m_rgbMidColor_g, m_rgbMidColor_b;
    float m_rgbHighColor_r, m_rgbHighColor_g, m_rgbHighColor_b;
    float m_rgbLowFilteredColor_r, m_rgbLowFilteredColor_g, m_rgbLowFilteredColor_b;
    float m_rgbMidFilteredColor_r, m_rgbMidFilteredColor_g, m_rgbMidFilteredColor_b;
    float m_rgbHighFilteredColor_r, m_rgbHighFilteredColor_g, m_rgbHighFilteredColor_b;
};
