// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/AudioReader.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <cmath>

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr std::uint64_t kChunkFrames = 4096;

float clean(float x) { return std::isfinite(x) ? std::clamp(x, -16.0f, 16.0f) : 0.0f; }

class WavReader final : public AudioReader {
public:
    explicit WavReader(const fs::path& path)
    {
#ifdef _WIN32
        const bool opened = drwav_init_file_w(&wav_, path.c_str(), nullptr);
#else
        const bool opened = drwav_init_file(&wav_, path.c_str(), nullptr);
#endif
        if (!opened) throw ProbeError("cannot decode WAV/AIFF audio");
        sampleRate_ = static_cast<int>(wav_.sampleRate);
        sourceChannels_ = wav_.channels;
        frames_ = wav_.totalPCMFrameCount;
    }
    ~WavReader() override { drwav_uninit(&wav_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return drwav_read_pcm_frames_f32(&wav_, n, out);
    }
    bool seekTo(std::uint64_t frame) override { return drwav_seek_to_pcm_frame(&wav_, frame); }

private:
    drwav wav_{};
};

class FlacReader final : public AudioReader {
public:
    explicit FlacReader(const fs::path& path)
    {
#ifdef _WIN32
        flac_ = drflac_open_file_w(path.c_str(), nullptr);
#else
        flac_ = drflac_open_file(path.c_str(), nullptr);
#endif
        if (!flac_) throw ProbeError("cannot decode FLAC audio");
        sampleRate_ = static_cast<int>(flac_->sampleRate);
        sourceChannels_ = flac_->channels;
        frames_ = flac_->totalPCMFrameCount; // 0 when the stream info does not say
    }
    ~FlacReader() override { drflac_close(flac_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return drflac_read_pcm_frames_f32(flac_, n, out);
    }
    bool seekTo(std::uint64_t frame) override { return drflac_seek_to_pcm_frame(flac_, frame); }

private:
    drflac* flac_ = nullptr;
};

class Mp3Reader final : public AudioReader {
public:
    explicit Mp3Reader(const fs::path& path)
    {
#ifdef _WIN32
        const bool opened = drmp3_init_file_w(&mp3_, path.c_str(), nullptr);
#else
        const bool opened = drmp3_init_file(&mp3_, path.c_str(), nullptr);
#endif
        if (!opened) throw ProbeError("cannot decode MP3 audio");
        sampleRate_ = static_cast<int>(mp3_.sampleRate);
        sourceChannels_ = static_cast<int>(mp3_.channels);
        // Without a seek table every seek decodes from the start, and reverse
        // playback of a long MP3 seeks once per block.
        drmp3_uint32 count = 256;
        seekPoints_.resize(count);
        if (drmp3_calculate_seek_points(&mp3_, &count, seekPoints_.data()) && count > 0) {
            seekPoints_.resize(count);
            drmp3_bind_seek_table(&mp3_, count, seekPoints_.data());
        } else {
            seekPoints_.clear();
        }
        frames_ = drmp3_get_pcm_frame_count(&mp3_);
    }
    ~Mp3Reader() override { drmp3_uninit(&mp3_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return drmp3_read_pcm_frames_f32(&mp3_, n, out);
    }
    bool seekTo(std::uint64_t frame) override { return drmp3_seek_to_pcm_frame(&mp3_, frame); }

private:
    drmp3 mp3_{};
    std::vector<drmp3_seek_point> seekPoints_;
};

class OggReader final : public AudioReader {
public:
    explicit OggReader(const fs::path& path) : file_(openFileRead(path))
    {
        if (!file_) throw FileAccessError("cannot open file");
        int error = 0;
        vorbis_ = stb_vorbis_open_file(file_.get(), 0, &error, nullptr);
        if (!vorbis_) throw ProbeError("cannot decode Ogg Vorbis audio");
        const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        sampleRate_ = static_cast<int>(info.sample_rate);
        sourceChannels_ = info.channels;
        frames_ = stb_vorbis_stream_length_in_samples(vorbis_);
    }
    ~OggReader() override { stb_vorbis_close(vorbis_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return static_cast<std::uint64_t>(stb_vorbis_get_samples_float_interleaved(
            vorbis_, sourceChannels_, out, static_cast<int>(n) * sourceChannels_));
    }
    bool seekTo(std::uint64_t frame) override { return stb_vorbis_seek(vorbis_, static_cast<unsigned>(frame)) != 0; }

private:
    FilePtr file_;
    stb_vorbis* vorbis_ = nullptr;
};

} // namespace

std::unique_ptr<AudioReader> AudioReader::open(const fs::path& path)
{
    std::unique_ptr<AudioReader> reader;
    switch (detectFormat(path)) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: reader = std::make_unique<WavReader>(path); break;
    case AudioFormat::Flac: reader = std::make_unique<FlacReader>(path); break;
    case AudioFormat::Mp3: reader = std::make_unique<Mp3Reader>(path); break;
    case AudioFormat::Ogg: reader = std::make_unique<OggReader>(path); break;
    }
    reader->init();
    return reader;
}

void AudioReader::init()
{
    if (sourceChannels_ <= 0) throw ProbeError("stream has no channels");
    // Rates outside what audio hardware uses come from corrupt headers; below
    // ~90 Hz the analysis frames would have no hop at all.
    if (sampleRate_ < 1000 || sampleRate_ > 768000) throw ProbeError("unsupported sample rate");
    scratch_.resize(static_cast<std::size_t>(kChunkFrames) * static_cast<std::size_t>(sourceChannels_));
    if (frames_ == 0) {
        // Some FLAC encoders leave the length out; count it once.
        std::uint64_t got = 0;
        while ((got = readInterleaved(scratch_.data(), kChunkFrames)) > 0) frames_ += got;
        seek(0);
    }
}

template <typename Emit>
std::uint64_t AudioReader::readChunks(std::uint64_t n, Emit emit)
{
    std::uint64_t done = 0;
    while (done < n && !atEnd_) {
        const std::uint64_t got = readInterleaved(scratch_.data(), std::min(kChunkFrames, n - done));
        if (got == 0) break;
        emit(done, got);
        done += got;
    }
    return done;
}

std::uint64_t AudioReader::read(float* const* out, std::uint64_t n)
{
    const auto source = static_cast<std::size_t>(sourceChannels_);
    const int outChannels = channels();
    return readChunks(n, [&](std::uint64_t at, std::uint64_t got) {
        for (std::uint64_t i = 0; i < got; ++i)
            for (int c = 0; c < outChannels; ++c)
                out[c][at + i] = clean(scratch_[static_cast<std::size_t>(i) * source + static_cast<std::size_t>(c)]);
    });
}

std::uint64_t AudioReader::readMono(float* out, std::uint64_t n)
{
    const auto source = static_cast<std::size_t>(sourceChannels_);
    return readChunks(n, [&](std::uint64_t at, std::uint64_t got) {
        for (std::uint64_t i = 0; i < got; ++i) {
            float sum = 0.0f;
            for (std::size_t c = 0; c < source; ++c) sum += clean(scratch_[static_cast<std::size_t>(i) * source + c]);
            out[at + i] = sum / static_cast<float>(source);
        }
    });
}

void AudioReader::seek(std::uint64_t frame)
{
    // Decoders disagree about seeking to the very end, so the end is ours.
    atEnd_ = frame >= frames_;
    if (!atEnd_ && !seekTo(frame)) throw ProbeError("cannot seek in audio stream");
}

} // namespace asma
