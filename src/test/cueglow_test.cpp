#include <gtest/gtest.h>

#include "widget/cueglow.h"

using namespace mixxx::cueglow;

TEST(CueGlowTest, FullAtPreviousCue) {
    // At the previous cue the glow is full and uses the previous colour.
    CueGlowResult r = calcCrossfadeIntensity(0.2, 0.2, 0.6);
    EXPECT_FLOAT_EQ(r.intensity, 1.0f);
    EXPECT_FALSE(r.useNext);
}

TEST(CueGlowTest, FullAtNextCue) {
    // At the next cue the glow is full and uses the next colour.
    CueGlowResult r = calcCrossfadeIntensity(0.2, 0.6, 0.6);
    EXPECT_FLOAT_EQ(r.intensity, 1.0f);
    EXPECT_TRUE(r.useNext);
}

TEST(CueGlowTest, NeutralAtMidpoint) {
    // Exactly at the midpoint the glow is neutral.
    CueGlowResult r = calcCrossfadeIntensity(0.2, 0.4, 0.6);
    EXPECT_FLOAT_EQ(r.intensity, 0.0f);
    EXPECT_FALSE(r.useNext);
}

TEST(CueGlowTest, DrainsPreviousInFirstHalf) {
    // midpoint = 0.2, span = 0.2, intensity = (0.2 - 0.1) / 0.2 = 0.5
    CueGlowResult r = calcCrossfadeIntensity(0.0, 0.1, 0.4);
    EXPECT_FLOAT_EQ(r.intensity, 0.5f);
    EXPECT_FALSE(r.useNext);
}

TEST(CueGlowTest, FillsNextInSecondHalf) {
    // midpoint = 0.2, span = 0.2, intensity = (0.3 - 0.2) / 0.2 = 0.5
    CueGlowResult r = calcCrossfadeIntensity(0.0, 0.3, 0.4);
    EXPECT_FLOAT_EQ(r.intensity, 0.5f);
    EXPECT_TRUE(r.useNext);
}

TEST(CueGlowTest, UsesNextColourPastMidpoint) {
    CueGlowResult r = calcCrossfadeIntensity(0.0, 0.21, 0.4);
    EXPECT_TRUE(r.useNext);
}

TEST(CueGlowTest, SymmetricAroundMidpoint) {
    // Equal distance either side of the midpoint -> equal intensity.
    const double prevPos = 0.0;
    const double nextPos = 0.8; // midpoint = 0.4
    CueGlowResult before = calcCrossfadeIntensity(prevPos, 0.3, nextPos);
    CueGlowResult after = calcCrossfadeIntensity(prevPos, 0.5, nextPos);
    EXPECT_FLOAT_EQ(before.intensity, after.intensity);
    EXPECT_FALSE(before.useNext);
    EXPECT_TRUE(after.useNext);
}

TEST(CueGlowTest, IntensityAlwaysInRange) {
    const double prevPos = 0.25;
    const double nextPos = 0.75;
    for (double p = prevPos; p <= nextPos; p += 0.001) {
        CueGlowResult r = calcCrossfadeIntensity(prevPos, p, nextPos);
        EXPECT_GE(r.intensity, 0.0f) << "at pos=" << p;
        EXPECT_LE(r.intensity, 1.0f) << "at pos=" << p;
    }
}

TEST(CueGlowTest, DegenerateZeroSpan) {
    // Coincident cues must not divide by zero.
    CueGlowResult r = calcCrossfadeIntensity(0.5, 0.5, 0.5);
    EXPECT_FLOAT_EQ(r.intensity, 0.0f);
}

TEST(CueGlowTest, MaxAlphaScaling) {
    EXPECT_FLOAT_EQ(calcCrossfadeIntensity(0.2, 0.2, 0.6).intensity * kMaxAlpha, 0.25f);
    EXPECT_FLOAT_EQ(calcCrossfadeIntensity(0.0, 0.1, 0.4).intensity * kMaxAlpha, 0.125f);
}
