// SPDX-License-Identifier: GPL-3.0-only
// An AudioReader over computed audio: any length without a file on disk, and
// every frame recognisable by value.
#pragma once

#include "asma/core/AudioReader.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>

namespace asma::test {

class FakeReader final : public AudioReader {
public:
    FakeReader(std::uint64_t frames, int channels = 1, int sampleRate = 1000)
    {
        sampleRate_ = sampleRate;
        sourceChannels_ = channels;
        frames_ = frames;
        init();
    }

    // Exact in float for frames below 2^24.
    static float value(std::int64_t frame, int channel)
    {
        return static_cast<float>(frame) / 16777216.0f + static_cast<float>(channel);
    }

    // Seeks from now on fail, as when a drive goes away mid-stream.
    std::shared_ptr<std::atomic<bool>> broken = std::make_shared<std::atomic<bool>>(false);
    // Reads from now on throw, as an allocation failure would.
    std::shared_ptr<std::atomic<bool>> throwing = std::make_shared<std::atomic<bool>>(false);

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        if (throwing->load()) throw std::bad_alloc();
        std::uint64_t i = 0;
        for (; i < n && pos_ < frames_; ++i, ++pos_)
            for (int c = 0; c < sourceChannels_; ++c)
                out[i * static_cast<std::uint64_t>(sourceChannels_) + static_cast<std::uint64_t>(c)] =
                    value(static_cast<std::int64_t>(pos_), c);
        return i;
    }
    bool seekTo(std::uint64_t frame) override
    {
        if (broken->load()) return false;
        pos_ = frame;
        return true;
    }

private:
    std::uint64_t pos_ = 0;
};

} // namespace asma::test
