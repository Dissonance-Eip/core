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
// Mask-shaping tests (issue #88)
// ---------------------------------------------------------------------------

namespace {

/** Build a MaskContext with flat per-bin thresholds for all channels/frames. */
MaskContext makeFlatMask(uint16_t numChannels, size_t framesPerChannel, size_t frameSize,
                         float threshold) {
    MaskContext ctx;
    ctx.frameSize = frameSize;
    ctx.hopSize = frameSize / 2;
    ctx.numChannels = numChannels;
    ctx.framesPerChannel = framesPerChannel;
    ctx.perChannelFrameThresholds.resize(numChannels);
    for (uint16_t ch = 0; ch < numChannels; ++ch) {
        ctx.perChannelFrameThresholds[ch].resize(framesPerChannel);
        for (size_t f = 0; f < framesPerChannel; ++f) {
            ctx.perChannelFrameThresholds[ch][f] = std::vector<float>(frameSize, threshold);
        }
    }
    return ctx;
}

} // namespace

// ---------------------------------------------------------------------------
// Null context fallback: noise is added, output in range
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, NullContextFallbackAddsNoise) {
    std::vector<float> samples(8192, 0.0f);
    std::vector<float> original = samples;

    PerturbationStage stage("white_noise", 1.0f, 44100, 42, nullptr);
    stage.process(samples, 1);

    EXPECT_NE(samples, original);
    EXPECT_GT(stage.rmsDbfs(), PerturbationStage::kSilentDbfs);
    for (float s : samples) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Mask-shaped noise: total noise power is bounded by the mask
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, MaskShapedNoiseStaysUnderThreshold) {
    // Buffer length must produce a whole number of mask frames. Any samples
    // beyond the last mask frame pass full-strength white noise.
    constexpr size_t kFrameSize = 2048;
    constexpr size_t kHopSize = kFrameSize / 2;
    constexpr size_t kFrameCount = 7; // (total - frameSize) / hop + 1
    constexpr size_t kTotalSamples = kFrameSize + (kFrameCount - 1) * kHopSize;
    constexpr float kThreshold = 0.005f;

    auto noiseRmsOf = [](const std::vector<float> &out, const std::vector<float> &in) {
        double sumSq = 0.0;
        for (size_t i = 0; i < out.size(); ++i) {
            double diff = static_cast<double>(out[i]) - static_cast<double>(in[i]);
            sumSq += diff * diff;
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(out.size())));
    };

    // Threshold-shaped white noise (mask context present).
    MaskContext mask = makeFlatMask(1, kFrameCount, kFrameSize, kThreshold);
    std::vector<float> shaped(kTotalSamples, 0.0f);
    std::vector<float> silent(kTotalSamples, 0.0f);
    PerturbationStage shapedStage("white_noise", 1.0f, 44100, 42, &mask);
    shapedStage.process(shaped, 1);

    // Flat white noise + HP (no mask) at the same seed and strength.
    std::vector<float> flat(kTotalSamples, 0.0f);
    PerturbationStage flatStage("white_noise", 1.0f, 44100, 42, nullptr);
    flatStage.process(flat, 1);

    const float shapedRms = noiseRmsOf(shaped, silent);
    const float flatRms = noiseRmsOf(flat, silent);

    // Noise was added in both cases.
    EXPECT_GT(shapedRms, 0.0f);
    EXPECT_GT(flatRms, 0.0f);

    // Gating every bin at threshold must cut the injected energy well below
    // the flat fallback: the shape of the noise is what makes it quieter, not
    // the overall amplitude setting.
    EXPECT_LT(shapedRms, flatRms / 10.0f);
}

// ---------------------------------------------------------------------------
// Very low mask thresholds yield near-silent output
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, VeryLowMaskYieldsNearSilentOutput) {
    // Coverage must span the whole buffer, not just the first frames.
    constexpr size_t kFrameSize = 2048;
    constexpr size_t kHopSize = kFrameSize / 2;
    constexpr size_t kFrameCount = 7; // (total - frameSize) / hop + 1
    constexpr size_t kTotalSamples = kFrameSize + (kFrameCount - 1) * kHopSize;

    MaskContext mask = makeFlatMask(1, kFrameCount, kFrameSize, 1e-6f);

    std::vector<float> samples(kTotalSamples, 0.0f);

    PerturbationStage stage("white_noise", 1.0f, 44100, 42, &mask);
    stage.process(samples, 1);

    // With every bin clamped to ~1e-6, injected energy is essentially zero.
    // The flat fallback alone would sit near -40 dBFS (rms ~0.005), so a
    // 1e-4 bound only passes if the mask actually suppressed the noise.
    float rms = computeRms(samples);
    EXPECT_LT(rms, 1e-4f);
}

// ---------------------------------------------------------------------------
// Empty MaskContext (hasMasks() == false) falls back to flat noise
// ---------------------------------------------------------------------------

TEST(PerturbationStageTest, EmptyMaskContextFallsBackToFlat) {
    MaskContext emptyMask;
    // framesPerChannel defaults to 0, so hasMasks() == false

    std::vector<float> samples(8192, 0.0f);
    std::vector<float> original = samples;

    PerturbationStage stage("white_noise", 1.0f, 44100, 42, &emptyMask);
    stage.process(samples, 1);

    EXPECT_NE(samples, original);
    EXPECT_GT(stage.rmsDbfs(), PerturbationStage::kSilentDbfs);
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
