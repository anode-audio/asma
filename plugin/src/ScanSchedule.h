// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace asma::app {

// Which folder to scan when: each once when first seen (at startup, or added
// elsewhere), a folder the watcher reports as soon as it can, and every
// folder every 15 minutes, for what change notices miss. A folder is queued at
// most once; a change while it is being scanned queues it again, since the
// scan may have passed it. JUCE-free; the caller gives the time.
class ScanSchedule {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::minutes kPollEvery{15};

    // The library's folders now. New ones are due at once; gone ones are
    // forgotten, queued or not.
    void setFolders(const std::vector<std::int64_t>& ids, Clock::time_point now);
    void changed(std::int64_t id);
    // The next folder to scan, if one is due; it leaves the queue.
    std::optional<std::int64_t> next(Clock::time_point now);
    // A scan of the folder ended, however (a scan refused by the writer lock
    // counts: the next change or poll catches up).
    void scanned(std::int64_t id, Clock::time_point now);

private:
    std::map<std::int64_t, Clock::time_point> pollAt_; // the folders, and when each is next polled
    std::deque<std::int64_t> queue_;
};

} // namespace asma::app
