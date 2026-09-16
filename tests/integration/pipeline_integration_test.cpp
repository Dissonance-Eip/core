#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio/WavProcessor.hpp"

static constexpr double PI = 3.14159265358979323846;

namespace {

std::string writeSineWav(const std::string &path, uint32_t sampleRate, float freq,
                         float durationSecs) {
    const size_t numSamples = static_cast<size_t>(sampleRate * durationSecs);
    std::vector<int16_t> pcm(numSamples);
    for (size_t i = 0; i < numSamples; ++i)
        pcm[i] = static_cast<int16_t>(
            std::sin(2.0 * PI * freq * static_cast<double>(i) / sampleRate) * 32767.0);

    const uint32_t subchunk2Size = static_cast<uint32_t>(numSamples * sizeof(int16_t));
    const uint32_t chunkSize = 36 + subchunk2Size;
    const uint32_t subchunk1Size = 16;
    const uint16_t audioFormat = 1, numChannels = 1, bitsPerSample = 16;
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * blockAlign;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char *>(&chunkSize), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    f.write(reinterpret_cast<const char *>(&subchunk1Size), 4);
    f.write(reinterpret_cast<const char *>(&audioFormat), 2);
    f.write(reinterpret_cast<const char *>(&numChannels), 2);
    f.write(reinterpret_cast<const char *>(&sampleRate), 4);
    f.write(reinterpret_cast<const char *>(&byteRate), 4);
    f.write(reinterpret_cast<const char *>(&blockAlign), 2);
    f.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char *>(&subchunk2Size), 4);
    for (int16_t s : pcm)
        f.write(reinterpret_cast<const char *>(&s), 2);
    return path;
}

} // namespace

class PipelineIntegrationTest : public ::testing::Test {
  protected:
    std::string inputPath;
    std::string outputPath;

    void SetUp() override {
        auto tmp = std::filesystem::temp_directory_path();
        inputPath = (tmp / "pipe_test_input.wav").string();
        outputPath = (tmp / "pipe_test_output.wav").string();
        writeSineWav(inputPath, 44100, 440.0f, 1.0f);
    }

    void TearDown() override {
        std::filesystem::remove(inputPath);
        std::filesystem::remove(outputPath);
    }
};

TEST_F(PipelineIntegrationTest, SampleCountPreserved) {
    ProcessingOptions opts;
    opts.gain = 1.0;
    opts.outputPath = outputPath;
    // cutoff at 1.0 (full spectrum) via default — use full pass
    ProcessedWav result = processWavFile(inputPath, opts);

    EXPECT_EQ(result.originalSamples.size(), result.processedSamples.size());
}

TEST_F(PipelineIntegrationTest, OutputFileCreated) {
    ProcessingOptions opts;
    opts.gain = 1.0;
    opts.outputPath = outputPath;
    ProcessedWav result = processWavFile(inputPath, opts);

    std::ifstream out(result.processedPath, std::ios::binary);
    EXPECT_TRUE(out.is_open());
    out.seekg(0, std::ios::end);
    EXPECT_GT(static_cast<size_t>(out.tellg()), 44u);
}

TEST_F(PipelineIntegrationTest, ProcessedSamplesInRange) {
    ProcessingOptions opts;
    opts.gain = 1.0;
    opts.outputPath = outputPath;
    ProcessedWav result = processWavFile(inputPath, opts);

    for (float s : result.processedSamples) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }
}

TEST_F(PipelineIntegrationTest, FFTReportPopulated) {
    ProcessingOptions opts;
    opts.gain = 1.0;
    opts.outputPath = outputPath;
    ProcessedWav result = processWavFile(inputPath, opts);

    EXPECT_TRUE(result.fftReport.applied);
    EXPECT_GT(result.fftReport.framesProcessed, 0u);
}
