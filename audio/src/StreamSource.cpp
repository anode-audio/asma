// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/StreamSource.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace asma::audio {

StreamSource::StreamSource(std::unique_ptr<AudioReader> reader, double headSeconds)
    : reader_(std::move(reader)),
      sampleRate_(reader_->sampleRate()),
      channels_(reader_->channels()),
      frames_(static_cast<std::int64_t>(reader_->frames()))
{
    const auto headFrames = static_cast<std::int64_t>(std::ceil(headSeconds * sampleRate_));
    headBlocks_ = std::min(blockCount(), (headFrames + kBlockFrames - 1) / kBlockFrames);
    const std::int64_t stride = headBlocks_ * kBlockFrames;
    head_.resize(static_cast<std::size_t>(channels_ * stride));
    for (std::int64_t b = 0; b < headBlocks_; ++b) loadBlock(b, head_.data() + b * kBlockFrames, stride);
    if (blockCount() > headBlocks_) {
        tail_.resize(static_cast<std::size_t>(channels_ * kBlockFrames));
        loadBlock(blockCount() - 1, tail_.data(), kBlockFrames);
    }
    for (auto& slot : slots_) slot.data.resize(static_cast<std::size_t>(channels_ * kBlockFrames));
}

StreamSource::~StreamSource() = default;

void StreamSource::loadBlock(std::int64_t block, float* base, std::int64_t stride)
{
    const std::int64_t start = block * kBlockFrames;
    const std::int64_t n = std::min(kBlockFrames, frames_ - start);
    float* out[2] = {base, base + (channels_ - 1) * stride};
    reader_->seek(static_cast<std::uint64_t>(start));
    const auto got = static_cast<std::int64_t>(reader_->read(out, static_cast<std::uint64_t>(n)));
    for (int c = 0; c < channels_; ++c) std::fill(out[c] + got, out[c] + kBlockFrames, 0.0f);
}

bool StreamSource::copyFrom(std::int64_t block, std::int64_t offset, int n, float* const* out, int at)
{
    const auto copy = [&](const float* base, std::int64_t stride) {
        for (int c = 0; c < channels_; ++c) {
            const float* src = base + c * stride + offset;
            std::copy(src, src + n, out[c] + at);
        }
    };
    if (block < headBlocks_) {
        copy(head_.data() + block * kBlockFrames, headBlocks_ * kBlockFrames);
        return true;
    }
    if (block == blockCount() - 1) {
        copy(tail_.data(), kBlockFrames);
        return true;
    }
    for (auto& slot : slots_) {
        // Announce the read before checking the block: fill() clears the block
        // and then waits for readers to leave before it overwrites the data.
        slot.readers.fetch_add(1);
        const bool here = slot.block.load() == block;
        if (here) copy(slot.data.data(), kBlockFrames);
        slot.readers.fetch_sub(1);
        if (here) return true;
    }
    return false;
}

bool StreamSource::read(std::int64_t start, int n, float* const* out)
{
    bool complete = true;
    int at = 0;
    while (at < n) {
        const std::int64_t pos = start + at;
        if (pos < 0 || pos >= frames_) {
            const int k = pos < 0 ? static_cast<int>(std::min<std::int64_t>(n - at, -pos)) : n - at;
            for (int c = 0; c < channels_; ++c) std::fill(out[c] + at, out[c] + at + k, 0.0f);
            at += k;
            continue;
        }
        const std::int64_t block = pos / kBlockFrames;
        const std::int64_t offset = pos % kBlockFrames;
        const int k = static_cast<int>(std::min({static_cast<std::int64_t>(n - at), kBlockFrames - offset, frames_ - pos}));
        if (!copyFrom(block, offset, k, out, at)) {
            for (int c = 0; c < channels_; ++c) std::fill(out[c] + at, out[c] + at + k, 0.0f);
            complete = false;
        }
        at += k;
    }
    if (!complete) underruns_.fetch_add(1, std::memory_order_relaxed);
    return complete;
}

void StreamSource::hint(std::int64_t frame, int direction)
{
    playhead_.store(frame, std::memory_order_relaxed);
    direction_.store(direction < 0 ? -1 : 1, std::memory_order_relaxed);
}

void StreamSource::region(std::int64_t start, std::int64_t end)
{
    regionStart_.store(start, std::memory_order_relaxed);
    regionEnd_.store(end, std::memory_order_relaxed);
}

bool StreamSource::resident(std::int64_t block) const
{
    return std::any_of(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.block.load() == block; });
}

bool StreamSource::ready(std::int64_t frame) const
{
    if (frame < 0 || frame >= frames_) return true; // silence needs no loading
    const std::int64_t block = frame / kBlockFrames;
    return pinned(block) || resident(block);
}

int StreamSource::fill(int maxBlocks)
{
    const std::int64_t count = blockCount();
    if (failed_.load() || count == 0) return 0;
    const std::int64_t here = std::clamp<std::int64_t>(playhead_.load(std::memory_order_relaxed) / kBlockFrames, 0, count - 1);
    const int dir = direction_.load(std::memory_order_relaxed);

    // The region playback stays in, as blocks.
    const std::int64_t end = regionEnd_.load(std::memory_order_relaxed);
    const std::int64_t first = std::clamp<std::int64_t>(regionStart_.load(std::memory_order_relaxed) / kBlockFrames, 0, count - 1);
    const std::int64_t last = end < 0 || end > frames_ ? count - 1 : std::clamp<std::int64_t>((end - 1) / kBlockFrames, first, count - 1);
    const bool inside = end >= 0 && here >= first && here <= last; // wrap only inside a loop's region

    // The playhead's block, then the far end of the region it wraps to, then
    // what follows in the direction of travel (wrapping inside the region),
    // then one behind (ping-pong turns around). Pinned blocks need no slot.
    std::vector<std::int64_t> wanted{here};
    if (inside) wanted.push_back(dir > 0 ? first : last);
    for (std::int64_t i = 1, b = here; i < kSlots - 1; ++i) {
        b += dir;
        if (inside && b > last) b = first;
        if (inside && b < first) b = last;
        wanted.push_back(b);
    }
    wanted.push_back(here - dir);
    std::vector<std::int64_t> unique;
    for (const std::int64_t b : wanted)
        if (b >= 0 && b < count && !pinned(b) && std::find(unique.begin(), unique.end(), b) == unique.end())
            unique.push_back(b);
    if (unique.size() > static_cast<std::size_t>(kSlots)) unique.resize(kSlots);
    wanted.swap(unique);

    const auto isWanted = [&](std::int64_t b) { return std::find(wanted.begin(), wanted.end(), b) != wanted.end(); };

    int loaded = 0;
    for (const std::int64_t b : wanted) {
        if (loaded >= maxBlocks) break;
        if (resident(b)) continue;
        const auto slot = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) {
            const std::int64_t held = s.block.load();
            return held < 0 || !isWanted(held);
        });
        if (slot == slots_.end()) break;
        slot->block.store(-1);
        while (slot->readers.load() != 0) std::this_thread::yield();
        try {
            loadBlock(b, slot->data.data(), kBlockFrames);
        } catch (const ProbeError&) {
            failed_.store(true);
            return loaded;
        }
        slot->block.store(b);
        ++loaded;
    }
    return loaded;
}

std::shared_ptr<SampleSource> openSource(const std::filesystem::path& path, PreviewCache& cache)
{
    if (auto hit = cache.find(path)) return std::make_shared<MemorySource>(std::move(hit));
    auto reader = AudioReader::open(path);
    if (static_cast<double>(reader->frames()) <= kMaxMemorySeconds * reader->sampleRate())
        return std::make_shared<MemorySource>(cache.load(path, *reader));
    return std::make_shared<StreamSource>(std::move(reader));
}

} // namespace asma::audio
