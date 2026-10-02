// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Overview.h"

#include <algorithm>

namespace asma::audio {

OverviewBuilder::OverviewBuilder(std::int64_t frames, int channels, int sampleRate)
{
    overview_.frames = std::max<std::int64_t>(frames, 0);
    overview_.sampleRate = sampleRate;
    const auto points = static_cast<std::size_t>(Overview::kPoints);
    overview_.min.assign(static_cast<std::size_t>(channels), std::vector<float>(points, 0.0f));
    overview_.max.assign(static_cast<std::size_t>(channels), std::vector<float>(points, 0.0f));
    touched_.assign(points, false);
}

void OverviewBuilder::add(const float* const* in, std::int64_t n)
{
    n = std::min(n, overview_.frames - seen_);
    for (std::int64_t i = 0; i < n; ++i) {
        const auto point = static_cast<std::size_t>((seen_ + i) * Overview::kPoints / overview_.frames);
        const bool first = !touched_[point];
        touched_[point] = true;
        for (std::size_t c = 0; c < overview_.min.size(); ++c) {
            const float x = in[c][i];
            float& lo = overview_.min[c][point];
            float& hi = overview_.max[c][point];
            lo = first ? x : std::min(lo, x);
            hi = first ? x : std::max(hi, x);
        }
    }
    seen_ += std::max<std::int64_t>(n, 0);
}

Overview makeOverview(const AudioBuffer& buffer)
{
    OverviewBuilder builder(buffer.frames(), buffer.channelCount(), buffer.sampleRate);
    std::vector<const float*> in;
    for (const auto& c : buffer.channels) in.push_back(c.data());
    builder.add(in.data(), buffer.frames());
    return builder.overview();
}

} // namespace asma::audio
