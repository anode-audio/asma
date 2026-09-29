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
    std::string error; // why source is null
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

    explicit Loader(PreviewCache& cache) : cache_(cache) {}
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    void start();

    // Asks for a file; a newer selection replaces one not loaded yet.
    // Returns the selection's generation.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info = {});

    // Audio thread: the newest preview that arrived since the last call, or
    // null. It stays valid while its generation is at or above what the audio
    // thread passes to release().
    Preview* takeReady();
    // Audio thread: the oldest generation it still plays (kNone for none).
    void release(std::uint64_t oldestInUse);

    // Loads the pending selection, hands finished previews to the audio
    // thread, feeds streams and frees what the audio thread let go. Returns
    // whether it did anything.
    bool pump();

    // Previews alive, for tests.
    std::size_t liveCount() const;

private:
    struct Request {
        std::uint64_t generation = 0;
        std::filesystem::path path;
        SampleInfo info;
    };
    struct Live {
        std::shared_ptr<Preview> preview;
        bool delivered = false;
    };

    PreviewCache& cache_;
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
