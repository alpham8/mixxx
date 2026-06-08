#include <gtest/gtest.h>

#include "widget/cueglow.h"

using namespace mixxx::cueglow;

TEST(CueGlowTest, ZeroIntensityWellBeforeCue) {
    EXPECT_FLOAT_EQ(calcIntensity(-5.0), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(-10.0), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(-100.0), 0.0f);
}

TEST(CueGlowTest, ZeroIntensityWellAfterCue) {
    EXPECT_FLOAT_EQ(calcIntensity(kHoldSeconds + kFadeOutSeconds + 0.01), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(10.0), 0.0f);
    EXPECT_FLOAT_EQ(calcIntensity(100.0), 0.0f);
}

TEST(CueGlowTest, FadeInBoundaryStart) {
    EXPECT_FLOAT_EQ(calcIntensity(-kFadeInSeconds), 0.0f);
}

TEST(CueGlowTest, FadeInMidpoint) {
    EXPECT_FLOAT_EQ(calcIntensity(-kFadeInSeconds / 2.0), 0.5f);
}

TEST(CueGlowTest, FadeInQuarter) {
    EXPECT_FLOAT_EQ(calcIntensity(-kFadeInSeconds * 0.75), 0.25f);
}

TEST(CueGlowTest, FullIntensityAtCuePoint) {
    EXPECT_FLOAT_EQ(calcIntensity(0.0), 1.0f);
}

TEST(CueGlowTest, FullIntensityDuringHold) {
    EXPECT_FLOAT_EQ(calcIntensity(0.0), 1.0f);
    EXPECT_FLOAT_EQ(calcIntensity(kHoldSeconds * 0.5), 1.0f);
    EXPECT_FLOAT_EQ(calcIntensity(kHoldSeconds * 0.99), 1.0f);
}

TEST(CueGlowTest, FadeOutStart) {
    float val = calcIntensity(kHoldSeconds);
    EXPECT_FLOAT_EQ(val, 1.0f);
}

TEST(CueGlowTest, FadeOutMidpoint) {
    float val = calcIntensity(kHoldSeconds + kFadeOutSeconds / 2.0);
    EXPECT_FLOAT_EQ(val, 0.5f);
}

TEST(CueGlowTest, FadeOutEnd) {
    float val = calcIntensity(kHoldSeconds + kFadeOutSeconds);
    EXPECT_FLOAT_EQ(val, 0.0f);
}

TEST(CueGlowTest, FadeOutQuarter) {
    float val = calcIntensity(kHoldSeconds + kFadeOutSeconds * 0.75);
    EXPECT_FLOAT_EQ(val, 0.25f);
}

TEST(CueGlowTest, IntensityAlwaysInRange) {
    for (double t = -5.0; t <= 10.0; t += 0.01) {
        float intensity = calcIntensity(t);
        EXPECT_GE(intensity, 0.0f) << "at t=" << t;
        EXPECT_LE(intensity, 1.0f) << "at t=" << t;
    }
}

TEST(CueGlowTest, FadeInIsMonotonicallyIncreasing) {
    float prev = 0.0f;
    for (double t = -kFadeInSeconds; t <= 0.0; t += 0.01) {
        float intensity = calcIntensity(t);
        EXPECT_GE(intensity, prev) << "at t=" << t;
        prev = intensity;
    }
}

TEST(CueGlowTest, FadeOutIsMonotonicallyDecreasing) {
    float prev = 1.0f;
    for (double t = kHoldSeconds; t <= kHoldSeconds + kFadeOutSeconds; t += 0.01) {
        float intensity = calcIntensity(t);
        EXPECT_LE(intensity, prev + 1e-6f) << "at t=" << t;
        prev = intensity;
    }
}

TEST(CueGlowTest, MaxAlphaScaling) {
    EXPECT_FLOAT_EQ(calcIntensity(0.0) * kMaxAlpha, 0.75f);
    EXPECT_FLOAT_EQ(calcIntensity(-kFadeInSeconds / 2.0) * kMaxAlpha, 0.375f);
}
