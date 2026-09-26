// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace asma {

enum class AudioFormat { Wav, Aiff, Flac, Mp3, Ogg };

// Case-insensitive: .wav .wave .aif .aiff .aifc .flac .mp3 .ogg
std::optional<AudioFormat> formatFromExtension(const std::filesystem::path& path);

// "wav", "aiff", "flac", "mp3" or "ogg".
std::string_view formatName(AudioFormat format);

class ProbeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AcidInfo {
    bool oneShot = false;
    float tempo = 0.0f; // 0 when the chunk carries none
};

struct ProbeResult {
    AudioFormat format = AudioFormat::Wav;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0; // 0 for lossy formats
    double durationSeconds = 0.0;
    // Byte range hashed for the content hash: the audio payload for WAV and
    // AIFF (so metadata edits keep the hash), the whole file otherwise.
    std::uint64_t hashOffset = 0;
    std::uint64_t hashLength = 0;
    std::optional<AcidInfo> acid;
    std::optional<int> smplUnityNote; // MIDI note, 60 = C4
};

// Reads headers only (MP3 also needs a frame scan). Throws ProbeError for
// unreadable, unsupported or malformed files.
ProbeResult probeFile(const std::filesystem::path& path);

} // namespace asma
