// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/PlayHead.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

namespace {

// Catmull-Rom through y1 (f = 0) and y2 (f = 1).
float cubic(float y0, float y1, float y2, float y3, float f)
{
    return y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0)));
}

} // namespace

void PlayHead::prepare(int outputRate)
{
    outputRate_ = outputRate;
    for (auto& w : window_) w.assign(kWindowFrames, 0.0f);
}

void PlayHead::start(SampleSource& source, const PlayOptions& options)
{
    const std::int64_t frames = source.frames();
    source_ = &source;
    a_ = std::clamp<std::int64_t>(options.trimStart, 0, frames);
    b_ = options.trimEnd < 0 || options.trimEnd > frames ? frames : std::max(options.trimEnd, a_);
    active_ = b_ > a_;
    if (!active_) return;
    step_ = static_cast<double>(source.sampleRate()) / outputRate_ * std::max(options.speed, 1e-3);
    loop_ = options.loop;
    pingPong_ = options.direction == Direction::PingPong && b_ - a_ >= 2;
    seamless_ = loop_ && options.direction == Direction::Forward && a_ == 0 && b_ == frames;
    dir_ = options.direction == Direction::Reverse ? -1 : 1;
    pos_ = dir_ > 0 ? static_cast<double>(a_) : static_cast<double>(b_ - 1);
    passes_ = 0;
    fadeSource_ = std::max(1.0, kFadeSeconds * outputRate_ * step_);
    beginPass(true);
    // Let a streaming source load where this starts and, for a loop, where it
    // wraps to (-1: no wrap).
    if (loop_ && !pingPong_) source.region(a_, b_);
    else source.region(0, -1);
    source.hint(static_cast<std::int64_t>(pos_), dir_);
}

void PlayHead::beginPass(bool first)
{
    const bool forward = dir_ > 0;
    fadeStart_ = first ? !(forward && a_ == 0) : !(pingPong_ || seamless_);
    if (pingPong_) fadeEnd_ = !loop_ && passes_ == 1; // the backward pass that ends playback
    else if (loop_) fadeEnd_ = !seamless_;
    else fadeEnd_ = !(forward && b_ == source_->frames());
}

void PlayHead::endPass()
{
    ++passes_;
    const auto a = static_cast<double>(a_);
    const auto last = static_cast<double>(b_ - 1);
    if (pingPong_) {
        if (!loop_ && passes_ >= 2) {
            active_ = false;
            return;
        }
        // Mirror around the end sample, so the turn neither repeats nor skips it.
        pos_ = dir_ > 0 ? 2.0 * last - pos_ : 2.0 * a - pos_;
        pos_ = std::clamp(pos_, a, last);
        dir_ = -dir_;
    } else if (loop_) {
        const auto length = static_cast<double>(b_ - a_);
        pos_ = dir_ > 0 ? a + std::fmod(pos_ - a, length) : last - std::fmod(last - pos_, length);
    } else {
        active_ = false;
        return;
    }
    beginPass(false);
}

float PlayHead::fade(double p) const
{
    double gain = 1.0;
    const auto a = static_cast<double>(a_);
    const auto last = static_cast<double>(b_ - 1);
    if (fadeStart_) gain = std::min(gain, ((dir_ > 0 ? p - a : last - p) + 1.0) / fadeSource_);
    if (fadeEnd_) gain = std::min(gain, ((dir_ > 0 ? last - p : p - a) + 1.0) / fadeSource_);
    return static_cast<float>(std::clamp(gain, 0.0, 1.0));
}

int PlayHead::render(float* const* out, int n)
{
    int done = 0;
    const int channels = source_ ? source_->channels() : 1;
    while (done < n && active_) {
        // Output frames left in this pass: positions stay below b_ going
        // forward and above a_ - 1 going backward.
        const double remain = dir_ > 0 ? static_cast<double>(b_) - pos_ : pos_ - static_cast<double>(a_ - 1);
        const int left = std::max(1, static_cast<int>(std::ceil(remain / step_)));
        const int fits = std::max(1, static_cast<int>((kWindowFrames - 4) / step_));
        const int seg = std::min({n - done, left, fits});

        const double end = pos_ + dir_ * step_ * (seg - 1);
        const auto lo = static_cast<std::int64_t>(std::floor(std::min(pos_, end))) - 1;
        const auto len = static_cast<int>(static_cast<std::int64_t>(std::floor(std::max(pos_, end))) + 3 - lo);
        float* window[2] = {window_[0].data(), window_[1].data()};
        source_->hint(static_cast<std::int64_t>(pos_), dir_);
        source_->read(lo, len, window);

        for (int i = 0; i < seg; ++i) {
            const double p = pos_ + dir_ * step_ * i;
            const double whole = std::floor(p);
            const auto f = static_cast<float>(p - whole);
            const auto at = static_cast<std::size_t>(static_cast<std::int64_t>(whole) - lo);
            const float gain = fade(p);
            for (int c = 0; c < channels; ++c) {
                const float* w = window[c];
                out[c][done + i] = gain * cubic(w[at - 1], w[at], w[at + 1], w[at + 2], f);
            }
            if (channels == 1) out[1][done + i] = out[0][done + i];
        }
        pos_ += dir_ * step_ * seg;
        done += seg;
        if (seg == left) endPass();
    }
    for (int c = 0; c < 2; ++c) std::fill(out[c] + done, out[c] + n, 0.0f);
    return done;
}

} // namespace asma::audio
