// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleSource.h"

#include <cstdint>
#include <vector>

namespace asma::audio {

// What the UI draws for a file: the lowest and highest sample of each of
// kPoints equal stretches of it, per channel. Point i covers the frames f
// with f * kPoints / frames == i; a file shorter than kPoints frames leaves
// some points empty, at zero.
struct Overview {
    static constexpr int kPoints = 2048;
    std::int64_t frames = 0;
    int sampleRate = 0;
    std::vector<std::vector<float>> min, max; // [channel][point]

    int channels() const { return static_cast<int>(min.size()); }
    double seconds() const { return sampleRate > 0 ? static_cast<double>(frames) / sampleRate : 0.0; }
};

// Builds an Overview from the frames of a file of known length, a chunk at a
// time and in order, so a long file can be summarised as it is read.
class OverviewBuilder {
public:
    OverviewBuilder(std::int64_t frames, int channels, int sampleRate);
    // The next n frames, planar, channels() buffers.
    void add(const float* const* in, std::int64_t n);
    bool done() const { return seen_ >= overview_.frames; }
    const Overview& overview() const { return overview_; }

private:
    Overview overview_;
    std::int64_t seen_ = 0;
    std::vector<bool> touched_; // per point: has a frame landed in it yet
};

Overview makeOverview(const AudioBuffer& buffer);

} // namespace asma::audio
