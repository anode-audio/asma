// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Stretcher.h"

#include <algorithm>
#include <cmath>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace asma::audio {

struct Stretcher::Impl {
    // A fixed seed: the same input always gives the same output.
    signalsmith::stretch::SignalsmithStretch<float> stretch{12345};
};

Stretcher::Stretcher() : impl_(std::make_unique<Impl>()) {}
Stretcher::~Stretcher() = default;

void Stretcher::prepare(int sampleRate, int maxBlock)
{
    maxBlock_ = maxBlock;
    auto& s = impl_->stretch;
    s.presetDefault(2, static_cast<float>(sampleRate));
    const int seek = s.outputSeekLength(static_cast<float>(kMaxRatio));
    const int frames = std::max(seek, static_cast<int>(std::ceil(maxBlock * kMaxRatio)) + 1);
    for (auto& channel : input_) channel.assign(static_cast<std::size_t>(frames), 0.0f);
    // Run a seek once so its scratch buffers are sized before the audio thread needs them.
    float* in[2] = {input_[0].data(), input_[1].data()};
    s.outputSeek(in, seek);
    s.reset();
}

void Stretcher::setTiming(double ratio, double semitones)
{
    ratio_ = std::clamp(ratio, kMinRatio, kMaxRatio);
    semitones_ = semitones;
    impl_->stretch.setTransposeSemitones(static_cast<float>(semitones));
}

void Stretcher::start(PlayHead& head, bool allowBypass)
{
    bypass_ = allowBypass && ratio_ == 1.0 && semitones_ == 0.0;
    carry_ = 0.0;
    tail_ = -1;
    if (bypass_) return;
    auto& s = impl_->stretch;
    const int seek = s.outputSeekLength(static_cast<float>(ratio_));
    float* in[2] = {input_[0].data(), input_[1].data()};
    head.render(in, seek);
    s.outputSeek(in, seek);
}

int Stretcher::process(PlayHead& head, float* const* out, int n)
{
    if (bypass_) return head.render(out, n);
    auto& s = impl_->stretch;
    int done = 0;
    int sounding = 0;
    float* in[2] = {input_[0].data(), input_[1].data()};
    while (done < n) {
        if (tail_ == 0) {
            for (int c = 0; c < 2; ++c) std::fill(out[c] + done, out[c] + n, 0.0f);
            break;
        }
        const int m = std::min(n - done, maxBlock_);
        const double want = m * ratio_ + carry_;
        const int frames = static_cast<int>(want);
        carry_ = want - frames;
        head.render(in, frames);
        if (!head.active() && tail_ < 0) {
            // What is still inside the stretch comes out over one seek length,
            // and the last analysis block needs one block more to ring out.
            tail_ = static_cast<int>(std::ceil(s.outputSeekLength(static_cast<float>(ratio_)) / ratio_))
                  + 2 * s.blockSamples();
        }
        float* o[2] = {out[0] + done, out[1] + done};
        s.process(in, frames, o, m);
        done += m;
        sounding = done;
        if (tail_ > 0) tail_ = std::max(0, tail_ - m);
    }
    return sounding;
}

} // namespace asma::audio
