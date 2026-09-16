#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/FFTProcessor.hpp"
#include "audio/MaskingThresholdStage.hpp"
#include "audio/PsychoacousticModel.hpp"
#include "audio/WindowFunctions.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

/** @brief Create a simple sine wave buffer (mono). */
std::vector<float> makeSine(size_t numSamples, float freq, float sampleRate,
                            float amplitude = 0.5f) {
    std::vector<float> buffer(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(2.0f * static_cast<float>(M_PI) * freq *
                                         static_cast<float>(i) / sampleRate);
    }
    return buffer;
}

/** @brief Replicate the exact per-frame threshold the stage should store. */
std::vector<float> expectedThreshold(const std::vector<float> &cleanSamples, uint16_t numChannels,
                                     uint16_t channel, uint32_t sampleRate, size_t frameSize,
                                     size_t hopSize, size_t frame, float maskingStrength) {
    const std::vector<float> window = window::generate(window::Type::Hann, frameSize);
    std::vector<float> cleanFrame(frameSize);
    for (size_t i = 0; i < frameSize; ++i)
        cleanFrame[i] = cleanSamples[(frame * hopSize + i) * numChannels + channel];

    window::apply(cleanFrame, window);
    auto spectrum = fft::transform(cleanFrame);

    std::vector<float> cleanMag(frameSize);
    for (size_t i = 0; i < frameSize; ++i)
        cleanMag[i] = std::abs(spectrum[i]);

    auto thresholds = PsychoacousticModel::computeThresholds(cleanMag, sampleRate, frameSize);
    if (maskingStrength < 1.0f) {
        for (auto &t : thresholds)
            t *= maskingStrength;
    }
    return thresholds;
}

} // namespace

// ---------------------------------------------------------------------------
// Frame count matches the input
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, FramesPerChannelMatches) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 1024;
    const size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    MaskContext ctx;
    MaskingThresholdStage stage(clean, sampleRate, numChannels, 1.0f, ctx, frameSize);
    stage.process(clean, numChannels);

    const size_t totalFrames = numSamples / numChannels;
    const size_t hopSize = frameSize / 2;
    const size_t expectedFrames = (totalFrames - frameSize) / hopSize + 1;

    EXPECT_EQ(stage.framesPerChannel(), expectedFrames);
    EXPECT_EQ(ctx.framesPerChannel, expectedFrames);
    EXPECT_EQ(ctx.perChannelFrameThresholds.size(), numChannels);
    ASSERT_GT(ctx.perChannelFrameThresholds.size(), 0u);
    EXPECT_EQ(ctx.perChannelFrameThresholds[0].size(), expectedFrames);
}

// ---------------------------------------------------------------------------
// Stored masks equal direct PsychoacousticModel computation
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, MasksMatchDirectComputation) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 2048;
    const size_t numSamples = 16384;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    MaskContext ctx;
    MaskingThresholdStage stage(clean, sampleRate, numChannels, 1.0f, ctx, frameSize);
    stage.process(clean, numChannels);

    ASSERT_EQ(ctx.perChannelFrameThresholds.size(), numChannels);
    for (size_t f = 0; f < ctx.framesPerChannel; ++f) {
        auto expected =
            expectedThreshold(clean, numChannels, 0, sampleRate, frameSize, frameSize / 2, f, 1.0f);
        const auto &actual = ctx.maskForChannelFrame(0, f);
        ASSERT_EQ(actual.size(), expected.size()) << " frame " << f;
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_FLOAT_EQ(actual[i], expected[i]) << " frame " << f << " bin " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// Determinism: same input yields identical masks
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, DeterministicAcrossRuns) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 2048;
    const size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    MaskContext ctxA;
    MaskContext ctxB;
    MaskingThresholdStage stageA(clean, sampleRate, numChannels, 1.0f, ctxA, frameSize);
    MaskingThresholdStage stageB(clean, sampleRate, numChannels, 1.0f, ctxB, frameSize);
    stageA.process(clean, numChannels);
    stageB.process(clean, numChannels);

    EXPECT_EQ(ctxA.perChannelFrameThresholds, ctxB.perChannelFrameThresholds);
}

// ---------------------------------------------------------------------------
// Invariance: masks are computed from clean samples, not the passed buffer
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, MasksIndependentOfProcessedBuffer) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 2048;
    const size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    // A different "samples" buffer is passed to process(); the masks must come
    // from cleanSamples_, so they must be identical to the clean-derived masks.
    std::vector<float> other(numSamples, 1.0f);

    MaskContext ctx;
    MaskingThresholdStage stage(clean, sampleRate, numChannels, 1.0f, ctx, frameSize);
    stage.process(other, numChannels);

    std::vector<float> expectedClean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    ASSERT_EQ(ctx.perChannelFrameThresholds.size(), numChannels);
    for (size_t f = 0; f < ctx.framesPerChannel; ++f) {
        auto expected = expectedThreshold(expectedClean, numChannels, 0, sampleRate, frameSize,
                                          frameSize / 2, f, 1.0f);
        const auto &actual = ctx.maskForChannelFrame(0, f);
        ASSERT_EQ(actual.size(), expected.size()) << " frame " << f;
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_FLOAT_EQ(actual[i], expected[i]) << " frame " << f << " bin " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// Masking strength scaling
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, MaskingStrengthScalesThresholds) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 1024;
    constexpr float kStrength = 0.5f;
    const size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);

    MaskContext ctxFull;
    MaskContext ctxHalf;
    MaskingThresholdStage stageFull(clean, sampleRate, numChannels, 1.0f, ctxFull, frameSize);
    MaskingThresholdStage stageHalf(clean, sampleRate, numChannels, kStrength, ctxHalf, frameSize);
    stageFull.process(clean, numChannels);
    stageHalf.process(clean, numChannels);

    ASSERT_EQ(ctxFull.framesPerChannel, ctxHalf.framesPerChannel);
    for (size_t f = 0; f < ctxFull.framesPerChannel; ++f) {
        const auto &full = ctxFull.maskForChannelFrame(0, f);
        const auto &half = ctxHalf.maskForChannelFrame(0, f);
        ASSERT_EQ(full.size(), half.size()) << " frame " << f;
        for (size_t i = 0; i < full.size(); ++i) {
            EXPECT_FLOAT_EQ(half[i], full[i] * kStrength) << " frame " << f << " bin " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// Stereo: both channels produce per-channel masks
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, StereoProducesPerChannelMasks) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 2;
    constexpr size_t frameSize = 1024;
    const size_t numFrames = 4096;

    std::vector<float> clean(numFrames * 2, 0.0f);
    for (size_t i = 0; i < numFrames; ++i) {
        clean[i * 2] = 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * i / sampleRate);
        clean[i * 2 + 1] =
            0.3f * std::sin(2.0f * static_cast<float>(M_PI) * 660.0f * i / sampleRate);
    }

    MaskContext ctx;
    MaskingThresholdStage stage(clean, sampleRate, numChannels, 1.0f, ctx, frameSize);
    stage.process(clean, numChannels);

    ASSERT_EQ(ctx.perChannelFrameThresholds.size(), numChannels);
    EXPECT_EQ(ctx.numChannels, numChannels);
    EXPECT_GT(ctx.framesPerChannel, 0u);

    // Left (stronger tone) should mask more, so its thresholds should differ
    // from the right channel.
    const auto &left = ctx.maskForChannelFrame(0, 0);
    const auto &right = ctx.maskForChannelFrame(1, 0);
    EXPECT_NE(left, right);
}

// ---------------------------------------------------------------------------
// Empty / short buffers: no crash, no frames, empty context
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, EmptyBufferProducesNothing) {
    std::vector<float> clean;
    MaskContext ctx;
    MaskingThresholdStage stage(clean, 44100, 1, 1.0f, ctx);
    EXPECT_NO_THROW(stage.process(clean, 1));
    EXPECT_EQ(stage.framesPerChannel(), 0u);
    EXPECT_FALSE(ctx.hasMasks());
}

TEST(MaskingThresholdStageTest, ShortBufferProducesNothing) {
    std::vector<float> clean(100, 0.0f);
    MaskContext ctx;
    MaskingThresholdStage stage(clean, 44100, 1, 1.0f, ctx);
    EXPECT_NO_THROW(stage.process(clean, 1));
    EXPECT_EQ(stage.framesPerChannel(), 0u);
    EXPECT_FALSE(ctx.hasMasks());
}

// ---------------------------------------------------------------------------
// Out-of-range accessor returns empty
// ---------------------------------------------------------------------------

TEST(MaskingThresholdStageTest, MaskAccessorOutOfRange) {
    MaskContext ctx;
    EXPECT_TRUE(ctx.maskForChannelFrame(0, 0).empty());
    EXPECT_TRUE(ctx.maskForChannelFrame(5, 100).empty());
}
