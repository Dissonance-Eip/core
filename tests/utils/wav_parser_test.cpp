#include <gtest/gtest.h>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "../support/WavFixture.hpp"
#include "core/Errors.hpp"
#include "utils/WavParser.hpp"

using fixture::Bytes;
using fixture::TempFile;

namespace {

constexpr uint16_t kPcm = 1;
constexpr uint16_t kFloat = 3;

Parser parse(const TempFile &file, bool readAudioData = true) {
    std::ifstream in(file.path(), std::ios::binary);
    return Parser::fromFile(in, readAudioData);
}

void writeMono(const TempFile &file, uint16_t audioFormat, uint16_t bitsPerSample,
               const Bytes &samples) {
    file.write(fixture::riff(Bytes()
                                 .append(fixture::fmtChunk(audioFormat, 1, 44100, bitsPerSample))
                                 .append(fixture::chunk("data", samples))));
}

void expectWavError(const std::function<void()> &fn, const std::string &message) {
    try {
        fn();
        FAIL() << "expected WavFormatError: " << message;
    } catch (const dissonance::WavFormatError &e) {
        EXPECT_EQ(std::string(e.what()), message);
    }
}

// 24-bit little-endian sample.
Bytes s24(int32_t v) {
    return Bytes()
        .value<uint8_t>(static_cast<uint8_t>(v & 0xFF))
        .value<uint8_t>(static_cast<uint8_t>((v >> 8) & 0xFF))
        .value<uint8_t>(static_cast<uint8_t>((v >> 16) & 0xFF));
}

struct FormatCase {
    uint16_t audioFormat;
    uint16_t bitsPerSample;
};

const std::vector<FormatCase> kSupportedFormats = {
    {kPcm, 8}, {kPcm, 16}, {kPcm, 24}, {kPcm, 32}, {kFloat, 32}, {kFloat, 64},
};

} // namespace

TEST(WavParserTest, ReadsEveryHeaderField) {
    TempFile file("wav_parser_header.wav");
    file.write(fixture::riff(
        Bytes()
            .append(fixture::fmtChunk(kPcm, 2, 48000, 16))
            .append(fixture::chunk("data", Bytes().value<int16_t>(1).value<int16_t>(-1)))));

    const Parser p = parse(file);

    EXPECT_EQ(p.getRiff(), "RIFF");
    EXPECT_EQ(p.getChunkSize(), 40u);
    EXPECT_EQ(p.getWave(), "WAVE");
    EXPECT_EQ(p.getFmt(), "fmt ");
    EXPECT_EQ(p.getSubchunk1Size(), 16u);
    EXPECT_EQ(p.getAudioFormat(), kPcm);
    EXPECT_EQ(p.getNumChannels(), 2);
    EXPECT_EQ(p.getSampleRate(), 48000u);
    EXPECT_EQ(p.getByteRate(), 192000u);
    EXPECT_EQ(p.getBlockAlign(), 4);
    EXPECT_EQ(p.getBitsPerSample(), 16);
    EXPECT_EQ(p.getData(), "data");
    EXPECT_EQ(p.getSubchunk2Size(), 4u);
    EXPECT_TRUE(p.getOtherChunks().empty());
}

TEST(WavParserTest, Decodes8BitPcm) {
    TempFile file("wav_parser_pcm8.wav");
    writeMono(file, kPcm, 8, Bytes().value<uint8_t>(0).value<uint8_t>(128).value<uint8_t>(255));

    const auto samples = parse(file).getAudioData();

    ASSERT_EQ(samples.size(), 3u);
    EXPECT_FLOAT_EQ(samples[0], -1.0f);
    EXPECT_FLOAT_EQ(samples[1], 0.0f);
    EXPECT_FLOAT_EQ(samples[2], 127.0f / 128.0f);
}

TEST(WavParserTest, Decodes16BitPcm) {
    TempFile file("wav_parser_pcm16.wav");
    writeMono(file, kPcm, 16, Bytes().value<int16_t>(16384).value<int16_t>(-32768));

    const auto samples = parse(file).getAudioData();

    ASSERT_EQ(samples.size(), 2u);
    EXPECT_FLOAT_EQ(samples[0], 0.5f);
    EXPECT_FLOAT_EQ(samples[1], -1.0f);
}

TEST(WavParserTest, Decodes24BitPcmIncludingNegativeSamples) {
    TempFile file("wav_parser_pcm24.wav");
    writeMono(file, kPcm, 24, Bytes().append(s24(0x400000)).append(s24(-0x400000)).append(s24(-1)));

    const auto samples = parse(file).getAudioData();

    ASSERT_EQ(samples.size(), 3u);
    EXPECT_FLOAT_EQ(samples[0], 0.5f);
    EXPECT_FLOAT_EQ(samples[1], -0.5f);
    EXPECT_FLOAT_EQ(samples[2], -1.0f / 8388608.0f);
}

TEST(WavParserTest, Decodes32BitPcm) {
    TempFile file("wav_parser_pcm32.wav");
    writeMono(file, kPcm, 32,
              Bytes().value<int32_t>(1 << 30).value<int32_t>(std::numeric_limits<int32_t>::min()));

    const auto samples = parse(file).getAudioData();

    ASSERT_EQ(samples.size(), 2u);
    EXPECT_FLOAT_EQ(samples[0], 0.5f);
    EXPECT_FLOAT_EQ(samples[1], -1.0f);
}

TEST(WavParserTest, Decodes32BitFloatClampingToFullScale) {
    TempFile file("wav_parser_float32.wav");
    writeMono(file, kFloat, 32, Bytes().value<float>(0.25f).value<float>(1.5f).value<float>(-2.0f));

    const auto samples = parse(file).getAudioData();

    ASSERT_EQ(samples.size(), 3u);
    EXPECT_FLOAT_EQ(samples[0], 0.25f);
    EXPECT_FLOAT_EQ(samples[1], 1.0f);
    EXPECT_FLOAT_EQ(samples[2], -1.0f);
}

TEST(WavParserTest, Decodes64BitFloatClampingToFullScale) {
    TempFile file("wav_parser_float64.wav");
    writeMono(file, kFloat, 64, Bytes().value<double>(0.5).value<double>(3.0).value<double>(-3.0));

    const auto samples = parse(file).getAudioData();

    ASSERT_EQ(samples.size(), 3u);
    EXPECT_FLOAT_EQ(samples[0], 0.5f);
    EXPECT_FLOAT_EQ(samples[1], 1.0f);
    EXPECT_FLOAT_EQ(samples[2], -1.0f);
}

TEST(WavParserTest, HeaderOnlyReadLeavesSamplesEmptyForEveryFormat) {
    for (const auto &format : kSupportedFormats) {
        SCOPED_TRACE("format " + std::to_string(format.audioFormat) + ", " +
                     std::to_string(format.bitsPerSample) + " bits");
        TempFile file("wav_parser_header_only.wav");
        Bytes samples;
        for (int i = 0; i < 4 * (format.bitsPerSample / 8); ++i)
            samples.value<uint8_t>(0);
        writeMono(file, format.audioFormat, format.bitsPerSample, samples);

        const Parser p = parse(file, false);

        EXPECT_TRUE(p.getAudioData().empty());
        EXPECT_EQ(p.getSubchunk2Size(), samples.size());
    }
}

TEST(WavParserTest, TruncatedAudioThrowsForEveryFormat) {
    for (const auto &format : kSupportedFormats) {
        SCOPED_TRACE("format " + std::to_string(format.audioFormat) + ", " +
                     std::to_string(format.bitsPerSample) + " bits");
        const uint32_t bytesPerSample = format.bitsPerSample / 8;
        TempFile file("wav_parser_truncated.wav");
        // The data chunk claims four samples but the file holds only one.
        Bytes body;
        body.append(fixture::fmtChunk(format.audioFormat, 1, 44100, format.bitsPerSample))
            .text("data")
            .u32(4 * bytesPerSample);
        for (uint32_t i = 0; i < bytesPerSample; ++i)
            body.value<uint8_t>(0);
        file.write(fixture::riff(body));

        expectWavError([&] { parse(file); }, "Failed to read audio data");
    }
}

TEST(WavParserTest, HeaderOnlyReadToleratesTruncatedAudioWhenItCanSkip) {
    for (const auto &format : {FormatCase{kPcm, 16}, FormatCase{kFloat, 32}}) {
        TempFile file("wav_parser_truncated_header_only.wav");
        Bytes body;
        body.append(fixture::fmtChunk(format.audioFormat, 1, 44100, format.bitsPerSample))
            .text("data")
            .u32(400);
        file.write(fixture::riff(body));

        EXPECT_NO_THROW(parse(file, false));
    }
}

TEST(WavParserTest, KeepsUnknownChunksAndSkipsFormatExtensionsAndPadding) {
    TempFile file("wav_parser_chunks.wav");
    const Bytes list = Bytes().text("INFOxyz"); // odd size, so it is padded
    file.write(fixture::riff(Bytes()
                                 .append(fixture::fmtChunk(kPcm, 1, 44100, 8, 3))
                                 .append(fixture::chunk("LIST", list))
                                 .append(fixture::chunk("data", Bytes().value<uint8_t>(255)))
                                 .append(fixture::chunk("junk", Bytes().text("after data")))));

    const Parser p = parse(file);

    EXPECT_EQ(p.getSubchunk1Size(), 19u);
    EXPECT_EQ(p.getAudioData().size(), 1u);
    ASSERT_EQ(p.getOtherChunks().count("LIST"), 1u);
    EXPECT_EQ(p.getOtherChunks().at("LIST"), list.data());
    EXPECT_EQ(p.getOtherChunks().count("junk"), 0u);
}

TEST(WavParserTest, RejectsAClosedStream) {
    std::ifstream closed;
    expectWavError([&] { Parser::fromFile(closed); }, "File not open");
}

TEST(WavParserTest, RejectsFilesThatEndInsideAHeader) {
    TempFile file("wav_parser_short.wav");

    file.write(Bytes().text("RIFF").u32(4));
    expectWavError([&] { parse(file); }, "Failed to read string field");

    file.write(Bytes().text("RIFF").u16(4));
    expectWavError([&] { parse(file); }, "Failed to read data field");
}

TEST(WavParserTest, RejectsAnUnknownChunkThatRunsPastTheEnd) {
    TempFile file("wav_parser_chunk_overrun.wav");
    file.write(fixture::riff(
        Bytes().append(fixture::fmtChunk(kPcm, 1, 44100, 16)).text("junk").u32(10).text("ab")));

    expectWavError([&] { parse(file); }, "Failed to read chunk data");
}

TEST(WavParserTest, RejectsDataBeforeTheFormatChunk) {
    TempFile file("wav_parser_no_fmt.wav");
    file.write(fixture::riff(fixture::chunk("data", Bytes().value<int16_t>(0))));

    expectWavError([&] { parse(file); }, "WAV missing fmt chunk before data");
}

TEST(WavParserTest, RejectsAZeroBitDepth) {
    TempFile file("wav_parser_zero_bits.wav");
    writeMono(file, kPcm, 0, Bytes().value<int16_t>(0));

    expectWavError([&] { parse(file); }, "Invalid bitsPerSample in WAV header");
}

TEST(WavParserTest, RejectsDataThatIsNotAWholeNumberOfSamples) {
    TempFile file("wav_parser_corrupt_size.wav");
    writeMono(file, kPcm, 16, Bytes().value<uint8_t>(1).value<uint8_t>(2).value<uint8_t>(3));

    expectWavError([&] { parse(file); }, "Corrupt WAV data chunk size");
}

TEST(WavParserTest, RejectsUnsupportedBitDepthsAndFormats) {
    TempFile file("wav_parser_unsupported.wav");

    writeMono(file, kPcm, 12, Bytes().value<uint8_t>(0).value<uint8_t>(0));
    expectWavError([&] { parse(file); }, "Unsupported PCM bitsPerSample: 12");

    writeMono(file, kFloat, 16, Bytes().value<int16_t>(0));
    expectWavError([&] { parse(file); }, "Unsupported float bitsPerSample: 16");

    // WAVE_FORMAT_EXTENSIBLE, as written by many DAWs for 24-bit exports.
    writeMono(file, 0xFFFE, 16, Bytes().value<int16_t>(0));
    expectWavError([&] { parse(file); }, "Unsupported WAV audioFormat: 65534");
}
