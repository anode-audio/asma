// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"
#include "asma/core/Query.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace asma::app {

// One change to what the user adds to the library: a rating, a favourite, a
// tag, a collection or a saved search. Collections and saved searches go by
// name, which is unique within each.
struct Write {
    enum class Kind {
        Rate, Favourite, AddTag, RemoveTag,
        CreateCollection, RenameCollection, DeleteCollection, AddToCollection, RemoveFromCollection,
        SaveSearch, RenameSearch, DeleteSearch,
    };
    Kind kind = Kind::Rate;
    std::int64_t fileId = 0; // the sample; for CreateCollection, one to add (0: none)
    int rating = 0;          // 0 clears it
    bool on = false;         // the favourite
    std::string name;        // the tag, collection or saved search
    std::string newName;     // what a rename gives
    SearchModel model;       // what SaveSearch saves

    static Write rate(std::int64_t fileId, int rating);
    static Write favourite(std::int64_t fileId, bool on);
    static Write addTag(std::int64_t fileId, std::string tag);
    static Write removeTag(std::int64_t fileId, std::string tag);
    static Write createCollection(std::string name, std::int64_t withFile = 0);
    static Write renameCollection(std::string name, std::string newName);
    static Write deleteCollection(std::string name);
    static Write addToCollection(std::string collection, std::int64_t fileId);
    static Write removeFromCollection(std::string collection, std::int64_t fileId);
    static Write saveSearch(std::string name, SearchModel model);
    static Write renameSearch(std::string name, std::string newName);
    static Write deleteSearch(std::string name);
};

// The asma commands that make a write, each after "asma --db PATH
// --errors-to-stdout"; two for a collection made with a sample in it.
std::vector<std::vector<std::string>> cliCommands(const Write& write);
// The asma commands for a retry of these files: "retry --id N...", in chunks
// a command line can hold (Windows takes 32,767 characters).
std::vector<std::vector<std::string>> retrySteps(const std::vector<std::int64_t>& fileIds);
// An error from the library or the helper in words for the footer.
std::string reasonText(const std::string& error);
// What the footer says when a write fails: "Could not save the rating: the
// library is busy".
std::string failureText(const Write& write, const std::string& reason);

// Called on the message thread once the write is made or has failed: empty
// on success, else the footer's words for why.
using WriteDone = std::function<void(const std::string& error)>;

struct RetryEvent {
    enum class Kind { Waiting, Finished, Failed };
    Kind kind = Kind::Finished;
    std::string error; // Failed: why, in the footer's words
};
using RetryUpdate = std::function<void(const RetryEvent&)>; // message thread

// Runs asma commands one at a time, in the order given, on its own thread.
// Each command's stdout lines and its end are reported on the message thread.
class CliLane {
public:
    struct Command {
        std::vector<std::vector<std::string>> steps; // run in turn; a failing step ends the command
        std::function<void(const std::string& line)> onLine;
        std::function<void(const std::string& error)> onEnd; // empty on success
    };
    CliLane(std::filesystem::path dbPath, std::filesystem::path cli);
    ~CliLane(); // drops what has not started, ends what runs, and waits
    CliLane(const CliLane&) = delete;
    CliLane& operator=(const CliLane&) = delete;

    void run(Command command);
    void setCli(std::filesystem::path cli);
    bool idle() const;

private:
    void loop();
    std::string runStep(const std::vector<std::string>& args, const std::function<void(const std::string&)>& onLine);

    const std::filesystem::path dbPath_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::filesystem::path cli_;
    std::deque<Command> queue_;
    bool running_ = false; // a command has started and not ended
    bool stopping_ = false;
    struct Running;
    std::shared_ptr<Running> current_; // the process to end on shutdown
    std::thread thread_;
};

// Writes what the user adds to the library. The standalone writes the
// library itself (DirectWriter); a plugin never writes inside its host and
// has the asma command-line helper do it (CliWriter). Either way a retry runs
// `asma retry`, which needs the writer lock and may wait for a scan.
// Message thread only.
class LibraryWriter {
public:
    LibraryWriter(std::filesystem::path dbPath, std::filesystem::path cli);
    virtual ~LibraryWriter() = default;

    virtual void write(const Write& write, WriteDone done = {}) = 0;
    void retry(std::vector<std::int64_t> fileIds, RetryUpdate update);
    // Where the helper is; takes effect from the next command.
    virtual void setCli(std::filesystem::path cli);
    // Nothing queued or running.
    virtual bool idle() const;

    // Where the app and the plugins ship the helper: beside their own binary,
    // as asma-cli (their own binaries are called asma).
    static std::filesystem::path cliNextTo(const std::filesystem::path& binary);

protected:
    const std::filesystem::path dbPath_;

private:
    CliLane retries_;
};

class DirectWriter final : public LibraryWriter {
public:
    using LibraryWriter::LibraryWriter;
    // Writes at once, in one short transaction without the writer lock, and
    // calls `done` before returning.
    void write(const Write& write, WriteDone done = {}) override;

private:
    std::optional<Db> db_; // opened on the first write, dropped after an error
};

class CliWriter final : public LibraryWriter {
public:
    CliWriter(std::filesystem::path dbPath, std::filesystem::path cli);
    // Queues the write: the helper makes it on a background thread, in the
    // order writes were given, and `done` follows on the message thread.
    void write(const Write& write, WriteDone done = {}) override;
    void setCli(std::filesystem::path cli) override;
    bool idle() const override;

private:
    CliLane writes_;
};

} // namespace asma::app
