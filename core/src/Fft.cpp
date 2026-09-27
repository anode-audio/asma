// SPDX-License-Identifier: GPL-3.0-only
#include "Fft.h"

#include <cmath>
#include <utility>

namespace asma::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Twiddle factors e^(-2*pi*i*k/n) for k < n/2.
std::vector<std::complex<float>> twiddles(std::size_t n)
{
    std::vector<std::complex<float>> w(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n);
        w[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }
    return w;
}

void transform(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& w)
{
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t stride = n / len;
        for (std::size_t i = 0; i < n; i += len) {
            for (std::size_t k = 0; k < half; ++k) {
                // Written out: std::complex operator* guards NaN/inf and is slow.
                const std::complex<float> tw = w[k * stride];
                const std::complex<float> odd = data[i + k + half];
                const float re = odd.real() * tw.real() - odd.imag() * tw.imag();
                const float im = odd.real() * tw.imag() + odd.imag() * tw.real();
                const std::complex<float> even = data[i + k];
                data[i + k] = {even.real() + re, even.imag() + im};
                data[i + k + half] = {even.real() - re, even.imag() - im};
            }
        }
    }
}

} // namespace

void fft(std::vector<std::complex<float>>& data) { transform(data, twiddles(data.size())); }

std::size_t powerOfTwoAtLeast(double value)
{
    std::size_t n = 1;
    while (static_cast<double>(n) < value) n <<= 1;
    return n;
}

std::vector<std::vector<float>> stft(const std::vector<float>& signal, std::size_t size, std::size_t hop)
{
    std::vector<float> window(size);
    for (std::size_t i = 0; i < size; ++i)
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(size)));
    const auto w = twiddles(size);

    std::vector<std::vector<float>> frames;
    frames.reserve(signal.size() / hop + 1);
    std::vector<std::complex<float>> buffer(size);
    for (std::size_t start = 0; start == 0 || start < signal.size(); start += hop) {
        for (std::size_t i = 0; i < size; ++i) {
            const float x = start + i < signal.size() ? signal[start + i] : 0.0f;
            buffer[i] = {x * window[i], 0.0f};
        }
        transform(buffer, w);
        std::vector<float> magnitudes(size / 2 + 1);
        for (std::size_t k = 0; k < magnitudes.size(); ++k)
            magnitudes[k] = std::sqrt(buffer[k].real() * buffer[k].real() + buffer[k].imag() * buffer[k].imag());
        frames.push_back(std::move(magnitudes));
    }
    return frames;
}

} // namespace asma::detail
