// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Loader.h"

#include "asma/audio/StreamSource.h"
#include "asma/core/Analysis.h"
#include "asma/core/AudioProbe.h"

#include <algorithm>
#include <chrono>

namespace asma::audio {

Loader::~Loader()
{
    if (thread_.joinable()) {
        {
            const std::lock_guard lock(requestMutex_);
            stop_.store(true);
        }
        wake_.notify_all();
        thread_.join();
    }
}

void Loader::start()
{
    thread_ = std::thread([this] {
        while (!stop_.load()) {
            if (pump()) continue;
            // Idle: wake for a selection, or after a few ms to feed streams.
            std::unique_lock lock(requestMutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(5), [this] { return stop_.load() || request_.has_value(); });
        }
    });
}

std::uint64_t Loader::select(const std::filesystem::path& path, SampleInfo info, bool autoplay)
{
    std::uint64_t generation = 0;
    {
        const std::lock_guard lock(requestMutex_);
        generation = nextGeneration_++;
        request_ = Request{generation, path, std::move(info), autoplay};
    }
    wake_.notify_one();
    return generation;
}

Preview* Loader::takeReady()
{
    Preview* newest = nullptr;
    Preview* next = nullptr;
    while (ready_.pop(next)) newest = next;
    if (newest) seen_.store(newest->generation);
    return newest;
}

void Loader::release(std::uint64_t oldestInUse)
{
    // Never above one past the newest preview taken: a preview still in the
    // queue, or taken but not yet reported, must look in use.
    inUseFrom_.store(std::min(oldestInUse, seen_.load() + 1));
}

bool Loader::pump()
{
    bool did = false;

    std::optional<Request> request;
    {
        const std::lock_guard lock(requestMutex_);
        request.swap(request_);
    }
    if (request) {
        auto preview = std::make_shared<Preview>();
        preview->generation = request->generation;
        preview->info = std::move(request->info);
        preview->autoplay = request->autoplay;
        try {
            preview->source = openSource(request->path, cache_);
        } catch (const ProbeError& e) {
            preview->error = e.what();
        }
        // Not analysed yet: a short file is cheap to measure for gain matching.
        if (const auto* memory = dynamic_cast<const MemorySource*>(preview->source.get()); memory && !preview->info.lufs) {
            const AudioBuffer& b = memory->buffer();
            std::vector<float> mono(b.channels[0]);
            if (b.channelCount() == 2)
                for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + b.channels[1][i]);
            const Loudness l = measureLoudness(mono, b.sampleRate);
            preview->info.lufs = l.lufs;
            preview->info.peak = l.peak;
        }
        const std::lock_guard lock(liveMutex_);
        live_.push_back({std::move(preview), false});
        did = true;
    }

    const std::lock_guard lock(liveMutex_);
    if (!live_.empty() && !live_.back().delivered && ready_.push(live_.back().preview.get())) {
        live_.back().delivered = true;
        did = true;
    }

    const std::uint64_t from = inUseFrom_.load();
    const std::uint64_t seen = seen_.load();
    for (const Live& l : live_)
        if (!l.delivered || l.preview->generation >= from)
            if (auto* stream = dynamic_cast<StreamSource*>(l.preview->source.get())) did |= stream->fill() > 0;

    const Preview* newest = live_.empty() ? nullptr : live_.back().preview.get();
    const auto before = live_.size();
    std::erase_if(live_, [&](const Live& l) {
        if (!l.delivered) return l.preview.get() != newest; // superseded before the audio thread saw it
        return l.preview->generation <= seen && l.preview->generation < from;
    });
    return did || live_.size() != before;
}

std::size_t Loader::liveCount() const
{
    const std::lock_guard lock(liveMutex_);
    return live_.size();
}

} // namespace asma::audio
