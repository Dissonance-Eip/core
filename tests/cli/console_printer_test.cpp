#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "../support/CoutCapture.hpp"
#include "../support/WavFixture.hpp"
#include "cli/ConsolePrinter.hpp"
#include "core/Errors.hpp"

using fixture::Bytes;
using fixture::CoutCapture;
using fixture::TempFile;

namespace {

Bytes tagChunk() {
    return fixture::chunk("LIST",
                          Bytes()
                              .text("INFO")
                              .append(fixture::chunk("INAM", Bytes().text("Song").value('\0')))
                              .append(fixture::chunk("IART", Bytes().text("Luca").value('\0'))));
}

// 20 bytes: one full 16-byte row, then a partial row ending in a non-printable byte.
Bytes junkChunk() {
    return fixture::chunk("junk", Bytes().text("0123456789abcdef").text("xyz").value('\x01'));
}

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

} // namespace

class ConsolePrinterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        input.write(fixture::pcm16Wav({0, 8000, -8000, 16000, -16000, 0, 4000, -4000}, 1, 44100,
                                      Bytes().append(tagChunk()).append(junkChunk())));
    }

    TempFile input{"console_printer.wav"};
    TempFile output{"console_printer-processed.wav"};
};

TEST_F(ConsolePrinterTest, LoadsTheFileAndSaysSo) {
    CoutCapture out;

    const ConsolePrinter printer(input.path());

    EXPECT_TRUE(printer.isValid());
    EXPECT_TRUE(contains(out.text(), "Opening file: " + input.path()));
    EXPECT_TRUE(contains(out.text(), "File opened successfully"));
}

TEST_F(ConsolePrinterTest, ColoursEachHeaderLabelAndValue) {
    CoutCapture out;
    const ConsolePrinter printer(input.path());
    CoutCapture metadata;

    printer.printMetadata();

    EXPECT_TRUE(
        contains(metadata.raw(), COLORS[0] + "Sample rate:" + COLORS[1] + " 44100" + COLORS[4]));
    EXPECT_TRUE(contains(metadata.text(), "Number of channels: 1\n"));
}

TEST_F(ConsolePrinterTest, PrintsTagsAndDumpsOtherChunks) {
    CoutCapture out;
    const ConsolePrinter printer(input.path());
    CoutCapture chunks;

    printer.printOtherChunks();

    const std::string text = chunks.text();
    EXPECT_TRUE(contains(text, "MetaData:\n"));
    EXPECT_TRUE(contains(text, "Title: Song\n"));
    EXPECT_TRUE(contains(text, "Name: Luca\n"));
    EXPECT_FALSE(contains(text, "Genre:"));
    EXPECT_TRUE(contains(text, "Chunk junk data:\n0123456789abcdef\nxyz.\n\n"));
}

TEST_F(ConsolePrinterTest, ColoursTheWaveformBars) {
    CoutCapture out;
    const ConsolePrinter printer(input.path());
    CoutCapture wave;

    printer.printWaveform();

    EXPECT_TRUE(contains(wave.raw(), COLORS[3] + "|" + COLORS[4]));
}

TEST_F(ConsolePrinterTest, RejectsAMissingFile) {
    TempFile missing("console_printer_missing.wav");
    CoutCapture out;

    EXPECT_THROW(ConsolePrinter printer(missing.path()), dissonance::WavFormatError);
}

TEST(ConsolePrinterStaticTest, DumpsAnEmptyChunkAsABlankLine) {
    CoutCapture out;

    ConsolePrinter::printGenericChunk("empty", {});

    EXPECT_EQ(out.text(), "Chunk empty data:\n\n");
}

TEST(ConsolePrinterStaticTest, PrintFieldSkipsEmptyValues) {
    CoutCapture out;

    printField("Title", "");
    printField("Artist", "Luca");

    EXPECT_EQ(out.text(), "Artist: Luca\n");
}
