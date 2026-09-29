// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>

namespace asma::audio {

// What the library knows about a sample, as audition needs it. Everything is
// optional: an unscanned file plays unsynced at unity gain from note 60.
struct SampleInfo {
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    std::optional<std::string> key; // canonical, as parseKeyToken: "C", "F#m"
    double keyConfidence = 0.0;
    std::optional<bool> isLoop;
    std::optional<double> lufs; // integrated loudness
    std::optional<double> peak; // linear sample peak
    std::optional<int> rootNote; // MIDI note from a smpl chunk
};

} // namespace asma::audio
