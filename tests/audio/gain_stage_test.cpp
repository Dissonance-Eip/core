#include <gtest/gtest.h>
#include <vector>

#include "audio/GainStage.hpp"

TEST(GainStageTest, ScalesEverySample) {
    std::vector<float> samples = {0.5f, -0.25f, 0.0f};

    GainStage(0.5).process(samples, 1);

    EXPECT_FLOAT_EQ(samples[0], 0.25f);
    EXPECT_FLOAT_EQ(samples[1], -0.125f);
    EXPECT_FLOAT_EQ(samples[2], 0.0f);
}

TEST(GainStageTest, ClampsToFullScale) {
    std::vector<float> samples = {0.5f, -0.5f};

    GainStage(10.0).process(samples, 2);

    EXPECT_FLOAT_EQ(samples[0], 1.0f);
    EXPECT_FLOAT_EQ(samples[1], -1.0f);
}

TEST(GainStageTest, SilencesAtZeroOrNegativeGain) {
    for (double gain : {0.0, -1.0}) {
        std::vector<float> samples = {0.5f, -0.75f, 1.0f};

        GainStage(gain).process(samples, 1);

        EXPECT_EQ(samples, (std::vector<float>{0.0f, 0.0f, 0.0f})) << "gain " << gain;
    }
}
