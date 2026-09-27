// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Decode.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr std::uint64_t kChunkFrames = 4096;

// Reads interleaved float frames through `read` until it returns 0 or
// maxFrames is reached, appending the channel average to audio.mono.
template <typename Read>
void readFrames(int channels, std::uint64_t maxFrames, DecodedAudio& audio, Read read)
{
    if (channels <= 0) throw ProbeError("stream has no channels");
    std::vector<float> buffer(static_cast<std::size_t>(kChunkFrames) * static_cast<std::size_t>(channels));
    std::uint64_t remaining = maxFrames;
    while (remaining > 0) {
        const std::uint64_t got = read(buffer.data(), std::min(kChunkFrames, remaining));
        if (got == 0) return;
        for (std::uint64_t i = 0; i < got; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += buffer[static_cast<std::size_t>(i * channels + c)];
            audio.mono.push_back(sum / static_cast<float>(channels));
        }
        remaining -= got;
    }
    // Hit the limit: the file is longer if one more frame can be read.
    audio.truncated = read(buffer.data(), 1) > 0;
}

std::uint64_t framesFor(double seconds, int rate)
{
    return static_cast<std::uint64_t>(std::ceil(seconds * rate));
}

DecodedAudio decodeWavOrAiff(const fs::path& path, double maxSeconds)
{
    auto wav = std::make_unique<drwav>();
#ifdef _WIN32
    const bool opened = drwav_init_file_w(wav.get(), path.c_str(), nullptr);
#else
    const bool opened = drwav_init_file(wav.get(), path.c_str(), nullptr);
#endif
    if (!opened) throw ProbeError("cannot decode WAV/AIFF audio");
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(wav->sampleRate);
    try {
        readFrames(wav->channels, framesFor(maxSeconds, audio.sampleRate), audio,
                   [&](float* out, std::uint64_t n) { return drwav_read_pcm_frames_f32(wav.get(), n, out); });
    } catch (...) {
        drwav_uninit(wav.get());
        throw;
    }
    drwav_uninit(wav.get());
    return audio;
}

DecodedAudio decodeFlac(const fs::path& path, double maxSeconds)
{
#ifdef _WIN32
    drflac* flac = drflac_open_file_w(path.c_str(), nullptr);
#else
    drflac* flac = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!flac) throw ProbeError("cannot decode FLAC audio");
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(flac->sampleRate);
    try {
        readFrames(flac->channels, framesFor(maxSeconds, audio.sampleRate), audio,
                   [&](float* out, std::uint64_t n) { return drflac_read_pcm_frames_f32(flac, n, out); });
    } catch (...) {
        drflac_close(flac);
        throw;
    }
    drflac_close(flac);
    return audio;
}

DecodedAudio decodeMp3(const fs::path& path, double maxSeconds)
{
    auto mp3 = std::make_unique<drmp3>();
#ifdef _WIN32
    const bool opened = drmp3_init_file_w(mp3.get(), path.c_str(), nullptr);
#else
    const bool opened = drmp3_init_file(mp3.get(), path.c_str(), nullptr);
#endif
    if (!opened) throw ProbeError("cannot decode MP3 audio");
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(mp3->sampleRate);
    try {
        readFrames(static_cast<int>(mp3->channels), framesFor(maxSeconds, audio.sampleRate), audio,
                   [&](float* out, std::uint64_t n) { return drmp3_read_pcm_frames_f32(mp3.get(), n, out); });
    } catch (...) {
        drmp3_uninit(mp3.get());
        throw;
    }
    drmp3_uninit(mp3.get());
    return audio;
}

DecodedAudio decodeOgg(const fs::path& path, double maxSeconds)
{
    FilePtr file(openFileRead(path));
    if (!file) throw FileAccessError("cannot open file");
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_file(file.get(), 0, &error, nullptr);
    if (!vorbis) throw ProbeError("cannot decode Ogg Vorbis audio");
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(info.sample_rate);
    const int channels = info.channels;
    try {
        readFrames(channels, framesFor(maxSeconds, audio.sampleRate), audio, [&](float* out, std::uint64_t n) {
            return static_cast<std::uint64_t>(
                stb_vorbis_get_samples_float_interleaved(vorbis, channels, out, static_cast<int>(n) * channels));
        });
    } catch (...) {
        stb_vorbis_close(vorbis);
        throw;
    }
    stb_vorbis_close(vorbis);
    return audio;
}

} // namespace

DecodedAudio decodeFile(const fs::path& path, double maxSeconds)
{
    DecodedAudio audio;
    switch (detectFormat(path)) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: audio = decodeWavOrAiff(path, maxSeconds); break;
    case AudioFormat::Flac: audio = decodeFlac(path, maxSeconds); break;
    case AudioFormat::Mp3: audio = decodeMp3(path, maxSeconds); break;
    case AudioFormat::Ogg: audio = decodeOgg(path, maxSeconds); break;
    }
    if (audio.sampleRate <= 0) throw ProbeError("stream has no sample rate");
    if (audio.mono.empty()) throw ProbeError("stream has no audio frames");
    return audio;
}

} // namespace asma
