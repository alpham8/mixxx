#pragma once

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRadialGradient>

#include <cmath>

namespace mixxx {
namespace cueglow {

constexpr double kDrainBeats = 16.0;
constexpr float kMaxAlpha = 0.25f;
constexpr int kGlowCircleSize = 64;

// Returns raw intensity [0.0, 1.0] based on beat distance past a cue point.
// distBeats < 0 means the playhead has not yet reached the cue (no glow).
// distBeats >= 0 means the playhead has passed the cue — intensity drains
// linearly from 1.0 down to 0.0 over kDrainBeats beats.
// Multiply the result by kMaxAlpha to get the final overlay alpha.
inline float calcIntensity(double distBeats) {
    if (distBeats < 0.0) {
        return 0.0f;
    }
    if (distBeats >= kDrainBeats) {
        return 0.0f;
    }
    return static_cast<float>(1.0 - distBeats / kDrainBeats);
}

inline QImage createTintedForeground(
        const QImage& fgImage,
        const QColor& glowColor,
        float rawIntensity) {
    QImage tinted = fgImage.copy();
    QPainter tp(&tinted);
    tp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    tp.fillRect(tinted.rect(), glowColor);

    const double cx = tinted.width() / 2.0;
    const double cy = tinted.height() / 2.0;
    const double radius = std::sqrt(cx * cx + cy * cy);
    QRadialGradient mask(cx, cy, radius);
    const double outerStop = std::min(1.0, static_cast<double>(rawIntensity));
    mask.setColorAt(0.0, Qt::white);
    if (outerStop > 0.0) {
        mask.setColorAt(std::max(0.0, outerStop - 0.001), Qt::white);
    }
    mask.setColorAt(outerStop, Qt::transparent);
    mask.setColorAt(1.0, Qt::transparent);

    tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    tp.fillRect(tinted.rect(), mask);
    tp.end();
    return tinted;
}

inline QImage createGlowCircle(const QColor& color, float alpha) {
    QImage img(kGlowCircleSize, kGlowCircleSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QColor c = color;
    c.setAlphaF(alpha);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(img.rect());
    p.end();
    return img;
}

} // namespace cueglow
} // namespace mixxx
