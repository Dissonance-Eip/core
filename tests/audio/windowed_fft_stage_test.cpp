#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "audio/WindowedFFTStage.hpp"

TEST(WindowedFFTStageTest, IgnoresEmptyInputAndZeroChannels) {
    WindowedFFTStage stage(8, 0.5f);
    std::vector<float> empty;
    std::vector<float> samples = {0.5f, 0.25f, 0.125f, 0.0625f};

    stage.process(empty, 1);
    stage.process(samples, 0);

    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(samples, (std::vector<float>{0.5f, 0.25f, 0.125f, 0.0625f}));
    EXPECT_EQ(stage.framesProcessed(), 0u);
}

TEST(WindowedFFTStageTest, LeavesASingleFrameUntouched) {
    WindowedFFTStage stage(8, 0.5f);
    std::vector<float> stereoFrame = {0.5f, -0.5f};

    stage.process(stereoFrame, 2);

    EXPECT_EQ(stereoFrame, (std::vector<float>{0.5f, -0.5f}));
    EXPECT_EQ(stage.framesProcessed(), 0u);
}

TEST(WindowedFFTStageTest, ReportsProgressFromZeroToOne) {
    std::vector<float> progress;
    WindowedFFTStage stage(8, 0.5f, [&](float p) { progress.push_back(p); });
    std::vector<float> samples(320, 0.1f);

    stage.process(samples, 1);

    ASSERT_GE(progress.size(), 2u);
    EXPECT_FLOAT_EQ(progress.front(), 0.0f);
    EXPECT_FLOAT_EQ(progress.back(), 1.0f);
    EXPECT_TRUE(std::is_sorted(progress.begin(), progress.end()));
    EXPECT_EQ(stage.bins(), 8u);
    EXPECT_EQ(stage.cutoffBin(), 4u);
}
