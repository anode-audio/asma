// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PreviewCache.h"
#include "asma/audio/SampleSource.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma {
class AudioReader;
}

namespace asma::audio {

// Files up to this long are decoded whole; longer ones stream.
constexpr double kMaxMemorySeconds = 10.0;

// A long file read in blocks. The first kMaxMemorySeconds and the last block
// are loaded at open and stay, so playback starts at once in either direction;
// the rest is read ahead of the playhead, in the direction of travel, by
// fill() on a loader thread. read() on the audio thread never waits: a block
// that is not loaded yet plays as silence.
class StreamSource final : public SampleSource {
public:
    static constexpr std::int64_t kBlockFrames = 32768;
    static constexpr int kSlots = 16; // rolling blocks: about 12 s at 44.1 kHz

    // Loads the head and the tail block. Throws ProbeError.
    explicit StreamSource(std::unique_ptr<AudioReader> reader, double headSeconds = kMaxMemorySeconds);
    ~StreamSource() override;

    int sampleRate() const override { return sampleRate_; }
    int channels() const override { return channels_; }
    std::int64_t frames() const override { return frames_; }
    bool read(std::int64_t start, int n, float* const* out) override;
    void hint(std::int64_t frame, int direction) override;
    void region(std::int64_t start, std::int64_t end) override;
    bool ready(std::int64_t frame) const override;

    // Loader thread: loads up to maxBlocks missing blocks around the playhead,
    // nearest first. Returns how many it loaded. A read error ends streaming:
    // failed() turns true and the missing blocks stay silent.
    int fill(int maxBlocks = 4);
    bool failed() const { return failed_.load(); }
    // Blocks read() found missing, for tests and diagnostics.
    std::int64_t underruns() const { return underruns_.load(); }

private:
    struct Slot {
        std::atomic<std::int64_t> block{-1};
        std::atomic<int> readers{0};
        std::vector<float> data; // channels_ * kBlockFrames, planar
    };

    std::int64_t blockCount() const { return (frames_ + kBlockFrames - 1) / kBlockFrames; }
    bool pinned(std::int64_t block) const { return block < headBlocks_ || block == blockCount() - 1; }
    bool resident(std::int64_t block) const;
    // Reads one block into planar channels `stride` floats apart.
    void loadBlock(std::int64_t block, float* base, std::int64_t stride);
    // Copies part of one block into out; false when the block is not loaded.
    bool copyFrom(std::int64_t block, std::int64_t offset, int n, float* const* out, int at);

    std::unique_ptr<AudioReader> reader_; // loader thread only after construction
    int sampleRate_ = 0;
    int channels_ = 0;
    std::int64_t frames_ = 0;
    std::int64_t headBlocks_ = 0;
    std::vector<float> head_; // planar, headBlocks_ * kBlockFrames per channel
    std::vector<float> tail_; // planar, kBlockFrames per channel
    std::array<Slot, kSlots> slots_;
    std::atomic<std::int64_t> playhead_{0};
    std::atomic<int> direction_{1};
    std::atomic<std::int64_t> regionStart_{0};
    std::atomic<std::int64_t> regionEnd_{-1}; // -1: not looping
    std::atomic<bool> failed_{false};
    std::atomic<std::int64_t> underruns_{0};
};

// A file ready to play: from the cache or decoded whole when it is at most
// kMaxMemorySeconds long, streamed otherwise. Throws ProbeError.
std::shared_ptr<SampleSource> openSource(const std::filesystem::path& path, PreviewCache& cache);

} // namespace asma::audio
