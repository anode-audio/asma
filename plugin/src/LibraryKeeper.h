// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"
#include "ScanJob.h"
#include "ScanSchedule.h"
#include "asma/core/FolderWatcher.h"

#include <juce_events/juce_events.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace asma::app {

// Keeps the library in step with its folders while any asma window is open:
// it watches every folder, scans each once when it first sees it, a folder
// the watcher reports, and every folder every 15 minutes, one scan at a time
// through asma-scan. One per process (the app's, or one for every asma window
// in a host), shared through shared(). Across processes the writer lock
// decides: a scan refused by it is skipped and the next change or poll
// catches up. With no asma-scan to run it does nothing. Message thread only,
// but for folderChanged().
class LibraryKeeper : private juce::Timer {
public:
    using Clock = ScanSchedule::Clock;

    LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner);
    ~LibraryKeeper() override;
    LibraryKeeper(const LibraryKeeper&) = delete;
    LibraryKeeper& operator=(const LibraryKeeper&) = delete;

    // The process's keeper for this library, made with a ScanJob running
    // `worker` if there is none yet; it lives while anyone holds it.
    static std::shared_ptr<LibraryKeeper> shared(const std::filesystem::path& dbPath,
                                                 const std::filesystem::path& worker);

    // The standalone's Add folder: adds it to the library and scans it next.
    bool addFolder(const std::filesystem::path& folder, std::string* error = nullptr);
    // What the footer says while a scan runs; empty otherwise.
    std::string progress() const { return runner_->progress(); }
    // The last scan's news ("Scan finished: 3 added", "Scan failed: ..."),
    // counted, so each window shows each once.
    std::uint64_t messageCount() const { return messages_; }
    const std::string& message() const { return message_; }
    // The watcher saw a change; any thread.
    void folderChanged(std::int64_t rootId);
    // One step: the timer gives the time, tests their own.
    void tick(Clock::time_point now);
    ScanRunner& runner() { return *runner_; }

private:
    void timerCallback() override { tick(Clock::now()); }
    void readFolders(Clock::time_point now);
    void finished(const ScanReport& report);

    const std::filesystem::path dbPath_;
    std::unique_ptr<ScanRunner> runner_;
    LibraryView view_;
    ScanSchedule schedule_;
    std::vector<FolderWatcher::Folder> folders_;
    std::map<std::int64_t, std::string> names_; // what progress calls each folder
    std::optional<std::int64_t> scanning_;
    std::string message_;
    std::uint64_t messages_ = 0;
    std::mutex changedMutex_; // guards changed_, filled by the watcher's thread
    std::vector<std::int64_t> changed_;
    std::unique_ptr<FolderWatcher> watcher_; // last: its thread calls folderChanged
};

} // namespace asma::app
