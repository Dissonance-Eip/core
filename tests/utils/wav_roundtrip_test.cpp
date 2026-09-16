#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/Errors.hpp"
#include "utils/WavParser.hpp"

namespace {

void writeMinimalWav(const std::string &path, uint16_t numChannels, uint32_t sampleRate,
                     uint16_t bitsPerSample, const std::vector<int16_t> &samples) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const uint32_t subchunk2Size = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t chunkSize = 36 + subchunk2Size;
    const uint32_t subchunk1Size = 16;
    const uint16_t audioFormat = 1;
    const uint16_t blockAlign = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
    const uint32_t byteRate = sampleRate * blockAlign;

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
    for (int16_t s : samples)
        f.write(reinterpret_cast<const char *>(&s), 2);
}

std::string tmpPath(const std::string &name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

TEST(WavRoundtripTest, MonoFieldsMatch) {
    const std::string path = tmpPath("rt_mono.wav");
    std::vector<int16_t> samples = {0, 16383, -16384, 32767, -32768};
    writeMinimalWav(path, 1, 44100, 16, samples);

    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint16_t audioFormat = 0;
    std::vector<float> audioData;
    {
        // Scope the stream so its handle is released before the file is removed.
        std::ifstream f(path, std::ios::binary);
        Parser p = Parser::fromFile(f);
        numChannels = p.getNumChannels();
        sampleRate = p.getSampleRate();
        bitsPerSample = p.getBitsPerSample();
        audioFormat = p.getAudioFormat();
        audioData = p.getAudioData();
    }

    EXPECT_EQ(numChannels, 1);
    EXPECT_EQ(sampleRate, 44100u);
    EXPECT_EQ(bitsPerSample, 16);
    EXPECT_EQ(audioFormat, 1);
    ASSERT_EQ(audioData.size(), samples.size());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(WavRoundtripTest, StereoSampleCountCorrect) {
    const std::string path = tmpPath("rt_stereo.wav");
    // 4 frames × 2 channels = 8 int16 samples
    std::vector<int16_t> samples = {100, -100, 200, -200, 300, -300, 400, -400};
    writeMinimalWav(path, 2, 48000, 16, samples);

    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    std::vector<float> audioData;
    {
        std::ifstream f(path, std::ios::binary);
        Parser p = Parser::fromFile(f);
        numChannels = p.getNumChannels();
        sampleRate = p.getSampleRate();
        audioData = p.getAudioData();
    }

    EXPECT_EQ(numChannels, 2);
    EXPECT_EQ(sampleRate, 48000u);
    ASSERT_EQ(audioData.size(), samples.size());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(WavRoundtripTest, FloatNormalisationInRange) {
    const std::string path = tmpPath("rt_norm.wav");
    std::vector<int16_t> samples = {32767, -32768, 0};
    writeMinimalWav(path, 1, 44100, 16, samples);

    std::vector<float> audioData;
    {
        std::ifstream f(path, std::ios::binary);
        Parser p = Parser::fromFile(f);
        audioData = p.getAudioData();
    }

    for (float v : audioData) {
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(WavRoundtripTest, TruncatedFileThrows) {
    const std::string path = tmpPath("rt_trunc.wav");
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write("RIFF", 4); // deliberately incomplete
    }
    {
        std::ifstream f(path, std::ios::binary);
        EXPECT_THROW(Parser::fromFile(f), dissonance::WavFormatError);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(WavRoundtripTest, WrongMagicThrows) {
    const std::string path = tmpPath("rt_magic.wav");
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write("BIFF", 4);
        uint32_t dummy = 0;
        f.write(reinterpret_cast<const char *>(&dummy), 4);
        f.write("WAVE", 4);
    }
    {
        std::ifstream f(path, std::ios::binary);
        EXPECT_THROW(Parser::fromFile(f), dissonance::WavFormatError);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
