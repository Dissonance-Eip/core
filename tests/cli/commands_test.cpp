#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "../support/CoutCapture.hpp"
#include "../support/WavFixture.hpp"
#include "cli/Commands.hpp"
#include "core/Errors.hpp"

using fixture::CoutCapture;
using fixture::TempFile;

namespace {

constexpr uint32_t kRate = 44100;
constexpr double kPi = 3.141592653589793;

std::vector<int16_t> sine(size_t count, double frequency) {
    std::vector<int16_t> samples(count);
    for (size_t i = 0; i < count; ++i)
        samples[i] = static_cast<int16_t>(
            16000.0 * std::sin(2.0 * kPi * frequency * static_cast<double>(i) / kRate));
    return samples;
}

/** @brief Owns command-line option strings and exposes them as argc/argv. */
class Args {
  public:
    Args(std::initializer_list<std::string> values) : values_(values) {
        for (auto &v : values_)
            pointers_.push_back(v.data());
    }

    [[nodiscard]] int argc() const { return static_cast<int>(pointers_.size()); }
    char **argv() { return pointers_.data(); }

  private:
    std::vector<std::string> values_;
    std::vector<char *> pointers_;
};

template <typename Error>
void expectError(const std::function<void()> &fn, const std::string &message) {
    try {
        fn();
        FAIL() << "expected error: " << message;
    } catch (const Error &e) {
        EXPECT_EQ(std::string(e.what()), message);
    }
}

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(CommandsTest, PrintUsageListsCommandsModesAndExamples) {
    CoutCapture out;

    Commands::printUsage("dissonance.core");

    const std::string text = out.text();
    EXPECT_TRUE(contains(text, "Usage: dissonance.core <command> <input_file> [options]"));
    for (const char *command : {"info", "process", "fft", "bark"})
        EXPECT_TRUE(contains(text, std::string("  ") + command)) << command;
    EXPECT_TRUE(contains(text, "(white_noise, phase_distortion, spectral_gate, pink_noise)"));
    EXPECT_TRUE(contains(text, "dissonance.core fft sound.wav --full --sort"));
}

TEST(CommandsTest, HandleInfoPrintsMetadataChunksAndWaveform) {
    TempFile input("commands_info.wav");
    TempFile output("commands_info-processed.wav");
    input.write(fixture::pcm16Wav(
        sine(256, 440.0), 1, kRate,
        fixture::chunk("LIST", fixture::Bytes().text("INFO").append(
                                   fixture::chunk("INAM", fixture::Bytes().text("Song"))))));
    CoutCapture out;

    EXPECT_EQ(Commands::handleInfo(input.path()), EXIT_SUCCESS);

    const std::string text = out.text();
    EXPECT_TRUE(contains(text, "=== WAV File Information ==="));
    EXPECT_TRUE(contains(text, "Sample rate: 44100"));
    EXPECT_TRUE(contains(text, "=== Metadata Chunks ==="));
    EXPECT_TRUE(contains(text, "Title: Song"));
    EXPECT_TRUE(contains(text, "=== Waveform ==="));
}

TEST(CommandsTest, HandleProcessAppliesEveryOptionAndReportsTheModes) {
    TempFile input("commands_process.wav");
    TempFile output("commands_process_out.wav");
    input.write(fixture::pcm16Wav(sine(4096, 440.0)));
    Args args{"--gain",
              "0.5",
              "--perturbation",
              "0.25",
              "--mode",
              "white_noise",
              "--mode",
              "phase_distortion",
              "--masking-strength",
              "0.8",
              "--output",
              output.path(),
              "--ignored",
              "--gain"};
    CoutCapture out;

    EXPECT_EQ(Commands::handleProcess(input.path(), args.argc(), args.argv(), 0), EXIT_SUCCESS);

    const std::string text = out.text();
    EXPECT_TRUE(std::filesystem::exists(output.path()));
    EXPECT_TRUE(contains(text, "Output: " + output.path()));
    EXPECT_TRUE(contains(text, "Gain applied: 0.500000"));
    EXPECT_TRUE(contains(text, "FFT applied: yes"));
    EXPECT_TRUE(contains(text, "FFT frames: "));
    EXPECT_TRUE(contains(text, "Cutoff bin: 512"));
    EXPECT_TRUE(contains(text, "Perturbation modes: white_noise, phase_distortion"));
    EXPECT_TRUE(contains(text, "Perturbation strength: 0.250000"));
    EXPECT_TRUE(contains(text, "Perturbation RMS: "));
}

TEST(CommandsTest, HandleProcessReportsTheDefaultPerturbationWithoutModes) {
    TempFile input("commands_process_default.wav");
    TempFile output("commands_process_default_out.wav");
    input.write(fixture::pcm16Wav(sine(4096, 440.0)));
    Args args{"--output", output.path()};
    CoutCapture out;

    Commands::handleProcess(input.path(), args.argc(), args.argv(), 0);

    const std::string text = out.text();
    EXPECT_FALSE(contains(text, "Perturbation modes"));
    EXPECT_TRUE(contains(text, "Perturbation strength: 0.500000"));
}

TEST(CommandsTest, HandleProcessOmitsPerturbationAndFftDetailsWhenNeitherRan) {
    TempFile input("commands_process_single.wav");
    TempFile output("commands_process_single_out.wav");
    input.write(fixture::pcm16Wav({1000}));
    Args args{"--perturbation", "0", "--output", output.path()};
    CoutCapture out;

    Commands::handleProcess(input.path(), args.argc(), args.argv(), 0);

    const std::string text = out.text();
    EXPECT_TRUE(contains(text, "FFT applied: no"));
    EXPECT_FALSE(contains(text, "FFT frames"));
    EXPECT_FALSE(contains(text, "Perturbation"));
}

TEST(CommandsTest, HandleFftAnalysesOneBlockAtAnOffset) {
    TempFile input("commands_fft_offset.wav");
    input.write(fixture::pcm16Wav(sine(kRate, 1000.0)));
    Args args{"--offset", "0.5", "--bins", "4"};
    CoutCapture out;

    EXPECT_EQ(Commands::handleFft(input.path(), args.argc(), args.argv(), 0), EXIT_SUCCESS);

    const std::string text = out.text();
    EXPECT_TRUE(contains(text, "=== FFT Analysis ==="));
    EXPECT_TRUE(contains(text, "Channels: 1"));
    EXPECT_TRUE(contains(text, "Offset: 0.500000 s (frame 22050)"));
    EXPECT_TRUE(contains(text, "Frames analyzed: 512"));
    EXPECT_TRUE(contains(text, "First 4 frequency bins:"));
    EXPECT_FALSE(contains(text, "DC bias"));
}

TEST(CommandsTest, HandleFftAveragesTheWholeFileAndSortsByMagnitude) {
    TempFile input("commands_fft_full.wav");
    input.write(fixture::pcm16Wav(sine(kRate, 1000.0)));
    Args args{"--full", "--sort", "--bins", "3"};
    CoutCapture out;

    Commands::handleFft(input.path(), args.argc(), args.argv(), 0);

    const std::string text = out.text();
    EXPECT_TRUE(contains(text, "Mode: full file (averaged periodogram)"));
    EXPECT_TRUE(contains(text, "Windows averaged: 171"));
    EXPECT_TRUE(contains(text, "Peak frequency: 1034 Hz (bin 12)"));
    EXPECT_TRUE(contains(text, "Top 3 frequency bins (sorted by magnitude):"));
}

TEST(CommandsTest, HandleFftFlagsAStrongDcOffset) {
    TempFile input("commands_fft_dc.wav");
    input.write(fixture::pcm16Wav(std::vector<int16_t>(1024, 16000)));
    Args args{};
    CoutCapture out;

    Commands::handleFft(input.path(), args.argc(), args.argv(), 0);

    EXPECT_TRUE(contains(out.text(), "DC bias: HIGH"));
}

TEST(CommandsTest, HandleFftHandlesSilence) {
    TempFile input("commands_fft_silence.wav");
    input.write(fixture::pcm16Wav(std::vector<int16_t>(1024, 0), 2));
    Args args{};
    CoutCapture out;

    Commands::handleFft(input.path(), args.argc(), args.argv(), 0);

    const std::string text = out.text();
    EXPECT_TRUE(contains(text, "Channels: 2"));
    EXPECT_TRUE(contains(text, "Spectral centroid: 0 Hz"));
    EXPECT_FALSE(contains(text, "DC bias"));
}

TEST(CommandsTest, HandleFftRejectsMissingAndTooShortFiles) {
    TempFile missing("commands_fft_missing.wav");
    Args none{};
    expectError<dissonance::WavFormatError>(
        [&] { Commands::handleFft(missing.path(), none.argc(), none.argv(), 0); },
        "Failed to open file: " + missing.path());

    TempFile shortFile("commands_fft_short.wav");
    shortFile.write(fixture::pcm16Wav(sine(100, 440.0)));
    Args full{"--full"};
    expectError<dissonance::DspError>(
        [&] { Commands::handleFft(shortFile.path(), full.argc(), full.argv(), 0); },
        "File too short for FFT analysis");

    Args pastTheEnd{"--offset", "10"};
    expectError<dissonance::DspError>(
        [&] { Commands::handleFft(shortFile.path(), pastTheEnd.argc(), pastTheEnd.argv(), 0); },
        "Not enough samples for FFT analysis at this offset");
}

TEST(CommandsTest, HandleBarkPrintsEveryBandAtTheRequestedFrameSize) {
    TempFile input("commands_bark.wav");
    input.write(fixture::pcm16Wav(sine(256, 440.0)));

    for (const auto &[option, frameSize] :
         std::vector<std::pair<std::vector<std::string>, std::string>>{
             {{}, "2048"}, {{"--frame-size", "512"}, "512"}}) {
        std::vector<std::string> values = option;
        std::vector<char *> argv;
        for (auto &v : values)
            argv.push_back(v.data());
        CoutCapture out;

        EXPECT_EQ(Commands::handleBark(input.path(), static_cast<int>(argv.size()), argv.data(), 0),
                  EXIT_SUCCESS);

        const std::string text = out.text();
        EXPECT_TRUE(contains(text, "Frame size: " + frameSize + " samples"));
        EXPECT_TRUE(contains(text, "Nyquist freq: 22050 Hz"));
        EXPECT_TRUE(contains(text, "Total bands: 24"));
        const auto table = text.substr(text.find(std::string(78, '-')));
        EXPECT_EQ(std::count(table.begin(), table.end(), '\n'), 25) << "24 bands after the rule";
    }
}

TEST(CommandsTest, HandleBarkRejectsAMissingFile) {
    TempFile missing("commands_bark_missing.wav");
    Args none{};

    expectError<dissonance::WavFormatError>(
        [&] { Commands::handleBark(missing.path(), none.argc(), none.argv(), 0); },
        "Failed to open file: " + missing.path());
}
