// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryKeeper.h"

#include "asma/core/Fs.h"
#include "asma/core/Json.h"


namespace asma::app {

namespace {

constexpr int kTickMs = 500;
constexpr auto kRetryAfter = std::chrono::seconds(30);  // a repair another asma had in hand
constexpr auto kBackupEvery = std::chrono::hours(24);
constexpr auto kBackupLookEvery = std::chrono::minutes(1);

// How long ago the file was written; nothing when it is not there.
std::optional<std::chrono::seconds> age(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(file, ec);
    if (ec) return std::nullopt;
    const auto now = std::filesystem::file_time_type::clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - written);
}

std::string field(const JsonValue& doc, const char* key)
{
    const JsonValue* v = doc.get(key);
    const std::string* s = v ? v->asString() : nullptr;
    return s ? *s : std::string();
}

std::int64_t number(const JsonValue& doc, const char* key)
{
    const JsonValue* v = doc.get(key);
    return v && v->asInt() ? *v->asInt() : 0;
}

} // namespace

LibraryKeeper::LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner, std::filesystem::path cli)
    : dbPath_(std::move(dbPath)), runner_(std::move(runner)), view_(dbPath_),
      backupPath_(dbPath_.parent_path() / "backup.json"), helper_(dbPath_, std::move(cli))
{
    startTimer(kTickMs);
}

LibraryKeeper::~LibraryKeeper()
{
    stopTimer();
    watcher_.reset(); // no more reports from its thread
}

std::shared_ptr<LibraryKeeper> LibraryKeeper::shared(const std::filesystem::path& dbPath,
                                                      const std::filesystem::path& worker,
                                                      const std::filesystem::path& cli)
{
    static std::map<std::filesystem::path, std::weak_ptr<LibraryKeeper>> keepers; // message thread only
    if (auto existing = keepers[dbPath].lock()) return existing;
    auto made = std::make_shared<LibraryKeeper>(dbPath, std::make_unique<ScanJob>(dbPath, worker), cli);
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
    if (!news.empty()) tell(news);
}

void LibraryKeeper::tell(std::string news)
{
    message_ = std::move(news);
    ++messages_;
}

void LibraryKeeper::repair()
{
    safety_ = Safety::Repairing;
    if (scanning_) // a scan of the damaged file is no use
        if (auto* job = dynamic_cast<ScanJob*>(runner_.get())) job->cancel();
    CliLane::Command command;
    command.steps = {{"repair"}};
    command.onLine = [this](const std::string& line) {
        JsonValue doc;
        try {
            doc = parseJson(line);
        } catch (const JsonError&) {
            return;
        }
        const std::string result = field(doc, "result");
        if (result == "repaired") {
            const std::string day = backupDay(field(doc, "written"));
            std::string news = "The library was damaged and has been rebuilt; ";
            news += day.empty() ? "there was no backup to restore your ratings and collections from."
                                : "your ratings and collections were restored from " + day + ".";
            if (const auto lost = number(doc, "unmatched"))
                news += " " + std::to_string(lost) + " organised samples were not found.";
            tell(news);
            schedule_ = ScanSchedule{}; // every folder is new to the new library: scan them all
        } else if (result == "locked" || result == "in_use") {
            retryAt_ = Clock::now() + kRetryAfter; // another asma has it in hand, or holds the file
        }
    };
    command.onEnd = [this](const std::string&) {
        if (safety_ == Safety::Repairing) safety_ = Safety::Idle;
    };
    helper_.run(std::move(command));
}

void LibraryKeeper::keepSafe(Clock::time_point now)
{
    if (safety_ == Safety::Checking || safety_ == Safety::Repairing) return;
    if (safety_ == Safety::Unchecked) {
        safety_ = Safety::Checking;
        CliLane::Command command;
        command.steps = {{"check"}};
        command.onLine = [this](const std::string& line) {
            if (line.find("\"health\":\"damaged\"") != std::string::npos) repair();
        };
        command.onEnd = [this](const std::string&) {
            if (safety_ == Safety::Checking) safety_ = Safety::Idle;
        };
        helper_.run(std::move(command));
        return;
    }
    // Damage found since: the view, reading, met a malformed page.
    if (view_.state() == LibraryState::Damaged && now >= retryAt_) {
        repair();
        return;
    }
    // Once a day, the user's data, beside the library.
    if (now < backupCheckAt_ || view_.state() != LibraryState::Open || !helper_.idle()) return;
    backupCheckAt_ = now + kBackupLookEvery;
    const auto old = age(backupPath_);
    if (old && *old < kBackupEvery) return;
    CliLane::Command command;
    command.steps = {{"backup"}};
    helper_.run(std::move(command));
}

void LibraryKeeper::tick(Clock::time_point now)
{
    view_.refresh();
    keepSafe(now);
    if (safety_ != Safety::Idle) return; // no scanning a library being checked or rebuilt
    if (!runner_->ready()) return;
    if (!watcher_) {
        watcher_ = std::make_unique<FolderWatcher>([this](std::int64_t id) { folderChanged(id); });
        watcher_->ignore(dbPath_.parent_path());
    }
    if (view_.changed() || folders_.empty()) readFolders(now);
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

std::string backupDay(const std::string& writtenAt)
{
    static const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};
    if (writtenAt.size() < 10 || writtenAt[4] != '-' || writtenAt[7] != '-') return {};
    const auto digits = [&](std::size_t at, std::size_t n) {
        int value = 0;
        for (std::size_t i = at; i < at + n; ++i) {
            if (writtenAt[i] < '0' || writtenAt[i] > '9') return -1;
            value = value * 10 + (writtenAt[i] - '0');
        }
        return value;
    };
    const int month = digits(5, 2), day = digits(8, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31) return {};
    return std::to_string(day) + " " + kMonths[month - 1];
}

} // namespace asma::app
