// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace asma {

struct Loudness {
    double peak = 0.0;   // linear sample peak
    double lufs = -70.0; // integrated; ungated over the whole sound under 400 ms; floor -70
};

struct TempoEstimate {
    double bpm = 0.0;
    double confidence = 0.0; // 0..1
};

struct KeyEstimate {
    std::string key; // canonical, as parseKeyToken
    double confidence = 0.0;
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);
// Free estimate for rhythmic material of unknown length. nullopt for sounds
// under one second or without onsets. Confidence is the normalised
// autocorrelation at the beat; below ~0.4 the tempo is a guess.
std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate);
// nullopt for noisy, atonal or very short sounds.
std::optional<KeyEstimate> estimateKey(const std::vector<float>& mono, int sampleRate);

} // namespace asma
