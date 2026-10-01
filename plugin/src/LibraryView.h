// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"
#include "asma/core/ChangeWatcher.h"
#include "asma/core/Db.h"
#include "asma/core/Query.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace asma::app {

enum class LibraryState {
    Missing,    // nothing scanned yet
    Open,
    Outdated,   // an older schema: a writer must open it once to migrate it
    TooNew,     // written by a newer asma
    Unreadable, // not a library, or not readable
};

// The library as the UI sees it: opened read-only (so it works inside a
// host), never created, and watched for other processes' writes. Message
// thread only.
class LibraryView {
public:
    explicit LibraryView(std::filesystem::path dbPath);
    ~LibraryView();

    // Opens the library when it is not open yet; cheap enough for a UI timer,
    // and how a library created or migrated later gets picked up.
    LibraryState refresh();
    LibraryState state() const { return state_; }
    // What to tell the user when the state is not Open.
    std::string message() const;
    // True once when the library has just opened, and after another process
    // commits to it.
    bool changed();

    // Empty unless open.
    std::vector<SearchRow> search(const SearchModel& model);
    audio::SampleInfo info(std::int64_t fileId);
    // The file's content hash, or empty when unknown.
    std::string contentHash(std::int64_t fileId);
    // What the library knows about a file by its path; empty when it is not
    // in the library (or the library is not open).
    audio::SampleInfo infoFor(const std::filesystem::path& file);

    static std::filesystem::path pathOf(const SearchRow& row);

private:
    std::filesystem::path path_;
    LibraryState state_ = LibraryState::Missing;
    std::optional<Db> db_;
    std::unique_ptr<ChangeWatcher> watcher_;
    bool opened_ = false; // reported by the next changed()
};

} // namespace asma::app
