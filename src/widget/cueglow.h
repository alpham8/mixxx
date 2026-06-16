#pragma once

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRadialGradient>

#include <cmath>

namespace mixxx {
namespace cueglow {

constexpr float kMaxAlpha = 0.25f;
constexpr int kGlowCircleSize = 64;

struct CueGlowResult {
    float intensity; // [0.0, 1.0]
    bool useNext;    // true: apply the next cue's colour, false: the previous one
};

// Crossfades the glow between the previous and next cue around the playhead.
// All positions are normalized to [0, 1] with prevPos <= playPos <= nextPos.
// The glow is full (1.0) at each cue and fades to neutral (0.0) exactly at the
// midpoint between them: the previous cue drains 1.0 -> 0.0 over the first half,
// then the next cue fills 0.0 -> 1.0 over the second half. useNext tells the
// caller which cue's colour to apply. Multiply intensity by kMaxAlpha for the
// final overlay alpha.
inline CueGlowResult calcCrossfadeIntensity(double prevPos, double playPos, double nextPos) {
    const double midpoint = (prevPos + nextPos) / 2.0;
    if (playPos <= midpoint) {
        const double span = midpoint - prevPos;
        const float intensity = span > 0.0
                ? static_cast<float>((midpoint - playPos) / span)
                : 0.0f;
        return {intensity, false};
    }
    const double span = nextPos - midpoint;
    const float intensity = span > 0.0
            ? static_cast<float>((playPos - midpoint) / span)
            : 0.0f;
    return {intensity, true};
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
