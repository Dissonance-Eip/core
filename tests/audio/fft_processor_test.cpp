#include <gtest/gtest.h>
#include <complex>
#include <vector>

#include "audio/FFTProcessor.hpp"
#include "core/Errors.hpp"

constexpr double EPS = 1e-5;

TEST(FFTProcessorTest, ImpulseHasFlatSpectrum) {
    std::vector<float> impulse = {1.0f, 0.0f, 0.0f, 0.0f};
    auto spectrum = fft::transform(impulse);

    ASSERT_EQ(spectrum.size(), impulse.size());
    for (const auto &bin : spectrum) {
        EXPECT_NEAR(bin.real(), 1.0, EPS);
        EXPECT_NEAR(bin.imag(), 0.0, EPS);
    }
}

TEST(FFTProcessorTest, InverseReconstructsImpulse) {
    std::vector<float> impulse = {1.0f, 0.0f, 0.0f, 0.0f};
    auto spectrum = fft::transform(impulse);
    auto time = fft::inverse(spectrum);

    ASSERT_EQ(time.size(), impulse.size());
    for (size_t i = 0; i < time.size(); ++i) {
        EXPECT_NEAR(time[i], static_cast<double>(impulse[i]), EPS);
    }
}

TEST(FFTProcessorTest, RoundTripSignal) {
    std::vector<float> signal = {0.0f, 1.0f, 0.0f, -1.0f};
    auto spectrum = fft::transform(signal);
    auto time = fft::inverse(spectrum);

    ASSERT_EQ(time.size(), signal.size());
    for (size_t i = 0; i < time.size(); ++i) {
        EXPECT_NEAR(time[i], static_cast<double>(signal[i]), EPS);
    }
}

TEST(FFTProcessorTest, MagnitudeMatchesSize) {
    std::vector<float> signal = {0.0f, 1.0f, 0.0f, -1.0f};
    auto spectrum = fft::transform(signal);
    auto mags = fft::magnitude(spectrum);

    EXPECT_EQ(mags.size(), spectrum.size());
}

TEST(FFTProcessorTest, ThrowsOnEmptyInput) {
    std::vector<float> emptyReal;
    std::vector<std::complex<float>> emptyComplex;

    EXPECT_THROW(fft::transform(emptyReal), dissonance::DspError);
    EXPECT_THROW(fft::inverse(emptyComplex), dissonance::DspError);
}
