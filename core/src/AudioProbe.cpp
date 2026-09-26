// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/AudioProbe.h"

#include "Riff.h"
#include "asma/core/Fs.h"

#include <cctype>
#include <memory>
#include <string>

#include <dr_flac.h>
#include <dr_mp3.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace fs = std::filesystem;

namespace asma {

std::optional<AudioFormat> formatFromExtension(const fs::path& path)
{
    std::string ext = toUtf8(path.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".wav" || ext == ".wave") return AudioFormat::Wav;
    if (ext == ".aif" || ext == ".aiff" || ext == ".aifc") return AudioFormat::Aiff;
    if (ext == ".flac") return AudioFormat::Flac;
    if (ext == ".mp3") return AudioFormat::Mp3;
    if (ext == ".ogg") return AudioFormat::Ogg;
    return std::nullopt;
}

std::string_view formatName(AudioFormat format)
{
    switch (format) {
    case AudioFormat::Wav: return "wav";
    case AudioFormat::Aiff: return "aiff";
    case AudioFormat::Flac: return "flac";
    case AudioFormat::Mp3: return "mp3";
    case AudioFormat::Ogg: return "ogg";
    }
    return "unknown";
}

namespace {

void requireAudio(const ProbeResult& r, std::uint64_t frames, const char* what)
{
    if (r.sampleRate <= 0 || r.channels <= 0) throw ProbeError(std::string(what) + " stream has no audio format");
    if (frames == 0) throw ProbeError(std::string(what) + " stream has no audio frames");
}

ProbeResult probeFlac(const fs::path& path, std::uint64_t fileSize)
{
#ifdef _WIN32
    drflac* flac = drflac_open_file_w(path.c_str(), nullptr);
#else
    drflac* flac = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!flac) throw ProbeError("cannot decode FLAC header");
    ProbeResult r;
    r.format = AudioFormat::Flac;
    r.sampleRate = static_cast<int>(flac->sampleRate);
    r.channels = flac->channels;
    r.bitDepth = flac->bitsPerSample;
    const std::uint64_t frames = flac->totalPCMFrameCount;
    drflac_close(flac);
    requireAudio(r, frames, "FLAC");
    r.durationSeconds = static_cast<double>(frames) / r.sampleRate;
    r.hashLength = fileSize;
    return r;
}

ProbeResult probeMp3(const fs::path& path, std::uint64_t fileSize)
{
    auto mp3 = std::make_unique<drmp3>(); // large struct: keep it off worker stacks
#ifdef _WIN32
    const bool opened = drmp3_init_file_w(mp3.get(), path.c_str(), nullptr);
#else
    const bool opened = drmp3_init_file(mp3.get(), path.c_str(), nullptr);
#endif
    if (!opened) throw ProbeError("cannot decode MP3 stream");
    ProbeResult r;
    r.format = AudioFormat::Mp3;
    r.sampleRate = static_cast<int>(mp3->sampleRate);
    r.channels = static_cast<int>(mp3->channels);
    const std::uint64_t frames = drmp3_get_pcm_frame_count(mp3.get());
    drmp3_uninit(mp3.get());
    requireAudio(r, frames, "MP3");
    r.durationSeconds = static_cast<double>(frames) / r.sampleRate;
    r.hashLength = fileSize;
    return r;
}

ProbeResult probeOgg(const fs::path& path, std::uint64_t fileSize)
{
    FilePtr file(openFileRead(path));
    if (!file) throw ProbeError("cannot open file");
    int error = 0;
    // close_handle_on_close = 0: the FilePtr owns the handle on every path.
    stb_vorbis* vorbis = stb_vorbis_open_file(file.get(), 0, &error, nullptr);
    if (!vorbis) throw ProbeError("cannot decode Ogg Vorbis header");
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    const std::uint64_t frames = stb_vorbis_stream_length_in_samples(vorbis);
    stb_vorbis_close(vorbis);
    ProbeResult r;
    r.format = AudioFormat::Ogg;
    r.sampleRate = static_cast<int>(info.sample_rate);
    r.channels = info.channels;
    requireAudio(r, frames, "Ogg Vorbis");
    r.durationSeconds = static_cast<double>(frames) / r.sampleRate;
    r.hashLength = fileSize;
    return r;
}

} // namespace

ProbeResult probeFile(const fs::path& path)
{
    const auto format = formatFromExtension(path);
    if (!format) throw ProbeError("unsupported file extension");

    std::error_code ec;
    const std::uint64_t size = fs::file_size(path, ec);
    if (ec) throw ProbeError("cannot read file size: " + ec.message());

    switch (*format) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: {
        FilePtr file(openFileRead(path));
        if (!file) throw ProbeError("cannot open file");
        return *format == AudioFormat::Wav ? detail::probeWav(file.get(), size)
                                           : detail::probeAiff(file.get(), size);
    }
    case AudioFormat::Flac: return probeFlac(path, size);
    case AudioFormat::Mp3: return probeMp3(path, size);
    case AudioFormat::Ogg: return probeOgg(path, size);
    }
    throw ProbeError("unsupported format");
}

} // namespace asma
