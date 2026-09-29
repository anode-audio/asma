// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/SampleSource.h"

#include "asma/core/AudioReader.h"

#include <algorithm>

namespace asma::audio {

AudioBuffer loadAudio(const std::filesystem::path& path) { return loadAudio(*AudioReader::open(path)); }

AudioBuffer loadAudio(AudioReader& reader)
{
    AudioBuffer buffer;
    buffer.sampleRate = reader.sampleRate();
    buffer.channels.assign(static_cast<std::size_t>(reader.channels()),
                           std::vector<float>(static_cast<std::size_t>(reader.frames())));
    float* out[2] = {buffer.channels[0].data(), buffer.channels.back().data()};
    const std::uint64_t got = reader.read(out, reader.frames());
    for (auto& channel : buffer.channels) channel.resize(static_cast<std::size_t>(got));
    return buffer;
}

bool MemorySource::read(std::int64_t start, int n, float* const* out)
{
    const std::int64_t from = std::max<std::int64_t>(start, 0);
    const std::int64_t to = std::min<std::int64_t>(start + n, buffer_->frames());
    for (int c = 0; c < channels(); ++c) {
        float* dst = out[c];
        std::fill(dst, dst + n, 0.0f);
        const float* src = buffer_->channels[static_cast<std::size_t>(c)].data();
        if (to > from) std::copy(src + from, src + to, dst + (from - start));
    }
    return true;
}

} // namespace asma::audio
