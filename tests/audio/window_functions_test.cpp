#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "audio/WindowFunctions.hpp"
#include "core/Errors.hpp"

constexpr double EPSILON = 1e-6;

class WindowFunctionsTest : public ::testing::Test {
  protected:
    static bool approxEqual(double a, double b, double epsilon = EPSILON) {
        return std::abs(a - b) < epsilon;
    }
};

TEST_F(WindowFunctionsTest, HannWindowSize8) {
    std::vector<float> win = window::generate(window::Type::Hann, 8);

    ASSERT_EQ(win.size(), 8);

    EXPECT_TRUE(approxEqual(win[0], 0.0));
    EXPECT_TRUE(approxEqual(win[7], 0.0));

    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(approxEqual(win[i], win[7 - i]));
    }

    EXPECT_GT(win[3], 0.9);
    EXPECT_GT(win[4], 0.9);
}

TEST_F(WindowFunctionsTest, HannWindowSize512) {
    std::vector<float> win = window::generate(window::Type::Hann, 512);

    ASSERT_EQ(win.size(), 512);

    EXPECT_TRUE(approxEqual(win[0], 0.0));
    EXPECT_TRUE(approxEqual(win[511], 0.0));

    EXPECT_TRUE(approxEqual(win[256], 1.0, 1e-4));

    for (size_t i = 0; i < 256; ++i) {
        EXPECT_TRUE(approxEqual(win[i], win[511 - i]));
    }
}

TEST_F(WindowFunctionsTest, HannWindowSize1) {
    std::vector<float> win = window::generate(window::Type::Hann, 1);

    ASSERT_EQ(win.size(), 1);
    EXPECT_EQ(win[0], 1.0);
}

TEST_F(WindowFunctionsTest, HammingWindowSize8) {
    std::vector<float> win = window::generate(window::Type::Hamming, 8);

    ASSERT_EQ(win.size(), 8);

    EXPECT_TRUE(approxEqual(win[0], 0.08, 0.01));
    EXPECT_TRUE(approxEqual(win[7], 0.08, 0.01));

    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(approxEqual(win[i], win[7 - i]));
    }

    EXPECT_GT(win[3], 0.9);
    EXPECT_GT(win[4], 0.9);
}

TEST_F(WindowFunctionsTest, HammingWindowSize512) {
    std::vector<float> win = window::generate(window::Type::Hamming, 512);

    ASSERT_EQ(win.size(), 512);

    EXPECT_TRUE(approxEqual(win[0], 0.08, 0.01));
    EXPECT_TRUE(approxEqual(win[511], 0.08, 0.01));

    EXPECT_TRUE(approxEqual(win[256], 1.0, 1e-4));

    for (size_t i = 0; i < 256; ++i) {
        EXPECT_TRUE(approxEqual(win[i], win[511 - i]));
    }
}

TEST_F(WindowFunctionsTest, HammingWindowSize1) {
    std::vector<float> win = window::generate(window::Type::Hamming, 1);

    ASSERT_EQ(win.size(), 1);
    EXPECT_EQ(win[0], 1.0);
}

TEST_F(WindowFunctionsTest, WindowValuesInRange) {
    std::vector<float> hannWindow = window::generate(window::Type::Hann, 256);
    std::vector<float> hammingWindow = window::generate(window::Type::Hamming, 256);

    for (double val : hannWindow) {
        EXPECT_GE(val, 0.0);
        EXPECT_LE(val, 1.0);
    }

    for (double val : hammingWindow) {
        EXPECT_GE(val, 0.0);
        EXPECT_LE(val, 1.0);
    }
}

TEST_F(WindowFunctionsTest, ApplyWindowToFloats) {
    std::vector<float> samples = {1.0f, 0.8f, 0.6f, 0.4f, 0.4f, 0.6f, 0.8f, 0.5f};
    std::vector<float> win = window::generate(window::Type::Hann, 8);

    std::vector<float> original = samples;
    window::apply(samples, win);

    EXPECT_NEAR(samples[0], 0.0f, 1e-6f);
    EXPECT_NEAR(samples[7], 0.0f, 1e-6f);

    EXPECT_LT(samples[3], original[3]);
    EXPECT_GT(samples[3], 0.0f);
}

TEST_F(WindowFunctionsTest, InvalidWindowSize) {
    EXPECT_THROW({ window::generate(window::Type::Hann, 0); }, dissonance::DspError);
}

TEST_F(WindowFunctionsTest, MismatchedSizes) {
    std::vector<float> samples = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> win = {0.5, 0.5, 0.5};

    EXPECT_THROW({ window::apply(samples, win); }, dissonance::DspError);
}
