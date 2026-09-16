#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "audio/PerturbationStage.hpp"

namespace {

/** Return the RMS of a sample buffer. */
float computeRms(const std::vector<float> &samples) {
    double sum = 0.0;
    for (float s : samples)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

/** Fill a buffer with a constant DC value. */
std::vector<float> makeDc(size_t n, float value = 0.5f) { return std::vector<float>(n, value); }

} // namespace

// ---------------------------------------------------------------------------
// Zero strength — signal must be unchanged
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, ZeroStrengthIsPassthrough) {
    std::vector<float> samples = makeDc(4096);
    std::vector<float> original = samples;

    PerturbationStage stage("white_noise", 0.0f, 44100, 42);
    stage.process(samples, 1);

    EXPECT_EQ(samples, original);
    EXPECT_EQ(stage.rmsDbfs(), PerturbationStage::kSilentDbfs);
}

// ---------------------------------------------------------------------------
// Positive strength — signal is modified
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, PositiveStrengthModifiesSignal) {
    std::vector<float> samples = makeDc(4096);
    std::vector<float> original = samples;

    PerturbationStage stage("white_noise", 1.0f, 44100, 42);
    stage.process(samples, 1);

    EXPECT_NE(samples, original);
}

// ---------------------------------------------------------------------------
// Output stays in [-1, 1]
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, OutputClampedToValidRange) {
    // Start near the rail to stress the clamp
    std::vector<float> samples(4096, 0.995f);

    PerturbationStage stage("white_noise", 1.0f, 44100, 7);
    stage.process(samples, 1);

    for (float s : samples) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// RMS is reported after processing
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, RmsDbfsReportedAfterProcessing) {
    std::vector<float> samples(8192, 0.0f); // silent input — noise is easy to measure

    PerturbationStage stage("white_noise", 1.0f, 44100, 99);
    stage.process(samples, 1);

    // Noise was actually added (RMS > -200 dBFS sentinel)
    EXPECT_GT(stage.rmsDbfs(), PerturbationStage::kSilentDbfs);

    // At strength = 1, kMaxAmplitude = 0.01 (-40 dBFS).
    // The HP filter and distribution mean actual RMS will be well below 0 dBFS.
    EXPECT_LT(stage.rmsDbfs(), 0.0f);
}

// ---------------------------------------------------------------------------
// Different seeds produce different output
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, DifferentSeedsProduceDifferentNoise) {
    std::vector<float> a(4096, 0.0f);
    std::vector<float> b(4096, 0.0f);

    PerturbationStage stageA("white_noise", 0.5f, 44100, 1);
    PerturbationStage stageB("white_noise", 0.5f, 44100, 2);
    stageA.process(a, 1);
    stageB.process(b, 1);

    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// Same seed produces identical output (determinism)
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, SameSeedIsDeterministic) {
    std::vector<float> a(4096, 0.3f);
    std::vector<float> b(4096, 0.3f);

    PerturbationStage stageA("white_noise", 0.5f, 44100, 12345);
    PerturbationStage stageB("white_noise", 0.5f, 44100, 12345);
    stageA.process(a, 1);
    stageB.process(b, 1);

    EXPECT_EQ(a, b);
}

// ---------------------------------------------------------------------------
// Stereo interleaved — both channels receive noise
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, StereoChannelsAreBothPerturbed) {
    // Interleaved: [L0, R0, L1, R1, ...]
    const size_t frames = 2048;
    std::vector<float> samples(frames * 2, 0.0f);

    PerturbationStage stage("white_noise", 1.0f, 44100, 77);
    stage.process(samples, 2);

    // Collect each channel
    std::vector<float> left, right;
    for (size_t i = 0; i < samples.size(); i += 2) {
        left.push_back(samples[i]);
        right.push_back(samples[i + 1]);
    }

    // Both channels should have been modified (non-zero RMS)
    EXPECT_GT(computeRms(left), 0.0f);
    EXPECT_GT(computeRms(right), 0.0f);

    // Channels should differ (independent HP filter state per channel)
    EXPECT_NE(left, right);
}

// ---------------------------------------------------------------------------
// Edge: empty buffer — no crash, sentinel preserved
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, EmptyBufferNoOp) {
    std::vector<float> samples;
    PerturbationStage stage("white_noise", 1.0f, 44100, 0);
    EXPECT_NO_THROW(stage.process(samples, 1));
    EXPECT_EQ(stage.rmsDbfs(), PerturbationStage::kSilentDbfs);
}

// ---------------------------------------------------------------------------
// Strength clamping: values > 1 treated as 1
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, StrengthClampedToOne) {
    std::vector<float> a(4096, 0.0f);
    std::vector<float> b(4096, 0.0f);

    PerturbationStage stageA("white_noise", 1.0f, 44100, 5);
    PerturbationStage stageB("white_noise", 999.0f, 44100, 5); // should clamp to 1
    stageA.process(a, 1);
    stageB.process(b, 1);

    EXPECT_EQ(a, b);
}
