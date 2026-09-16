#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/FFTProcessor.hpp"

static constexpr double PI = 3.14159265358979323846;

namespace {

template <size_t N> double benchFFT(int iters) {
    std::vector<float> signal(N);
    for (size_t i = 0; i < N; ++i)
        signal[i] =
            static_cast<float>(std::sin(2.0 * PI * 440.0 * static_cast<double>(i) / 44100.0));

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int k = 0; k < iters; ++k) {
        auto spec = fft::transform(signal);
        (void)spec;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return ms / static_cast<double>(iters);
}

} // namespace

int main() {
    constexpr int ITERS = 1000;

    std::printf("fft::transform benchmark (%d iterations each)\n", ITERS);
    std::printf("  N=2048 : %.4f ms/call\n", benchFFT<2048>(ITERS));
    std::printf("  N=4096 : %.4f ms/call\n", benchFFT<4096>(ITERS));
    std::printf("  N=8192 : %.4f ms/call\n", benchFFT<8192>(ITERS));
    return 0;
}
