// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"
#include "asma/core/ChangeWatcher.h"
#include "asma/core/Db.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

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
    Damaged,    // SQLite finds it malformed: it is to be rebuilt
};

// The library as the UI sees it: opened read-only (so it works inside a
// host), never created, and watched for other processes' writes. Message
// thread only.
// The samples that sound most like one, nearest first.
struct SimilarResult {
    enum class State {
        Ok,          // matches, possibly none
        NotAnalysed, // the sample has no sound profile yet
        Failed,      // the query failed, or the library is not open
    };
    struct Match {
        SearchRow row;
        double distance = 0.0; // 0 sounds the same; 1 minus the similarity
    };
    State state = State::Failed;
    std::vector<Match> matches;
};

class LibraryView {
public:
    // ReadOnly inside a host. The standalone may migrate an older library
    // (opening it once as a writer), since nobody else would.
    enum class Access { ReadOnly, MayMigrate };
    explicit LibraryView(std::filesystem::path dbPath, Access access = Access::ReadOnly);
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
    // Whether a read met damage since the last call: kept until taken, so
    // the keeper hears of it whatever the state did after.
    bool takeDamage()
    {
        const bool seen = damageSeen_;
        damageSeen_ = false;
        return seen;
    }
    // While a rebuild runs: lets go of the file (Windows will not move a file
    // a connection holds) and opens nothing until resumed.
    void suspend(bool on);

    // Empty unless open.
    std::vector<SearchRow> search(const SearchModel& model);
    // The library's folders, collections and saved searches; empty unless open.
    std::vector<Root> roots();
    std::vector<Collection> collections();
    std::vector<SavedSearch> savedSearches();
    // The collections a file is in, by id; empty unless open.
    std::vector<std::int64_t> collectionsOf(std::int64_t fileId);
    // A file's tags and where each came from; empty unless open.
    std::vector<std::pair<std::string, TagSource>> tagsOf(std::int64_t fileId);
    // What sounds like the file, from analysed sound profiles.
    SimilarResult similar(std::int64_t fileId, int limit = 10);
    // The tags searches can find, most used first; empty unless open.
    std::vector<TagCount> tagCounts();
    // Files in enabled folders that failed to decode or analyse: the
    // Problems entry's count. 0 unless open.
    std::int64_t problemCount();
    // Those files, read failures first, each with why; empty unless open.
    std::vector<Problem> problems();
    // Every sample a search could find: readable files in enabled folders. 0 unless open.
    std::int64_t sampleCount();
    // What the model matches, past the page search() returns. 0 unless open.
    std::int64_t matchCount(const SearchModel& model);
    // Where the file falls in the model's order, from 0; nothing when it does
    // not match, or the library is not open.
    std::optional<std::int64_t> position(const SearchModel& model, std::int64_t fileId);
    // The library's id for a file, by its path; nothing when it is not in it.
    std::optional<std::int64_t> fileId(const std::filesystem::path& file);
    audio::SampleInfo info(std::int64_t fileId);
    // The file's content hash, or empty when unknown.
    std::string contentHash(std::int64_t fileId);
    // What the library knows about a file by its path; empty when it is not
    // in the library (or the library is not open).
    audio::SampleInfo infoFor(const std::filesystem::path& file);

    static std::filesystem::path pathOf(const SearchRow& row);

private:
    // Runs a query; an error (a corrupt page, an I/O error, a writer holding
    // the database past the busy timeout) closes the library and gives
    // `fallback`, so a broken library never throws into the host. The next
    // refresh() tries to open it again.
    template <typename Query, typename Result>
    Result guarded(Query&& query, Result fallback);
    void close(const std::string& why); // after an error: Damaged or Unreadable by what it says

    std::filesystem::path path_;
    std::string identity_; // the file the connection is to, to notice it replaced
    Access access_;
    LibraryState state_ = LibraryState::Missing;
    std::optional<Db> db_;
    std::unique_ptr<ChangeWatcher> watcher_;
    bool opened_ = false; // reported by the next changed()
    bool damageSeen_ = false;
    bool suspended_ = false;
};

} // namespace asma::app
