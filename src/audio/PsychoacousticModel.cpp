/**
 * @file PsychoacousticModel.cpp
 * @brief Psychoacoustic masking threshold computation.
 *
 * Implements Bark-scale band mapping, triangular spreading function, and
 * per-bin masking threshold computation.
 */

#include "audio/PsychoacousticModel.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

/** @brief Tiny epsilon to avoid log10(0) or division by zero. */
constexpr float kEps = 1.0e-12f;

/** @brief Reference dB level corresponding to linear magnitude 1.0 (full-scale). */
constexpr float kRefDb = 90.0f;

} // namespace

// ============================================================================
// Hz-to-Bark conversion
// ============================================================================

float PsychoacousticModel::hzToBark(float hz) {
    if (hz <= 0.0f)
        return 0.0f;
    // Standard formula from Zwicker & Terhardt
    return 13.0f * std::atan(0.00076f * hz) + 3.5f * std::atan(std::pow(hz / 7500.0f, 2.0f));
}

// ============================================================================
// Bark band center frequencies
// ============================================================================

std::vector<float> PsychoacousticModel::barkBandCenters() {
    // Center frequencies (Hz) of the 24 Bark-scale critical bands
    // Standard Zwicker values
    static const float kCenters[kNumBarkBands] = {
        50.0f,   150.0f,  250.0f,  350.0f,  450.0f,  570.0f,  700.0f,   840.0f,
        1000.0f, 1170.0f, 1370.0f, 1600.0f, 1850.0f, 2150.0f, 2500.0f,  2900.0f,
        3400.0f, 4000.0f, 4800.0f, 5800.0f, 7000.0f, 8500.0f, 10500.0f, 13500.0f};
    return std::vector<float>(kCenters, kCenters + kNumBarkBands);
}

// ============================================================================
// FFT bin to Bark band mapping
// ============================================================================

size_t PsychoacousticModel::binToBarkBand(size_t binIndex, uint32_t sampleRate, size_t frameSize) {
    if (binIndex == 0)
        return 0;

    const float hz = static_cast<float>(binIndex) * static_cast<float>(sampleRate) /
                     static_cast<float>(frameSize);
    const float bark = hzToBark(hz);

    // Clamp to [0, kNumBarkBands - 1]
    size_t band = static_cast<size_t>(std::floor(bark));
    if (band >= kNumBarkBands)
        band = kNumBarkBands - 1;
    return band;
}

// ============================================================================
// Spreading function
// ============================================================================

float PsychoacousticModel::spreadingAttenuation(float maskerBark, float targetBark) {
    const float dz = targetBark - maskerBark;
    if (dz >= 0.0f) {
        // Upward spread (target higher frequency than masker): the "upward
        // spread of masking" phenomenon — a masker suppresses content above
        // its own frequency more effectively than below it, so this slope
        // must be the gentler one (~10 dB/Bark) for loud low/mid content to
        // actually mask noise sitting above it in frequency.
        return 10.0f * dz;
    } else {
        // Downward spread (target lower frequency than masker): falls off
        // faster below the masker — steeper slope, ~25 dB/Bark.
        return -25.0f * dz; // dz negative → positive attenuation
    }
}

std::vector<float> PsychoacousticModel::applySpreading(const std::vector<float> &bandEnergiesDB) {
    std::vector<float> spreadMask(kNumBarkBands, -kRefDb);

    // Compute the Bark value at the center of each band
    std::vector<float> barkCenters(kNumBarkBands);
    for (size_t b = 0; b < kNumBarkBands; ++b) {
        barkCenters[b] = hzToBark(barkBandCenters()[b]);
    }

    // For each target band, take the maximum mask contribution from every masker
    for (size_t target = 0; target < kNumBarkBands; ++target) {
        float maxMask = -kRefDb;

        for (size_t masker = 0; masker < kNumBarkBands; ++masker) {
            const float atten = spreadingAttenuation(barkCenters[masker], barkCenters[target]);
            const float contribution = bandEnergiesDB[masker] - atten;
            if (contribution > maxMask)
                maxMask = contribution;
        }

        spreadMask[target] = maxMask;
    }

    return spreadMask;
}

// ============================================================================
// Compute per-bin masking thresholds
// ============================================================================

std::vector<float> PsychoacousticModel::computeThresholds(const std::vector<float> &magnitude,
                                                          uint32_t sampleRate, size_t frameSize) {
    if (magnitude.size() != frameSize)
        return {}; // size mismatch

    const size_t halfBins = frameSize / 2;

    // ── Step 1: Accumulate average per-bin energy per Bark band ──
    // Bark bands vary hugely in width — 5 bins in the lowest band vs. ~500
    // bins in the top band at a 2048-sample frame. Summing raw energy across
    // a band and reusing that sum as the per-bin threshold (Step 4) scales
    // the allowance with bin count: a wide band ends up with a threshold
    // ~sqrt(binCount) times larger than any individual bin's real magnitude
    // justifies, letting far more noise through per-bin in wide bands. This
    // is most visible with high-frequency perturbation content, which lands
    // almost entirely in the widest top bands.
    // Averaging keeps the threshold representative of a single bin.
    std::vector<float> bandLinearEnergy(kNumBarkBands, 0.0f);
    std::vector<size_t> bandBinCount(kNumBarkBands, 0);
    for (size_t i = 0; i <= halfBins; ++i) {
        const size_t band = binToBarkBand(i, sampleRate, frameSize);
        const float energy = magnitude[i] * magnitude[i];
        bandLinearEnergy[band] += energy;
        ++bandBinCount[band];
    }

    // Convert average per-bin energy to dB
    std::vector<float> bandEnergyDb(kNumBarkBands);
    for (size_t b = 0; b < kNumBarkBands; ++b) {
        const float avgEnergy =
            bandBinCount[b] > 0 ? bandLinearEnergy[b] / static_cast<float>(bandBinCount[b]) : 0.0f;
        const float e = std::max(avgEnergy, kEps);
        bandEnergyDb[b] = 10.0f * std::log10(e);
    }

    // ── Step 2: Apply spreading function ──
    std::vector<float> spreadMaskDb = applySpreading(bandEnergyDb);

    // ── Step 3: Apply minimum threshold floor ──
    // ATH is intentionally not used as an absolute comparison here.
    // The ATH values are in dB SPL but our signal energies are in an
    // uncalibrated dB scale from raw FFT magnitudes — comparing them
    // directly lets the ATH override the spreading function in quiet
    // bands, letting noise through (the bug you can hear).
    //
    // For perturbation clamping, the spreading function alone provides
    // correct behavior: bands near the signal have high thresholds (noise
    // is masked), bands far from the signal have low thresholds (noise is
    // clamped). A conservative floor prevents numerical issues.
    constexpr float kMinThresholdDb = -80.0f;
    for (size_t b = 0; b < kNumBarkBands; ++b) {
        spreadMaskDb[b] = std::max(spreadMaskDb[b], kMinThresholdDb);
    }

    // ── Step 4: Map per-band thresholds back to per-bin linear magnitude ──
    std::vector<float> thresholds(frameSize, 0.0f);
    for (size_t i = 0; i <= halfBins; ++i) {
        const size_t band = binToBarkBand(i, sampleRate, frameSize);
        const float threshDb = spreadMaskDb[band];
        const float threshLin = std::pow(10.0f, threshDb / 20.0f);

        thresholds[i] = threshLin;

        // Mirror to negative-frequency bin
        if (i > 0 && i < halfBins)
            thresholds[frameSize - i] = threshLin;
        else if (i == halfBins)
            thresholds[halfBins] = threshLin; // Nyquist bin
    }

    return thresholds;
}
