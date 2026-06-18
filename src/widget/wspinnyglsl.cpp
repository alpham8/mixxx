#include "widget/wspinnyglsl.h"

#include <QOpenGLTexture>
#include <QPainter>
#include <array>

#include "widget/cueglow.h"

#include "moc_wspinnyglsl.cpp"

WSpinnyGLSL::WSpinnyGLSL(
        QWidget* parent,
        const QString& group,
        UserSettingsPointer pConfig,
        VinylControlManager* pVCMan,
        BaseTrackPlayer* pPlayer)
        : WSpinnyBase(parent, group, pConfig, pVCMan, pPlayer) {
}

WSpinnyGLSL::~WSpinnyGLSL() {
    cleanupGL();
}

void WSpinnyGLSL::cleanupGL() {
    makeCurrentIfNeeded();
    m_bgTexture.destroy();
    m_maskTexture.destroy();
    m_fgTextureScaled.destroy();
    m_ghostTextureScaled.destroy();
    m_loadedCoverTextureScaled.destroy();
    m_qTexture.destroy();
    doneCurrent();
}

void WSpinnyGLSL::coverChanged() {
    if (isContextValid()) {
        makeCurrentIfNeeded();
        m_loadedCoverTextureScaled.setData(m_loadedCoverScaled);
        doneCurrent();
    }
    // otherwise this will happen in initializeGL
}

void WSpinnyGLSL::draw() {
    if (shouldRender()) {
        makeCurrentIfNeeded();
        paintGL();
        doneCurrent();
    }
}

void WSpinnyGLSL::resizeGL(int w, int h) {
    Q_UNUSED(w);
    Q_UNUSED(h);
    // The images were resized in WSpinnyBase::resizeEvent.
    updateTextures();
}

void WSpinnyGLSL::updateTextures() {
    if (m_pBgImage) {
        m_bgTexture.setData(*m_pBgImage);
    }
    if (m_pMaskImage) {
        m_maskTexture.setData(*m_pMaskImage);
    }
    m_fgTextureScaled.setData(m_fgImageScaled);
    m_ghostTextureScaled.setData(m_ghostImageScaled);
    m_loadedCoverTextureScaled.setData(m_loadedCoverScaled);
}

void WSpinnyGLSL::setupVinylSignalQuality() {
}

void WSpinnyGLSL::updateVinylSignalQualityImage(
        const QColor& qual_color, const unsigned char* data) {
    m_vinylQualityColor = qual_color;
    m_vinylQualityColor.setAlphaF(0.75f);
    if (m_qTexture.isStorageAllocated()) {
        makeCurrentIfNeeded();
        m_qTexture.bind();
        // Using a texture of one byte per pixel so we can store the vinyl
        // signal quality data directly. The VinylQualityShader will draw this
        // colorized with alpha transparency.
        glTexSubImage2D(GL_TEXTURE_2D,
                0,
                0,
                0,
                m_iVinylScopeSize,
                m_iVinylScopeSize,
#if defined(GL_RED)
                GL_RED,
#else
                GL_RED_EXT,
#endif
                GL_UNSIGNED_BYTE,
                data);
        m_qTexture.release();
        doneCurrent();
    }
}

void WSpinnyGLSL::paintGL() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_textureShader.bind();

    int matrixLocation = m_textureShader.matrixLocation();
    int textureLocation = m_textureShader.textureLocation();
    int positionLocation = m_textureShader.positionLocation();
    int texcoordLocation = m_textureShader.texcoordLocation();

    QMatrix4x4 matrix;
    m_textureShader.setUniformValue(matrixLocation, matrix);

    m_textureShader.enableAttributeArray(positionLocation);
    m_textureShader.enableAttributeArray(texcoordLocation);

    m_textureShader.setUniformValue(textureLocation, 0);

    if (m_bgTexture.isStorageAllocated()) {
        drawTexture(&m_bgTexture);
    }

    if (m_bShowCover && m_loadedCoverTextureScaled.isStorageAllocated()) {
        drawTexture(&m_loadedCoverTextureScaled);
    }

    if (m_maskTexture.isStorageAllocated()) {
        drawTexture(&m_maskTexture);
    }

    // Overlay the signal quality drawing if vinyl is active
    if (shouldDrawVinylQuality()) {
        m_textureShader.release();
        drawVinylQuality();
        m_textureShader.bind();
    }

    // To rotate the foreground image around the center of the image,
    // we use the classic trick of translating the coordinate system such that
    // the origin is at the center of the image. We then rotate the coordinate system,
    // and draw the image at the corner.
    // p.translate(width() / 2, height() / 2);

    bool paintGhost = m_bGhostPlayback && m_ghostTextureScaled.isStorageAllocated();

    if (paintGhost) {
        QMatrix4x4 rotate;
        rotate.rotate(m_fGhostAngle, 0, 0, -1);
        m_textureShader.setUniformValue(matrixLocation, rotate);

        drawTexture(&m_ghostTextureScaled);
    }

    if (m_fgTextureScaled.isStorageAllocated()) {
        QMatrix4x4 rotate;
        rotate.rotate(m_fAngle, 0, 0, -1);
        m_textureShader.setUniformValue(matrixLocation, rotate);

        drawTexture(&m_fgTextureScaled);

        if (m_cueGlowIntensity > 0.01f && !m_fgImageScaled.isNull()) {
            const float rawIntensity = m_cueGlowIntensity /
                    mixxx::cueglow::kMaxAlpha;
            QImage tinted = mixxx::cueglow::createTintedForeground(
                    m_fgImageScaled, m_cueGlowColor, rawIntensity);
            m_cueGlowFgTexture.setData(tinted);
            drawTexture(&m_cueGlowFgTexture);
        }
    }

    if (m_cueGlowIntensity > 0.01f) {
        QImage glowImg = mixxx::cueglow::createGlowCircle(
                m_cueGlowColor, m_cueGlowIntensity);
        m_cueGlowTexture.setData(glowImg);

        QMatrix4x4 identity;
        m_textureShader.setUniformValue(matrixLocation, identity);

        const std::array<float, 8> posarray = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
        const std::array<float, 8> texarray = {0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f};
        m_textureShader.setAttributeArray(
                positionLocation, GL_FLOAT, posarray.data(), 2);
        m_textureShader.setAttributeArray(
                texcoordLocation, GL_FLOAT, texarray.data(), 2);
        m_cueGlowTexture.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_cueGlowTexture.release();
    }

    m_textureShader.release();

    // Serato-style BPM/Time overlay using QPainter on GL surface
    const double bpm = m_pBpm.get();
    if (bpm > 0.0 && width() >= 80) {
        QPainter p(paintDevice());
        p.setRenderHint(QPainter::Antialiasing);

        const int cx = width() / 2;
        const int cy = height() / 2;
        const int overlayRadius = qMin(width(), height()) / 4;

        // Draw the centred labels with a soft dark shadow so they stay readable
        // on top of the (white) needle and the cover art.
        const auto drawSpinnyText = [&p](const QRect& rect, const QString& text) {
            const QColor color = p.pen().color();
            const int off = qMax(1, p.font().pixelSize() / 14);
            p.setPen(QColor(0, 0, 0, 180));
            p.drawText(rect.translated(off, off), Qt::AlignCenter, text);
            p.setPen(color);
            p.drawText(rect, Qt::AlignCenter, text);
        };

        QFont bpmFont;
        bpmFont.setPixelSize(overlayRadius * 3 / 4);
        bpmFont.setWeight(QFont::Bold);
        p.setFont(bpmFont);
        p.setPen(QColor(255, 255, 255));
        QString bpmText = QString::number(bpm, 'f', 1);
        QRect bpmRect(cx - overlayRadius, cy - overlayRadius * 5 / 4,
                overlayRadius * 2, overlayRadius / 2);
        drawSpinnyText(bpmRect, bpmText);

        const double rateRatio = m_pRateRatio.get();
        const double pitchPct = (rateRatio - 1.0) * 100.0;
        QFont labelFont;
        labelFont.setPixelSize(overlayRadius * 2 / 5);
        labelFont.setBold(true);
        p.setFont(labelFont);
        p.setPen(QColor(255, 255, 255));
        QString pitchText = QStringLiteral("%1%2%")
                .arg(pitchPct >= 0 ? "+" : "")
                .arg(pitchPct, 0, 'f', 1);
        QRect pitchRect(cx - overlayRadius * 7 / 4, cy - overlayRadius / 5,
                overlayRadius * 3 / 2, overlayRadius * 2 / 5);
        drawSpinnyText(pitchRect, pitchText);

        const int rangePct = qRound(m_pRateRange.get() * 100.0);
        QString rangeText = QChar(0x00B1) + QString::number(rangePct);
        QRect rangeRect(cx + overlayRadius / 4, cy - overlayRadius / 5,
                overlayRadius * 3 / 2, overlayRadius * 2 / 5);
        drawSpinnyText(rangeRect, rangeText);

        const double trackSamples = m_pTrackSamples.get();
        const double sampleRate = m_pTrackSampleRate.get();
        const double playPos = m_pPlayPos.get();
        if (trackSamples > 0.0 && sampleRate > 0.0) {
            const double totalSeconds = trackSamples / sampleRate / 2.0;
            const double elapsedSeconds = playPos * totalSeconds;
            const int mins = static_cast<int>(elapsedSeconds) / 60;
            const int secs = static_cast<int>(elapsedSeconds) % 60;
            const int tenths = static_cast<int>(elapsedSeconds * 10) % 10;

            QFont timeFont;
            timeFont.setPixelSize(overlayRadius * 7 / 16);
            timeFont.setBold(true);
            timeFont.setWeight(QFont::Bold);
            p.setFont(timeFont);
            p.setPen(QColor(255, 255, 255));
            QString timeText = QStringLiteral("%1:%2.%3")
                    .arg(mins, 2, 10, QChar('0'))
                    .arg(secs, 2, 10, QChar('0'))
                    .arg(tenths);
            QRect timeRect(cx - overlayRadius * 5 / 4, cy + overlayRadius * 3 / 8 + 3,
                    overlayRadius * 5 / 2, overlayRadius / 2);
            drawSpinnyText(timeRect, timeText);

            const double remainingSeconds = totalSeconds - elapsedSeconds;
            const int remMins = static_cast<int>(remainingSeconds) / 60;
            const int remSecs = static_cast<int>(remainingSeconds) % 60;
            const int remTenths = static_cast<int>(remainingSeconds * 10) % 10;
            QString remText = QStringLiteral("%1:%2.%3")
                    .arg(remMins, 2, 10, QChar('0'))
                    .arg(remSecs, 2, 10, QChar('0'))
                    .arg(remTenths);
            QRect remRect(cx - overlayRadius * 5 / 4, cy + overlayRadius * 7 / 8 + 3,
                    overlayRadius * 5 / 2, overlayRadius / 2);
            drawSpinnyText(remRect, remText);
        }
        p.end();
    }
}

void WSpinnyGLSL::initializeGL() {
    initializeOpenGLFunctions();

    updateTextures();

    m_qTexture.setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
    m_qTexture.setSize(m_iVinylScopeSize, m_iVinylScopeSize);
    m_qTexture.setFormat(QOpenGLTexture::R8_UNorm);
    m_qTexture.allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);

    m_textureShader.init();
    m_vinylQualityShader.init();
}

void WSpinnyGLSL::drawTexture(QOpenGLTexture* pTexture) {
    const float texx1 = 0.f;
    const float texy1 = 1.0;
    const float texx2 = 1.f;
    const float texy2 = 0.f;

    const float tw = pTexture->width();
    const float th = pTexture->height();

    // fill centered
    const float posx2 = tw >= th ? 1.f : tw / th;
    const float posy2 = th >= tw ? 1.f : th / tw;
    const float posx1 = -posx2;
    const float posy1 = -posy2;

    const std::array<float, 8> posarray = {posx1, posy1, posx2, posy1, posx1, posy2, posx2, posy2};
    const std::array<float, 8> texarray = {texx1, texy1, texx2, texy1, texx1, texy2, texx2, texy2};

    int positionLocation = m_textureShader.positionLocation();
    int texcoordLocation = m_textureShader.texcoordLocation();

    m_textureShader.setAttributeArray(
            positionLocation, GL_FLOAT, posarray.data(), 2);
    m_textureShader.setAttributeArray(
            texcoordLocation, GL_FLOAT, texarray.data(), 2);

    pTexture->bind();

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    pTexture->release();
}

void WSpinnyGLSL::drawVinylQuality() {
    const float texx1 = 0.f;
    const float texy1 = 1.f;
    const float texx2 = 1.f;
    const float texy2 = 0.f;

    const float posx2 = 1.f;
    const float posy2 = 1.f;
    const float posx1 = -1.f;
    const float posy1 = -1.f;

    const std::array<float, 8> posarray = {posx1, posy1, posx2, posy1, posx1, posy2, posx2, posy2};
    const std::array<float, 8> texarray = {texx1, texy1, texx2, texy1, texx1, texy2, texx2, texy2};

    m_vinylQualityShader.bind();
    int matrixLocation = m_vinylQualityShader.matrixLocation();
    int colorLocation = m_vinylQualityShader.colorLocation();
    int textureLocation = m_vinylQualityShader.textureLocation();
    int positionLocation = m_vinylQualityShader.positionLocation();
    int texcoordLocation = m_vinylQualityShader.texcoordLocation();

    QMatrix4x4 matrix;
    m_vinylQualityShader.setUniformValue(matrixLocation, matrix);
    m_vinylQualityShader.setUniformValue(colorLocation, m_vinylQualityColor);

    m_vinylQualityShader.enableAttributeArray(positionLocation);
    m_vinylQualityShader.enableAttributeArray(texcoordLocation);

    m_vinylQualityShader.setUniformValue(textureLocation, 0);

    m_vinylQualityShader.setAttributeArray(
            positionLocation, GL_FLOAT, posarray.data(), 2);
    m_vinylQualityShader.setAttributeArray(
            texcoordLocation, GL_FLOAT, texarray.data(), 2);

    m_qTexture.bind();

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_qTexture.release();

    m_vinylQualityShader.release();
}
