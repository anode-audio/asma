// SPDX-License-Identifier: GPL-3.0-only
#include "FileOpsJob.h"

#include "asma/core/Folders.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <juce_events/juce_events.h>

#include <optional>

namespace asma::app {

namespace {

constexpr auto kLockPoll = std::chrono::milliseconds(100);

std::string what(std::size_t count, const std::string& name)
{
    return count == 1 ? name : std::to_string(count) + " samples";
}

} // namespace

FileRequest FileRequest::rename(std::int64_t file, std::string name)
{
    FileRequest r;
    r.kind = Kind::Rename;
    r.files = {file};
    r.name = std::move(name);
    return r;
}

FileRequest FileRequest::move(std::vector<std::int64_t> files, std::filesystem::path folder)
{
    FileRequest r;
    r.kind = Kind::Move;
    r.files = std::move(files);
    r.folder = std::move(folder);
    return r;
}

FileRequest FileRequest::trash(std::vector<std::int64_t> files)
{
    FileRequest r;
    r.kind = Kind::Trash;
    r.files = std::move(files);
    return r;
}

FileRequest FileRequest::removeFolder(std::int64_t rootId)
{
    FileRequest r;
    r.kind = Kind::RemoveFolder;
    r.rootId = rootId;
    return r;
}

FileRequest FileRequest::undo() { return FileRequest{}; }

FileRequest FileRequest::addFolder(std::filesystem::path folder, bool merge)
{
    FileRequest r;
    r.kind = Kind::AddFolder;
    r.folder = std::move(folder);
    r.merge = merge;
    return r;
}

std::string FileOpsJob::undoKey()
{
#if JUCE_MAC
    return "Cmd+Z";
#else
    return "Ctrl+Z";
#endif
}

std::string doneText(const FileRequest& request, const OpResult& r)
{
    const std::string undo = " " + FileOpsJob::undoKey() + " to undo.";
    switch (r.op) {
    case Operation::Rename: return "Renamed " + r.name + " to " + request.name + "." + undo;
    case Operation::Move: return "Moved " + what(r.count, r.name) + " to " + toUtf8(request.folder.filename()) + "." + undo;
    case Operation::Trash: return "Moved " + what(r.count, r.name) + " to the Trash." + undo;
    case Operation::RemoveFolder: return "Removed " + r.name + " from the library." + undo;
    }
    return {};
}

FileOpsJob::FileOpsJob(std::filesystem::path dbPath, TrashBackend trash, std::chrono::milliseconds lockWait)
    : dbPath_(std::move(dbPath)), trash_(std::move(trash)), lockWait_(lockWait)
{
    FileRequest startup;
    startup.kind = FileRequest::Kind::Startup;
    queue_.push_back({startup, {}});
    thread_ = std::thread([this] { loop(); });
}

FileOpsJob::~FileOpsJob()
{
    alive_->store(false);
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    thread_.join();
}

void FileOpsJob::run(FileRequest request, Done done)
{
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back({std::move(request), std::move(done)});
    }
    wake_.notify_all();
}

bool FileOpsJob::idle() const
{
    const std::lock_guard lock(mutex_);
    return queue_.empty() && !running_;
}

std::string FileOpsJob::undoLabel() const
{
    const std::lock_guard lock(mutex_);
    return undoLabel_;
}

void FileOpsJob::loop()
{
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            running_ = true;
        }
        const FileOutcome outcome = carryOut(job.request);
        juce::MessageManager::callAsync([this, alive = alive_, done = std::move(job.done), outcome] {
            if (!alive->load()) return;
            if (!outcome.text.empty()) {
                message_ = outcome.text;
                ++messages_;
            }
            if (done) done(outcome);
        });
        // Idle only once the outcome is on its way, so a test that waits for
        // idle and then runs the message loop sees it.
        const std::lock_guard lock(mutex_);
        running_ = false;
    }
}

FileOutcome FileOpsJob::carryOut(const FileRequest& request)
{
    using Kind = FileRequest::Kind;
    FileOutcome outcome;
    std::optional<WriterLock> lock;
    const auto giveUp = std::chrono::steady_clock::now() + lockWait_;
    while (!(lock = WriterLock::tryAcquire(dbPath_.parent_path()))) {
        if (std::chrono::steady_clock::now() >= giveUp) {
            if (request.kind != Kind::Startup) outcome.text = "The library is busy scanning; try again in a moment.";
            return outcome;
        }
        std::this_thread::sleep_for(kLockPoll);
    }
    // Every request first rolls back what an interrupted asma left and
    // merges nested folders, which the first one does unless a long scan kept
    // the lock from it; both cost nothing when there is nothing to do.
    std::string said;
    const auto say = [&](const std::string& text) {
        if (!text.empty()) said += (said.empty() ? "" : " ") + text;
    };
    try {
        Db db = Db::open(dbPath_);
        FileOps ops(db, trash_);
        for (const auto& r : ops.recover()) say(interruptedText(r));
        for (const auto& m : mergeNestedFolders(db)) say(m);
        switch (request.kind) {
        case Kind::Startup: break;
        case Kind::Undo: {
            const auto r = ops.undo();
            outcome.text = r ? undoneText(*r) : "Nothing to undo.";
            if (r) outcome.files = r->files;
            break;
        }
        case Kind::AddFolder: {
            const AddCheck check = checkAddFolder(db, request.folder);
            addFolder(db, request.folder, request.merge);
            if (check.result == AddCheck::Result::Contains) {
                std::string names;
                for (std::size_t i = 0; i < check.contained.size(); ++i) {
                    const std::string n = toUtf8(fromUtf8(check.contained[i].path).filename());
                    names += (i == 0 ? "" : i + 1 == check.contained.size() ? " and " : ", ") + n;
                }
                outcome.text = "Added " + toUtf8(request.folder.filename()) + " in place of " + names + ".";
            }
            break;
        }
        case Kind::Rename:
        case Kind::Move:
        case Kind::Trash:
        case Kind::RemoveFolder: {
            OpResult r;
            if (request.kind == Kind::Rename) r = ops.rename(request.files.at(0), request.name);
            else if (request.kind == Kind::Move) r = ops.move(request.files, request.folder);
            else if (request.kind == Kind::Trash) r = ops.trash(request.files);
            else r = ops.removeFolder(request.rootId);
            outcome.text = doneText(request, r);
            outcome.files = r.files;
            break;
        }
        }
        outcome.done = true;
        say(outcome.text);
        outcome.text = said;
        const auto next = ops.undoable();
        const std::lock_guard guard(mutex_);
        undoLabel_ = next ? next->label : std::string();
    } catch (const OperationRefused& e) {
        say(e.what());
        outcome.text = said;
    } catch (const std::exception& e) {
        say(std::string("Could not do that: ") + e.what());
        outcome.text = said;
    }
    return outcome;
}

} // namespace asma::app
