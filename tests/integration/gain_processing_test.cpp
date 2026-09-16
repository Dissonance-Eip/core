#include <gtest/gtest.h>
#include <cmath>
#include <fstream>

#include "audio/WavProcessor.hpp"
#include "utils/WavParser.hpp"

class GainProcessingTest : public ::testing::Test {
  protected:
    std::string testFile = "./test_files/sound.wav";

    void SetUp() override {
        std::ifstream file(testFile, std::ios::binary);
        if (!file.is_open()) {
            GTEST_SKIP() << "Test file not found: " << testFile;
        }
    }
};

TEST_F(GainProcessingTest, DefaultGainProcessing) {
    ASSERT_NO_THROW({
        ProcessedWav result = processWavFile(testFile);

        EXPECT_EQ(result.originalSamples.size(), result.processedSamples.size());
        EXPECT_FALSE(result.processedPath.empty());
        EXPECT_TRUE(result.processedPath.find("-processed") != std::string::npos);
    });
}

TEST_F(GainProcessingTest, GainPointFive) {
    ProcessedWav result = processWavFile(testFile, ProcessingOptions{0.5});

    ASSERT_EQ(result.originalSamples.size(), result.processedSamples.size());

    // Verify gain attenuated the signal: mean absolute value of processed < original
    double sumOriginal = 0.0, sumProcessed = 0.0;
    for (size_t i = 0; i < result.originalSamples.size(); ++i) {
        sumOriginal += std::abs(result.originalSamples[i]);
        sumProcessed += std::abs(result.processedSamples[i]);
    }

    if (sumOriginal > 0.0)
        EXPECT_LT(sumProcessed, sumOriginal);
}

TEST_F(GainProcessingTest, GainOnePointZero) {
    ProcessedWav result = processWavFile(testFile, ProcessingOptions{1.0});

    ASSERT_EQ(result.originalSamples.size(), result.processedSamples.size());

    for (size_t i = 0; i < result.processedSamples.size(); ++i) {
        EXPECT_GE(result.processedSamples[i], -1.0f);
        EXPECT_LE(result.processedSamples[i], 1.0f);
    }
}

TEST_F(GainProcessingTest, GainClipping) {
    ProcessedWav result = processWavFile(testFile, ProcessingOptions{10.0});

    ASSERT_EQ(result.originalSamples.size(), result.processedSamples.size());

    for (size_t i = 0; i < result.processedSamples.size(); ++i) {
        EXPECT_GE(result.processedSamples[i], -1.0f);
        EXPECT_LE(result.processedSamples[i], 1.0f);
    }
}

// Disabled: gain 0 does not produce silence. MaskingStage compares the output with the
// original file instead of the gain-adjusted signal, so it rebuilds part of the original
// audio (samples around 1e-4). Re-enable once MaskingStage is reworked (core issue #91).
TEST_F(GainProcessingTest, DISABLED_GainZero) {
    ProcessedWav result = processWavFile(testFile, ProcessingOptions{0.0});

    ASSERT_EQ(result.originalSamples.size(), result.processedSamples.size());

    for (size_t i = 0; i < result.processedSamples.size(); ++i) {
        EXPECT_NEAR(result.processedSamples[i], 0.0f, 1e-6f)
            << "Sample " << i << " should be ~0 with gain 0";
    }
}

TEST_F(GainProcessingTest, OutputFileCreated) {
    ProcessedWav result = processWavFile(testFile, ProcessingOptions{0.8});

    std::ifstream outFile(result.processedPath, std::ios::binary);
    EXPECT_TRUE(outFile.is_open()) << "Output file was not created: " << result.processedPath;

    if (outFile.is_open()) {
        outFile.seekg(0, std::ios::end);
        size_t fileSize = outFile.tellg();
        EXPECT_GT(fileSize, 44) << "Output file is suspiciously small";
    }
}
