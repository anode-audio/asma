// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/FolderWatcher.h"

#include "asma/core/Fs.h"

#include <efsw/efsw.hpp>

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace asma {

namespace {

// True when any part of `relative` starts with a dot.
bool hidden(const fs::path& relative)
{
    for (const auto& part : relative) {
        const std::string name = toUtf8(part);
        if (!name.empty() && name.front() == '.' && name != "." && name != "..") return true;
    }
    return false;
}

// The path as the operating system reports it (macOS gives /private/var
// for /var), so reported changes compare with it.
fs::path real(const fs::path& path)
{
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

bool under(const fs::path& path, const fs::path& dir)
{
    const auto rel = path.lexically_relative(dir);
    return !rel.empty() && *rel.begin() != "..";
}

} // namespace

struct FolderWatcher::Impl final : efsw::FileWatchListener {
    Changed onChanged;
    std::chrono::milliseconds quiet;

    std::mutex mutex; // guards what follows; never held while calling into efsw
    std::condition_variable wake;
    struct Watched {
        efsw::WatchID watch = 0;
        fs::path path;
    };
    std::map<std::int64_t, Watched> folders;            // by root id
    std::map<efsw::WatchID, std::int64_t> byWatch;      // efsw's id to ours
    std::map<std::int64_t, std::chrono::steady_clock::time_point> pending; // last change, not yet reported
    std::vector<fs::path> ignored;
    bool stopping = false;
    std::thread reporter;
    std::optional<std::vector<Folder>> wanted; // the last watch(), not yet carried out
    bool applying = false;
    std::vector<std::int64_t> failedIds;
    std::condition_variable wantedWake;
    std::thread applier;
    // Last, so it goes first: its thread calls handleFileAction, which uses
    // everything above.
    efsw::FileWatcher watcher;

    Impl(Changed changed, std::chrono::milliseconds q) : onChanged(std::move(changed)), quiet(q)
    {
        watcher.watch(); // efsw's own thread delivers handleFileAction
        reporter = std::thread([this] { report(); });
        applier = std::thread([this] { apply(); });
    }

    ~Impl() override
    {
        std::vector<efsw::WatchID> watches;
        {
            const std::lock_guard lock(mutex);
            stopping = true;
            for (const auto& [id, w] : folders) watches.push_back(w.watch);
            byWatch.clear();
        }
        wake.notify_all();
        wantedWake.notify_all();
        reporter.join();
        applier.join();
        {
            const std::lock_guard lock(mutex); // what the applier added meanwhile
            watches.clear();
            for (const auto& [id, w] : folders) watches.push_back(w.watch);
        }
        for (const auto w : watches) watcher.removeWatch(w);
    }

    // Carries out each watch() in turn, on this thread.
    void apply()
    {
        std::unique_lock lock(mutex);
        while (!stopping) {
            if (!wanted) {
                wantedWake.wait(lock);
                continue;
            }
            std::vector<Folder> next = std::move(*wanted);
            wanted.reset();
            applying = true;
            lock.unlock();
            std::vector<std::int64_t> notWatched = change(next);
            lock.lock();
            failedIds = std::move(notWatched);
            applying = false;
        }
    }

    std::vector<std::int64_t> change(const std::vector<Folder>& asked);

    void handleFileAction(efsw::WatchID watchId, const std::string& dir, const std::string& filename,
                          efsw::Action action, const std::string&) override
    {
        const fs::path changed = fromUtf8(dir) / fromUtf8(filename);
        // A folder is "modified" whenever anything inside it changes, hidden
        // files too; what changed inside has its own notice. A folder added,
        // deleted or moved still counts: its samples come or go with it.
        std::error_code ec;
        if (action == efsw::Actions::Modified && fs::is_directory(changed, ec)) return;
        const std::lock_guard lock(mutex);
        const auto found = byWatch.find(watchId);
        if (found == byWatch.end()) return;
        const auto& folder = folders[found->second];
        if (hidden(changed.lexically_relative(folder.path))) return;
        for (const auto& skip : ignored)
            if (under(changed, skip)) return;
        pending[found->second] = std::chrono::steady_clock::now();
        wake.notify_all();
    }

    // Reports each folder once it has been quiet for `quiet`.
    void report()
    {
        std::unique_lock lock(mutex);
        while (!stopping) {
            if (pending.empty()) {
                wake.wait(lock);
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            auto next = now + quiet;
            std::vector<std::int64_t> due;
            for (auto it = pending.begin(); it != pending.end();) {
                const auto at = it->second + quiet;
                if (at <= now) {
                    due.push_back(it->first);
                    it = pending.erase(it);
                } else {
                    next = std::min(next, at);
                    ++it;
                }
            }
            if (!due.empty()) {
                lock.unlock();
                for (const auto id : due) onChanged(id);
                lock.lock();
                continue;
            }
            wake.wait_until(lock, next);
        }
    }
};

FolderWatcher::FolderWatcher(Changed onChanged, std::chrono::milliseconds quiet)
    : impl_(std::make_unique<Impl>(std::move(onChanged), quiet))
{
}

FolderWatcher::~FolderWatcher() = default;

std::vector<std::int64_t> FolderWatcher::Impl::change(const std::vector<Folder>& asked)
{
    std::vector<Folder> want;
    for (const auto& f : asked) want.push_back({f.id, real(f.path)});
    // What to drop and what to add, decided under the lock; efsw is called
    // outside it, since its thread may be waiting for the lock to report.
    std::vector<efsw::WatchID> drop;
    std::vector<Folder> add;
    {
        const std::lock_guard lock(mutex);
        for (auto it = folders.begin(); it != folders.end();) {
            const auto keep = std::find_if(want.begin(), want.end(), [&](const Folder& f) {
                return f.id == it->first && f.path == it->second.path;
            });
            if (keep != want.end()) {
                ++it;
                continue;
            }
            drop.push_back(it->second.watch);
            byWatch.erase(it->second.watch);
            pending.erase(it->first);
            it = folders.erase(it);
        }
        for (const auto& f : want)
            if (!folders.count(f.id)) add.push_back(f);
    }
    for (const auto w : drop) watcher.removeWatch(w);
    std::vector<std::int64_t> notWatched;
    for (const auto& f : add) {
        std::error_code ec;
        const efsw::WatchID id = fs::is_directory(f.path, ec) ? watcher.addWatch(toUtf8(f.path), this, true)
                                                              : efsw::WatchID(-1);
        if (id < 0) {
            notWatched.push_back(f.id);
            continue;
        }
        const std::lock_guard lock(mutex);
        folders[f.id] = {id, f.path};
        byWatch[id] = f.id;
    }
    return notWatched;
}

void FolderWatcher::watch(const std::vector<Folder>& folders)
{
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->wanted = folders;
    }
    impl_->wantedWake.notify_all();
}

bool FolderWatcher::applied() const
{
    const std::lock_guard lock(impl_->mutex);
    return !impl_->wanted && !impl_->applying;
}

std::vector<std::int64_t> FolderWatcher::failed() const
{
    const std::lock_guard lock(impl_->mutex);
    return impl_->failedIds;
}

void FolderWatcher::ignore(const std::filesystem::path& dir)
{
    const std::lock_guard lock(impl_->mutex);
    impl_->ignored.push_back(real(dir));
}

} // namespace asma
