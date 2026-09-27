// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <vector>

namespace asma {

struct Loudness {
    double peak = 0.0;   // linear sample peak
    double lufs = -70.0; // integrated; ungated over the whole sound under 400 ms; floor -70
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);

} // namespace asma
