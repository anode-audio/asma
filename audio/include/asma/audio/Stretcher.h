// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PlayHead.h"

#include <array>
#include <memory>
#include <vector>

namespace asma::audio {

// Time-stretch and pitch-shift after a PlayHead, with Signalsmith Stretch.
// Bypassed (the PlayHead's output untouched) when a sound starts at ratio 1
// and 0 semitones with bypass allowed. A stretched sound is pre-rolled, so
// its first frame still comes out first rather than one latency late.
//
// prepare() allocates; everything else is safe on the audio thread.
class Stretcher {
public:
    static constexpr double kMinRatio = 0.25;
    static constexpr double kMaxRatio = 4.0;

    Stretcher();
    ~Stretcher();
    Stretcher(const Stretcher&) = delete;
    Stretcher& operator=(const Stretcher&) = delete;

    void prepare(int sampleRate, int maxBlock);

    // ratio: input frames per output frame (2 plays twice as fast), clamped
    // to kMinRatio..kMaxRatio. Takes effect at once, even mid-sound.
    void setTiming(double ratio, double semitones);
    double ratio() const { return ratio_; }
    double semitones() const { return semitones_; }

    // Call right after head.start(). With allowBypass and neutral timing the
    // sound plays unprocessed until the next start.
    void start(PlayHead& head, bool allowBypass);
    // Writes n stereo frames. Returns how many carried sound: n until the
    // head has finished and the stretch has let out its tail.
    int process(PlayHead& head, float* const* out, int n);
    bool active(const PlayHead& head) const { return bypass_ ? head.active() : head.active() || tail_ > 0; }
    bool bypassed() const { return bypass_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int maxBlock_ = 512;
    double ratio_ = 1.0;
    double semitones_ = 0.0;
    bool bypass_ = true;
    double carry_ = 0.0; // fraction of an input frame owed
    int tail_ = 0;       // output frames left after the head finished; -1 while it plays
    std::array<std::vector<float>, 2> input_;
};

} // namespace asma::audio
