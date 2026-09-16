#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/FFTProcessor.hpp"

static constexpr double PI = 3.14159265358979323846;

TEST(FFTSinusoidTest, PeakBinAt440Hz) {
    constexpr size_t N = 4096;
    constexpr double sampleRate = 44100.0;
    constexpr double freq = 440.0;

    std::vector<float> signal(N);
    for (size_t i = 0; i < N; ++i)
        signal[i] =
            static_cast<float>(std::sin(2.0 * PI * freq * static_cast<double>(i) / sampleRate));

    auto spectrum = fft::transform(signal);
    auto mags = fft::magnitude(spectrum);

    // Only inspect positive-frequency half
    size_t half = N / 2;
    size_t peakBin = 0;
    double peakMag = 0.0;
    for (size_t k = 0; k < half; ++k) {
        if (mags[k] > peakMag) {
            peakMag = mags[k];
            peakBin = k;
        }
    }

    size_t expectedBin =
        static_cast<size_t>(std::round(freq * static_cast<double>(N) / sampleRate));
    EXPECT_EQ(peakBin, expectedBin) << "Peak should be at bin " << expectedBin << " for 440 Hz";
}

TEST(FFTSinusoidTest, DCSignalPeakAtBinZero) {
    constexpr size_t N = 1024;
    std::vector<float> signal(N, 1.0f);

    auto spectrum = fft::transform(signal);
    auto mags = fft::magnitude(spectrum);

    size_t half = N / 2;
    size_t peakBin = 0;
    double peakMag = 0.0;
    for (size_t k = 0; k < half; ++k) {
        if (mags[k] > peakMag) {
            peakMag = mags[k];
            peakBin = k;
        }
    }

    EXPECT_EQ(peakBin, 0u) << "DC signal should peak at bin 0";
}
