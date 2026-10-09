// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/FileOps.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace asma::app {

// A file operation the standalone asks for.
struct FileRequest {
    enum class Kind { Rename, Move, Trash, RemoveFolder, Undo, AddFolder, Startup };
    Kind kind = Kind::Undo;
    std::vector<std::int64_t> files; // Rename (one), Move, Trash
    std::int64_t rootId = 0;         // RemoveFolder
    std::string name;                // Rename: the new name
    std::filesystem::path folder;    // Move: where to; AddFolder: what
    bool merge = false;              // AddFolder: in place of the folders inside it

    static FileRequest rename(std::int64_t file, std::string name);
    static FileRequest move(std::vector<std::int64_t> files, std::filesystem::path folder);
    static FileRequest trash(std::vector<std::int64_t> files);
    static FileRequest removeFolder(std::int64_t rootId);
    static FileRequest undo();
    static FileRequest addFolder(std::filesystem::path folder, bool merge);
};

// What a request came to.
struct FileOutcome {
    bool done = false;               // false: refused or failed, and nothing changed
    std::string text;                // the footer's words; empty when there is nothing to say
    std::vector<std::int64_t> files; // the samples it moved or brought back, to select
};

// Runs the standalone's file operations one at a time on its own thread,
// each under the writer lock (waiting up to `lockWait` for a scan to end),
// so the window is never blocked. The first request, made by the
// constructor, rolls back what an interrupted asma left and merges nested
// library folders. Message thread only.
class FileOpsJob {
public:
    using Done = std::function<void(const FileOutcome&)>;

    FileOpsJob(std::filesystem::path dbPath, TrashBackend trash = TrashBackend::system(),
               std::chrono::milliseconds lockWait = std::chrono::seconds(30));
    // Finishes the operation under way; drops the rest.
    ~FileOpsJob();
    FileOpsJob(const FileOpsJob&) = delete;
    FileOpsJob& operator=(const FileOpsJob&) = delete;

    // `done` follows on the message thread.
    void run(FileRequest request, Done done = {});
    bool idle() const;
    // What Cmd+Z would undo now ("Move 5 Samples"); empty when nothing.
    std::string undoLabel() const;
    // Each outcome's words, counted, so the footer shows each once.
    std::uint64_t messageCount() const { return messages_; }
    const std::string& message() const { return message_; }

    // "Cmd+Z" on macOS, "Ctrl+Z" elsewhere.
    static std::string undoKey();

private:
    struct Job {
        FileRequest request;
        Done done;
    };
    void loop();
    FileOutcome carryOut(const FileRequest& request);

    const std::filesystem::path dbPath_;
    const TrashBackend trash_;
    const std::chrono::milliseconds lockWait_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    bool running_ = false;
    bool stopping_ = false;
    std::string undoLabel_; // guarded by mutex_
    std::string message_;   // message thread
    std::uint64_t messages_ = 0;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::thread thread_;
};

// "Moved 3 samples to the Trash. Cmd+Z to undo."
std::string doneText(const FileRequest& request, const OpResult& result);

} // namespace asma::app
