// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include <ebur128.h>

namespace asma {

// ---- Loudness

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate)
{
    Loudness result;
    if (mono.empty() || sampleRate <= 0) return result;
    ebur128_state* state =
        ebur128_init(1, static_cast<unsigned long>(sampleRate), EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK);
    if (!state) return result;
    const auto durationMs =
        std::max<unsigned long>(1, static_cast<unsigned long>(mono.size() * 1000 / static_cast<std::size_t>(sampleRate)));
    ebur128_add_frames_float(state, mono.data(), mono.size());

    double lufs = -HUGE_VAL;
    ebur128_loudness_global(state, &lufs);
    // Sounds under 400 ms are too short to gate: measure them ungated over
    // their whole length, which the default 400 ms window still holds.
    if (!std::isfinite(lufs) && durationMs <= 400) ebur128_loudness_window(state, durationMs, &lufs);
    double peak = 0.0;
    ebur128_sample_peak(state, 0, &peak);
    ebur128_destroy(&state);

    result.peak = peak;
    result.lufs = std::isfinite(lufs) ? std::max(lufs, -70.0) : -70.0;
    return result;
}

} // namespace asma
