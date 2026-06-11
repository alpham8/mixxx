#include "widget/wspinny.h"

#include <QPainter>
#include <QPainterPath>

#include "widget/cueglow.h"

#include "moc_wspinny.cpp"

WSpinny::WSpinny(
        QWidget* pParent,
        const QString& group,
        UserSettingsPointer pConfig,
        VinylControlManager* pVCMan,
        BaseTrackPlayer* pPlayer)
        : WSpinnyBase(pParent, group, pConfig, pVCMan, pPlayer) {
}

void WSpinny::draw() {
    double scaleFactor = devicePixelRatioF();

    QPainter p(paintDevice());
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_pBgImage) {
        p.drawImage(rect(), *m_pBgImage, m_pBgImage->rect());
    }

    if (m_bShowCover && !m_loadedCoverScaled.isNull()) {
        double x = (width() - m_loadedCoverScaled.width() / scaleFactor) / 2;
        double y = (height() - m_loadedCoverScaled.height() / scaleFactor) / 2;
        p.drawPixmap(QPointF(x, y), m_loadedCoverScaled);
    }

    if (m_pMaskImage) {
        p.drawImage(rect(), *m_pMaskImage, m_pMaskImage->rect());
    }

    // Overlay the signal quality drawing if vinyl is active
    if (shouldDrawVinylQuality()) {
        // draw the last good image
        p.drawImage(this->rect(), m_qImage);
    }

    // To rotate the foreground image around the center of the image,
    // we use the classic trick of translating the coordinate system such that
    // the origin is at the center of the image. We then rotate the coordinate system,
    // and draw the image at the corner.
    p.translate(width() / 2, height() / 2);

    bool paintGhost = m_bGhostPlayback && m_pGhostImage && !m_pGhostImage->isNull();
    if (paintGhost) {
        p.save();
    }

    if (paintGhost) {
        p.restore();
        p.save();
        p.rotate(m_fGhostAngle);
        p.drawImage(QPointF(-m_ghostImageScaled.width() / scaleFactor / 2.0,
                            -m_ghostImageScaled.height() / scaleFactor / 2.0),
                m_ghostImageScaled);

        //Rotate back to the playback position (not the ghost position),
        //and draw the beat marks from there.
        p.restore();
    }

    if (m_pFgImage && !m_pFgImage->isNull()) {
        p.rotate(m_fAngle);
        p.drawImage(QPointF(-m_fgImageScaled.width() / scaleFactor / 2.0,
                            -m_fgImageScaled.height() / scaleFactor / 2.0),
                m_fgImageScaled);

        if (m_cueGlowIntensity > 0.01f) {
            const float rawIntensity = m_cueGlowIntensity /
                    mixxx::cueglow::kMaxAlpha;
            QImage tinted = mixxx::cueglow::createTintedForeground(
                    m_fgImageScaled, m_cueGlowColor, rawIntensity);
            p.drawImage(QPointF(-tinted.width() / scaleFactor / 2.0,
                                -tinted.height() / scaleFactor / 2.0),
                    tinted);
        }
    }

    if (m_cueGlowIntensity > 0.01f) {
        p.resetTransform();
        QPainterPath circle;
        circle.addEllipse(rect());
        p.setClipPath(circle);
        QColor glowColor = m_cueGlowColor;
        glowColor.setAlphaF(m_cueGlowIntensity);
        p.fillRect(rect(), glowColor);
    }

    // Serato-style BPM/Time overlay on spinny
    // Must come after cue glow and reset clip path
    {
        p.resetTransform();
        p.setClipping(false);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        const double bpm = m_pBpm.get();
        const double playPos = m_pPlayPos.get();
        const double trackSamples = m_pTrackSamples.get();
        const double sampleRate = m_pTrackSampleRate.get();

        if (bpm > 0.0 && width() >= 80) {
            const int cx = width() / 2;
            const int cy = height() / 2;

            const int overlayRadius = qMin(width(), height()) / 4;

            // BPM large text
            QFont bpmFont;
            bpmFont.setPixelSize(overlayRadius * 3 / 4);
            bpmFont.setBold(true);
            bpmFont.setWeight(QFont::Black);
            p.setFont(bpmFont);
            p.setPen(QColor(40, 40, 40));
            QString bpmText = QString::number(bpm, 'f', 1);
            QRect bpmRect(cx - overlayRadius, cy - overlayRadius * 3 / 4,
                    overlayRadius * 2, overlayRadius / 2);
            p.drawText(bpmRect, Qt::AlignCenter, bpmText);

            // Pitch% below BPM
            const double rateRatio = m_pRateRatio.get();
            const double pitchPct = (rateRatio - 1.0) * 100.0;
            QFont pitchFont;
            pitchFont.setPixelSize(overlayRadius / 5);
            p.setFont(pitchFont);
            p.setPen(QColor(80, 80, 80));
            QString pitchText = QStringLiteral("%1%2%")
                    .arg(pitchPct >= 0 ? "+" : "")
                    .arg(pitchPct, 0, 'f', 1);
            QRect pitchRect(cx - overlayRadius, cy - overlayRadius / 3,
                    overlayRadius * 2, overlayRadius / 4);
            p.drawText(pitchRect, Qt::AlignCenter, pitchText);

            // Time text below BPM
            if (trackSamples > 0.0 && sampleRate > 0.0) {
                const double totalSeconds = trackSamples / sampleRate / 2.0;
                const double elapsedSeconds = playPos * totalSeconds;
                const int mins = static_cast<int>(elapsedSeconds) / 60;
                const int secs = static_cast<int>(elapsedSeconds) % 60;
                const int tenths = static_cast<int>(elapsedSeconds * 10) % 10;

                QFont timeFont;
                timeFont.setPixelSize(overlayRadius / 3);
                p.setFont(timeFont);
                p.setPen(QColor(60, 60, 60));
                QString timeText = QStringLiteral("%1:%2.%3")
                        .arg(mins, 2, 10, QChar('0'))
                        .arg(secs, 2, 10, QChar('0'))
                        .arg(tenths);
                QRect timeRect(cx - overlayRadius, cy - overlayRadius / 6,
                        overlayRadius * 2, overlayRadius / 2);
                p.drawText(timeRect, Qt::AlignCenter, timeText);

                // Duration below elapsed time
                const int durMins = static_cast<int>(totalSeconds) / 60;
                const int durSecs = static_cast<int>(totalSeconds) % 60;
                QString durText = QStringLiteral("%1:%2")
                        .arg(durMins, 2, 10, QChar('0'))
                        .arg(durSecs, 2, 10, QChar('0'));
                QRect durRect(cx - overlayRadius, cy + overlayRadius / 4,
                        overlayRadius * 2, overlayRadius / 2);
                p.setPen(QColor(100, 100, 100));
                p.drawText(durRect, Qt::AlignCenter, durText);
            }
        }
    }
}

void WSpinny::setupVinylSignalQuality() {
    m_qImage = QImage(m_iVinylScopeSize, m_iVinylScopeSize, QImage::Format_ARGB32);
}

void WSpinny::updateVinylSignalQualityImage(const QColor& qual_color, const unsigned char* data) {
    int r, g, b;
    qual_color.getRgb(&r, &g, &b);

    for (int y = 0; y < m_iVinylScopeSize; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(m_qImage.scanLine(y));
        for (int x = 0; x < m_iVinylScopeSize; ++x) {
            // use xwax's bitmap to set alpha data only
            // adjust alpha by 3/4 so it's not quite so distracting
            // setpixel is slow, use scanlines instead
            // m_qImage.setPixel(x, y, qRgba(r,g,b,(int)buf[x+m_iVinylScopeSize*y] * .75));
            *line = qRgba(r, g, b, static_cast<int>(data[x + m_iVinylScopeSize * y] * .75));
            line++;
        }
    }
}

void WSpinny::coverChanged() {
}
