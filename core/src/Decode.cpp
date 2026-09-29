// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Decode.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace asma {

DecodedAudio decodeFile(const std::filesystem::path& path, double maxSeconds)
{
    const auto reader = AudioReader::open(path);
    DecodedAudio audio;
    audio.sampleRate = reader->sampleRate();
    const auto limit = static_cast<std::uint64_t>(std::ceil(maxSeconds * audio.sampleRate));
    // Read until the decoder stops rather than trusting the header's length.
    std::uint64_t have = 0;
    audio.mono.resize(static_cast<std::size_t>(std::min(limit, reader->frames())));
    while (have < limit) {
        if (have == audio.mono.size())
            audio.mono.resize(static_cast<std::size_t>(std::min<std::uint64_t>(limit, have + 65536)));
        const std::uint64_t want = audio.mono.size() - have;
        const std::uint64_t got = reader->readMono(audio.mono.data() + have, want);
        have += got;
        if (got < want) break;
    }
    audio.mono.resize(static_cast<std::size_t>(have));
    // Hit the limit: the file is longer if one more frame can be read.
    float extra = 0.0f;
    audio.truncated = audio.mono.size() == limit && reader->readMono(&extra, 1) > 0;
    if (audio.mono.empty()) throw ProbeError("stream has no audio frames");
    return audio;
}

} // namespace asma
