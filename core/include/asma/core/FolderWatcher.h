// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace asma {

// Watches sample folders, recursively, through the operating system's change
// notices (FSEvents, ReadDirectoryChangesW, inotify, by way of efsw) and
// reports a folder changed once it has been quiet for a while, so a burst of
// files gives one report. Changes to hidden files and folders (".DS_Store",
// "._x.wav", ".git") and anything under an ignored directory never count, nor
// does a folder merely "modified" (what changed inside has its own notice).
// macOS may still add a notice for the folder holding a hidden file, which
// costs one scan that finds nothing.
class FolderWatcher {
public:
    struct Folder {
        std::int64_t id = 0; // the library's root id, given back in reports
        std::filesystem::path path;
    };
    // Called on the watcher's own thread, never on the caller's.
    using Changed = std::function<void(std::int64_t id)>;

    explicit FolderWatcher(Changed onChanged, std::chrono::milliseconds quiet = std::chrono::seconds(2));
    ~FolderWatcher(); // stops reporting before it returns
    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    // Watches exactly these folders from now on: new ones are added, ones no
    // longer listed are dropped. Gives the ids of folders it could not watch
    // (missing, or not a folder), which the caller may offer again later.
    std::vector<std::int64_t> watch(const std::vector<Folder>& folders);
    // Changes under this directory never count (asma's own data directory).
    void ignore(const std::filesystem::path& dir);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace asma
