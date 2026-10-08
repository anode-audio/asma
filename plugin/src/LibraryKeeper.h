// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"
#include "LibraryWriter.h"
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
// catches up. With no asma-scan to run it does not scan.
// It also keeps the library safe, through the asma helper so a plugin never
// writes inside its host: it checks the library when it starts, has a damaged
// one rebuilt (asma repair) at once, and has the user's data backed up once a
// day (asma backup). Message thread only, but for folderChanged().
class LibraryKeeper : private juce::Timer {
public:
    using Clock = ScanSchedule::Clock;

    LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner, std::filesystem::path cli);
    ~LibraryKeeper() override;
    LibraryKeeper(const LibraryKeeper&) = delete;
    LibraryKeeper& operator=(const LibraryKeeper&) = delete;

    // The process's keeper for this library, made with a ScanJob running
    // `worker` if there is none yet; it lives while anyone holds it.
    static std::shared_ptr<LibraryKeeper> shared(const std::filesystem::path& dbPath, const std::filesystem::path& worker,
                                                 const std::filesystem::path& cli);
    // Where the asma helper is; takes effect from the next command.
    void setCli(std::filesystem::path cli) { helper_.setCli(std::move(cli)); }
    // A window's read met damage: rebuild, as for damage the check finds.
    void reportDamage() { damageSuspected_ = true; }
    // A rebuild is under way: every window lets go of the library file.
    bool rebuilding() const { return safety_ == Safety::Repairing; }
    // Nothing being checked, rebuilt or backed up, and no rebuild due.
    bool settled() const
    {
        return safety_ == Safety::Idle && helper_.idle() && !(damageSuspected_ && lastTick_ >= retryAt_);
    }

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
    void keepSafe(Clock::time_point now);
    void startRepair(Clock::time_point now);
    void launchRepair();
    void tell(std::string news);

    const std::filesystem::path dbPath_;
    std::unique_ptr<ScanRunner> runner_;
    LibraryView view_;
    ScanSchedule schedule_;
    std::vector<FolderWatcher::Folder> folders_;
    std::map<std::int64_t, std::string> names_; // what progress calls each folder
    std::optional<std::int64_t> scanning_;
    std::string message_;
    std::uint64_t messages_ = 0;
    enum class Safety { Unchecked, Checking, Repairing, Idle };
    Safety safety_ = Safety::Unchecked;
    Clock::time_point retryAt_{};        // the next rebuild may start then (backing off)
    std::chrono::seconds backoff_{30};
    Clock::time_point lastTick_{};
    bool damageSuspected_ = false;       // until a rebuild, or a check, says otherwise
    bool repairLaunched_ = false;
    int letGoTicks_ = 0;                 // ticks given to the windows to let go of the file
    bool waitTold_ = false;              // "waiting for other windows" said for this damage
    Clock::time_point backupCheckAt_{};  // when to look at the backup's age again
    std::filesystem::path backupPath_;
    CliLane helper_;
    std::mutex changedMutex_; // guards changed_, filled by the watcher's thread
    std::vector<std::int64_t> changed_;
    std::unique_ptr<FolderWatcher> watcher_; // last: its thread calls folderChanged
};

// "7 October" for "2026-10-07T09:00:00Z"; empty when it is not a date.
std::string backupDay(const std::string& writtenAt);

} // namespace asma::app
