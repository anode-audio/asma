// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"
#include "asma/core/Library.h"
#include "asma/core/Trash.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

// Why a file operation did not happen, in words for the footer. Nothing was
// touched: either preflight refused it, or a step failed and the group was
// rolled back.
class OperationRefused : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Thrown by crashAfter() in tests, leaving the group as a crash would.
class SimulatedCrash : public std::runtime_error {
public:
    SimulatedCrash() : std::runtime_error("simulated crash") {}
};

enum class Operation { Rename, Move, Trash, RemoveFolder };

// What an operation, an undo or a recovery did.
struct OpResult {
    std::int64_t group = 0;
    Operation op = Operation::Move;
    std::string label;               // "Move 5 Samples", as the Edit menu shows it
    std::size_t count = 0;           // samples (or the one folder) it was about
    std::string name;                // the one sample's or folder's name
    std::vector<std::int64_t> files; // the samples it moved (or brought back)
    // Undo and recovery: what could not be put back, and why
    // ("kick.wav could not be moved back: its old place is taken").
    std::vector<std::string> skipped;
};

struct JournalGroup {
    std::int64_t id = 0;
    std::string label;
    std::string at;    // UTC, ISO 8601
    std::string state; // running, done, undone, rolled_back
};

// The trash file operations use: the system's, or a stand-in in tests.
struct TrashBackend {
    std::function<bool(const std::filesystem::path&)> available;
    std::function<TrashResult(const std::filesystem::path&)> move;
    std::function<std::string(const std::filesystem::path&, const std::filesystem::path&)> restore;
    static TrashBackend system();
};

// Renames, moves and trashes samples and removes folders, each as a group of
// steps that is planned, checked in full before anything is touched,
// journaled, then carried out, and undone as one. A sample keeps its id, so
// its ratings, tags and collections follow it. The caller holds the writer
// lock.
class FileOps {
public:
    static constexpr std::size_t kKeptGroups = 50;

    explicit FileOps(Db& db, TrashBackend trash = TrashBackend::system());

    // Each throws OperationRefused when it does not happen.
    OpResult rename(std::int64_t fileId, std::string_view newName);
    OpResult move(const std::vector<std::int64_t>& fileIds, const std::filesystem::path& folder);
    OpResult trash(const std::vector<std::int64_t>& fileIds);
    OpResult removeFolder(std::int64_t rootId);

    // Undoes the newest group still done; nothing when there is none.
    std::optional<OpResult> undo();
    // What undo() would undo.
    std::optional<JournalGroup> undoable();
    // The newest groups first.
    std::vector<JournalGroup> history(std::size_t limit = kKeptGroups);
    // Rolls back the groups an interrupted process left running.
    std::vector<OpResult> recover();

    // Tests: the next group stops after this many steps, as a crash would.
    void crashAfter(std::size_t steps) { crashAfter_ = steps; }

private:
    struct Step;
    OpResult run(Operation op, std::vector<Step> steps, std::string label, std::string name);
    void rollBack(std::int64_t group, const char* state, OpResult& result);
    // A planned step whose file a crash changed before its record: puts it
    // back. Empty when done or nothing to do, else what could not be.
    std::string undoUnrecorded(Library& lib, const Step& step);
    void prune();

    Db& db_;
    TrashBackend trash_;
    std::optional<std::size_t> crashAfter_;
};

// Why a sample called `current` may not be renamed `name`, without looking at
// the disk: a character no system takes, another extension, no name, the
// same name. Nothing when it may.
std::optional<std::string> renameProblem(std::string_view current, std::string_view name);

// "asma was interrupted while moving 12 samples; they are back where they
// were." and what could not be put back.
std::string interruptedText(const OpResult& result);
// "Undid Move 5 Samples." and what could not be put back.
std::string undoneText(const OpResult& result);

} // namespace asma
