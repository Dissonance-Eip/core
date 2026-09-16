#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../support/WavFixture.hpp"
#include "utils/WavParser.hpp"
#include "utils/WavUtils.hpp"

using fixture::Bytes;
using fixture::TempFile;

namespace {

void expectRuntimeError(const std::function<void()> &fn, const std::string &message) {
    try {
        fn();
        FAIL() << "expected runtime_error: " << message;
    } catch (const std::runtime_error &e) {
        EXPECT_EQ(std::string(e.what()), message);
    }
}

// An INFO sub-chunk as stored inside a LIST payload.
Bytes infoField(const std::string &id, const std::string &value) {
    return fixture::chunk(id, Bytes().text(value));
}

ListTags allTags() {
    ListTags tags;
    tags.title = "Song";
    tags.artist = "Luca!";
    tags.comment = "Protected";
    tags.date = "2026-09-16";
    tags.software = "Dissonance";
    tags.genre = "Ambient";
    tags.copyright = "(c) 2026";
    return tags;
}

void expectSameTags(const ListTags &actual, const ListTags &expected) {
    EXPECT_EQ(actual.title, expected.title);
    EXPECT_EQ(actual.artist, expected.artist);
    EXPECT_EQ(actual.comment, expected.comment);
    EXPECT_EQ(actual.date, expected.date);
    EXPECT_EQ(actual.software, expected.software);
    EXPECT_EQ(actual.genre, expected.genre);
    EXPECT_EQ(actual.copyright, expected.copyright);
}

// Chunk ids of a RIFF file, in file order.
std::vector<std::string> chunkIds(const std::vector<char> &file) {
    std::vector<std::string> ids;
    size_t offset = 12;
    while (offset + 8 <= file.size()) {
        ids.emplace_back(file.data() + offset, 4);
        uint32_t size = 0;
        std::memcpy(&size, file.data() + offset + 4, 4);
        offset += 8 + size + (size % 2);
    }
    return ids;
}

Bytes monoWav(const Bytes &extraChunksBeforeData) {
    return fixture::riff(
        Bytes()
            .append(fixture::fmtChunk(1, 1, 44100, 16))
            .append(extraChunksBeforeData)
            .append(fixture::chunk("data", Bytes().value<int16_t>(16384).value<int16_t>(-16384))));
}

} // namespace

TEST(WavUtilsTest, ParseListChunkReadsEveryTag) {
    const Bytes payload = Bytes()
                              .text("INFO")
                              .append(infoField("INAM", "Song"))
                              .append(infoField("IART", "Luca"))
                              .append(infoField("ICMT", "Protected"))
                              .append(infoField("ICRD", "2026-09-16"))
                              .append(infoField("ISFT", "Dissonance"))
                              .append(infoField("IGNR", "Ambient"))
                              .append(infoField("ICOP", "(c) 2026"));

    const ListTags tags = parseListChunk(payload.data());

    EXPECT_EQ(tags.title, "Song");
    EXPECT_EQ(tags.artist, "Luca");
    EXPECT_EQ(tags.comment, "Protected");
    EXPECT_EQ(tags.date, "2026-09-16");
    EXPECT_EQ(tags.software, "Dissonance");
    EXPECT_EQ(tags.genre, "Ambient");
    EXPECT_EQ(tags.copyright, "(c) 2026");
}

TEST(WavUtilsTest, ParseListChunkTrimsTrailingNullsAndSpacesAndSkipsUnknownFields) {
    const Bytes payload = Bytes()
                              .text("INFO")
                              .append(infoField("IXYZ", "ignored"))
                              .append(fixture::chunk("INAM", Bytes().text("Song  ").value('\0')))
                              .append(infoField("IART", "Odd"));

    const ListTags tags = parseListChunk(payload.data());

    EXPECT_EQ(tags.title, "Song");
    EXPECT_EQ(tags.artist, "Odd");
    EXPECT_TRUE(tags.comment.empty());
}

TEST(WavUtilsTest, ParseListChunkReformatsEightDigitDatesOnly) {
    const ListTags compact =
        parseListChunk(Bytes().text("INFO").append(infoField("ICRD", "20260916")).data());
    const ListTags year =
        parseListChunk(Bytes().text("INFO").append(infoField("ICRD", "2026")).data());

    EXPECT_EQ(compact.date, "2026-09-16");
    EXPECT_EQ(year.date, "2026");
}

TEST(WavUtilsTest, ParseListChunkClipsAFieldThatRunsPastTheEnd) {
    const Bytes payload = Bytes().text("INFO").text("INAM").u32(100).text("Short");

    EXPECT_EQ(parseListChunk(payload.data()).title, "Short");
}

TEST(WavUtilsTest, ParseListChunkHandlesAnEmptyPayload) {
    expectSameTags(parseListChunk(Bytes().text("INFO").data()), ListTags{});
    expectSameTags(parseListChunk({}), ListTags{});
}

TEST(WavUtilsTest, BuildListInfoChunkOmitsEmptyFieldsAndWordAligns) {
    ListTags tags;
    tags.title = "Song";   // 4 chars + null = 5 bytes, so padded
    tags.artist = "Luca!"; // 5 chars + null = 6 bytes, no padding

    const std::vector<char> chunk = buildListInfoChunk(tags);

    const Bytes expected = Bytes()
                               .text("LIST")
                               .u32(32)
                               .text("INFO")
                               .text("INAM")
                               .u32(5)
                               .text("Song")
                               .value('\0')
                               .value('\0')
                               .text("IART")
                               .u32(6)
                               .text("Luca!")
                               .value('\0');
    EXPECT_EQ(chunk, expected.data());
}

TEST(WavUtilsTest, BuildListInfoChunkRoundTripsThroughParseListChunk) {
    const std::vector<char> chunk = buildListInfoChunk(allTags());

    const std::vector<char> payload(chunk.begin() + 8, chunk.end());
    expectSameTags(parseListChunk(payload), allTags());
}

TEST(WavUtilsTest, BuildListInfoChunkIsEmptyWithoutTags) {
    EXPECT_TRUE(buildListInfoChunk(ListTags{}).empty());
}

TEST(WavUtilsTest, WriteTagsReplacesTheListChunkAndPlacesItBeforeTheData) {
    TempFile file("wav_utils_write_tags.wav");
    const Bytes oldList = Bytes().text("INFO").append(infoField("INAM", "Old title"));
    file.write(monoWav(Bytes()
                           .append(fixture::chunk("LIST", oldList))
                           .append(fixture::chunk("junk", Bytes().text("odd")))));

    writeTagsToWav(file.path(), allTags());

    const std::vector<char> bytes = file.read();
    EXPECT_EQ(chunkIds(bytes), (std::vector<std::string>{"fmt ", "junk", "LIST", "data"}));
    uint32_t riffSize = 0;
    std::memcpy(&riffSize, bytes.data() + 4, 4);
    EXPECT_EQ(riffSize, bytes.size() - 8);

    std::ifstream in(file.path(), std::ios::binary);
    const Parser parser = Parser::fromFile(in);
    expectSameTags(parseListChunk(parser.getOtherChunks().at("LIST")), allTags());
    ASSERT_EQ(parser.getAudioData().size(), 2u);
    EXPECT_FLOAT_EQ(parser.getAudioData()[0], 0.5f);
}

TEST(WavUtilsTest, WriteTagsWithoutTagsRemovesTheListChunk) {
    TempFile file("wav_utils_clear_tags.wav");
    file.write(
        monoWav(fixture::chunk("LIST", Bytes().text("INFO").append(infoField("INAM", "x")))));

    writeTagsToWav(file.path(), ListTags{});

    EXPECT_EQ(chunkIds(file.read()), (std::vector<std::string>{"fmt ", "data"}));
}

TEST(WavUtilsTest, WriteTagsCopiesAChunkThatRunsPastTheEndOfTheFile) {
    TempFile file("wav_utils_overrun.wav");
    file.write(fixture::riff(
        Bytes().append(fixture::fmtChunk(1, 1, 44100, 16)).text("data").u32(1000).u16(7)));

    writeTagsToWav(file.path(), allTags());

    const std::vector<char> bytes = file.read();
    ASSERT_GE(bytes.size(), 10u);
    EXPECT_EQ(std::string(bytes.end() - 10, bytes.end() - 6), "data");
}

TEST(WavUtilsTest, WriteTagsRejectsMissingAndNonWavFiles) {
    TempFile missing("wav_utils_missing.wav");
    expectRuntimeError([&] { writeTagsToWav(missing.path(), allTags()); },
                       "Cannot open file for tag write: " + missing.path());

    TempFile tooShort("wav_utils_short.wav");
    tooShort.write(Bytes().text("RIFF"));
    expectRuntimeError([&] { writeTagsToWav(tooShort.path(), allTags()); },
                       "Not a valid RIFF/WAVE file: " + tooShort.path());

    TempFile notRiff("wav_utils_not_riff.wav");
    notRiff.write(Bytes().text("RIFX").u32(4).text("WAVE"));
    expectRuntimeError([&] { writeTagsToWav(notRiff.path(), allTags()); },
                       "Not a valid RIFF/WAVE file: " + notRiff.path());

    TempFile notWave("wav_utils_not_wave.wav");
    notWave.write(Bytes().text("RIFF").u32(4).text("AVI "));
    expectRuntimeError([&] { writeTagsToWav(notWave.path(), allTags()); },
                       "Not a valid RIFF/WAVE file: " + notWave.path());
}

TEST(WavUtilsTest, WriteTagsReportsAFileItCannotRewrite) {
    namespace fs = std::filesystem;
    TempFile file("wav_utils_read_only.wav");
    file.write(monoWav(Bytes()));
    fs::permissions(file.path(), fs::perms::owner_read, fs::perm_options::replace);

    if (std::ofstream(file.path(), std::ios::app).is_open()) {
        fs::permissions(file.path(), fs::perms::owner_all, fs::perm_options::replace);
        GTEST_SKIP() << "file permissions are not enforced for this user";
    }

    expectRuntimeError([&] { writeTagsToWav(file.path(), allTags()); },
                       "Cannot write file: " + file.path());
    fs::permissions(file.path(), fs::perms::owner_all, fs::perm_options::replace);
}

TEST(WavUtilsTest, FormatMetadataTextListsTheHeaderFields) {
    TempFile file("wav_utils_metadata.wav");
    file.write(monoWav(Bytes()));
    std::ifstream in(file.path(), std::ios::binary);

    const std::string text = formatMetadataText(Parser::fromFile(in));

    EXPECT_EQ(text, "Chunk size: 40\n"
                    "Audio format: 1\n"
                    "Number of channels: 1\n"
                    "Sample rate: 44100\n"
                    "Byte rate: 88200\n"
                    "Block align: 2\n"
                    "Bits per sample: 16\n"
                    "Data size: 4\n");
}

TEST(WavUtilsTest, RenderWaveformDrawsOneColumnPerCharacter) {
    const std::string wave = renderWaveformASCII({-1.0f, 0.0f, 1.0f, 0.0f, -1.0f}, 5, 3);

    std::istringstream lines(wave);
    std::vector<std::string> rows;
    for (std::string row; std::getline(lines, row);)
        rows.push_back(row);
    ASSERT_EQ(rows.size(), 3u);
    for (const auto &row : rows)
        EXPECT_EQ(row.size(), 5u);
    EXPECT_EQ(rows[0][0], '|');
    EXPECT_EQ(rows[2][2], '|');
}

TEST(WavUtilsTest, RenderWaveformHandlesSilenceASingleColumnAndNoAudio) {
    EXPECT_EQ(renderWaveformASCII({}), "(no audio data)");
    EXPECT_NE(renderWaveformASCII({0.0f, 0.0f}, 4, 2).find('|'), std::string::npos);
    EXPECT_EQ(renderWaveformASCII({0.5f}, 1, 1), "|\n");
}
