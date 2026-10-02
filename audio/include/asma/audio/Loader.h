// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PreviewCache.h"
#include "asma/audio/SampleInfo.h"
#include "asma/audio/SampleSource.h"
#include "asma/audio/SpscQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace asma::audio {

// A selected file, ready to play. Generations count selections from 1.
struct Preview {
    std::uint64_t generation = 0;
    std::shared_ptr<SampleSource> source; // null when the file could not be opened
    SampleInfo info;
    std::string error;     // why source is null
    bool autoplay = false; // start as soon as it arrives
};

// Turns selections into previews off the audio thread, keeps streaming
// sources fed, and frees previews once the audio thread is done with them.
//
// Threads: select() from one control thread; takeReady() and release() from
// the audio thread; pump() from the loader thread that start() runs, or from
// a test that never calls start().
class Loader {
public:
    static constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

    // Opens a file for playing; openSource unless a test says otherwise.
    using Opener = std::function<std::shared_ptr<SampleSource>(const std::filesystem::path&, PreviewCache&)>;

    explicit Loader(PreviewCache& cache, Opener opener = {}) : cache_(cache), opener_(std::move(opener)) {}
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    void start();

    // Asks for a file; a newer selection replaces one not loaded yet.
    // Returns the selection's generation.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info = {}, bool autoplay = false);

    // Audio thread: the newest preview that arrived since the last call, or
    // null. It stays valid while its generation is at or above what the audio
    // thread passes to release().
    Preview* takeReady();
    // Audio thread: the oldest generation it still plays (kNone for none).
    void release(std::uint64_t oldestInUse);

    // Loads the pending selection, hands finished previews to the audio
    // thread, feeds streams and frees what the audio thread let go. Returns
    // whether it did anything. A load that throws, whatever it throws, gives
    // a preview with no source and the reason.
    bool pump();

    // Previews alive, for tests.
    std::size_t liveCount() const;

private:
    struct Request {
        std::uint64_t generation = 0;
        std::filesystem::path path;
        SampleInfo info;
        bool autoplay = false;
    };
    struct Live {
        std::shared_ptr<Preview> preview;
        bool delivered = false;
    };

    // Opens the file and measures what playing it needs. Throws.
    void load(Preview& preview, const std::filesystem::path& path);

    PreviewCache& cache_;
    Opener opener_;
    std::mutex requestMutex_;
    std::condition_variable wake_;
    std::optional<Request> request_;
    std::uint64_t nextGeneration_ = 1;

    std::vector<Live> live_; // pump() only, oldest first
    mutable std::mutex liveMutex_; // guards live_ against liveCount()
    SpscQueue<Preview*, 16> ready_;
    std::atomic<std::uint64_t> seen_{0};         // newest generation takeReady popped
    std::atomic<std::uint64_t> inUseFrom_{1};    // nothing older is in use

    std::thread thread_;
    std::atomic<bool> stop_{false};
};

} // namespace asma::audio
