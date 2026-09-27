// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace asma::detail {

// In-place iterative radix-2 FFT. data.size() must be a power of two.
void fft(std::vector<std::complex<float>>& data);

// Smallest power of two >= value (at least 1).
std::size_t powerOfTwoAtLeast(double value);

// Magnitude spectra (size / 2 + 1 bins) of Hann-windowed frames starting every
// `hop` samples. The final partial frame is zero-padded; a signal shorter than
// one frame yields one frame.
std::vector<std::vector<float>> stft(const std::vector<float>& signal, std::size_t size, std::size_t hop);

} // namespace asma::detail
