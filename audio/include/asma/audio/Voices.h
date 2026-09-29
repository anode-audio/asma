// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PlayHead.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace asma::audio {

// Plays the selected sample from MIDI notes, pitched like a classic sampler:
// speed changes with pitch, so higher notes are shorter. Eight voices with a
// linear attack and release; each plays the trimmed region once in the chosen
// direction (ping-pong: there and back).
//
// prepare() allocates; everything else is safe on the audio thread.
class VoicePool {
public:
    static constexpr int kVoices = 8;
    static constexpr int kDefaultRootNote = 60;
    static constexpr double kAttackSeconds = 0.002;
    static constexpr double kReleaseSeconds = 0.08;
    static constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

    void prepare(int outputRate, int maxBlock);

    // velocity 0..1. A note already sounding is released and started again
    // on a fresh voice. With all voices busy, a releasing voice is taken
    // first, then the oldest. `generation` is the preview's, so the caller
    // can keep the source alive while the voice uses it.
    void noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                int rootNote);
    void noteOff(int note);
    void releaseAll();
    void kill(); // silence at once

    // Adds every sounding voice into out (two channels).
    void render(float* const* out, int n);

    int active() const;
    // The oldest preview generation a voice still plays, or kNone.
    std::uint64_t oldestGeneration() const;

private:
    enum class Stage { Off, Attack, Hold, Release };
    struct Voice {
        PlayHead head;
        Stage stage = Stage::Off;
        int note = -1;
        float velocity = 1.0f;
        float level = 0.0f;
        std::uint64_t started = 0; // order of note-ons, for stealing
        std::uint64_t generation = 0;
    };

    std::array<Voice, kVoices> voices_;
    std::array<std::vector<float>, 2> scratch_;
    float attackStep_ = 1.0f;
    float releaseStep_ = 1.0f;
    std::uint64_t counter_ = 0;
};

} // namespace asma::audio
