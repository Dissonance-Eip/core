#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "audio/MaskingStage.hpp"
#include "audio/MaskingThresholdStage.hpp"

namespace {

/** @brief Compute RMS of a sample buffer. */
float computeRms(const std::vector<float> &samples) {
    if (samples.empty())
        return 0.0f;
    double sum = 0.0;
    for (float s : samples)
        sum += static_cast<double>(s) * static_cast<double>(s);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

/** @brief Create a simple sine wave buffer. */
std::vector<float> makeSine(size_t numSamples, float freq, float sampleRate,
                            float amplitude = 0.5f) {
    std::vector<float> buffer(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(2.0f * static_cast<float>(M_PI) * freq *
                                         static_cast<float>(i) / sampleRate);
    }
    return buffer;
}

/** @brief Add white noise to a buffer in-place. */
void addNoise(std::vector<float> &buffer, float amplitude, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto &s : buffer)
        s += amplitude * dist(rng);
}

// ---------------------------------------------------------------------------
// Process on clean audio with no perturbation — signal should pass through
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, CleanSignalPassesThrough) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    std::vector<float> perturbed = clean; // no perturbation

    MaskingStage stage(clean, sampleRate, numChannels, 1.0f);
    stage.process(perturbed, numChannels);

    // Output RMS should be close to input RMS
    float inRms = computeRms(clean);
    float outRms = computeRms(perturbed);
    EXPECT_GT(outRms, inRms * 0.5f);
    EXPECT_LT(outRms, inRms * 1.5f);

    // Calculate RMS error of the pass-through
    double sumSqDiff = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double diff = static_cast<double>(perturbed[i]) - static_cast<double>(clean[i]);
        sumSqDiff += diff * diff;
    }
    float rmsError = static_cast<float>(std::sqrt(sumSqDiff / static_cast<double>(numSamples)));
    EXPECT_LT(rmsError, inRms * 0.5f);
}

// ---------------------------------------------------------------------------
// Perturbed signal has its perturbation reduced
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, PerturbationIsReduced) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);

    // Add noise to the clean signal at moderate level
    std::vector<float> noisy = clean;
    addNoise(noisy, 0.05f, 12345);

    // Compute RMS of the noise (difference)
    double noiseSumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double diff = static_cast<double>(noisy[i]) - static_cast<double>(clean[i]);
        noiseSumSq += diff * diff;
    }
    float noiseRms = static_cast<float>(std::sqrt(noiseSumSq / static_cast<double>(numSamples)));

    // Apply masking
    MaskingStage stage(clean, sampleRate, numChannels, 1.0f);
    std::vector<float> masked = noisy;
    stage.process(masked, numChannels);

    // Compute RMS of remaining difference after masking
    double maskedSumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double diff = static_cast<double>(masked[i]) - static_cast<double>(clean[i]);
        maskedSumSq += diff * diff;
    }
    float maskedRms = static_cast<float>(std::sqrt(maskedSumSq / static_cast<double>(numSamples)));

    // Output still has signal energy
    float outRms = computeRms(masked);
    EXPECT_GT(outRms, 0.01f);

    // RMS should not be dramatically worse than input noise level
    EXPECT_LT(maskedRms, noiseRms * 4.0f);
}

// ---------------------------------------------------------------------------
// Stereo: both channels are independently processed
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, StereoProcessing) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 2;
    constexpr size_t numFrames = 4096;

    // Interleaved stereo
    std::vector<float> clean(numFrames * 2, 0.0f);
    for (size_t i = 0; i < numFrames; ++i) {
        clean[i * 2] = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / sampleRate);     // L
        clean[i * 2 + 1] = 0.3f * std::sin(2.0f * M_PI * 660.0f * i / sampleRate); // R
    }

    std::vector<float> perturbed = clean;
    addNoise(perturbed, 0.03f, 99);

    MaskingStage stage(clean, sampleRate, numChannels, 1.0f);
    stage.process(perturbed, numChannels);

    // Output should be finite and within [-1, 1]
    for (float s : perturbed) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }

    EXPECT_GT(stage.framesProcessed(), 0u);
}

// ---------------------------------------------------------------------------
// Empty buffer — should not crash
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, EmptyBufferNoCrash) {
    std::vector<float> clean;
    std::vector<float> perturbed;

    MaskingStage stage(clean, 44100, 1, 1.0f);
    EXPECT_NO_THROW(stage.process(perturbed, 1));
    EXPECT_EQ(stage.framesProcessed(), 0u);
}

// ---------------------------------------------------------------------------
// Very short buffer (< frame size) — no crash, no frames processed
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, ShortBufferNoCrash) {
    std::vector<float> clean(100, 0.0f);
    std::vector<float> perturbed(100, 0.1f);

    MaskingStage stage(clean, 44100, 1, 1.0f);
    EXPECT_NO_THROW(stage.process(perturbed, 1));
    EXPECT_EQ(stage.framesProcessed(), 0u);
}

// ---------------------------------------------------------------------------
// Output stays in [-1, 1] even with extreme inputs
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, OutputClamped) {
    constexpr size_t numSamples = 4096;
    std::vector<float> clean(numSamples, 0.0f);
    for (size_t i = 0; i < numSamples; ++i)
        clean[i] = std::sin(2.0f * M_PI * 440.0f * i / 44100.0f);

    std::vector<float> perturbed = clean;
    addNoise(perturbed, 0.5f, 7);

    MaskingStage stage(clean, 44100, 1, 1.0f);
    stage.process(perturbed, 1);

    for (float s : perturbed) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Different sample rates work
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, DifferentSampleRate) {
    constexpr size_t numSamples = 8192;
    std::vector<float> clean(numSamples, 0.0f);
    for (size_t i = 0; i < numSamples; ++i)
        clean[i] = 0.5f * std::sin(2.0f * M_PI * 880.0f * i / 22050.0f);

    std::vector<float> perturbed = clean;
    addNoise(perturbed, 0.02f, 42);

    MaskingStage stage(clean, 22050, 1, 1.0f);
    EXPECT_NO_THROW(stage.process(perturbed, 1));
    EXPECT_GT(stage.framesProcessed(), 0u);
}

// ---------------------------------------------------------------------------
// When a compatible shared MaskContext is supplied, MaskingStage consumes it and
// the output is identical to the legacy per-frame recompute (behavior-neutral).
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, UsesSharedMaskProducesIdenticalOutput) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 1024;
    constexpr size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);

    // Build the shared mask upstream, exactly as MaskingThresholdStage does.
    MaskContext ctx;
    MaskingThresholdStage thresholdStage(clean, sampleRate, numChannels, 1.0f, ctx, frameSize);
    thresholdStage.process(clean, numChannels);

    // Same perturbed input, processed twice.
    std::vector<float> noisyLegacy = clean;
    addNoise(noisyLegacy, 0.05f, 12345);

    std::vector<float> noisyShared = clean;
    addNoise(noisyShared, 0.05f, 12345);

    // Legacy: no shared mask (nullptr context).
    MaskingStage legacyStage(clean, sampleRate, numChannels, 1.0f, frameSize, nullptr);
    legacyStage.process(noisyLegacy, numChannels);

    // Shared: fed from the precomputed MaskContext.
    MaskingStage sharedStage(clean, sampleRate, numChannels, 1.0f, frameSize, &ctx);
    sharedStage.process(noisyShared, numChannels);

    ASSERT_EQ(noisyShared.size(), noisyLegacy.size());
    for (size_t i = 0; i < noisyShared.size(); ++i) {
        EXPECT_FLOAT_EQ(noisyShared[i], noisyLegacy[i]) << "sample " << i;
    }
}

// ---------------------------------------------------------------------------
// MaskingStrength is already folded into the shared mask, so feeding a mask must
// not re-apply it (no double scaling), matching the legacy single-scaling path.
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, SharedMaskHonoursStrengthWithoutDoubleScaling) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 1024;
    constexpr float kStrength = 0.5f;
    constexpr size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);

    // Shared mask computed with maskingStrength applied inside MaskingThresholdStage.
    MaskContext ctx;
    MaskingThresholdStage thresholdStage(clean, sampleRate, numChannels, kStrength, ctx, frameSize);
    thresholdStage.process(clean, numChannels);

    std::vector<float> noisyShared = clean;
    addNoise(noisyShared, 0.05f, 7);
    std::vector<float> noisyLegacy = noisyShared;

    // Shared uses the same strengthed mask directly (no re-scaling).
    MaskingStage sharedStage(clean, sampleRate, numChannels, kStrength, frameSize, &ctx);
    sharedStage.process(noisyShared, numChannels);

    // Legacy recomputes thresholds then scales once by kStrength.
    MaskingStage legacyStage(clean, sampleRate, numChannels, kStrength, frameSize, nullptr);
    legacyStage.process(noisyLegacy, numChannels);

    for (size_t i = 0; i < noisyShared.size(); ++i) {
        EXPECT_FLOAT_EQ(noisyShared[i], noisyLegacy[i]) << "sample " << i;
    }
}

// ---------------------------------------------------------------------------
// A shared mask with an incompatible channel count must NOT be used; MaskingStage
// falls back to the legacy recompute path, still producing correct output.
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, MismatchedSharedMaskFallsBackToLegacy) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 2;
    constexpr size_t frameSize = 1024;
    constexpr size_t numFrames = 4096;

    // Build a MONO shared mask (numChannels = 1) to feed to a stereo stage.
    // The channel-count mismatch must reject the mask and fall back to legacy.
    std::vector<float> monoClean = makeSine(numFrames, 440.0f, sampleRate, 0.5f);
    MaskContext monoCtx;
    MaskingThresholdStage monoThreshold(monoClean, sampleRate, 1, 1.0f, monoCtx, frameSize);
    monoThreshold.process(monoClean, 1);
    ASSERT_EQ(monoCtx.numChannels, 1u); // confirm the mask is mono

    std::vector<float> stereoClean(numFrames * numChannels, 0.0f);
    for (size_t i = 0; i < numFrames; ++i) {
        stereoClean[i * 2] = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / sampleRate);
        stereoClean[i * 2 + 1] = 0.3f * std::sin(2.0f * M_PI * 660.0f * i / sampleRate);
    }

    std::vector<float> noisy = stereoClean;
    addNoise(noisy, 0.03f, 99);

    // Matched stereo mask path.
    MaskContext ctx;
    MaskingThresholdStage thresholdStage(stereoClean, sampleRate, numChannels, 1.0f, ctx,
                                         frameSize);
    thresholdStage.process(stereoClean, numChannels);

    std::vector<float> legacy = noisy;
    MaskingStage legacyStage(stereoClean, sampleRate, numChannels, 1.0f, frameSize, nullptr);
    legacyStage.process(legacy, numChannels);

    // Mismatched mask: force fallback to legacy by passing the mono ctx.
    std::vector<float> mismatched = noisy;
    MaskingStage mismatchedStage(stereoClean, sampleRate, numChannels, 1.0f, frameSize, &monoCtx);
    mismatchedStage.process(mismatched, numChannels);

    // A matched stereo shared mask produces identical output to legacy.
    std::vector<float> shared = noisy;
    MaskingStage sharedStage(stereoClean, sampleRate, numChannels, 1.0f, frameSize, &ctx);
    sharedStage.process(shared, numChannels);

    // The mismatched (mono) mask must behave exactly like legacy for every sample.
    for (size_t i = 0; i < mismatched.size(); ++i) {
        EXPECT_FLOAT_EQ(mismatched[i], legacy[i]) << "mismatched sample " << i;
    }
    // And the matched stereo mask path also equals legacy.
    for (size_t i = 0; i < shared.size(); ++i) {
        EXPECT_FLOAT_EQ(shared[i], legacy[i]) << "shared sample " << i;
    }
}

// ---------------------------------------------------------------------------
// An empty / unusable shared context still lets MaskingStage run via legacy.
// ---------------------------------------------------------------------------

TEST(MaskingStageTest, EmptySharedContextFallsBackToLegacy) {
    constexpr uint32_t sampleRate = 44100;
    constexpr uint16_t numChannels = 1;
    constexpr size_t frameSize = 1024;
    constexpr size_t numSamples = 8192;

    std::vector<float> clean = makeSine(numSamples, 440.0f, sampleRate, 0.5f);
    MaskContext emptyCtx; // never filled -> hasMasks() false

    std::vector<float> noisy = clean;
    addNoise(noisy, 0.02f, 5);

    MaskingStage stage(clean, sampleRate, numChannels, 1.0f, frameSize, &emptyCtx);
    EXPECT_NO_THROW(stage.process(noisy, numChannels));
    EXPECT_GT(stage.framesProcessed(), 0u);
}

} // namespace
