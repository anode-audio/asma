// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma {
class AudioReader;
}

namespace asma::audio {

// A whole file decoded to planar float, one or two channels.
struct AudioBuffer {
    int sampleRate = 0;
    std::vector<std::vector<float>> channels;

    int channelCount() const { return static_cast<int>(channels.size()); }
    std::int64_t frames() const { return channels.empty() ? 0 : static_cast<std::int64_t>(channels[0].size()); }
    std::size_t bytes() const
    {
        return channels.size() * static_cast<std::size_t>(frames()) * sizeof(float);
    }
};

// Decodes every frame of a file, first two channels. Throws ProbeError.
AudioBuffer loadAudio(const std::filesystem::path& path);
// The same, through a reader that has not read anything yet.
AudioBuffer loadAudio(AudioReader& reader);

// What playback reads from. read() and hint() run on the audio thread: they
// never block, allocate or throw.
class SampleSource {
public:
    virtual ~SampleSource() = default;
    virtual int sampleRate() const = 0;
    virtual int channels() const = 0; // 1 or 2
    virtual std::int64_t frames() const = 0;
    // Copies frames [start, start + n) into channels() planar buffers. Frames
    // outside the file, or not loaded yet, come out as zeros; returns false
    // when any frame inside the file was not loaded (an underrun).
    virtual bool read(std::int64_t start, int n, float* const* out) = 0;
    // Where playback is and which way it is moving (+1 or -1), so a
    // streaming source can read ahead.
    virtual void hint(std::int64_t /*frame*/, int /*direction*/) {}
    // The region a loop repeats, [start, end), so a streaming source keeps
    // its far end loaded for the wrap; end -1 means no loop.
    virtual void region(std::int64_t /*start*/, std::int64_t /*end*/) {}
    // Whether the frame can be read now. A streaming source may still be
    // loading it.
    virtual bool ready(std::int64_t /*frame*/) const { return true; }
};

// A decoded file held in memory.
class MemorySource final : public SampleSource {
public:
    explicit MemorySource(std::shared_ptr<const AudioBuffer> buffer) : buffer_(std::move(buffer)) {}
    int sampleRate() const override { return buffer_->sampleRate; }
    int channels() const override { return buffer_->channelCount(); }
    std::int64_t frames() const override { return buffer_->frames(); }
    bool read(std::int64_t start, int n, float* const* out) override;
    const AudioBuffer& buffer() const { return *buffer_; }

private:
    std::shared_ptr<const AudioBuffer> buffer_;
};

} // namespace asma::audio
