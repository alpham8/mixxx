#include <gtest/gtest.h>

#include "widget/cueglow.h"

using namespace mixxx::cueglow;

TEST(CueGlowTest, ZeroIntensityBeforeCue) {
    EXPECT_FLOAT_EQ(calcIntensity(-0.01), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(-1.0), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(-100.0), 0.0f);
}

TEST(CueGlowTest, ZeroIntensityWellAfterDrain) {
    EXPECT_FLOAT_EQ(calcIntensity(kDrainBeats + 0.01), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(32.0), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(100.0), 0.0f);
}

TEST(CueGlowTest, FullIntensityAtCuePoint) {
    EXPECT_FLOAT_EQ(calcIntensity(0.0), 1.0f);
}

TEST(CueGlowTest, DrainMidpoint) {
    EXPECT_FLOAT_EQ(calcIntensity(kDrainBeats / 2.0), 0.5f);
}

TEST(CueGlowTest, DrainQuarter) {
    EXPECT_FLOAT_EQ(calcIntensity(kDrainBeats * 0.25), 0.75f);
}

TEST(CueGlowTest, DrainThreeQuarters) {
    EXPECT_FLOAT_EQ(calcIntensity(kDrainBeats * 0.75), 0.25f);
}

TEST(CueGlowTest, DrainEnd) {
    EXPECT_FLOAT_EQ(calcIntensity(kDrainBeats), 0.0f);
}

TEST(CueGlowTest, IntensityAlwaysInRange) {
    for (double b = -8.0; b <= 24.0; b += 0.01) {
        float intensity = calcIntensity(b);
        EXPECT_GE(intensity, 0.0f) << "at beats=" << b;
        EXPECT_LE(intensity, 1.0f) << "at beats=" << b;
    }
}

TEST(CueGlowTest, DrainIsMonotonicallyDecreasing) {
    float prev = 1.0f;
    for (double b = 0.0; b <= kDrainBeats; b += 0.01) {
        float intensity = calcIntensity(b);
        EXPECT_LE(intensity, prev + 1e-6f) << "at beats=" << b;
        prev = intensity;
    }
}

TEST(CueGlowTest, MaxAlphaScaling) {
    EXPECT_FLOAT_EQ(calcIntensity(0.0) * kMaxAlpha, 0.25f);
    EXPECT_FLOAT_EQ(calcIntensity(kDrainBeats / 2.0) * kMaxAlpha, 0.125f);
}

TEST(CueGlowTest, SingleBeatSteps) {
    for (int i = 0; i <= static_cast<int>(kDrainBeats); ++i) {
        float expected = static_cast<float>(1.0 - static_cast<double>(i) / kDrainBeats);
        EXPECT_FLOAT_EQ(calcIntensity(static_cast<double>(i)), expected)
                << "at beat " << i;
    }
}
