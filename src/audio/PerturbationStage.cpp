#include "audio/PerturbationStage.hpp"
#include "audio/FFTProcessor.hpp"
#include "audio/WindowFunctions.hpp"
#include <algorithm>
#include <cmath>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr float kPinkNoiseSources = 16;

}

PerturbationStage::PerturbationStage(const std::string &mode, float strength, uint32_t sampleRate,
                                     uint64_t seed, MaskContext *context)
    : mode_(mode), strength_(std::clamp(strength, 0.0f, 1.0f)), sampleRate_(sampleRate),
      seed_(seed), context_(context) {}

void PerturbationStage::process(std::vector<float> &samples, uint16_t numChannels) {
    if (strength_ <= 0.0f || samples.empty() || numChannels == 0)
        return;

    const size_t totalSamples = samples.size();
    const float noiseAmplitude = strength_ * kMaxAmplitude;

    std::vector<float> noise(totalSamples);

    if (mode_ == "phase_distortion" || mode_ == "spectral_gate") {
        generateWhiteNoise(noise);
        if (mode_ == "phase_distortion")
            applyPhaseDistortion(noise, numChannels);
        else
            applySpectralGate(noise, numChannels);
    } else if (mode_ == "pink_noise") {
        applyPinkNoise(noise, numChannels);
    } else {
        generateWhiteNoise(noise);
        applyWhiteNoise(noise, numChannels);
    }

    // 8 kHz HP filter removed for white_noise when the psychoacoustic mask
    // is active -- the mask already bounds energy across all frequencies,
    // including above 8 kHz. Frequencies beyond human hearing are already at
    // near-zero threshold. Retain HP for other modes and for the null-context
    // fallback path (flat white noise without mask shaping).
    if (!(mode_ == "white_noise" && context_ != nullptr && context_->hasMasks())) {
        highPassFilter(noise, numChannels);
    }

    double sumSq = 0.0;
    for (size_t i = 0; i < totalSamples; ++i) {
        float n = noise[i] * noiseAmplitude;
        sumSq += static_cast<double>(n) * static_cast<double>(n);
        samples[i] = std::clamp(samples[i] + n, -1.0f, 1.0f);
    }

    const double rms = std::sqrt(sumSq / static_cast<double>(totalSamples));
    rmsDbfs_ = (rms > 0.0) ? static_cast<float>(20.0 * std::log10(rms)) : kSilentDbfs;
}

void PerturbationStage::applyWhiteNoise(std::vector<float> &noise, uint16_t numChannels) {
    const bool hasMask = context_ != nullptr && context_->hasMasks();
    if (!hasMask)
        return;

    const size_t totalSamples = noise.size();
    const size_t totalFrames = totalSamples / numChannels;
    if (totalFrames < kFrameSize)
        return;

    const std::vector<float> window = window::generate(window::Type::Hann, kFrameSize);

    for (uint16_t ch = 0; ch < numChannels; ++ch) {
        std::vector<float> accum(totalFrames, 0.0f);
        std::vector<float> norm(totalFrames, 0.0f);

        for (size_t start = 0; start + kFrameSize <= totalFrames; start += kHopSize) {
            std::vector<float> frame(kFrameSize);
            for (size_t i = 0; i < kFrameSize; ++i)
                frame[i] = noise[(start + i) * numChannels + ch];

            window::apply(frame, window);

            auto spectrum = fft::transform(frame);

            const size_t maskFrameIndex = start / kHopSize;
            const auto &mask = context_->maskForChannelFrame(ch, maskFrameIndex);

            if (mask.size() == kFrameSize) {
                for (size_t i = 0; i < kFrameSize; ++i) {
                    float mag = std::abs(spectrum[i]);
                    // Gate noise magnitude under the psychoacoustic masking
                    // threshold. mask[bin] is the linear-magnitude threshold
                    // from MaskingThresholdStage.
                    //
                    // No safety margin is applied here for now -- the model is
                    // trusted exactly. Psychoacoustic models are approximations:
                    // the true hearing threshold may be slightly lower than
                    // computed. If faintly audible artifacts appear near the
                    // threshold edge, add a safety multiplier:
                    //   float ceiling = mask[i] * kSafetyMargin;
                    // with kSafetyMargin ~= 0.8 providing ~2 dB of headroom.
                    float ceiling = mask[i];
                    if (mag > ceiling && ceiling >= 0.0f) {
                        float phase = std::arg(spectrum[i]);
                        spectrum[i] = std::complex<float>(ceiling * std::cos(phase),
                                                          ceiling * std::sin(phase));
                    }
                }
            }

            auto reconstructed = fft::inverse(spectrum);
            for (size_t i = 0; i < kFrameSize; ++i) {
                accum[start + i] += reconstructed[i];
                norm[start + i] += 1.0f;
            }
        }

        for (size_t i = 0; i < totalFrames; ++i) {
            if (norm[i] > 0.0f)
                noise[i * numChannels + ch] = accum[i] / norm[i];
        }
    }
}

void PerturbationStage::applyPhaseDistortion(std::vector<float> &noise, uint16_t numChannels) {
    const size_t totalSamples = noise.size();
    const size_t totalFrames = totalSamples / numChannels;
    if (totalFrames < kFrameSize)
        return;

    std::mt19937 rng(static_cast<uint32_t>(seed_ ^ (seed_ >> 32)));
    std::uniform_real_distribution<float> phaseDist(-static_cast<float>(M_PI),
                                                    static_cast<float>(M_PI));

    const std::vector<float> window = window::generate(window::Type::Hann, kFrameSize);

    for (uint16_t ch = 0; ch < numChannels; ++ch) {
        std::vector<float> accum(totalFrames, 0.0f);
        std::vector<float> norm(totalFrames, 0.0f);

        for (size_t start = 0; start + kFrameSize <= totalFrames; start += kHopSize) {
            std::vector<float> frame(kFrameSize);
            for (size_t i = 0; i < kFrameSize; ++i)
                frame[i] = noise[(start + i) * numChannels + ch];

            window::apply(frame, window);

            auto spectrum = fft::transform(frame);
            for (auto &bin : spectrum) {
                float phase = phaseDist(rng);
                float mag = std::abs(bin);
                bin = std::complex<float>(mag * std::cos(phase), mag * std::sin(phase));
            }

            auto reconstructed = fft::inverse(spectrum);
            for (size_t i = 0; i < kFrameSize; ++i) {
                accum[start + i] += reconstructed[i];
                norm[start + i] += 1.0f;
            }
        }

        for (size_t i = 0; i < totalFrames; ++i) {
            if (norm[i] > 0.0f)
                noise[i * numChannels + ch] = accum[i] / norm[i];
        }
    }
}

void PerturbationStage::applySpectralGate(std::vector<float> &noise, uint16_t numChannels) {
    const size_t totalSamples = noise.size();
    const size_t totalFrames = totalSamples / numChannels;
    if (totalFrames < kFrameSize)
        return;

    std::mt19937 rng(static_cast<uint32_t>(seed_ ^ (seed_ >> 32)));
    std::uniform_real_distribution<float> zeroDist(0.0f, 1.0f);

    constexpr float kGateFraction = 0.4f;
    const std::vector<float> window = window::generate(window::Type::Hann, kFrameSize);

    for (uint16_t ch = 0; ch < numChannels; ++ch) {
        std::vector<float> accum(totalFrames, 0.0f);
        std::vector<float> norm(totalFrames, 0.0f);

        for (size_t start = 0; start + kFrameSize <= totalFrames; start += kHopSize) {
            std::vector<float> frame(kFrameSize);
            for (size_t i = 0; i < kFrameSize; ++i)
                frame[i] = noise[(start + i) * numChannels + ch];

            window::apply(frame, window);

            auto spectrum = fft::transform(frame);
            for (auto &bin : spectrum) {
                if (zeroDist(rng) < kGateFraction)
                    bin = 0.0f;
            }

            auto reconstructed = fft::inverse(spectrum);
            for (size_t i = 0; i < kFrameSize; ++i) {
                accum[start + i] += reconstructed[i];
                norm[start + i] += 1.0f;
            }
        }

        for (size_t i = 0; i < totalFrames; ++i) {
            if (norm[i] > 0.0f)
                noise[i * numChannels + ch] = accum[i] / norm[i];
        }
    }
}

void PerturbationStage::applyPinkNoise(std::vector<float> &noise, uint16_t numChannels) {
    const size_t totalSamples = noise.size();
    const size_t frames = totalSamples / numChannels;

    std::mt19937 rng(static_cast<uint32_t>(seed_ ^ (seed_ >> 32)));
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (uint16_t ch = 0; ch < numChannels; ++ch) {
        std::vector<float> octaveSources(kPinkNoiseSources, 0.0f);
        std::vector<size_t> octaveCounters(kPinkNoiseSources, 0);

        for (size_t i = 0; i < frames; ++i) {
            float sample = 0.0f;

            for (size_t oct = 0; oct < kPinkNoiseSources; ++oct) {
                size_t period = size_t(1) << oct;
                if (octaveCounters[oct] % period == 0)
                    octaveSources[oct] = dist(rng);
                sample += octaveSources[oct];
                ++octaveCounters[oct];
            }

            sample /= static_cast<float>(kPinkNoiseSources);
            noise[i * numChannels + ch] = sample;
        }
    }
}

void PerturbationStage::generateWhiteNoise(std::vector<float> &buffer) {
    std::mt19937 rng(static_cast<uint32_t>(seed_ ^ (seed_ >> 32)));
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::generate(buffer.begin(), buffer.end(), [&]() { return dist(rng); });
}

void PerturbationStage::highPassFilter(std::vector<float> &buffer, uint16_t numChannels) {
    const size_t totalSamples = buffer.size();
    const size_t framesCount = totalSamples / static_cast<size_t>(numChannels);
    const float Ts = 1.0f / static_cast<float>(sampleRate_);
    const float tau = 1.0f / (2.0f * static_cast<float>(M_PI) * kCutoffHz);
    const float alpha = tau / (tau + Ts);

    for (uint16_t ch = 0; ch < numChannels; ++ch) {
        float prevIn = 0.0f;
        float prevOut = 0.0f;
        for (size_t frame = 0; frame < framesCount; ++frame) {
            const size_t idx = frame * numChannels + ch;
            const float x = buffer[idx];
            const float y = alpha * (prevOut + x - prevIn);
            prevIn = x;
            prevOut = y;
            buffer[idx] = y;
        }
    }
}
