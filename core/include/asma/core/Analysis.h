// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Decode.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace asma {

// Bump when analysis output changes; stored rows with a lower version are
// re-analysed lazily.
constexpr int kAnalysisVersion = 1;

// 13 MFCC means, 13 MFCC standard deviations, log centroid, log rolloff,
// flatness, log onset density, log duration.
constexpr std::size_t kFeatureVectorSize = 31;

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

struct AnalysisResult {
    Loudness loudness;
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    std::optional<std::string> key;
    double keyConfidence = 0.0;
    std::optional<bool> isLoop;
    double centroid = 0.0;     // Hz
    double rolloff = 0.0;      // Hz, 85% of spectral energy
    double flatness = 0.0;     // 0 (tonal) .. 1 (noise)
    double onsetDensity = 0.0; // onsets per second
    std::vector<float> featureVector; // kFeatureVectorSize values
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);
// Free estimate for rhythmic material of unknown length. nullopt for sounds
// under one second or without onsets. Confidence is the normalised
// autocorrelation at the beat; below ~0.4 the tempo is a guess.
std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate);
// nullopt for noisy, atonal or very short sounds.
std::optional<KeyEstimate> estimateKey(const std::vector<float>& mono, int sampleRate);

// Pure and deterministic: the same audio always gives the same result.
AnalysisResult analyse(const DecodedAudio& audio);

} // namespace asma
