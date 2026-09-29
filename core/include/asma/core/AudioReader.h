// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma {

// Reads a file as float frames, in chunks and from any position. One reader
// belongs to one thread at a time. Samples are sanitised as they are read:
// NaN and inf become 0 and the rest is clamped to -16..16, because a broken
// float render would otherwise poison everything downstream.
class AudioReader {
public:
    // The header decides the format, as in probeFile. Throws FileAccessError
    // when the file cannot be read and ProbeError when it cannot be decoded,
    // has no channels or has a sample rate outside 1 kHz..768 kHz.
    static std::unique_ptr<AudioReader> open(const std::filesystem::path& path);

    virtual ~AudioReader() = default;
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    int sampleRate() const { return sampleRate_; }
    // Channels in the file.
    int sourceChannels() const { return sourceChannels_; }
    // Channels read(): 1 or 2. Files with more play their first two.
    int channels() const { return sourceChannels_ < 2 ? 1 : 2; }
    // Exact length in frames, counted at open when the header does not say.
    std::uint64_t frames() const { return frames_; }

    // Reads up to n frames from the current position into channels() planar
    // buffers. Returns the frames read; fewer than n only at the end.
    std::uint64_t read(float* const* out, std::uint64_t n);
    // As read, but the average of every source channel.
    std::uint64_t readMono(float* out, std::uint64_t n);
    // Moves to a frame; past the end clamps to the end. Throws ProbeError.
    void seek(std::uint64_t frame);

protected:
    AudioReader() = default;
    // Called by open() once the subclass has set the fields below.
    void init();

    virtual std::uint64_t readInterleaved(float* out, std::uint64_t n) = 0;
    virtual bool seekTo(std::uint64_t frame) = 0;

    int sampleRate_ = 0;
    int sourceChannels_ = 0;
    std::uint64_t frames_ = 0;

private:
    template <typename Emit>
    std::uint64_t readChunks(std::uint64_t n, Emit emit);

    std::vector<float> scratch_;
    bool atEnd_ = false;
};

} // namespace asma
