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

void Loader::load(Preview& preview, const std::filesystem::path& path)
{
    preview.source = opener_ ? opener_(path, cache_) : openSource(path, cache_);
    // Not analysed yet: a short file is cheap to measure for gain matching.
    if (const auto* memory = dynamic_cast<const MemorySource*>(preview.source.get()); memory && !preview.info.lufs) {
        const AudioBuffer& b = memory->buffer();
        std::vector<float> mono(b.channels[0]);
        if (b.channelCount() == 2)
            for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + b.channels[1][i]);
        const Loudness l = measureLoudness(mono, b.sampleRate);
        preview.info.lufs = l.lufs;
        preview.info.peak = l.peak;
    }
    if (const auto* memory = dynamic_cast<const MemorySource*>(preview.source.get()))
        publishOverview(preview.generation, makeOverview(memory->buffer()));
    else if (preview.source)
        overviewJob_ = OverviewJob{preview.generation, path, nullptr, std::nullopt};
}

bool Loader::stepOverview(std::uint64_t newest)
{
    if (!overviewJob_) return false;
    if (overviewJob_->generation != newest) { // the selection moved on
        overviewJob_.reset();
        return true;
    }
    // A failure here only costs the picture: the preview plays on.
    try {
        OverviewJob& job = *overviewJob_;
        if (!job.reader) {
            job.reader = AudioReader::open(job.path);
            job.builder.emplace(static_cast<std::int64_t>(job.reader->frames()), job.reader->channels(),
                                job.reader->sampleRate());
            overviewScratch_.assign(static_cast<std::size_t>(job.reader->channels()), std::vector<float>(kOverviewChunk));
        }
        std::vector<float*> out;
        for (auto& c : overviewScratch_) out.push_back(c.data());
        const std::uint64_t got = job.reader->read(out.data(), kOverviewChunk);
        std::vector<const float*> in(out.begin(), out.end());
        job.builder->add(in.data(), static_cast<std::int64_t>(got));
        if (job.builder->done() || got == 0) {
            publishOverview(job.generation, job.builder->overview());
            overviewJob_.reset();
        }
    } catch (...) {
        overviewJob_.reset();
    }
    return true;
}

void Loader::publishOverview(std::uint64_t generation, Overview overview)
{
    auto shared = std::make_shared<const Overview>(std::move(overview));
    const std::lock_guard lock(overviewMutex_);
    overviewGeneration_ = generation;
    overview_ = std::move(shared);
}

std::shared_ptr<const Overview> Loader::overview(std::uint64_t generation) const
{
    const std::lock_guard lock(overviewMutex_);
    return generation == overviewGeneration_ ? overview_ : nullptr;
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
        overviewJob_.reset(); // a new selection: the old read-through is moot
        // Nothing a load throws may escape: it would end the loader thread,
        // and the whole host with it.
        try {
            load(*preview, request->path);
        } catch (const std::exception& e) {
            preview->source.reset();
            preview->error = e.what();
        } catch (...) {
            preview->source.reset();
            preview->error = "unknown error";
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
    // After the streams: playing comes before drawing.
    did |= stepOverview(newest ? newest->generation : 0);
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
