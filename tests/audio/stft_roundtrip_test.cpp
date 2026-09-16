#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "audio/WindowedFFTStage.hpp"

TEST(STFTRoundtripTest, SilenceRemainsNearZero) {
    constexpr size_t numSamples = 88200;
    std::vector<float> silence(numSamples, 0.0f);

    // cutoffFraction = 1.0 means no bins are zeroed — full pass-through
    WindowedFFTStage stage(2048, 1.0f);
    stage.process(silence, 1);

    for (size_t i = 0; i < silence.size(); ++i) {
        EXPECT_NEAR(silence[i], 0.0f, 1e-4f) << "Sample " << i << " drifted from silence";
    }
}

TEST(STFTRoundtripTest, StereoSilenceRemainsNearZero) {
    // 2-channel interleaved: 44100 frames × 2 = 88200 samples
    constexpr size_t numSamples = 88200;
    std::vector<float> silence(numSamples, 0.0f);

    WindowedFFTStage stage(2048, 1.0f);
    stage.process(silence, 2);

    for (size_t i = 0; i < silence.size(); ++i) {
        EXPECT_NEAR(silence[i], 0.0f, 1e-4f) << "Sample " << i << " drifted from stereo silence";
    }
}

TEST(STFTRoundtripTest, FramesProcessedNonZero) {
    constexpr size_t numSamples = 88200;
    std::vector<float> samples(numSamples, 0.5f);

    WindowedFFTStage stage(2048, 1.0f);
    stage.process(samples, 1);

    EXPECT_GT(stage.framesProcessed(), 0u);
}
