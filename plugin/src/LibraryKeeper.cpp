// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryKeeper.h"

#include "asma/core/Fs.h"

namespace asma::app {

namespace {

constexpr int kTickMs = 500;

} // namespace

LibraryKeeper::LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner)
    : dbPath_(std::move(dbPath)), runner_(std::move(runner)), view_(dbPath_)
{
    startTimer(kTickMs);
}

LibraryKeeper::~LibraryKeeper()
{
    stopTimer();
    watcher_.reset(); // no more reports from its thread
}

std::shared_ptr<LibraryKeeper> LibraryKeeper::shared(const std::filesystem::path& dbPath,
                                                      const std::filesystem::path& worker)
{
    static std::map<std::filesystem::path, std::weak_ptr<LibraryKeeper>> keepers; // message thread only
    if (auto existing = keepers[dbPath].lock()) return existing;
    auto made = std::make_shared<LibraryKeeper>(dbPath, std::make_unique<ScanJob>(dbPath, worker));
    keepers[dbPath] = made;
    return made;
}

bool LibraryKeeper::addFolder(const std::filesystem::path& folder, std::string* error)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        if (error) *error = "not a folder";
        return false;
    }
    try {
        Db db = Db::open(dbPath_);
        Library(db).addRoot(folder); // seen at the next tick, and scanned first
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

void LibraryKeeper::folderChanged(std::int64_t rootId)
{
    const std::lock_guard lock(changedMutex_);
    changed_.push_back(rootId);
}

void LibraryKeeper::readFolders(Clock::time_point now)
{
    folders_.clear();
    names_.clear();
    std::vector<std::int64_t> ids;
    for (const auto& r : view_.roots()) {
        if (!r.enabled) continue;
        const auto path = fromUtf8(r.path);
        folders_.push_back({r.id, path});
        names_[r.id] = toUtf8(path.filename());
        ids.push_back(r.id);
    }
    schedule_.setFolders(ids, now);
    watcher_->watch(folders_); // a folder it cannot watch now (unplugged) is polled, and offered again later
}

void LibraryKeeper::finished(const ScanReport& report)
{
    using Result = ScanReport::Result;
    std::string news;
    switch (report.result) {
    case Result::Finished: {
        const auto& s = report.index;
        if (s.added + s.updated + s.relinked + s.missing == 0) break; // nothing to tell
        news = "Scan finished: " + std::to_string(s.added) + " added";
        if (s.missing) news += ", " + std::to_string(s.missing) + " gone";
        break;
    }
    case Result::Failed:
    case Result::Crashed: news = "Scan failed: " + report.message; break;
    case Result::Locked:    // another asma is writing: the next change or poll catches up
    case Result::Cancelled: break;
    }
    if (news.empty()) return;
    message_ = news;
    ++messages_;
}

void LibraryKeeper::tick(Clock::time_point now)
{
    if (!runner_->ready()) return;
    if (!watcher_) {
        watcher_ = std::make_unique<FolderWatcher>([this](std::int64_t id) { folderChanged(id); });
        watcher_->ignore(dbPath_.parent_path());
    }
    view_.refresh();
    if (view_.changed()) readFolders(now);
    {
        const std::lock_guard lock(changedMutex_);
        for (const auto id : changed_) schedule_.changed(id);
        changed_.clear();
    }
    if (scanning_ && !runner_->busy()) {
        if (const auto report = runner_->takeReport()) finished(*report);
        schedule_.scanned(*scanning_, now);
        scanning_.reset();
        watcher_->watch(folders_); // a folder back from an unplugged drive is watched again
    }
    if (!scanning_)
        if (const auto next = schedule_.next(now))
            if (runner_->start(*next, names_[*next])) scanning_ = next;
}

} // namespace asma::app
