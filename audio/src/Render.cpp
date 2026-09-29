// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Render.h"

#include "asma/audio/PlayHead.h"
#include "asma/audio/Stretcher.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/ContentHash.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include <dr_wav.h>

namespace fs = std::filesystem;

namespace asma::audio {

namespace {

constexpr int kBlock = 4096;

void writeWav(const fs::path& path, int sampleRate, int channels, const std::vector<float>& interleaved)
{
    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = static_cast<drwav_uint32>(channels);
    format.sampleRate = static_cast<drwav_uint32>(sampleRate);
    format.bitsPerSample = 32;
    drwav wav;
#ifdef _WIN32
    const bool opened = drwav_init_file_write_w(&wav, path.c_str(), &format, nullptr);
#else
    const bool opened = drwav_init_file_write(&wav, path.c_str(), &format, nullptr);
#endif
    if (!opened) throw std::runtime_error("cannot write " + toUtf8(path));
    const auto frames = static_cast<drwav_uint64>(interleaved.size() / static_cast<std::size_t>(channels));
    const drwav_uint64 written = drwav_write_pcm_frames(&wav, frames, interleaved.data());
    drwav_uninit(&wav);
    if (written != frames) throw std::runtime_error("cannot write " + toUtf8(path));
}

} // namespace

void renderToFile(const fs::path& source, const RenderSettings& settings, const fs::path& out)
{
    auto buffer = std::make_shared<const AudioBuffer>(loadAudio(source));
    MemorySource src(buffer);
    const int rate = settings.sampleRate > 0 ? settings.sampleRate : buffer->sampleRate;
    PlayOptions options = toPlayOptions(settings.edits, buffer->sampleRate, false);

    // One pass, exactly: what the trim leaves, there and back for ping-pong,
    // at the output rate, divided by the tempo ratio.
    const std::int64_t a = std::clamp<std::int64_t>(options.trimStart, 0, buffer->frames());
    const std::int64_t b = options.trimEnd < 0 || options.trimEnd > buffer->frames() ? buffer->frames()
                                                                                       : std::max(options.trimEnd, a);
    std::int64_t pass = b - a;
    if (options.direction == Direction::PingPong && pass >= 2) pass = 2 * pass - 1;
    const double ratio = std::clamp(settings.ratio, Stretcher::kMinRatio, Stretcher::kMaxRatio);
    const auto frames = static_cast<std::size_t>(
        std::llround(static_cast<double>(pass) * rate / buffer->sampleRate / ratio));

    PlayHead head;
    head.prepare(rate);
    Stretcher stretcher;
    stretcher.prepare(rate, kBlock);
    stretcher.setTiming(ratio, settings.semitones);
    head.start(src, options);
    stretcher.start(head, true);

    const int channels = buffer->channelCount();
    std::vector<float> interleaved(frames * static_cast<std::size_t>(channels), 0.0f);
    std::vector<float> l(kBlock), r(kBlock);
    float* block[2] = {l.data(), r.data()};
    for (std::size_t done = 0; done < frames && stretcher.active(head); done += kBlock) {
        stretcher.process(head, block, kBlock);
        const std::size_t n = std::min<std::size_t>(kBlock, frames - done);
        for (std::size_t i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c)
                interleaved[(done + i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] = block[c][i];
    }

    std::random_device random;
    const fs::path temp = out.parent_path() / (toUtf8(out.filename()) + ".tmp" + std::to_string(random()));
    try {
        writeWav(temp, rate, channels, interleaved);
        fs::rename(temp, out);
    } catch (...) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw;
    }
}

std::string RenderCache::fileName(std::string_view contentHash, const RenderSettings& s, int sampleRate)
{
    const auto micros = [](double seconds) { return std::to_string(std::llround(seconds * 1e6)); };
    const char direction = s.edits.direction == Direction::Forward ? 'f' : s.edits.direction == Direction::Reverse ? 'r' : 'p';
    return std::string(contentHash) + "-s" + micros(std::max(0.0, s.edits.trimStart)) + "-e"
         + (s.edits.trimEnd < 0.0 ? std::string("end") : micros(s.edits.trimEnd)) + "-" + direction + "-x"
         + std::to_string(std::llround(s.ratio * 1e6)) + "-c" + std::to_string(std::llround(s.semitones * 100.0))
         + "-" + std::to_string(sampleRate) + ".wav";
}

fs::path RenderCache::fileFor(const fs::path& source, const RenderSettings& settings, std::string contentHash)
{
    if (!settings.changesAudio()) return source;
    const ProbeResult probe = probeFile(source);
    if (contentHash.empty()) contentHash = asma::contentHash(source, probe.hashOffset, probe.hashLength);
    const int rate = settings.sampleRate > 0 ? settings.sampleRate : probe.sampleRate;
    const fs::path out = dir_ / fromUtf8(fileName(contentHash, settings, rate));
    std::error_code ec;
    if (fs::exists(out, ec)) {
        fs::last_write_time(out, fs::file_time_type::clock::now(), ec); // used: most recent again
        return out;
    }
    fs::create_directories(dir_);
    renderToFile(source, settings, out);
    evict(out);
    return out;
}

void RenderCache::evict(const fs::path& keep)
{
    struct Entry {
        fs::path path;
        std::uintmax_t size;
        fs::file_time_type time;
    };
    std::vector<Entry> entries;
    std::uintmax_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".wav") continue;
        const auto size = e.file_size(ec);
        if (ec) continue;
        entries.push_back({e.path(), size, e.last_write_time(ec)});
        total += size;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& x, const Entry& y) { return x.time < y.time; });
    for (const Entry& e : entries) {
        if (total <= capacity_) break;
        if (e.path == keep) continue;
        if (fs::remove(e.path, ec)) total -= e.size;
    }
}

} // namespace asma::audio
