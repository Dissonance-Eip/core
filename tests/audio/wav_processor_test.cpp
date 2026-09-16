#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "../support/WavFixture.hpp"
#include "audio/PerturbationStage.hpp"
#include "audio/WavProcessor.hpp"
#include "core/Errors.hpp"

using fixture::TempFile;

namespace {

// Options that skip perturbation, so these tests only exercise WavProcessor itself.
ProcessingOptions withoutPerturbation() {
    ProcessingOptions opts;
    opts.perturbation = 0.0f;
    return opts;
}

std::vector<int16_t> ramp(size_t count) {
    std::vector<int16_t> samples(count);
    for (size_t i = 0; i < count; ++i)
        samples[i] = static_cast<int16_t>(i * 100);
    return samples;
}

void expectWavError(const std::function<void()> &fn, const std::string &message) {
    try {
        fn();
        FAIL() << "expected WavFormatError: " << message;
    } catch (const dissonance::WavFormatError &e) {
        EXPECT_EQ(std::string(e.what()), message);
    }
}

} // namespace

TEST(WavProcessorTest, RejectsAMissingInputFile) {
    TempFile missing("wav_processor_missing.wav");

    expectWavError([&] { processWavFile(missing.path(), withoutPerturbation()); },
                   "Failed to open file: " + missing.path());
}

TEST(WavProcessorTest, NamesTheOutputAfterAnInputWithoutAnExtension) {
    TempFile input("wav_processor_no_extension");
    TempFile output("wav_processor_no_extension-processed.wav");
    input.write(fixture::pcm16Wav(ramp(64)));

    const ProcessedWav result = processWavFile(input.path(), withoutPerturbation());

    EXPECT_EQ(result.processedPath, output.path());
    EXPECT_TRUE(std::filesystem::exists(output.path()));
}

TEST(WavProcessorTest, RejectsAnOutputPathItCannotWrite) {
    TempFile input("wav_processor_unwritable.wav");
    input.write(fixture::pcm16Wav(ramp(64)));
    ProcessingOptions opts = withoutPerturbation();
    opts.outputPath =
        (std::filesystem::temp_directory_path() / "dissonance-missing-dir" / "out.wav").string();

    expectWavError([&] { processWavFile(input.path(), opts); },
                   "Failed to open output file: " + opts.outputPath);
}

TEST(WavProcessorTest, ReportsNoPerturbationForAFileWithoutAudio) {
    TempFile input("wav_processor_empty.wav");
    TempFile output("wav_processor_empty-processed.wav");
    input.write(fixture::pcm16Wav({}));

    const ProcessedWav result = processWavFile(input.path());

    EXPECT_TRUE(result.originalSamples.empty());
    EXPECT_FALSE(result.fftReport.applied);
    EXPECT_FLOAT_EQ(result.perturbationRmsDbfs, PerturbationStage::kSilentDbfs);
}
