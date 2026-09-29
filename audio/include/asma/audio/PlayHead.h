// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleSource.h"

#include <array>
#include <cstdint>
#include <vector>

namespace asma::audio {

enum class Direction { Forward, Reverse, PingPong };

struct PlayOptions {
    std::int64_t trimStart = 0; // source frames
    std::int64_t trimEnd = -1;  // exclusive; -1 or past the end means the end of the file
    Direction direction = Direction::Forward;
    bool loop = false;
    double speed = 1.0; // on top of the sample-rate conversion; 2 is an octave up and twice as fast
};

// Reads a region of a source at the output sample rate: trim, direction,
// looping and resampling (cubic, so equal rates at speed 1 copy samples
// exactly). Edges that would click get a 5 ms fade; edges a sound was made to
// have do not: a forward start at frame 0, a one-shot's own end, and the wrap
// of an untrimmed forward loop.
//
// prepare() allocates; everything else is safe on the audio thread.
class PlayHead {
public:
    static constexpr double kFadeSeconds = 0.005;
    static constexpr int kWindowFrames = 8192; // source frames read per step

    void prepare(int outputRate);
    // Does nothing when the region is empty.
    void start(SampleSource& source, const PlayOptions& options);
    void stop() { active_ = false; }
    bool active() const { return active_; }

    // Writes n frames of stereo (mono sources on both sides) and returns how
    // many carried sound; the rest are zeros once the region has played out.
    int render(float* const* out, int n);

    double position() const { return pos_; } // source frames
    int direction() const { return dir_; }    // +1 or -1

private:
    void beginPass(bool first);
    void endPass();
    float fade(double p) const;

    int outputRate_ = 44100;
    std::array<std::vector<float>, 2> window_;

    SampleSource* source_ = nullptr;
    bool active_ = false;
    double pos_ = 0.0;
    double step_ = 1.0;
    int dir_ = 1;
    std::int64_t a_ = 0, b_ = 0; // region [a_, b_)
    bool loop_ = false, pingPong_ = false, seamless_ = false;
    int passes_ = 0;
    bool fadeStart_ = false, fadeEnd_ = false;
    double fadeSource_ = 1.0; // fade length in source frames
};

} // namespace asma::audio
