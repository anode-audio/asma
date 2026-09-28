# asma Plan 3a: Organise and Scan Supervision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Users can rate, favourite, tag and collect samples and save searches,
from the CLI and (later) from the plugin without writing inside the host; the
app can run `asma-scan`, see it through crashes and cancel it; readers open the
library read-only and notice when someone else changed it.

**Architecture:** Everything here is in `asma-core` or the CLI, with no JUCE.
Schema version 3 adds the user-data tables. `UserData` writes them in short
transactions outside the writer lock. The `asma` CLI grows the commands the
plugin will run as its out-of-process writer. `Subprocess` (POSIX `posix_spawn`,
Windows `CreateProcessW`) runs `asma-scan`; `ScanRecovery` holds the
crash-recovery decisions as a pure state machine; `ScanSupervisor` joins the
two. `Db::openReadOnly` and `ChangeWatcher` (`PRAGMA data_version`) are what the
plugin and the UI read through.

**Tech Stack:** C++20, SQLite 3.53 (FTS5, `RETURNING`), Catch2. No new
third-party code.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (sections 5 and 6;
section 9, Plugin state). Task 10 amends it where this plan decides something
the spec left open.

**How this plan was checked:** every task was built on a clone of `main`
(818ceb1), then replayed commit by commit on a Release build: with only the
task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The replay ran on macOS 26 (Apple clang). The Linux
and Windows branches of `Subprocess` were written but not compiled locally: CI
on all three platforms must be green before merging (Task 11).

## Where this sits

The roadmap in plan 1 has five plans. Plan 3 (UI, audition and plugin) is split
in three, each shippable and testable on its own:

- **3a (this plan):** organise data, search model serialisation, the CLI writer
  commands, the scan supervisor, read-only access and change notification. No
  JUCE.
- **3b:** audition engine, tested headless: preview cache, trim, reverse and
  ping-pong, Signalsmith Stretch, tempo and key sync, MIDI voices, LUFS gain
  matching, the drag-out render cache.
- **3c:** JUCE 8, `asma-ui`, the standalone and the VST3/AU/CLAP/LV2 shells,
  Ableton Link, pluginval and clap-validator.

Plans 1 and 2 are merged. Plan 4 (file manager) and plan 5 (packaging) are
unchanged.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (CMake files: `# SPDX-...`).
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11 on Ubuntu 22.04),
  no floating-point `std::to_chars` or `std::from_chars` (macOS 12). Use
  `snprintf` to format numbers and a classic-locale stream to parse them.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` never links JUCE or any GUI library.
- Paths in the database are UTF-8 with `/` separators, via `toUtf8`/`fromUtf8`.
- Schema changes are new migrations; never edit one that has shipped. Migration
  3 has not shipped, so it can still change while this plan is open.
- The plugin never writes to the database inside the host. It opens the library
  with `Db::openReadOnly` and writes by running the `asma` CLI.
- User-data writes do not take the writer lock; they rely on SQLite's 5 s busy
  timeout. Scans and file operations still take it.
- No em-dashes in code, comments, docs or commit messages. No AI references or
  co-author trailers. Worktree, then a direct merge; no PR.

## Decisions made while prototyping

- **Plan 3 is split in three** and **the plugin writes through the CLI** were
  the user's calls (2026-09-27). Task 10 writes the second into the spec.
- **User data skips the writer lock.** A scan holds the lock for minutes; rating
  a sample must not wait for it. Scan batches are short transactions, so the
  busy timeout covers them (Review Focus 1).
- **Collection ids are `AUTOINCREMENT`.** Saved searches and plugin state refer
  to a collection by id; a plain `INTEGER PRIMARY KEY` hands the highest id to
  the next collection after a delete, so an old search would silently show
  another collection.
- **Names are unique ignoring case, ASCII only.** SQLite `NOCASE` folds ASCII
  letters: "Set Ü" and "SET Ü" clash, "set ü" does not. Good enough; ICU is not
  worth the build weight.
- **`JsonLine::real` rewrites a decimal comma.** `snprintf` follows
  `LC_NUMERIC`, and a host may set a comma locale; the output was
  `{"bpm":128,5}` in the replay before the fix.
- **The child's stderr is the null device on every platform.** A DAW may have
  closed fd 2, and on Windows a non-inheritable stderr handle in the handle list
  makes `CreateProcessW` fail. `asma-scan` reports errors on stdout.
- **No handle leaks between instances.** `POSIX_SPAWN_CLOEXEC_DEFAULT` on macOS,
  `pipe2(O_CLOEXEC)` on Linux, `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` on Windows.
  Without them, a second plugin instance spawning at the same moment can inherit
  the first one's pipe and keep it from ever reaching EOF.
- **Crashes are recognised by events, not exit codes.** A run that ends without
  `done` (and `analyse_done` when analysing) and without an `error` event
  crashed. `abort()` exits with code 3 on Windows, the same code as "locked".
- **After pinning a file, the thread count goes back up.** The protocol says to
  restart with `--threads 1` after a parallel crash but not for how long; each
  bad file costs two restarts instead of making the rest of the scan serial.
- **Crash loops are bounded:** two single-threaded crashes that no file explains
  end the scan, and so does a file that crashes the worker again after it was
  marked.
- **Report counts come from the last run.** Files a crashed run added count as
  `unchanged` in the report. The UI refreshes from the database, not the counts.
- **`Args::positional()` skips `--x` but not its value**, so the organise
  commands read `--id` values before any positional, and `search save` wants the
  name straight after `save`.
- **A statement left mid-step holds its read snapshot.** The first draft of the
  read-only test looked like a stale-read bug in `SQLITE_OPEN_READONLY`; it was
  the test's own unfinished `SELECT`. `search()` steps to completion, but plan
  3c's UI code must reset or finish statements before polling `ChangeWatcher`.

## Review Focus

1. **Rating while a scan is committing.** The helper must wait for the batch,
   not fail. Pinned in Task 5 ("a rating waits for a scan batch that is
   committing"). A batch that holds the lock over 5 s still fails with exit 1
   and "database is locked"; plan 3c shows that as a retryable error.
2. **Files that move or go missing keep their ratings, favourites and
   collections**, and lose them only when their root is removed. Pinned in
   Task 1.
3. **Saved searches and plugin state written by another asma version** (unknown
   fields, wrong types, a sort or key this build does not know) load what they
   can instead of failing. Pinned in Task 4.
4. **A host with a decimal-comma locale** must not produce or misread JSON.
   Pinned in Task 3 (skipped with a message where no such locale is installed).
5. **A scanner that crashes on the same file every time, or before any file**:
   the scan ends with a report instead of looping. Pinned in Tasks 7 and 8. The
   Windows crash dialog (`SetErrorMode` in `asma-scan`) has no automated test;
   Task 11 checks by hand that the Windows CI job's crash tests end promptly.

---

## File Structure

```
core/include/asma/core/
  UserData.h        ratings, favourites, collections, saved searches
  Subprocess.h      child process with line-by-line stdout
  ScanEvents.h      asma-scan events and ScanRecovery (pure)
  ScanSupervisor.h  runs asma-scan through crashes
  ChangeWatcher.h   PRAGMA data_version polling
core/src/
  UserData.cpp, Subprocess.cpp, ScanEvents.cpp, ScanSupervisor.cpp,
  ChangeWatcher.cpp
apps/
  SearchArgs.h/.cpp        query options to a SearchModel (shared by query and search save)
  OrganiseCommands.h/.cpp  rate, fav, tag, collection, search
tests/
  child/asma_test_child.cpp  helper process: echo, exit, crash, hang, fake asma-scan
  test_user_data.cpp, test_search_model_json.cpp, test_subprocess.cpp,
  test_scan_events.cpp, test_scan_supervisor.cpp, test_change_watcher.cpp
```

Modified: `Schema.cpp` (migration 3), `Library.h/.cpp` (`removeUserTag`),
`Query.h/.cpp` (facets, JSON), `Json.h/.cpp` (reader, arrays, locale),
`Db.h/.cpp` (`openReadOnly`, `SchemaMismatchError`), `apps/asma_main.cpp`,
`apps/asma_scan_main.cpp`, `apps/CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/test_query.cpp`, `tests/test_json.cpp`, `tests/test_cli_e2e.cpp`, docs.

Commands as in plans 1 and 2 (`cmake --build build`,
`./build/tests/asma_tests "[tag]"`,
`ctest --test-dir build --output-on-failure`).

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-3a -b plan-3a-organise
```

All paths below are relative to `product/asma/.worktrees/plan-3a`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` (the analysis
accuracy tests are slow in Debug).

---

### Task 1: Ratings, favourites and collections

The tables hang off `files.id`, so the scanner's re-linking (same row, new path)
carries user data along for free, and missing files keep theirs.

**Files:**

- Create: `core/include/asma/core/UserData.h`, `core/src/UserData.cpp`,
  `tests/test_user_data.cpp`
- Modify: `core/include/asma/core/Library.h`, `core/src/Library.cpp`,
  `core/src/Schema.cpp`

**Interfaces:**

- Consumes: `Db`, `Transaction`, `Library`, `scanRoot` (plans 1 and 2).
- Produces: migration 3 (`ratings`, `favourites`, `collections`,
  `collection_items`, `saved_searches`); `class asma::UserDataError`;
  `struct asma::Collection { id; name; size; }`; `class asma::UserData(Db&)`
  with `setRating(fileId, int)` (0 clears), `std::optional<int> rating(fileId)`,
  `setFavourite(fileId, bool)`, `bool isFavourite(fileId)`,
  `int64 createCollection(name)`, `renameCollection(id, name)`,
  `deleteCollection(id)`, `std::vector<Collection> collections()`,
  `std::optional<Collection> collectionByName(name)`,
  `addToCollection(collectionId, fileId)`,
  `removeFromCollection(collectionId, fileId)`;
  `void Library::removeUserTag(fileId, tag)`.

Behaviour the tests pin:

- Ratings are 1 to 5; 0 clears; anything else, or an unknown file, throws
  `UserDataError`.
- Collection names are trimmed, non-empty and unique ignoring (ASCII) case; a
  collection may be renamed to a case change of its own name.
- Collection ids are never reused.
- Moving a file keeps its rating, favourite and collections; a missing file
  keeps them; removing its root removes them.
- `removeUserTag` only removes the user's own tag and updates text search.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_user_data.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    TempDir dir;
    fs::path root = dir.path() / "lib";
    Db db = Db::openInMemory();
    Library lib{db};
    UserData user{db};
    std::int64_t rootId = 0;

    Fixture()
    {
        fs::create_directories(root);
        rootId = lib.addRoot(root);
    }

    std::int64_t wav(const std::string& rel, std::uint32_t seed = 1)
    {
        test::WavSpec spec;
        spec.seed = seed;
        test::writeWav(root / fromUtf8(rel), spec);
        ScanOptions options;
        scanRoot(db, rootId, options);
        return lib.fileByPath(rootId, rel).value().id;
    }
};

} // namespace

TEST_CASE("migration 3 adds the user-data tables to a version 2 library", "[userdata]")
{
    Db db = Db::openInMemory(2);
    db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
    db.exec("INSERT INTO files(id, root_id, rel_path, name, size, mtime, format, status) VALUES "
            "(1, 1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok')");
    migrate(db);
    CHECK(db.schemaVersion() == 3);
    UserData user(db);
    user.setRating(1, 4);
    CHECK(user.rating(1) == 4);
}

TEST_CASE("ratings are 1 to 5 and 0 clears them", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("a.wav");
    CHECK_FALSE(f.user.rating(id));
    f.user.setRating(id, 5);
    CHECK(f.user.rating(id) == 5);
    f.user.setRating(id, 2);
    CHECK(f.user.rating(id) == 2);
    f.user.setRating(id, 0);
    CHECK_FALSE(f.user.rating(id));
    CHECK_THROWS_AS(f.user.setRating(id, 6), UserDataError);
    CHECK_THROWS_AS(f.user.setRating(id, -1), UserDataError);
    CHECK_THROWS_AS(f.user.setRating(id + 100, 3), UserDataError);
}

TEST_CASE("favourites toggle and ignore repeats", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("a.wav");
    f.user.setFavourite(id, true);
    f.user.setFavourite(id, true);
    CHECK(f.user.isFavourite(id));
    f.user.setFavourite(id, false);
    f.user.setFavourite(id, false);
    CHECK_FALSE(f.user.isFavourite(id));
    CHECK_THROWS_AS(f.user.setFavourite(id + 100, true), UserDataError);
}

TEST_CASE("collections have unique trimmed names, ignoring case", "[userdata]")
{
    Fixture f;
    const auto drums = f.user.createCollection("  Drums ");
    CHECK(f.user.collectionByName("drums")->id == drums);
    CHECK(f.user.collectionByName("DRUMS")->name == "Drums");
    CHECK_THROWS_AS(f.user.createCollection("drums"), UserDataError);
    CHECK_THROWS_AS(f.user.createCollection("   "), UserDataError);

    const auto bass = f.user.createCollection("Bass");
    CHECK_THROWS_AS(f.user.renameCollection(bass, "DRUMS"), UserDataError);
    f.user.renameCollection(drums, "drums"); // a case change of its own name is fine
    CHECK(f.user.collectionByName("Drums")->name == "drums");

    const auto all = f.user.collections();
    REQUIRE(all.size() == 2);
    CHECK(all[0].name == "Bass");
    CHECK(all[1].name == "drums");
    CHECK_THROWS_AS(f.user.renameCollection(999, "x"), UserDataError);
    CHECK_THROWS_AS(f.user.deleteCollection(999), UserDataError);
}

TEST_CASE("collection items: add twice, remove, delete the collection", "[userdata]")
{
    Fixture f;
    const auto a = f.wav("a.wav", 1);
    const auto b = f.wav("b.wav", 2);
    const auto c = f.user.createCollection("Set");
    f.user.addToCollection(c, a);
    f.user.addToCollection(c, a);
    f.user.addToCollection(c, b);
    CHECK(f.user.collectionByName("Set")->size == 2);
    f.user.removeFromCollection(c, a);
    CHECK(f.user.collectionByName("Set")->size == 1);
    CHECK_THROWS_AS(f.user.addToCollection(c, 999), UserDataError);
    f.user.deleteCollection(c);
    CHECK(f.user.collections().empty());
    CHECK(f.lib.fileById(b)); // the file itself stays
    CHECK(f.user.createCollection("Next") != c); // ids are never reused
}

TEST_CASE("user data follows a file the scanner re-links", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("old/a.wav", 7);
    const auto c = f.user.createCollection("Keep");
    f.user.setRating(id, 4);
    f.user.setFavourite(id, true);
    f.user.addToCollection(c, id);

    fs::create_directories(f.root / "new");
    fs::rename(f.root / "old" / "a.wav", f.root / "new" / "a.wav");
    scanRoot(f.db, f.rootId, {});

    const auto moved = f.lib.fileByPath(f.rootId, "new/a.wav").value();
    CHECK(moved.id == id);
    CHECK(f.user.rating(id) == 4);
    CHECK(f.user.isFavourite(id));
    CHECK(f.user.collectionByName("Keep")->size == 1);
}

TEST_CASE("user data stays on a missing file and cascades when its root goes", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("a.wav");
    f.user.setRating(id, 3);
    fs::remove(f.root / "a.wav");
    scanRoot(f.db, f.rootId, {});
    CHECK(f.lib.fileById(id)->status == FileStatus::Missing);
    CHECK(f.user.rating(id) == 3);

    f.db.exec("DELETE FROM roots");
    CHECK_FALSE(f.user.rating(id));
}

TEST_CASE("removeUserTag drops only user tags and updates text search", "[userdata]")
{
    Fixture f;
    const auto id = f.wav("kick_01.wav");
    f.lib.addUserTag(id, "Punchy");
    f.lib.removeUserTag(id, "punchy");
    for (const auto& [name, source] : f.lib.tags(id)) CHECK(name != "punchy");
    auto q = f.db.prepare("SELECT count(*) FROM fts_files WHERE fts_files MATCH 'punchy'");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);

    // "kick" came from the file name; removing it as a user tag does nothing.
    f.lib.removeUserTag(id, "kick");
    bool hasKick = false;
    for (const auto& [name, source] : f.lib.tags(id)) hasKick = hasKick || name == "kick";
    CHECK(hasKick);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_user_data.cpp:7:10: fatal error: 'asma/core/UserData.h' file not found`

- [ ] **Step 3: Implement**

In `core/include/asma/core/Library.h`, replace:

```cpp
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty

    void addUserTag(std::int64_t fileId, std::string_view tag);
    std::vector<std::pair<std::string, TagSource>> tags(std::int64_t fileId);

private:
```

with:

```cpp
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty

    void addUserTag(std::int64_t fileId, std::string_view tag);
    // Removes the tag only where the user added it; auto and embedded tags
    // belong to the scanner and would come back on the next scan.
    void removeUserTag(std::int64_t fileId, std::string_view tag);
    std::vector<std::pair<std::string, TagSource>> tags(std::int64_t fileId);

private:
```

Create `core/include/asma/core/UserData.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

class UserDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Collection {
    std::int64_t id = 0;
    std::string name;
    std::int64_t size = 0; // files in the collection, any status
};

// What the user adds to a library: ratings, favourites and collections (user
// tags live in Library). Rows hang off file ids, so they survive a file going
// missing and follow it when the scanner re-links it. Each method is one short
// write; callers group writes in a Transaction when they need several at once.
// Throws UserDataError for an unknown file or collection, or a bad value.
class UserData {
public:
    explicit UserData(Db& db) : db_(db) {}

    // 1 to 5; 0 clears the rating.
    void setRating(std::int64_t fileId, int rating);
    std::optional<int> rating(std::int64_t fileId);

    void setFavourite(std::int64_t fileId, bool favourite);
    bool isFavourite(std::int64_t fileId);

    // Names are trimmed, non-empty and unique ignoring case.
    std::int64_t createCollection(std::string_view name);
    void renameCollection(std::int64_t id, std::string_view name);
    void deleteCollection(std::int64_t id); // the files stay in the library
    std::vector<Collection> collections();  // by name
    std::optional<Collection> collectionByName(std::string_view name);
    // Adding a file that is already there is not an error.
    void addToCollection(std::int64_t collectionId, std::int64_t fileId);
    void removeFromCollection(std::int64_t collectionId, std::int64_t fileId);

private:
    void requireFile(std::int64_t fileId);
    void requireCollection(std::int64_t id);
    std::string validName(std::string_view name);

    Db& db_;
};

} // namespace asma
```

In `core/src/Library.cpp`, replace:

```cpp
    refreshFts(fileId);
}

std::vector<std::pair<std::string, TagSource>> Library::tags(std::int64_t fileId)
{
    std::vector<std::pair<std::string, TagSource>> out;
```

with:

```cpp
    refreshFts(fileId);
}

void Library::removeUserTag(std::int64_t fileId, std::string_view tag)
{
    const std::string normalised = lower(tag);
    auto q = db_.prepare("DELETE FROM file_tags WHERE file_id = ? AND source = 'user' "
                         "AND tag_id = (SELECT id FROM tags WHERE name = ?)");
    q.bind(1, fileId).bind(2, std::string_view(normalised));
    q.run();
    refreshFts(fileId);
}

std::vector<std::pair<std::string, TagSource>> Library::tags(std::int64_t fileId)
{
    std::vector<std::pair<std::string, TagSource>> out;
```

In `core/src/Schema.cpp`, replace:

```cpp
namespace {

// Append-only. Never edit a migration that has shipped; add a new one.
constexpr std::array<std::string_view, 2> kMigrations = {
    R"SQL(
CREATE TABLE roots (
    id INTEGER PRIMARY KEY,
```

with:

```cpp
namespace {

// Append-only. Never edit a migration that has shipped; add a new one.
constexpr std::array<std::string_view, 3> kMigrations = {
    R"SQL(
CREATE TABLE roots (
    id INTEGER PRIMARY KEY,
```

In `core/src/Schema.cpp`, replace:

```cpp
UPDATE features SET loop_source = 'filename' WHERE is_loop IS NOT NULL;

CREATE INDEX files_analysis_pending ON files(analysis_version) WHERE status = 'ok';
)SQL",
};
```

with:

```cpp
UPDATE features SET loop_source = 'filename' WHERE is_loop IS NOT NULL;

CREATE INDEX files_analysis_pending ON files(analysis_version) WHERE status = 'ok';
)SQL",
    R"SQL(
CREATE TABLE ratings (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    rating INTEGER NOT NULL CHECK (rating BETWEEN 1 AND 5)
);

CREATE TABLE favourites (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE
);

-- AUTOINCREMENT: saved searches refer to collections by id, so an id must
-- never be handed to a new collection after its owner is deleted.
CREATE TABLE collections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE
);

CREATE TABLE collection_items (
    collection_id INTEGER NOT NULL REFERENCES collections(id) ON DELETE CASCADE,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    PRIMARY KEY (collection_id, file_id)
);
CREATE INDEX collection_items_file ON collection_items(file_id);

CREATE TABLE saved_searches (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE,
    model TEXT NOT NULL
);
)SQL",
};
```

Create `core/src/UserData.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/UserData.h"

#include <string>

namespace asma {

namespace {

std::string trim(std::string_view s)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && isSpace(s[begin])) ++begin;
    while (end > begin && isSpace(s[end - 1])) --end;
    return std::string(s.substr(begin, end - begin));
}

} // namespace

void UserData::requireFile(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT 1 FROM files WHERE id = ?");
    q.bind(1, fileId);
    if (!q.step()) throw UserDataError("no file with id " + std::to_string(fileId));
}

void UserData::requireCollection(std::int64_t id)
{
    auto q = db_.prepare("SELECT 1 FROM collections WHERE id = ?");
    q.bind(1, id);
    if (!q.step()) throw UserDataError("no collection with id " + std::to_string(id));
}

std::string UserData::validName(std::string_view name)
{
    std::string trimmed = trim(name);
    if (trimmed.empty()) throw UserDataError("a collection needs a name");
    return trimmed;
}

void UserData::setRating(std::int64_t fileId, int rating)
{
    if (rating < 0 || rating > 5) throw UserDataError("a rating is 1 to 5, or 0 to clear it");
    requireFile(fileId);
    if (rating == 0) {
        auto q = db_.prepare("DELETE FROM ratings WHERE file_id = ?");
        q.bind(1, fileId);
        q.run();
        return;
    }
    auto q = db_.prepare("INSERT INTO ratings(file_id, rating) VALUES (?, ?) "
                         "ON CONFLICT(file_id) DO UPDATE SET rating = excluded.rating");
    q.bind(1, fileId).bind(2, rating);
    q.run();
}

std::optional<int> UserData::rating(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT rating FROM ratings WHERE file_id = ?");
    q.bind(1, fileId);
    if (!q.step()) return std::nullopt;
    return static_cast<int>(q.getInt(0));
}

void UserData::setFavourite(std::int64_t fileId, bool favourite)
{
    requireFile(fileId);
    auto q = db_.prepare(favourite ? "INSERT OR IGNORE INTO favourites(file_id) VALUES (?)"
                                   : "DELETE FROM favourites WHERE file_id = ?");
    q.bind(1, fileId);
    q.run();
}

bool UserData::isFavourite(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT 1 FROM favourites WHERE file_id = ?");
    q.bind(1, fileId);
    return q.step();
}

std::int64_t UserData::createCollection(std::string_view name)
{
    const std::string valid = validName(name);
    if (collectionByName(valid)) throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("INSERT INTO collections(name) VALUES (?)");
    q.bind(1, std::string_view(valid));
    q.run();
    return db_.lastInsertId();
}

void UserData::renameCollection(std::int64_t id, std::string_view name)
{
    requireCollection(id);
    const std::string valid = validName(name);
    if (const auto existing = collectionByName(valid); existing && existing->id != id)
        throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("UPDATE collections SET name = ? WHERE id = ?");
    q.bind(1, std::string_view(valid)).bind(2, id);
    q.run();
}

void UserData::deleteCollection(std::int64_t id)
{
    requireCollection(id);
    auto q = db_.prepare("DELETE FROM collections WHERE id = ?");
    q.bind(1, id);
    q.run();
}

std::vector<Collection> UserData::collections()
{
    std::vector<Collection> out;
    auto q = db_.prepare("SELECT c.id, c.name, (SELECT count(*) FROM collection_items i WHERE i.collection_id = c.id) "
                         "FROM collections c ORDER BY c.name COLLATE NOCASE, c.id");
    while (q.step()) out.push_back({q.getInt(0), q.getText(1), q.getInt(2)});
    return out;
}

std::optional<Collection> UserData::collectionByName(std::string_view name)
{
    const std::string trimmed = trim(name);
    auto q = db_.prepare("SELECT c.id, c.name, (SELECT count(*) FROM collection_items i WHERE i.collection_id = c.id) "
                         "FROM collections c WHERE c.name = ?");
    q.bind(1, std::string_view(trimmed));
    if (!q.step()) return std::nullopt;
    return Collection{q.getInt(0), q.getText(1), q.getInt(2)};
}

void UserData::addToCollection(std::int64_t collectionId, std::int64_t fileId)
{
    requireCollection(collectionId);
    requireFile(fileId);
    auto q = db_.prepare("INSERT OR IGNORE INTO collection_items(collection_id, file_id) VALUES (?, ?)");
    q.bind(1, collectionId).bind(2, fileId);
    q.run();
}

void UserData::removeFromCollection(std::int64_t collectionId, std::int64_t fileId)
{
    requireCollection(collectionId);
    auto q = db_.prepare("DELETE FROM collection_items WHERE collection_id = ? AND file_id = ?");
    q.bind(1, collectionId).bind(2, fileId);
    q.run();
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[userdata]"`

Expected: no compiler warnings;
`All tests passed (40 assertions in 8 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 160`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: store ratings, favourites and collections"
```

---

### Task 2: Rating, favourite and collection facets

The search model learns the new facets; rows carry the rating and favourite flag
the UI will draw.

**Files:**

- Modify: `core/include/asma/core/Query.h`, `core/src/Query.cpp`,
  `tests/test_query.cpp`

**Interfaces:**

- Consumes: `UserData` (Task 1), `SearchModel`, `search`, `rowsForIds`.
- Produces: `SearchModel::minRating` (`std::optional<int>`),
  `SearchModel::favouritesOnly` (`bool`), `SearchModel::collectionId`
  (`std::optional<int64>`), `SortField::Rating`; `SearchRow::rating`
  (`std::optional<int>`), `SearchRow::favourite` (`bool`).

Behaviour the tests pin:

- Unrated files never match `minRating` and sort last in either direction.
- Facets combine with AND, as the existing ones do.
- The hidden `[.perf]` search over 200k files stays under the spec's 50 ms (24
  ms in the replay; run it once by hand: `./build/tests/asma_tests "[.perf]"`).

- [ ] **Step 1: Write the failing tests**

In `tests/test_query.cpp`, replace:

```cpp
#include "TestUtil.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
```

with:

```cpp
#include "TestUtil.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
```

In `tests/test_query.cpp`, replace:

```cpp
    CHECK(rows[0].rootPath == s.lib.root(s.root)->path);
}

// Hidden: run with ./build/tests/asma_tests "[.perf]" on a Release build.
TEST_CASE("search over 200k files", "[.perf]")
{
```

with:

```cpp
    CHECK(rows[0].rootPath == s.lib.root(s.root)->path);
}

TEST_CASE("rating, favourite and collection facets", "[query]")
{
    Seeded s;
    UserData user(s.db);
    user.setRating(s.kick, 5);
    user.setRating(s.padLoop, 3);
    user.setRating(s.snare, 1);
    user.setFavourite(s.padLoop, true);
    const auto set = user.createCollection("Set");
    user.addToCollection(set, s.snare);
    user.addToCollection(set, s.flacHit);

    SearchModel rated;
    rated.minRating = 3;
    CHECK(ids(search(s.db, rated)) == Ids{s.kick, s.padLoop});

    SearchModel favourites;
    favourites.favouritesOnly = true;
    CHECK(ids(search(s.db, favourites)) == Ids{s.padLoop});

    SearchModel collection;
    collection.collectionId = set;
    CHECK(ids(search(s.db, collection)) == Ids{s.flacHit, s.snare});

    SearchModel both;
    both.collectionId = set;
    both.minRating = 1;
    CHECK(ids(search(s.db, both)) == Ids{s.snare});

    SearchModel byRating;
    byRating.sort = SortField::Rating;
    byRating.descending = true;
    const auto rows = search(s.db, byRating);
    REQUIRE(rows.size() == 5);
    CHECK(ids({rows.begin(), rows.begin() + 3}) == Ids{s.kick, s.padLoop, s.snare});
    CHECK_FALSE(rows[4].rating); // unrated sorts last either way
    CHECK(rows[0].rating == 5);
    CHECK_FALSE(rows[0].favourite);
    CHECK(rows[1].favourite);
}

// Hidden: run with ./build/tests/asma_tests "[.perf]" on a Release build.
TEST_CASE("search over 200k files", "[.perf]")
{
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_query.cpp:204:11: error: no member named 'minRating' in 'asma::SearchModel'`

- [ ] **Step 3: Implement**

In `core/include/asma/core/Query.h`, replace:

```cpp
namespace asma {

enum class SampleType { Any, Loop, OneShot };
enum class SortField { Name, Bpm, Duration, Key };

struct SearchModel {
    std::string text;                  // every word must prefix-match name, folder or tags
```

with:

```cpp
namespace asma {

enum class SampleType { Any, Loop, OneShot };
enum class SortField { Name, Bpm, Duration, Key, Rating };

struct SearchModel {
    std::string text;                  // every word must prefix-match name, folder or tags
```

In `core/include/asma/core/Query.h`, replace:

```cpp
    std::optional<double> durationMax;
    std::vector<std::string> formats;  // any of
    std::optional<std::int64_t> rootId;
    SortField sort = SortField::Name;
    bool descending = false;
    int limit = 500;
```

with:

```cpp
    std::optional<double> durationMax;
    std::vector<std::string> formats;  // any of
    std::optional<std::int64_t> rootId;
    std::optional<int> minRating;              // 1 to 5; unrated files never match
    bool favouritesOnly = false;
    std::optional<std::int64_t> collectionId;
    SortField sort = SortField::Name;
    bool descending = false;
    int limit = 500;
```

In `core/include/asma/core/Query.h`, replace:

```cpp
    std::optional<double> bpm;
    std::optional<std::string> key;
    std::optional<bool> isLoop;
};

using SqlParam = std::variant<std::int64_t, double, std::string>;
```

with:

```cpp
    std::optional<double> bpm;
    std::optional<std::string> key;
    std::optional<bool> isLoop;
    std::optional<int> rating;
    bool favourite = false;
};

using SqlParam = std::variant<std::int64_t, double, std::string>;
```

In `core/src/Query.cpp`, replace:

```cpp
}

constexpr std::string_view kRowSelect =
    "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop "
    "FROM files f JOIN roots r ON r.id = f.root_id "
    "LEFT JOIN features ft ON ft.file_id = f.id "
    "WHERE f.status = 'ok' AND r.enabled = 1";

SearchRow readRow(const Statement& s)
```

with:

```cpp
}

constexpr std::string_view kRowSelect =
    "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop, rt.rating, "
    "EXISTS (SELECT 1 FROM favourites fv WHERE fv.file_id = f.id) "
    "FROM files f JOIN roots r ON r.id = f.root_id "
    "LEFT JOIN features ft ON ft.file_id = f.id "
    "LEFT JOIN ratings rt ON rt.file_id = f.id "
    "WHERE f.status = 'ok' AND r.enabled = 1";

SearchRow readRow(const Statement& s)
```

In `core/src/Query.cpp`, replace:

```cpp
    if (!s.isNull(6)) r.bpm = s.getDouble(6);
    if (!s.isNull(7)) r.key = s.getText(7);
    if (!s.isNull(8)) r.isLoop = s.getInt(8) != 0;
    return r;
}
```

with:

```cpp
    if (!s.isNull(6)) r.bpm = s.getDouble(6);
    if (!s.isNull(7)) r.key = s.getText(7);
    if (!s.isNull(8)) r.isLoop = s.getInt(8) != 0;
    if (!s.isNull(9)) r.rating = static_cast<int>(s.getInt(9));
    r.favourite = s.getInt(10) != 0;
    return r;
}
```

In `core/src/Query.cpp`, replace:

```cpp
        q.sql += " AND f.root_id = ?";
        q.params.emplace_back(*m.rootId);
    }

    const std::string direction = m.descending ? " DESC" : " ASC";
    std::string order;
```

with:

```cpp
        q.sql += " AND f.root_id = ?";
        q.params.emplace_back(*m.rootId);
    }
    if (m.minRating) {
        q.sql += " AND rt.rating >= ?";
        q.params.emplace_back(static_cast<std::int64_t>(*m.minRating));
    }
    if (m.favouritesOnly) q.sql += " AND f.id IN (SELECT file_id FROM favourites)";
    if (m.collectionId) {
        q.sql += " AND f.id IN (SELECT file_id FROM collection_items WHERE collection_id = ?)";
        q.params.emplace_back(*m.collectionId);
    }

    const std::string direction = m.descending ? " DESC" : " ASC";
    std::string order;
```

In `core/src/Query.cpp`, replace:

```cpp
    case SortField::Bpm: order = "ft.bpm IS NULL, ft.bpm" + direction; break;
    case SortField::Duration: order = "f.duration" + direction; break;
    case SortField::Key: order = "ft.key IS NULL, ft.key" + direction; break;
    }
    q.sql += " ORDER BY " + order + ", f.id LIMIT ? OFFSET ?";
    q.params.emplace_back(static_cast<std::int64_t>(m.limit));
```

with:

```cpp
    case SortField::Bpm: order = "ft.bpm IS NULL, ft.bpm" + direction; break;
    case SortField::Duration: order = "f.duration" + direction; break;
    case SortField::Key: order = "ft.key IS NULL, ft.key" + direction; break;
    case SortField::Rating: order = "rt.rating IS NULL, rt.rating" + direction; break;
    }
    q.sql += " ORDER BY " + order + ", f.id LIMIT ? OFFSET ?";
    q.params.emplace_back(static_cast<std::int64_t>(m.limit));
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[query]"`

Expected: no compiler warnings;
`All tests passed (52 assertions in 10 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 161`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: filter and sort searches by rating, favourite and collection"
```

---

### Task 3: Reading JSON; locale-proof numbers

Saved searches, plugin state and the supervisor all need to read JSON; so far
asma only writes it.

**Files:**

- Modify: `core/include/asma/core/Json.h`, `core/src/Json.cpp`,
  `tests/test_json.cpp`

**Interfaces:**

- Consumes: `JsonLine`, `jsonEscape` (plan 1).
- Produces: `JsonLine& JsonLine::strings(key, const std::vector<std::string>&)`;
  `class asma::JsonError`; `class asma::JsonValue` with `isNull()`,
  `isObject()`, `asBool()`, `asNumber()`, `asInt()` (whole numbers that fit),
  `asString()`, `asArray()`, `asObject()` (pointers, null on a type mismatch),
  `get(key)` (last of repeated keys);
  `asma::JsonValue asma::parseJson(std::string_view)`.

Behaviour the tests pin:

- Strict RFC 8259: no trailing commas, single quotes, leading zeros, NaN, or
  text after the value; lone surrogates are errors; `\u` escapes and surrogate
  pairs become UTF-8.
- Nesting deeper than 64 levels throws instead of overflowing the stack.
- Numbers are written with a decimal point and read back correctly whatever
  `LC_NUMERIC` says.

- [ ] **Step 1: Write the failing tests**

In `tests/test_json.cpp`, replace:

```cpp
#include "asma/core/Json.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using asma::JsonLine;
```

with:

```cpp
#include "asma/core/Json.h"

#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cmath>

using asma::JsonLine;
```

In `tests/test_json.cpp`, replace:

```cpp
    CHECK(JsonLine().real("x", std::nan("")).build() == R"({"x":null})");
    CHECK(JsonLine().build() == "{}");
}
```

with:

```cpp
    CHECK(JsonLine().real("x", std::nan("")).build() == R"({"x":null})");
    CHECK(JsonLine().build() == "{}");
}

TEST_CASE("JsonLine writes string arrays", "[json]")
{
    CHECK(JsonLine().strings("keys", {"Am", "C\"#"}).build() == R"({"keys":["Am","C\"#"]})");
    CHECK(JsonLine().strings("keys", {}).build() == R"({"keys":[]})");
}

TEST_CASE("numbers use a decimal point whatever LC_NUMERIC says", "[json]")
{
    const char* previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = previous ? previous : "C";
    bool switched = false;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "it_IT.UTF-8", "fr_FR.UTF-8", "German_Germany.1252"})
        if (std::setlocale(LC_NUMERIC, name)) {
            switched = true;
            break;
        }
    if (!switched) SKIP("no locale with a decimal comma is installed");
    const std::string line = JsonLine().real("bpm", 128.5).build();
    const auto parsed = asma::parseJson(R"({"bpm":128.5})");
    std::setlocale(LC_NUMERIC, saved.c_str());
    CHECK(line == R"({"bpm":128.5})");
    CHECK(parsed.get("bpm")->asNumber() == 128.5);
}

TEST_CASE("parseJson reads every JSON type", "[json]")
{
    const auto v = asma::parseJson(R"( {"s":"a\"b\\c\/\n\u00e9\ud83d\ude00", "n":-12.5e1, "i":42, "t":true,
        "f":false, "z":null, "a":[1,"x",[]], "o":{"k":"v"}} )");
    REQUIRE(v.isObject());
    CHECK(*v.get("s")->asString() == "a\"b\\c/\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v.get("n")->asNumber() == -125.0);
    CHECK(v.get("i")->asInt() == 42);
    CHECK(v.get("n")->asInt() == -125); // a whole number written as a real
    CHECK_FALSE(asma::parseJson("1.5").asInt());
    CHECK(v.get("t")->asBool() == true);
    CHECK(v.get("f")->asBool() == false);
    CHECK(v.get("z")->isNull());
    REQUIRE(v.get("a")->asArray());
    CHECK(v.get("a")->asArray()->size() == 3);
    CHECK(*v.get("o")->get("k")->asString() == "v");
    CHECK(v.get("missing") == nullptr);
    CHECK(v.get("s")->get("x") == nullptr);
    CHECK_FALSE(v.get("s")->asNumber());
}

TEST_CASE("parseJson keeps the last of repeated keys", "[json]")
{
    CHECK(asma::parseJson(R"({"a":1,"a":2})").get("a")->asInt() == 2);
}

TEST_CASE("parseJson rejects malformed input", "[json]")
{
    for (const char* bad : {"", "   ", "{", "[1,]", "{\"a\":}", "{\"a\" 1}", "{'a':1}", "tru", "nul", "01", "1.",
                            "-", "1e", "+1", "\"abc", "\"\\x\"", "\"\\u12\"", "\"\\ud800\"", "\"\\udc00\"",
                            "\"a\nb\"", "{} {}", "[1] x", "1e999", "NaN"})
        CHECK_THROWS_AS(asma::parseJson(bad), asma::JsonError);
}

TEST_CASE("parseJson refuses absurd nesting instead of overflowing the stack", "[json]")
{
    CHECK_NOTHROW(asma::parseJson(std::string(64, '[') + std::string(64, ']')));
    CHECK_THROWS_AS(asma::parseJson(std::string(100000, '[')), asma::JsonError);
}

TEST_CASE("JsonLine output parses back", "[json]")
{
    const std::string line = JsonLine()
                                 .str("path", "Café/\"odd\"\x01.wav")
                                 .num("done", -3)
                                 .real("bpm", 0.1)
                                 .strings("tags", {"a", "b"})
                                 .build();
    const auto v = asma::parseJson(line);
    CHECK(*v.get("path")->asString() == "Café/\"odd\"\x01.wav");
    CHECK(v.get("done")->asInt() == -3);
    CHECK(v.get("bpm")->asNumber() == 0.1);
    CHECK(v.get("tags")->asArray()->size() == 2);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_json.cpp:38:22: error: no member named 'strings' in 'asma::JsonLine'`

- [ ] **Step 3: Implement**

In `core/include/asma/core/Json.h`, replace:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace asma {
```

with:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace asma {
```

In `core/include/asma/core/Json.h`, replace:

```cpp
    JsonLine& real(std::string_view key, double value); // NaN/inf become null
    JsonLine& boolean(std::string_view key, bool value);
    JsonLine& null(std::string_view key);
    std::string build() const;

private:
```

with:

```cpp
    JsonLine& real(std::string_view key, double value); // NaN/inf become null
    JsonLine& boolean(std::string_view key, bool value);
    JsonLine& null(std::string_view key);
    JsonLine& strings(std::string_view key, const std::vector<std::string>& values);
    std::string build() const;

private:
```

In `core/include/asma/core/Json.h`, replace:

```cpp
    std::string body_;
};

} // namespace asma
```

with:

```cpp
    std::string body_;
};

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A parsed JSON value. The typed accessors return nothing on a type mismatch,
// so a reader can skip a bad field instead of failing the whole document.
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>; // document order

    JsonValue() = default;
    explicit JsonValue(bool value) : value_(value) {}
    explicit JsonValue(double value) : value_(value) {}
    explicit JsonValue(std::string value) : value_(std::move(value)) {}
    explicit JsonValue(Array value) : value_(std::move(value)) {}
    explicit JsonValue(Object value) : value_(std::move(value)) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(value_); }
    bool isObject() const { return std::holds_alternative<Object>(value_); }
    std::optional<bool> asBool() const;
    std::optional<double> asNumber() const;
    std::optional<std::int64_t> asInt() const; // whole numbers that fit only
    const std::string* asString() const;
    const Array* asArray() const;
    const Object* asObject() const;
    // Member of an object (the last one when a key repeats); nullptr when this
    // is not an object or has no such key.
    const JsonValue* get(std::string_view key) const;

private:
    std::variant<std::monostate, bool, double, std::string, Array, Object> value_;
};

// Parses one JSON document (RFC 8259). Surrounding whitespace is allowed,
// anything else after the value is not. Throws JsonError on malformed input,
// including nesting deeper than 64 levels.
JsonValue parseJson(std::string_view text);

} // namespace asma
```

In `core/src/Json.cpp`, replace:

```cpp

#include <cmath>
#include <cstdio>

namespace asma {
```

with:

```cpp

#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace asma {
```

In `core/src/Json.cpp`, replace:

```cpp
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", value);
    body_ += buf;
    return *this;
}
```

with:

```cpp
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", value);
    // snprintf follows LC_NUMERIC, and a host application may have set one
    // with a decimal comma.
    for (char* c = buf; *c; ++c)
        if (*c == ',') *c = '.';
    body_ += buf;
    return *this;
}
```

In `core/src/Json.cpp`, replace:

```cpp
    return *this;
}

std::string JsonLine::build() const { return "{" + body_ + "}"; }

} // namespace asma
```

with:

```cpp
    return *this;
}

JsonLine& JsonLine::strings(std::string_view k, const std::vector<std::string>& values)
{
    key(k);
    body_ += '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) body_ += ',';
        body_ += '"';
        body_ += jsonEscape(values[i]);
        body_ += '"';
    }
    body_ += ']';
    return *this;
}

std::string JsonLine::build() const { return "{" + body_ + "}"; }

std::optional<bool> JsonValue::asBool() const
{
    if (const auto* v = std::get_if<bool>(&value_)) return *v;
    return std::nullopt;
}

std::optional<double> JsonValue::asNumber() const
{
    if (const auto* v = std::get_if<double>(&value_)) return *v;
    return std::nullopt;
}

std::optional<std::int64_t> JsonValue::asInt() const
{
    const auto* v = std::get_if<double>(&value_);
    // 2^63 is exactly representable; anything at or beyond it does not fit.
    if (!v || std::floor(*v) != *v || *v < -9223372036854775808.0 || *v >= 9223372036854775808.0)
        return std::nullopt;
    return static_cast<std::int64_t>(*v);
}

const std::string* JsonValue::asString() const { return std::get_if<std::string>(&value_); }
const JsonValue::Array* JsonValue::asArray() const { return std::get_if<Array>(&value_); }
const JsonValue::Object* JsonValue::asObject() const { return std::get_if<Object>(&value_); }

const JsonValue* JsonValue::get(std::string_view key) const
{
    const auto* object = asObject();
    if (!object) return nullptr;
    for (auto it = object->rbegin(); it != object->rend(); ++it)
        if (it->first == key) return &it->second;
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonValue document()
    {
        JsonValue value = parseValue(0);
        skipSpace();
        if (pos_ != text_.size()) fail("unexpected text after the value");
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    [[noreturn]] void fail(const std::string& what) const
    {
        throw JsonError("invalid JSON at byte " + std::to_string(pos_) + ": " + what);
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipSpace()
    {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) ++pos_;
    }

    void expect(char c)
    {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    void literal(std::string_view word)
    {
        if (text_.substr(pos_, word.size()) != word) fail("unknown literal");
        pos_ += word.size();
    }

    JsonValue parseValue(int depth)
    {
        if (depth > kMaxDepth) fail("nested too deeply");
        skipSpace();
        switch (peek()) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': return JsonValue(parseString());
        case 't': literal("true"); return JsonValue(true);
        case 'f': literal("false"); return JsonValue(false);
        case 'n': literal("null"); return JsonValue();
        default: return JsonValue(parseNumber());
        }
    }

    JsonValue parseObject(int depth)
    {
        expect('{');
        JsonValue::Object object;
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            return JsonValue(std::move(object));
        }
        for (;;) {
            skipSpace();
            std::string key = parseString();
            skipSpace();
            expect(':');
            object.emplace_back(std::move(key), parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}');
            return JsonValue(std::move(object));
        }
    }

    JsonValue parseArray(int depth)
    {
        expect('[');
        JsonValue::Array array;
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            return JsonValue(std::move(array));
        }
        for (;;) {
            array.push_back(parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']');
            return JsonValue(std::move(array));
        }
    }

    unsigned hex4()
    {
        if (pos_ + 4 > text_.size()) fail("short \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return value;
    }

    static void appendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        for (;;) {
            if (atEnd()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (atEnd()) fail("unterminated string");
            switch (text_[pos_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (text_.substr(pos_, 2) != "\\u") fail("unpaired surrogate");
                    pos_ += 2;
                    const unsigned low = hex4();
                    if (low < 0xDC00 || low > 0xDFFF) fail("unpaired surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    fail("unpaired surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
    }

    double parseNumber()
    {
        const std::size_t start = pos_;
        const auto digits = [&] {
            const std::size_t from = pos_;
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
            return pos_ - from;
        };
        if (peek() == '-') ++pos_;
        if (peek() == '0') ++pos_;
        else if (digits() == 0) fail("expected a value");
        if (peek() == '.') {
            ++pos_;
            if (digits() == 0) fail("expected digits after '.'");
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (digits() == 0) fail("expected exponent digits");
        }
        // A classic-locale stream, because strtod follows LC_NUMERIC.
        std::istringstream in(std::string(text_.substr(start, pos_ - start)));
        in.imbue(std::locale::classic());
        double value = 0.0;
        in >> value;
        if (!std::isfinite(value)) fail("number out of range");
        return value;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

JsonValue parseJson(std::string_view text) { return Parser(text).document(); }

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[json]"`

Expected: no compiler warnings;
`All tests passed (57 assertions in 10 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 168`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: read JSON, and write numbers the same in every locale"
```

---

### Task 4: Saved searches as JSON

One serialisation for saved searches now and plugin state in plan 3c. Reading is
forgiving: a DAW project may be opened by an older or newer asma.

**Files:**

- Create: `tests/test_search_model_json.cpp`
- Modify: `core/include/asma/core/Query.h`, `core/include/asma/core/UserData.h`,
  `core/src/Query.cpp`, `core/src/UserData.cpp`

**Interfaces:**

- Consumes: `parseJson`, `JsonLine::strings` (Task 3), `parseKeyToken` (plan 1),
  `UserData` (Task 1), the Task 2 facets.
- Produces: `std::string asma::searchModelToJson(const SearchModel&)`;
  `std::optional<SearchModel> asma::searchModelFromJson(std::string_view)`;
  `struct asma::SavedSearch { id; name; SearchModel model; }`;
  `UserData::saveSearch(name, model)` (replaces by name, returns the id),
  `savedSearches()`, `savedSearchByName(name)`, `deleteSavedSearch(id)`.

Behaviour the tests pin:

- The format is `{"v":1, ...}` with only non-default fields; `limit` and
  `offset` are view state and are not saved.
- Unknown fields are ignored; a field with the wrong type or an out-of-range
  value is skipped and the rest is kept; keys are re-canonicalised (`Dbm`
  becomes `C#m`) and unknown keys dropped.
- Text that is not a JSON object gives nothing; a stored search that no longer
  parses comes back as the default model rather than disappearing.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_search_model_json.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;

TEST_CASE("a default model is just the version", "[searchjson]")
{
    CHECK(searchModelToJson({}) == R"({"v":1})");
}

TEST_CASE("every field round-trips; paging does not", "[searchjson]")
{
    SearchModel m;
    m.text = "dusty \"kick\"";
    m.type = SampleType::OneShot;
    m.bpmMin = 90.5;
    m.bpmMax = 100;
    m.keys = {"Am", "C#"};
    m.tags = {"kick", "808"};
    m.durationMin = 0.1;
    m.durationMax = 2;
    m.formats = {"wav"};
    m.rootId = 3;
    m.minRating = 4;
    m.favouritesOnly = true;
    m.collectionId = 9;
    m.sort = SortField::Rating;
    m.descending = true;
    m.limit = 20;
    m.offset = 40;

    const auto back = searchModelFromJson(searchModelToJson(m)).value();
    CHECK(back.text == m.text);
    CHECK(back.type == m.type);
    CHECK(back.bpmMin == m.bpmMin);
    CHECK(back.bpmMax == m.bpmMax);
    CHECK(back.keys == m.keys);
    CHECK(back.tags == m.tags);
    CHECK(back.durationMin == m.durationMin);
    CHECK(back.durationMax == m.durationMax);
    CHECK(back.formats == m.formats);
    CHECK(back.rootId == m.rootId);
    CHECK(back.minRating == m.minRating);
    CHECK(back.favouritesOnly);
    CHECK(back.collectionId == m.collectionId);
    CHECK(back.sort == SortField::Rating);
    CHECK(back.descending);
    CHECK(back.limit == SearchModel{}.limit);
    CHECK(back.offset == 0);
}

TEST_CASE("unreadable text gives nothing", "[searchjson]")
{
    CHECK_FALSE(searchModelFromJson(""));
    CHECK_FALSE(searchModelFromJson("{\"text\":"));
    CHECK_FALSE(searchModelFromJson("[1,2]"));
    CHECK_FALSE(searchModelFromJson("\"text\""));
}

TEST_CASE("bad or unknown fields are skipped, the rest is kept", "[searchjson]")
{
    const auto m = searchModelFromJson(R"({"v":7,"text":"pad","future_field":{"x":1},"type":"granular",
        "bpm_min":"fast","bpm_max":120,"keys":["Am","H minor",3,"Dbm"],"tags":"kick","min_rating":9,
        "favourites":"yes","collection":1.5,"root":2,"sort":"loudness","desc":1})").value();
    CHECK(m.text == "pad");
    CHECK(m.type == SampleType::Any);
    CHECK_FALSE(m.bpmMin);
    CHECK(m.bpmMax == 120.0);
    CHECK(m.keys == std::vector<std::string>{"Am", "C#m"});
    CHECK(m.tags.empty());
    CHECK_FALSE(m.minRating);
    CHECK_FALSE(m.favouritesOnly);
    CHECK_FALSE(m.collectionId);
    CHECK(m.rootId == 2);
    CHECK(m.sort == SortField::Name);
    CHECK_FALSE(m.descending);
}

TEST_CASE("saved searches: save, replace by name, list, delete", "[searchjson][userdata]")
{
    Db db = Db::openInMemory();
    UserData user(db);
    SearchModel loops;
    loops.type = SampleType::Loop;
    const auto id = user.saveSearch(" Loops ", loops);

    SearchModel fast;
    fast.bpmMin = 140;
    CHECK(user.saveSearch("LOOPS", fast) == id); // same name, ignoring case: replaced
    user.saveSearch("Ambient", {});

    const auto all = user.savedSearches();
    REQUIRE(all.size() == 2);
    CHECK(all[0].name == "Ambient");
    CHECK(all[1].name == "Loops");
    CHECK(all[1].model.bpmMin == 140.0);
    CHECK(all[1].model.type == SampleType::Any);
    CHECK(user.savedSearchByName("loops")->id == id);

    user.deleteSavedSearch(id);
    CHECK_FALSE(user.savedSearchByName("Loops"));
    CHECK_THROWS_AS(user.deleteSavedSearch(id), UserDataError);
    CHECK_THROWS_AS(user.saveSearch("  ", {}), UserDataError);
}

TEST_CASE("a stored search that no longer parses comes back as the default model", "[searchjson][userdata]")
{
    Db db = Db::openInMemory();
    db.exec("INSERT INTO saved_searches(name, model) VALUES ('Broken', '{not json')");
    UserData user(db);
    const auto broken = user.savedSearchByName("Broken").value();
    CHECK(broken.model.text.empty());
    CHECK(broken.model.type == SampleType::Any);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_search_model_json.cpp:11:11: error: use of undeclared identifier 'searchModelToJson'`

- [ ] **Step 3: Implement**

In `core/include/asma/core/Query.h`, replace:

```cpp
SqlQuery buildSearchSql(const SearchModel& model);
std::vector<SearchRow> search(Db& db, const SearchModel& model);

// Rows for these file ids, in the given order. Ids that are unknown, not ok
// or in a disabled root are left out.
std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids);
```

with:

```cpp
SqlQuery buildSearchSql(const SearchModel& model);
std::vector<SearchRow> search(Db& db, const SearchModel& model);

// The model as one JSON object, for saved searches and plugin state. Paging
// (limit, offset) is view state and is left out; so are default values.
std::string searchModelToJson(const SearchModel& model);

// Reads what searchModelToJson wrote, possibly by another asma version:
// unknown fields are ignored, and so are fields with the wrong type or an
// out-of-range value. Nothing when the text is not a JSON object.
std::optional<SearchModel> searchModelFromJson(std::string_view json);

// Rows for these file ids, in the given order. Ids that are unknown, not ok
// or in a disabled root are left out.
std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids);
```

In `core/include/asma/core/UserData.h`, replace:

```cpp
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <optional>
```

with:

```cpp
#pragma once

#include "asma/core/Db.h"
#include "asma/core/Query.h"

#include <cstdint>
#include <optional>
```

In `core/include/asma/core/UserData.h`, replace:

```cpp
    using std::runtime_error::runtime_error;
};

struct Collection {
    std::int64_t id = 0;
    std::string name;
    std::int64_t size = 0; // files in the collection, any status
};

// What the user adds to a library: ratings, favourites and collections (user
// tags live in Library). Rows hang off file ids, so they survive a file going
// missing and follow it when the scanner re-links it. Each method is one short
// write; callers group writes in a Transaction when they need several at once.
// Throws UserDataError for an unknown file or collection, or a bad value.
```

with:

```cpp
    using std::runtime_error::runtime_error;
};

struct SavedSearch {
    std::int64_t id = 0;
    std::string name;
    SearchModel model; // the default model when the stored text is unreadable
};

struct Collection {
    std::int64_t id = 0;
    std::string name;
    std::int64_t size = 0; // files in the collection, any status
};

// What the user adds to a library: ratings, favourites, collections and saved
// searches (user tags live in Library). Rows hang off file ids, so they survive a file going
// missing and follow it when the scanner re-links it. Each method is one short
// write; callers group writes in a Transaction when they need several at once.
// Throws UserDataError for an unknown file or collection, or a bad value.
```

In `core/include/asma/core/UserData.h`, replace:

```cpp
    void setFavourite(std::int64_t fileId, bool favourite);
    bool isFavourite(std::int64_t fileId);

    // Names are trimmed, non-empty and unique ignoring case.
    std::int64_t createCollection(std::string_view name);
    void renameCollection(std::int64_t id, std::string_view name);
    void deleteCollection(std::int64_t id); // the files stay in the library
```

with:

```cpp
    void setFavourite(std::int64_t fileId, bool favourite);
    bool isFavourite(std::int64_t fileId);

    // Collection and saved-search names are trimmed, non-empty and unique
    // ignoring case.
    std::int64_t createCollection(std::string_view name);
    void renameCollection(std::int64_t id, std::string_view name);
    void deleteCollection(std::int64_t id); // the files stay in the library
```

In `core/include/asma/core/UserData.h`, replace:

```cpp
    void addToCollection(std::int64_t collectionId, std::int64_t fileId);
    void removeFromCollection(std::int64_t collectionId, std::int64_t fileId);

private:
    void requireFile(std::int64_t fileId);
    void requireCollection(std::int64_t id);
    std::string validName(std::string_view name);

    Db& db_;
};
```

with:

```cpp
    void addToCollection(std::int64_t collectionId, std::int64_t fileId);
    void removeFromCollection(std::int64_t collectionId, std::int64_t fileId);

    // Saving under an existing name (ignoring case) replaces that search.
    std::int64_t saveSearch(std::string_view name, const SearchModel& model);
    std::vector<SavedSearch> savedSearches(); // by name
    std::optional<SavedSearch> savedSearchByName(std::string_view name);
    void deleteSavedSearch(std::int64_t id);

private:
    void requireFile(std::int64_t fileId);
    void requireCollection(std::int64_t id);
    static std::string validName(std::string_view name, const char* what);

    Db& db_;
};
```

In `core/src/Query.cpp`, replace:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"

#include <algorithm>
#include <cctype>
#include <type_traits>

namespace asma {
```

with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"

#include "asma/core/Json.h"
#include "asma/core/NameParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>
#include <type_traits>

namespace asma {
```

In `core/src/Query.cpp`, replace:

```cpp
    return rows;
}

} // namespace asma
```

with:

```cpp
    return rows;
}

namespace {

constexpr std::pair<SortField, std::string_view> kSortNames[] = {
    {SortField::Name, "name"}, {SortField::Bpm, "bpm"},       {SortField::Duration, "duration"},
    {SortField::Key, "key"},   {SortField::Rating, "rating"},
};

std::vector<std::string> stringArray(const JsonValue* value)
{
    std::vector<std::string> out;
    if (!value || !value->asArray()) return out;
    for (const auto& item : *value->asArray())
        if (const auto* text = item.asString()) out.push_back(*text);
    return out;
}

std::optional<double> finiteNumber(const JsonValue* value)
{
    if (!value) return std::nullopt;
    const auto number = value->asNumber();
    if (!number || !std::isfinite(*number)) return std::nullopt;
    return number;
}

} // namespace

std::string searchModelToJson(const SearchModel& m)
{
    JsonLine j;
    j.num("v", 1);
    if (!m.text.empty()) j.str("text", m.text);
    if (m.type == SampleType::Loop) j.str("type", "loop");
    if (m.type == SampleType::OneShot) j.str("type", "oneshot");
    if (m.bpmMin) j.real("bpm_min", *m.bpmMin);
    if (m.bpmMax) j.real("bpm_max", *m.bpmMax);
    if (!m.keys.empty()) j.strings("keys", m.keys);
    if (!m.tags.empty()) j.strings("tags", m.tags);
    if (m.durationMin) j.real("duration_min", *m.durationMin);
    if (m.durationMax) j.real("duration_max", *m.durationMax);
    if (!m.formats.empty()) j.strings("formats", m.formats);
    if (m.rootId) j.num("root", *m.rootId);
    if (m.minRating) j.num("min_rating", *m.minRating);
    if (m.favouritesOnly) j.boolean("favourites", true);
    if (m.collectionId) j.num("collection", *m.collectionId);
    if (m.sort != SortField::Name)
        for (const auto& [field, name] : kSortNames)
            if (field == m.sort) j.str("sort", name);
    if (m.descending) j.boolean("desc", true);
    return j.build();
}

std::optional<SearchModel> searchModelFromJson(std::string_view json)
{
    JsonValue doc;
    try {
        doc = parseJson(json);
    } catch (const JsonError&) {
        return std::nullopt;
    }
    if (!doc.isObject()) return std::nullopt;

    SearchModel m;
    if (const auto* v = doc.get("text"); v && v->asString()) m.text = *v->asString();
    if (const auto* v = doc.get("type"); v && v->asString()) {
        if (*v->asString() == "loop") m.type = SampleType::Loop;
        if (*v->asString() == "oneshot") m.type = SampleType::OneShot;
    }
    m.bpmMin = finiteNumber(doc.get("bpm_min"));
    m.bpmMax = finiteNumber(doc.get("bpm_max"));
    // Keep only keys search can match, in canonical spelling.
    for (const auto& key : stringArray(doc.get("keys")))
        if (const auto canonical = parseKeyToken(key)) m.keys.push_back(*canonical);
    m.tags = stringArray(doc.get("tags"));
    m.durationMin = finiteNumber(doc.get("duration_min"));
    m.durationMax = finiteNumber(doc.get("duration_max"));
    m.formats = stringArray(doc.get("formats"));
    if (const auto* v = doc.get("root")) m.rootId = v->asInt();
    if (const auto* v = doc.get("min_rating"))
        if (const auto rating = v->asInt(); rating && *rating >= 1 && *rating <= 5)
            m.minRating = static_cast<int>(*rating);
    if (const auto* v = doc.get("favourites")) m.favouritesOnly = v->asBool().value_or(false);
    if (const auto* v = doc.get("collection")) m.collectionId = v->asInt();
    if (const auto* v = doc.get("sort"); v && v->asString())
        for (const auto& [field, name] : kSortNames)
            if (name == *v->asString()) m.sort = field;
    if (const auto* v = doc.get("desc")) m.descending = v->asBool().value_or(false);
    return m;
}

} // namespace asma
```

In `core/src/UserData.cpp`, replace:

```cpp
    if (!q.step()) throw UserDataError("no collection with id " + std::to_string(id));
}

std::string UserData::validName(std::string_view name)
{
    std::string trimmed = trim(name);
    if (trimmed.empty()) throw UserDataError("a collection needs a name");
    return trimmed;
}
```

with:

```cpp
    if (!q.step()) throw UserDataError("no collection with id " + std::to_string(id));
}

std::string UserData::validName(std::string_view name, const char* what)
{
    std::string trimmed = trim(name);
    if (trimmed.empty()) throw UserDataError(std::string("a ") + what + " needs a name");
    return trimmed;
}
```

In `core/src/UserData.cpp`, replace:

```cpp

std::int64_t UserData::createCollection(std::string_view name)
{
    const std::string valid = validName(name);
    if (collectionByName(valid)) throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("INSERT INTO collections(name) VALUES (?)");
    q.bind(1, std::string_view(valid));
```

with:

```cpp

std::int64_t UserData::createCollection(std::string_view name)
{
    const std::string valid = validName(name, "collection");
    if (collectionByName(valid)) throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("INSERT INTO collections(name) VALUES (?)");
    q.bind(1, std::string_view(valid));
```

In `core/src/UserData.cpp`, replace:

```cpp
void UserData::renameCollection(std::int64_t id, std::string_view name)
{
    requireCollection(id);
    const std::string valid = validName(name);
    if (const auto existing = collectionByName(valid); existing && existing->id != id)
        throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("UPDATE collections SET name = ? WHERE id = ?");
```

with:

```cpp
void UserData::renameCollection(std::int64_t id, std::string_view name)
{
    requireCollection(id);
    const std::string valid = validName(name, "collection");
    if (const auto existing = collectionByName(valid); existing && existing->id != id)
        throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("UPDATE collections SET name = ? WHERE id = ?");
```

In `core/src/UserData.cpp`, replace:

```cpp
    q.run();
}

} // namespace asma
```

with:

```cpp
    q.run();
}

std::int64_t UserData::saveSearch(std::string_view name, const SearchModel& model)
{
    const std::string valid = validName(name, "saved search");
    const std::string json = searchModelToJson(model);
    auto q = db_.prepare("INSERT INTO saved_searches(name, model) VALUES (?, ?) "
                         "ON CONFLICT(name) DO UPDATE SET model = excluded.model RETURNING id");
    q.bind(1, std::string_view(valid)).bind(2, std::string_view(json));
    q.step();
    return q.getInt(0);
}

std::vector<SavedSearch> UserData::savedSearches()
{
    std::vector<SavedSearch> out;
    auto q = db_.prepare("SELECT id, name, model FROM saved_searches ORDER BY name COLLATE NOCASE, id");
    while (q.step())
        out.push_back({q.getInt(0), q.getText(1), searchModelFromJson(q.getText(2)).value_or(SearchModel{})});
    return out;
}

std::optional<SavedSearch> UserData::savedSearchByName(std::string_view name)
{
    const std::string trimmed = trim(name);
    auto q = db_.prepare("SELECT id, name, model FROM saved_searches WHERE name = ?");
    q.bind(1, std::string_view(trimmed));
    if (!q.step()) return std::nullopt;
    return SavedSearch{q.getInt(0), q.getText(1), searchModelFromJson(q.getText(2)).value_or(SearchModel{})};
}

void UserData::deleteSavedSearch(std::int64_t id)
{
    auto exists = db_.prepare("SELECT 1 FROM saved_searches WHERE id = ?");
    exists.bind(1, id);
    if (!exists.step()) throw UserDataError("no saved search with id " + std::to_string(id));
    auto q = db_.prepare("DELETE FROM saved_searches WHERE id = ?");
    q.bind(1, id);
    q.run();
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[searchjson]"`

Expected: no compiler warnings;
`All tests passed (46 assertions in 6 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 174`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: save searches as JSON"
```

---

### Task 5: Organise commands in the CLI

These are the commands the plugin runs as its writer, so each is one
transaction, prints nothing on success and exits 1 on a bad file (the whole
batch is refused). Query option parsing moves to `SearchArgs.cpp` so `query` and
`search save` share it.

**Files:**

- Create: `apps/OrganiseCommands.cpp`, `apps/OrganiseCommands.h`,
  `apps/SearchArgs.cpp`, `apps/SearchArgs.h`
- Modify: `apps/CMakeLists.txt`, `apps/asma_main.cpp`, `tests/test_cli_e2e.cpp`

**Interfaces:**

- Consumes: `UserData`, `Library::removeUserTag` (Task 1), facets (Task 2),
  saved searches (Task 4).
- Produces: `asma rate <0-5>`, `asma fav on|off`, `asma tag add|remove <tag>`
  (each with `<file>...` or `--id N...`),
  `asma collection list|create|rename|delete|add|remove`,
  `asma search list|save|delete`;
  `query --saved NAME --min-rating N --favourites --collection NAME --sort rating --type any`;
  JSON rows gain `rating` and `favourite`. In `apps/SearchArgs.h`:
  `asma::cli::rejectLeftovers`, `asma::cli::toDouble`,
  `SearchModel asma::cli::modelFromArgs(Args&, Db&)`. In
  `apps/OrganiseCommands.h`: `cmdRate`, `cmdFav`, `cmdTag`, `cmdCollection`,
  `cmdSearch`, each `int (Args&, Db&)`.

Behaviour the tests pin:

- `--id` works before or after the value (`asma rate --id 5 4`).
- One unknown file or id fails the whole command with exit 1 and writes nothing;
  bad usage exits 2.
- Options after `--saved` change the saved model for that query only.
- A rating waits for a transaction another process is committing instead of
  failing (Review Focus 1).

- [ ] **Step 1: Write the failing tests**

In `tests/test_cli_e2e.cpp`, replace:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
```

with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
```

In `tests/test_cli_e2e.cpp`, replace:

```cpp
    CHECK(r.out.find("{\"event\":\"analyse_start\",\"path\":\"Drums/Kick") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_done\",\"analysed\":1,\"failed\":0,\"skipped\":0}") != std::string::npos);
}
```

with:

```cpp
    CHECK(r.out.find("{\"event\":\"analyse_start\",\"path\":\"Drums/Kick") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_done\",\"analysed\":1,\"failed\":0,\"skipped\":0}") != std::string::npos);
}

TEST_CASE("rate, fav and tag files by path or id, then filter on them", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");
    const auto kick = quote(cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav"));

    const RunResult rate = cli.runAsma("rate 4 " + loop + " " + kick);
    CHECK(rate.exitCode == 0);
    CHECK(rate.out.empty());
    const std::string kickRow = cli.runAsma("query kick --json").out; // {"id":N,...
    const std::string kickId = kickRow.substr(6, kickRow.find(',') - 6);
    CHECK(cli.runAsma("rate --id " + kickId + " 5").exitCode == 0); // --id before the value still works
    CHECK(cli.runAsma("fav on " + loop).exitCode == 0);
    CHECK(cli.runAsma("tag add Dusty " + kick).exitCode == 0);

    const RunResult top = cli.runAsma("query --min-rating 5 --json");
    CHECK(top.out.find("\"rating\":5,\"favourite\":false") != std::string::npos);
    CHECK(top.out.find("Bass_Loop") == std::string::npos);
    CHECK(cli.runAsma("query --favourites").out.find("Bass_Loop") != std::string::npos);
    CHECK(cli.runAsma("query dusty").out.find("Kick") != std::string::npos);
    const RunResult byRating = cli.runAsma("query --sort rating --desc");
    CHECK(byRating.out.find("Kick") < byRating.out.find("Bass_Loop"));

    CHECK(cli.runAsma("tag remove dusty " + kick).exitCode == 0);
    CHECK(cli.runAsma("query dusty").out.empty());
    CHECK(cli.runAsma("rate 0 --id 1 --id 2").exitCode == 0);
    CHECK(cli.runAsma("query --min-rating 1").out.empty());
}

TEST_CASE("a rating waits for a scan batch that is committing", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);

    // Stands in for asma-scan holding the write lock while it commits a batch.
    asma::Db scanner = asma::Db::open(cli.db);
    auto batch = std::make_unique<asma::Transaction>(scanner);
    std::thread commitLater([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        batch->commit();
    });
    const auto started = std::chrono::steady_clock::now();
    const RunResult rate = cli.runAsma("rate 3 --id 1");
    const auto waited = std::chrono::steady_clock::now() - started;
    commitLater.join();
    CHECK(rate.exitCode == 0);
    CHECK(waited >= std::chrono::milliseconds(400)); // it waited instead of failing
    CHECK_FALSE(cli.runAsma("query --min-rating 3").out.empty());
}

TEST_CASE("organise commands refuse bad input without writing anything", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");

    CHECK(cli.runAsma("rate 6 " + loop).exitCode == 1);
    CHECK(cli.runAsma("rate 2.5 " + loop).exitCode == 2);
    CHECK(cli.runAsma("rate 3").exitCode == 2);
    CHECK(cli.runAsma("fav maybe " + loop).exitCode == 2);
    // One unknown file fails the whole batch; the known one is not rated.
    CHECK(cli.runAsma("rate 3 " + loop + " " + quote(cli.lib / "nope.wav")).exitCode == 1);
    CHECK(cli.runAsma("rate 3 --id 1 --id 999").exitCode == 1);
    CHECK(cli.runAsma("query --min-rating 1").out.empty());
    CHECK(cli.runAsma("query --min-rating 7").exitCode == 2);
    CHECK(cli.runAsma("query --collection Nope").exitCode == 2);
    CHECK(cli.runAsma("query --saved Nope").exitCode == 2);
}

TEST_CASE("collections and saved searches from the CLI", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");

    const RunResult created = cli.runAsma("collection create " + quote(asma::fromUtf8("Set Ü")));
    CHECK(created.exitCode == 0);
    CHECK(created.out == "1\n");
    CHECK(cli.runAsma("collection create " + quote(asma::fromUtf8("SET Ü"))).exitCode == 1);
    CHECK(cli.runAsma("collection add " + quote(asma::fromUtf8("Set Ü")) + " " + loop).exitCode == 0);
    CHECK(cli.runAsma("collection list").out.find("Set Ü\t1") != std::string::npos);
    CHECK(cli.runAsma("query --collection " + quote(asma::fromUtf8("set Ü"))).out.find("Bass_Loop") !=
          std::string::npos);
    CHECK(cli.runAsma("collection list --id 1").exitCode == 2);

    CHECK(cli.runAsma("search save Loops --type loop --key Am").exitCode == 0);
    CHECK(cli.runAsma("search save --type loop Loops").exitCode == 2);
    const RunResult list = cli.runAsma("search list");
    CHECK(list.out.find("Loops\t{\"v\":1,\"type\":\"loop\",\"keys\":[\"Am\"]}") != std::string::npos);
    const RunResult saved = cli.runAsma("query --saved loops");
    CHECK(saved.out.find("Bass_Loop") != std::string::npos);
    CHECK(saved.out.find("Kick") == std::string::npos);
    // Options after --saved change the saved model for this query only.
    CHECK(cli.runAsma("query --saved Loops --type oneshot").out.empty());

    CHECK(cli.runAsma("search delete Loops").exitCode == 0);
    CHECK(cli.runAsma("search delete Loops").exitCode == 1);
    CHECK(cli.runAsma("collection rename " + quote(asma::fromUtf8("Set Ü")) + " Keepers").exitCode == 0);
    CHECK(cli.runAsma("collection delete Keepers").exitCode == 0);
    CHECK(cli.runAsma("query").out.find("Bass_Loop") != std::string::npos); // files stay
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build && ./build/tests/asma_tests "[e2e]"`

Expected: builds, then FAIL:
`test cases: 12 |  8 passed |  4 failed | assertions: 97 | 68 passed | 29 failed`

- [ ] **Step 3: Implement**

In `apps/CMakeLists.txt`, replace:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
add_library(asma_cli_support STATIC Args.cpp CliCommon.cpp)
target_include_directories(asma_cli_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asma_cli_support PUBLIC asma::core)
if(WIN32)
```

with:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
add_library(asma_cli_support STATIC Args.cpp CliCommon.cpp SearchArgs.cpp)
target_include_directories(asma_cli_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asma_cli_support PUBLIC asma::core)
if(WIN32)
```

In `apps/CMakeLists.txt`, replace:

```cmake
endif()
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp)
target_link_libraries(asma PRIVATE asma_cli_support)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)
```

with:

```cmake
endif()
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp OrganiseCommands.cpp)
target_link_libraries(asma PRIVATE asma_cli_support)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)
```

Create `apps/OrganiseCommands.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "OrganiseCommands.h"

#include "CliCommon.h"
#include "SearchArgs.h"

#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace asma::cli {

namespace {

// The files named by --id options and the remaining positional paths. Read
// the --id values before any positional, or an id could be taken for one.
// Throws UsageError when no file is named, UserDataError for a path that is
// not in the library.
std::vector<std::int64_t> targetFiles(Args& args, Db& db, const std::vector<std::string>& idOptions)
{
    std::vector<std::int64_t> ids;
    for (const auto& id : idOptions) ids.push_back(static_cast<std::int64_t>(toDouble(id, "--id")));
    Library lib(db);
    while (const auto path = args.positional()) {
        const auto file = lib.fileByAbsolutePath(fromUtf8(*path));
        if (!file) throw UserDataError("not in the library: " + *path);
        ids.push_back(file->id);
    }
    rejectLeftovers(args);
    if (ids.empty()) throw UsageError("name at least one file, by path or --id N");
    return ids;
}

// Positional arguments are read in order, so this is the next one.
std::string required(Args& args, const char* what)
{
    const auto value = args.positional();
    if (!value) throw UsageError(std::string("missing ") + what);
    return *value;
}

Collection collectionNamed(UserData& user, const std::string& name)
{
    const auto collection = user.collectionByName(name);
    if (!collection) throw UserDataError("no collection called '" + name + "'");
    return *collection;
}

} // namespace

int cmdRate(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    const std::string value = required(args, "rating (0 to 5)");
    const double rating = toDouble(value, "rating");
    if (rating != static_cast<int>(rating)) throw UsageError("a rating is a whole number, 0 to 5");
    const auto ids = targetFiles(args, db, idOptions);
    UserData user(db);
    Transaction tx(db);
    for (const auto id : ids) user.setRating(id, static_cast<int>(rating));
    tx.commit();
    return kOk;
}

int cmdFav(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    const std::string state = required(args, "on or off");
    if (state != "on" && state != "off") throw UsageError("fav needs 'on' or 'off'");
    const auto ids = targetFiles(args, db, idOptions);
    UserData user(db);
    Transaction tx(db);
    for (const auto id : ids) user.setFavourite(id, state == "on");
    tx.commit();
    return kOk;
}

int cmdTag(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    const std::string sub = required(args, "add or remove");
    if (sub != "add" && sub != "remove") throw UsageError("tag needs 'add' or 'remove'");
    const std::string tag = required(args, "tag");
    const auto ids = targetFiles(args, db, idOptions);
    Library lib(db);
    UserData user(db);
    Transaction tx(db);
    for (const auto id : ids) {
        if (!lib.fileById(id)) throw UserDataError("no file with id " + std::to_string(id));
        if (sub == "add") lib.addUserTag(id, tag);
        else lib.removeUserTag(id, tag);
    }
    tx.commit();
    return kOk;
}

int cmdCollection(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    UserData user(db);
    const std::string sub = required(args, "collection command");
    if (sub != "add" && sub != "remove" && !idOptions.empty()) throw UsageError("--id only goes with add or remove");
    if (sub == "list") {
        rejectLeftovers(args);
        for (const auto& c : user.collections()) std::cout << c.id << "\t" << c.name << "\t" << c.size << "\n";
        return kOk;
    }
    if (sub == "create") {
        const std::string name = required(args, "collection name");
        rejectLeftovers(args);
        std::cout << user.createCollection(name) << "\n";
        return kOk;
    }
    if (sub == "rename") {
        const std::string from = required(args, "collection name");
        const std::string to = required(args, "new name");
        rejectLeftovers(args);
        user.renameCollection(collectionNamed(user, from).id, to);
        return kOk;
    }
    if (sub == "delete") {
        const std::string name = required(args, "collection name");
        rejectLeftovers(args);
        user.deleteCollection(collectionNamed(user, name).id);
        return kOk;
    }
    if (sub == "add" || sub == "remove") {
        const std::string name = required(args, "collection name");
        const auto collection = collectionNamed(user, name);
        const auto ids = targetFiles(args, db, idOptions);
        Transaction tx(db);
        for (const auto id : ids) {
            if (sub == "add") user.addToCollection(collection.id, id);
            else user.removeFromCollection(collection.id, id);
        }
        tx.commit();
        return kOk;
    }
    throw UsageError("collection needs list, create, rename, delete, add or remove");
}

int cmdSearch(Args& args, Db& db)
{
    UserData user(db);
    const std::string sub = required(args, "search command");
    if (sub == "list") {
        rejectLeftovers(args);
        for (const auto& s : user.savedSearches())
            std::cout << s.name << "\t" << searchModelToJson(s.model) << "\n";
        return kOk;
    }
    if (sub == "save") {
        // The name first: an option value would otherwise pass for it.
        if (args.rest().empty() || args.rest().front().rfind("--", 0) == 0)
            throw UsageError("search save needs the name before any option");
        const std::string name = required(args, "search name");
        const SearchModel model = modelFromArgs(args, db);
        user.saveSearch(name, model);
        return kOk;
    }
    if (sub == "delete") {
        const std::string name = required(args, "search name");
        rejectLeftovers(args);
        const auto saved = user.savedSearchByName(name);
        if (!saved) throw UserDataError("no saved search called '" + name + "'");
        user.deleteSavedSearch(saved->id);
        return kOk;
    }
    throw UsageError("search needs list, save or delete");
}

} // namespace asma::cli
```

Create `apps/OrganiseCommands.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include "asma/core/Db.h"

namespace asma::cli {

// User-data commands. The plugin runs these as its out-of-process writer, so
// each one is a single transaction and prints nothing on success.
int cmdRate(Args& args, Db& db);
int cmdFav(Args& args, Db& db);
int cmdTag(Args& args, Db& db);
int cmdCollection(Args& args, Db& db);
int cmdSearch(Args& args, Db& db);

} // namespace asma::cli
```

Create `apps/SearchArgs.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "SearchArgs.h"

#include "asma/core/NameParse.h"
#include "asma/core/UserData.h"

namespace asma::cli {

void rejectLeftovers(const Args& args)
{
    if (!args.rest().empty()) throw UsageError("unexpected argument: " + args.rest().front());
}

double toDouble(const std::string& text, const char* what)
{
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size()) return value;
    } catch (const std::exception&) {
    }
    throw UsageError(std::string("not a number for ") + what + ": " + text);
}

SearchModel modelFromArgs(Args& args, Db& db)
{
    UserData user(db);
    SearchModel m;
    if (const auto saved = args.option("saved")) {
        const auto found = user.savedSearchByName(*saved);
        if (!found) throw UsageError("no saved search called '" + *saved + "'");
        m = found->model;
    }
    if (const auto type = args.option("type")) {
        if (*type == "loop") m.type = SampleType::Loop;
        else if (*type == "oneshot") m.type = SampleType::OneShot;
        else if (*type == "any") m.type = SampleType::Any;
        else throw UsageError("--type must be loop, oneshot or any");
    }
    if (const auto bpm = args.option("bpm")) {
        const auto dash = bpm->find('-');
        if (dash == std::string::npos) {
            const double v = toDouble(*bpm, "--bpm");
            m.bpmMin = v - 0.5;
            m.bpmMax = v + 0.5;
        } else {
            m.bpmMin = toDouble(bpm->substr(0, dash), "--bpm");
            m.bpmMax = toDouble(bpm->substr(dash + 1), "--bpm");
        }
    }
    for (const auto& key : args.options("key")) {
        const auto canonical = parseKeyToken(key);
        if (!canonical) throw UsageError("not a key: " + key + " (try Am, C#m, Eb, F#maj)");
        m.keys.push_back(*canonical);
    }
    for (auto& tag : args.options("tag")) m.tags.push_back(std::move(tag));
    for (auto& format : args.options("format")) m.formats.push_back(std::move(format));
    if (const auto v = args.option("min-duration")) m.durationMin = toDouble(*v, "--min-duration");
    if (const auto v = args.option("max-duration")) m.durationMax = toDouble(*v, "--max-duration");
    if (const auto v = args.option("min-rating")) {
        const double rating = toDouble(*v, "--min-rating");
        if (rating < 1 || rating > 5 || rating != static_cast<int>(rating))
            throw UsageError("--min-rating must be 1 to 5");
        m.minRating = static_cast<int>(rating);
    }
    if (args.flag("favourites")) m.favouritesOnly = true;
    if (const auto name = args.option("collection")) {
        const auto collection = user.collectionByName(*name);
        if (!collection) throw UsageError("no collection called '" + *name + "'");
        m.collectionId = collection->id;
    }
    if (const auto sort = args.option("sort")) {
        if (*sort == "name") m.sort = SortField::Name;
        else if (*sort == "bpm") m.sort = SortField::Bpm;
        else if (*sort == "duration") m.sort = SortField::Duration;
        else if (*sort == "key") m.sort = SortField::Key;
        else if (*sort == "rating") m.sort = SortField::Rating;
        else throw UsageError("--sort must be name, bpm, duration, key or rating");
    }
    if (args.flag("desc")) m.descending = true;
    if (const auto limit = args.option("limit")) m.limit = static_cast<int>(toDouble(*limit, "--limit"));

    std::string words;
    for (const auto& word : args.rest()) {
        if (word.rfind("--", 0) == 0) throw UsageError("unknown option: " + word);
        if (!words.empty()) words += ' ';
        words += word;
    }
    if (!words.empty()) m.text = words;
    return m;
}

} // namespace asma::cli
```

Create `apps/SearchArgs.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include "asma/core/Db.h"
#include "asma/core/Query.h"

#include <string>

namespace asma::cli {

void rejectLeftovers(const Args& args);
double toDouble(const std::string& text, const char* what);

// Builds a search model from query options and the remaining words, starting
// from --saved NAME when given. Consumes everything it reads and throws
// UsageError for an unknown option, a bad value or an unknown collection or
// saved search. --limit is read too; the caller decides whether it matters.
SearchModel modelFromArgs(Args& args, Db& db);

} // namespace asma::cli
```

In `apps/asma_main.cpp`, replace:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Analyser.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/NameParse.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/Similar.h"
```

with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"
#include "CliCommon.h"
#include "OrganiseCommands.h"
#include "SearchArgs.h"

#include "asma/core/Analyser.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/Similar.h"
```

In `apps/asma_main.cpp`, replace:

```cpp
    "  root add <dir>          add a sample folder\n"
    "  root list               list sample folders\n"
    "  scan [--root ID] [--threads N] [--no-analysis]\n"
    "  query [words...] [--type loop|oneshot] [--bpm N|MIN-MAX] [--key K]...\n"
    "        [--tag T]... [--format F]... [--min-duration S] [--max-duration S]\n"
    "        [--sort name|bpm|duration|key] [--desc] [--limit N] [--json]\n"
    "  similar <file> [--limit N] [--json]   files that sound like <file>\n"
    "  --version\n";

void rejectLeftovers(const Args& args)
{
    if (!args.rest().empty()) throw UsageError("unexpected argument: " + args.rest().front());
}

double toDouble(const std::string& text, const char* what)
{
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size()) return value;
    } catch (const std::exception&) {
    }
    throw UsageError(std::string("not a number for ") + what + ": " + text);
}

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON.
void printRows(const std::vector<SearchRow>& rows, bool json, const std::vector<double>& similarity)
{
    for (std::size_t i = 0; i < rows.size(); ++i) {
```

with:

```cpp
    "  root add <dir>          add a sample folder\n"
    "  root list               list sample folders\n"
    "  scan [--root ID] [--threads N] [--no-analysis]\n"
    "  query [words...] [--saved NAME] [--type loop|oneshot|any] [--bpm N|MIN-MAX]\n"
    "        [--key K]... [--tag T]... [--format F]... [--min-duration S]\n"
    "        [--max-duration S] [--min-rating N] [--favourites] [--collection NAME]\n"
    "        [--sort name|bpm|duration|key|rating] [--desc] [--limit N] [--json]\n"
    "  similar <file> [--limit N] [--json]   files that sound like <file>\n"
    "  rate <0-5> <file>... | --id N...      0 clears the rating\n"
    "  fav on|off <file>... | --id N...\n"
    "  tag add|remove <tag> <file>... | --id N...\n"
    "  collection list | create <name> | rename <name> <new> | delete <name>\n"
    "  collection add|remove <name> <file>... | --id N...\n"
    "  search list | save <name> [query options] [words...] | delete <name>\n"
    "  --version\n";

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
// which also carries the rating and favourite flag.
void printRows(const std::vector<SearchRow>& rows, bool json, const std::vector<double>& similarity)
{
    for (std::size_t i = 0; i < rows.size(); ++i) {
```

In `apps/asma_main.cpp`, replace:

```cpp
            else line.null("key");
            if (row.isLoop) line.boolean("is_loop", *row.isLoop);
            else line.null("is_loop");
            if (i < similarity.size()) line.real("similarity", similarity[i]);
            std::cout << line.build() << "\n";
        } else {
```

with:

```cpp
            else line.null("key");
            if (row.isLoop) line.boolean("is_loop", *row.isLoop);
            else line.null("is_loop");
            if (row.rating) line.num("rating", *row.rating);
            else line.null("rating");
            line.boolean("favourite", row.favourite);
            if (i < similarity.size()) line.real("similarity", similarity[i]);
            std::cout << line.build() << "\n";
        } else {
```

In `apps/asma_main.cpp`, replace:

```cpp

int cmdQuery(Args& args, Db& db)
{
    SearchModel m;
    if (const auto type = args.option("type")) {
        if (*type == "loop") m.type = SampleType::Loop;
        else if (*type == "oneshot") m.type = SampleType::OneShot;
        else throw UsageError("--type must be loop or oneshot");
    }
    if (const auto bpm = args.option("bpm")) {
        const auto dash = bpm->find('-');
        if (dash == std::string::npos) {
            const double v = toDouble(*bpm, "--bpm");
            m.bpmMin = v - 0.5;
            m.bpmMax = v + 0.5;
        } else {
            m.bpmMin = toDouble(bpm->substr(0, dash), "--bpm");
            m.bpmMax = toDouble(bpm->substr(dash + 1), "--bpm");
        }
    }
    for (const auto& key : args.options("key")) {
        const auto canonical = parseKeyToken(key);
        if (!canonical) throw UsageError("not a key: " + key + " (try Am, C#m, Eb, F#maj)");
        m.keys.push_back(*canonical);
    }
    m.tags = args.options("tag");
    m.formats = args.options("format");
    if (const auto v = args.option("min-duration")) m.durationMin = toDouble(*v, "--min-duration");
    if (const auto v = args.option("max-duration")) m.durationMax = toDouble(*v, "--max-duration");
    if (const auto sort = args.option("sort")) {
        if (*sort == "name") m.sort = SortField::Name;
        else if (*sort == "bpm") m.sort = SortField::Bpm;
        else if (*sort == "duration") m.sort = SortField::Duration;
        else if (*sort == "key") m.sort = SortField::Key;
        else throw UsageError("--sort must be name, bpm, duration or key");
    }
    m.descending = args.flag("desc");
    if (const auto limit = args.option("limit")) m.limit = static_cast<int>(toDouble(*limit, "--limit"));
    const bool json = args.flag("json");

    for (const auto& word : args.rest()) {
        if (word.rfind("--", 0) == 0) throw UsageError("unknown option: " + word);
        if (!m.text.empty()) m.text += ' ';
        m.text += word;
    }

    printRows(search(db, m), json, {});
    return kOk;
}
```

with:

```cpp

int cmdQuery(Args& args, Db& db)
{
    const bool json = args.flag("json");
    printRows(search(db, modelFromArgs(args, db)), json, {});
    return kOk;
}
```

In `apps/asma_main.cpp`, replace:

```cpp
        const std::filesystem::path dbPath = resolveDbPath(args);
        const auto command = args.positional();
        if (!command) throw UsageError("missing command");
        if (*command != "root" && *command != "scan" && *command != "query" && *command != "similar")
            throw UsageError("unknown command: " + *command);

        Db db = Db::open(dbPath);
        if (*command == "root") return cmdRoot(args, db);
        if (*command == "scan") return cmdScan(args, db, dbPath);
        if (*command == "similar") return cmdSimilar(args, db);
        return cmdQuery(args, db);
    } catch (const UsageError& e) {
        std::cerr << "asma: " << e.what() << "\n" << kUsageText;
        return kUsage;
```

with:

```cpp
        const std::filesystem::path dbPath = resolveDbPath(args);
        const auto command = args.positional();
        if (!command) throw UsageError("missing command");
        using Command = int (*)(Args&, Db&);
        const std::pair<const char*, Command> commands[] = {
            {"root", cmdRoot},
            {"query", cmdQuery},
            {"similar", cmdSimilar},
            {"rate", cmdRate},
            {"fav", cmdFav},
            {"tag", cmdTag},
            {"collection", cmdCollection},
            {"search", cmdSearch},
        };
        if (*command == "scan") {
            Db db = Db::open(dbPath);
            return cmdScan(args, db, dbPath);
        }
        for (const auto& [name, run] : commands) {
            if (*command != name) continue;
            Db db = Db::open(dbPath);
            return run(args, db);
        }
        throw UsageError("unknown command: " + *command);
    } catch (const UsageError& e) {
        std::cerr << "asma: " << e.what() << "\n" << kUsageText;
        return kUsage;
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build && ./build/tests/asma_tests "[e2e]"`

Expected: no compiler warnings;
`All tests passed (97 assertions in 12 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 178`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "apps: rate, favourite, tag and collect files, and save searches, from the CLI"
```

---

### Task 6: Running a child process

The supervisor needs to start `asma-scan`, read its lines as they come, notice
how it ended and kill it on cancel, identically on three platforms and from
inside a plugin host. `tests/child/asma_test_child.cpp` is a helper executable
the tests drive; Task 8 adds a fake `asma-scan` mode to it.

**Files:**

- Create: `core/include/asma/core/Subprocess.h`, `core/src/Subprocess.cpp`,
  `tests/child/asma_test_child.cpp`, `tests/test_subprocess.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `toUtf8` (plan 1), `Args::fromMain` (UTF-8 argv in the child).
- Produces: `class asma::SubprocessError`;
  `struct asma::ExitStatus { bool signalled; int code; }`;
  `class asma::Subprocess` (move-only) with
  `static Subprocess start(const std::filesystem::path&, const std::vector<std::string>&)`,
  `std::optional<std::string> readLine()`, `ExitStatus wait()`, `void kill()`;
  CMake target `asma_test_child` and the test define `ASMA_TEST_CHILD_PATH`.

Behaviour the tests pin:

- Lines arrive in order without `\n` or `\r\n`; a final line without a newline
  is still returned; then the end of the stream.
- Arguments with spaces, quotes, trailing backslashes, empty strings and
  non-ASCII text arrive unchanged (Windows quoting follows
  `CommandLineToArgvW`).
- A crash is reported as `signalled`; `kill()` from another thread ends a
  blocked `readLine()`; destroying a running child kills it; a missing program
  throws `SubprocessError`.

On POSIX, `wait()` first waits with `WNOWAIT` and reaps under the mutex that
`kill()` takes, so `kill()` can never signal a recycled pid.

- [ ] **Step 1: Write the failing tests**

In `tests/CMakeLists.txt`, replace:

```cmake
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma_cli_support Catch2::Catch2WithMain)
```

with:

```cmake
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(asma_test_child child/asma_test_child.cpp)
target_link_libraries(asma_test_child PRIVATE asma_cli_support)
asma_set_warnings(asma_test_child)

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma_cli_support Catch2::Catch2WithMain)
```

In `tests/CMakeLists.txt`, replace:

```cmake
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  ASMA_VERSION="${PROJECT_VERSION}"
  ASMA_CLI_PATH="$<TARGET_FILE:asma>"
  ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>")
add_dependencies(asma_tests asma asma-scan)
asma_set_warnings(asma_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
```

with:

```cmake
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  ASMA_VERSION="${PROJECT_VERSION}"
  ASMA_CLI_PATH="$<TARGET_FILE:asma>"
  ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>"
  ASMA_TEST_CHILD_PATH="$<TARGET_FILE:asma_test_child>")
add_dependencies(asma_tests asma asma-scan asma_test_child)
asma_set_warnings(asma_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
```

Create `tests/child/asma_test_child.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// A child process for Subprocess and ScanSupervisor tests.
//   lines A B...  each argument on its own line
//   partial X     X with no line ending
//   crlf X        X ending in \r\n
//   args A B...   each argument as a JSON string, one per line
//   exit N        exit with code N
//   crash         die the way a real crash does
//   hang          print "ready", then sleep for a minute
#include "Args.h"

#include "asma/core/Json.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

[[noreturn]] void crash()
{
    std::cout << std::flush;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX); // no crash dialog on CI
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    std::abort();
}

int main(int argc, char** argv)
{
    asma::cli::Args parsed = asma::cli::Args::fromMain(argc, argv);
    std::vector<std::string> args = parsed.rest();
    if (args.empty()) return 2;
    const std::string mode = args.front();
    args.erase(args.begin());

    if (mode == "lines")
        for (const auto& a : args) std::cout << a << "\n";
    else if (mode == "partial")
        std::cout << args.at(0);
    else if (mode == "crlf")
        std::cout << args.at(0) << "\r\n";
    else if (mode == "args")
        for (const auto& a : args) std::cout << '"' << asma::jsonEscape(a) << "\"\n";
    else if (mode == "exit")
        return std::stoi(args.at(0));
    else if (mode == "crash")
        crash();
    else if (mode == "hang") {
        std::cout << "ready" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));
    } else
        return 2;
    return 0;
}
```

Create `tests/test_subprocess.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Subprocess.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace asma;
using Strings = std::vector<std::string>;

namespace {

Subprocess child(const Strings& args) { return Subprocess::start(fromUtf8(ASMA_TEST_CHILD_PATH), args); }

Strings readAll(Subprocess& p)
{
    Strings lines;
    while (auto line = p.readLine()) lines.push_back(*line);
    return lines;
}

} // namespace

TEST_CASE("lines come back in order, then the end of the stream", "[subprocess]")
{
    auto p = child({"lines", "one", "two", "three"});
    CHECK(readAll(p) == Strings{"one", "two", "three"});
    CHECK_FALSE(p.readLine());
    const ExitStatus status = p.wait();
    CHECK_FALSE(status.signalled);
    CHECK(status.code == 0);
}

TEST_CASE("a last line without a newline, and CRLF endings", "[subprocess]")
{
    auto partial = child({"partial", "tail"});
    CHECK(readAll(partial) == Strings{"tail"});
    auto crlf = child({"crlf", "x"});
    CHECK(readAll(crlf) == Strings{"x"});
}

TEST_CASE("exit codes are reported; wait can be called again", "[subprocess]")
{
    auto p = child({"exit", "7"});
    CHECK(readAll(p).empty());
    CHECK(p.wait().code == 7);
    CHECK(p.wait().code == 7);
}

TEST_CASE("a crash is reported as signalled", "[subprocess]")
{
    auto p = child({"crash"});
    readAll(p);
    CHECK(p.wait().signalled);
}

TEST_CASE("arguments arrive unchanged, whatever they contain", "[subprocess]")
{
    const Strings tricky = {"plain", "two words", "", "quote\"inside", "trailing\\", "back\\\\\"slash",
                            "Café Ü 日本", "--db", "tab\there"};
    Strings args = {"args"};
    args.insert(args.end(), tricky.begin(), tricky.end());
    auto p = child(args);
    Strings expected;
    for (const auto& t : tricky) expected.push_back("\"" + jsonEscape(t) + "\"");
    CHECK(readAll(p) == expected);
    CHECK(p.wait().code == 0);
}

TEST_CASE("kill from another thread ends a blocked read", "[subprocess]")
{
    auto p = child({"hang"});
    REQUIRE(p.readLine() == "ready");
    const auto started = std::chrono::steady_clock::now();
    std::thread killer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        p.kill();
    });
    CHECK_FALSE(p.readLine());
    killer.join();
    p.wait();
    p.kill(); // after exit: harmless
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST_CASE("destroying a running child kills it instead of hanging", "[subprocess]")
{
    const auto started = std::chrono::steady_clock::now();
    {
        auto p = child({"hang"});
        REQUIRE(p.readLine() == "ready");
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST_CASE("a program that does not exist cannot start", "[subprocess]")
{
    CHECK_THROWS_AS(Subprocess::start(fromUtf8(ASMA_TEST_CHILD_PATH).parent_path() / "no-such-program", {}),
                    SubprocessError);
}

TEST_CASE("a moved-from Subprocess is inert and the new owner reads on", "[subprocess]")
{
    auto a = child({"lines", "x", "y"});
    REQUIRE(a.readLine() == "x");
    Subprocess b = std::move(a);
    CHECK(b.readLine() == "y");
    CHECK(b.wait().code == 0);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_subprocess.cpp:4:10: fatal error: 'asma/core/Subprocess.h' file not found`

- [ ] **Step 3: Implement**

Create `core/include/asma/core/Subprocess.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace asma {

class SubprocessError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ExitStatus {
    // Killed by a signal (POSIX) or ended by an unhandled exception
    // (Windows NTSTATUS 0xC0000000 and up). Do not rely on it alone to spot a
    // crash: abort() exits with code 3 on Windows.
    bool signalled = false;
    int code = 0; // exit code, or the signal number when signalled
};

// A child process whose stdout is read line by line. stdin and stderr are the
// null device (a plugin host may have closed its own stderr), and no other
// handle of this process leaks into the child, so several instances can run
// side by side in one host. On Windows the child gets no console window.
class Subprocess {
public:
    // Throws SubprocessError when the program cannot be started.
    static Subprocess start(const std::filesystem::path& program, const std::vector<std::string>& args);

    ~Subprocess(); // kills and reaps a child that is still running
    Subprocess(Subprocess&& other) noexcept;
    Subprocess& operator=(Subprocess&& other) noexcept;
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    // The next line without its line ending; nothing once stdout is closed.
    // A final line with no newline is still returned.
    std::optional<std::string> readLine();

    // Blocks until the child exits; later calls return the same status.
    ExitStatus wait();

    // Ends the child at once. Safe to call from another thread while
    // readLine() or wait() blocks, and after the child has exited.
    void kill();

private:
    Subprocess() = default;
    void close() noexcept;
    bool fill(); // reads more of stdout into buffer_; false at end of stream

#ifdef _WIN32
    void* process_ = nullptr;
    void* stdout_ = nullptr;
#else
    int pid_ = -1;
    int stdout_ = -1;
#endif
    std::string buffer_;
    bool eof_ = false;
    std::optional<ExitStatus> status_;
    std::unique_ptr<std::mutex> mutex_ = std::make_unique<std::mutex>(); // guards pid/process against kill()
};

} // namespace asma
```

Create `core/src/Subprocess.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Subprocess.h"

#include "asma/core/Fs.h"

#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace asma {

namespace {

#ifdef _WIN32

std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

// Quotes one argument so CommandLineToArgvW gives it back unchanged.
void appendQuoted(std::wstring& line, const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // Backslashes are literal unless they precede a quote.
        line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        line += c;
    }
    line.append(backslashes * 2, L'\\'); // before the closing quote
    line += L'"';
}

std::string lastError(const char* what)
{
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

#else

char** currentEnvironment()
{
#ifdef __APPLE__
    return *_NSGetEnviron(); // `environ` is not available to a plugin bundle
#else
    return environ;
#endif
}

#endif

} // namespace

#ifdef _WIN32

Subprocess Subprocess::start(const std::filesystem::path& program, const std::vector<std::string>& args)
{
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) throw SubprocessError(lastError("CreatePipe"));
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) {
        const std::string error = lastError("opening NUL");
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        throw SubprocessError(error);
    }

    // Only these handles reach the child, so a pipe of another instance
    // started at the same moment cannot leak into it and hold it open.
    HANDLE inherited[2] = {writeEnd, nul};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuffer(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr,
                              nullptr);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul;
    startup.StartupInfo.hStdOutput = writeEnd;
    startup.StartupInfo.hStdError = nul;
    startup.lpAttributeList = attrs;

    const std::wstring exe = program.wstring();
    std::wstring line;
    appendQuoted(line, exe);
    for (const auto& arg : args) {
        line += L' ';
        appendQuoted(line, widen(arg));
    }

    PROCESS_INFORMATION info{};
    const BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                   &startup.StartupInfo, &info);
    const std::string error = ok ? std::string() : lastError("CreateProcess");
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(writeEnd);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(readEnd);
        throw SubprocessError(error + ": " + toUtf8(program));
    }
    CloseHandle(info.hThread);

    Subprocess p;
    p.process_ = info.hProcess;
    p.stdout_ = readEnd;
    return p;
}

bool Subprocess::fill()
{
    char chunk[4096];
    DWORD got = 0;
    if (!ReadFile(stdout_, chunk, sizeof chunk, &got, nullptr) || got == 0) return false;
    buffer_.append(chunk, got);
    return true;
}

ExitStatus Subprocess::wait()
{
    if (status_) return *status_;
    WaitForSingleObject(process_, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    ExitStatus status;
    status.signalled = code >= 0xC0000000u;
    status.code = static_cast<int>(code);
    status_ = status;
    return status;
}

void Subprocess::kill()
{
    std::lock_guard lock(*mutex_);
    // The handle keeps the process object alive, so this can never hit a
    // different process; on one that has exited it simply fails.
    if (process_) TerminateProcess(process_, 1);
}

void Subprocess::close() noexcept
{
    if (process_) {
        if (!status_) {
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, INFINITE);
        }
        CloseHandle(process_);
    }
    if (stdout_) CloseHandle(stdout_);
    process_ = nullptr;
    stdout_ = nullptr;
}

Subprocess::Subprocess(Subprocess&& other) noexcept
    : process_(std::exchange(other.process_, nullptr)), stdout_(std::exchange(other.stdout_, nullptr)),
      buffer_(std::move(other.buffer_)), eof_(other.eof_), status_(other.status_), mutex_(std::move(other.mutex_))
{
    other.mutex_ = std::make_unique<std::mutex>();
}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept
{
    if (this != &other) {
        close();
        process_ = std::exchange(other.process_, nullptr);
        stdout_ = std::exchange(other.stdout_, nullptr);
        buffer_ = std::move(other.buffer_);
        eof_ = other.eof_;
        status_ = other.status_;
        std::swap(mutex_, other.mutex_);
    }
    return *this;
}

#else

Subprocess Subprocess::start(const std::filesystem::path& program, const std::vector<std::string>& args)
{
    int fds[2];
#ifdef __APPLE__
    if (::pipe(fds) != 0) throw SubprocessError(std::string("pipe failed: ") + std::strerror(errno));
    // POSIX_SPAWN_CLOEXEC_DEFAULT below keeps these out of every other child.
#else
    // Close-on-exec from the start, so a child another thread spawns at this
    // moment cannot inherit the pipe and hold it open.
    if (::pipe2(fds, O_CLOEXEC) != 0) throw SubprocessError(std::string("pipe2 failed: ") + std::strerror(errno));
#endif

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#ifdef __APPLE__
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

    const std::string exe = program.string();
    std::vector<std::string> argvStrings;
    argvStrings.push_back(exe);
    argvStrings.insert(argvStrings.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : argvStrings) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), currentEnvironment());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        throw SubprocessError("cannot start " + exe + ": " + std::strerror(rc));
    }

    Subprocess p;
    p.pid_ = pid;
    p.stdout_ = fds[0];
    return p;
}

bool Subprocess::fill()
{
    char chunk[4096];
    for (;;) {
        const ssize_t got = ::read(stdout_, chunk, sizeof chunk);
        if (got > 0) {
            buffer_.append(chunk, static_cast<std::size_t>(got));
            return true;
        }
        if (got < 0 && errno == EINTR) continue;
        return false;
    }
}

ExitStatus Subprocess::wait()
{
    if (status_) return *status_;
    // Wait without reaping, so the pid stays ours (a zombie) until the reap
    // below, which happens under the lock kill() takes.
    siginfo_t info{};
    while (::waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOWAIT) != 0 && errno == EINTR) {
    }
    std::lock_guard lock(*mutex_);
    int raw = 0;
    while (::waitpid(pid_, &raw, 0) < 0 && errno == EINTR) {
    }
    ExitStatus status;
    if (WIFSIGNALED(raw)) {
        status.signalled = true;
        status.code = WTERMSIG(raw);
    } else {
        status.code = WEXITSTATUS(raw);
    }
    status_ = status;
    pid_ = -1;
    return status;
}

void Subprocess::kill()
{
    std::lock_guard lock(*mutex_);
    if (pid_ > 0) ::kill(pid_, SIGKILL);
}

void Subprocess::close() noexcept
{
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        int raw = 0;
        while (::waitpid(pid_, &raw, 0) < 0 && errno == EINTR) {
        }
    }
    if (stdout_ >= 0) ::close(stdout_);
    pid_ = -1;
    stdout_ = -1;
}

Subprocess::Subprocess(Subprocess&& other) noexcept
    : pid_(std::exchange(other.pid_, -1)), stdout_(std::exchange(other.stdout_, -1)), buffer_(std::move(other.buffer_)),
      eof_(other.eof_), status_(other.status_), mutex_(std::move(other.mutex_))
{
    other.mutex_ = std::make_unique<std::mutex>();
}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept
{
    if (this != &other) {
        close();
        pid_ = std::exchange(other.pid_, -1);
        stdout_ = std::exchange(other.stdout_, -1);
        buffer_ = std::move(other.buffer_);
        eof_ = other.eof_;
        status_ = other.status_;
        std::swap(mutex_, other.mutex_);
    }
    return *this;
}

#endif

Subprocess::~Subprocess() { close(); }

std::optional<std::string> Subprocess::readLine()
{
    for (;;) {
        if (const auto newline = buffer_.find('\n'); newline != std::string::npos) {
            std::string line = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        if (eof_ || !fill()) {
            eof_ = true;
            if (buffer_.empty()) return std::nullopt;
            std::string rest = std::move(buffer_);
            buffer_.clear();
            if (!rest.empty() && rest.back() == '\r') rest.pop_back();
            return rest;
        }
    }
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[subprocess]"`

Expected: no compiler warnings;
`All tests passed (21 assertions in 9 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 187`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: run a child process and read its output line by line"
```

---

### Task 7: Scanner events and crash recovery

The recovery contract in `docs/scan-protocol.md`, as a state machine fed with
parsed events. Keeping it free of processes lets every branch be tested in
microseconds.

**Files:**

- Create: `core/include/asma/core/ScanEvents.h`, `core/src/ScanEvents.cpp`,
  `tests/test_scan_events.cpp`

**Interfaces:**

- Consumes: `parseJson` (Task 3), `ScanStats` (plan 1), `AnalyseStats` (plan 2).
- Produces: `enum class asma::ScanPhase { Index, Analyse }`;
  `struct asma::ScanEvent` (`kind`, `path`, `done`, `total`, `index`,
  `analysis`, `code`, `message`, `pid`);
  `std::optional<ScanEvent> asma::parseScanEvent(std::string_view)`;
  `struct asma::ScanAttempt { unsigned threads; std::vector<std::string> fail, failAnalysis; }`;
  `class asma::ScanRecovery(unsigned threads, bool analyse)` with `attempt()`,
  `onEvent(const ScanEvent&)`, `Next onExit()` (`Finished`, `Retry`, `Locked`,
  `Failed`, `Crashed`), `culprits()`, `error()`.

Behaviour the tests pin:

- Lines that are not events are ignored; unknown events parse as `Unknown`.
- A parallel crash retries on one thread; a one-thread crash marks the file in
  flight, in the right phase, and restores the requested thread count.
- The same path in both phases is tracked separately.
- Two unexplained crashes, or a marked file crashing again, end with `Crashed`;
  `error` events end with `Locked` or `Failed`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_scan_events.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ScanEvents.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using Kind = ScanEvent::Kind;
using Next = ScanRecovery::Next;
using Strings = std::vector<std::string>;

namespace {

void feed(ScanRecovery& r, std::initializer_list<const char*> lines)
{
    for (const char* line : lines) r.onEvent(parseScanEvent(line).value());
}

} // namespace

TEST_CASE("parseScanEvent reads every event of the protocol", "[scanevents]")
{
    const auto start = parseScanEvent(R"({"event":"start","path":"Drums/Kick Ü.wav"})").value();
    CHECK(start.kind == Kind::Start);
    CHECK(start.path == "Drums/Kick Ü.wav");

    const auto progress = parseScanEvent(R"({"event":"analyse_progress","done":3,"total":8,"path":"a.wav"})");
    CHECK(progress->kind == Kind::AnalyseProgress);
    CHECK(progress->done == 3);
    CHECK(progress->total == 8);

    const auto done = parseScanEvent(
        R"({"event":"done","added":1,"updated":2,"unchanged":3,"relinked":4,"missing":5,"failed":6,"skipped":7})");
    CHECK(done->index.added == 1);
    CHECK(done->index.relinked == 4);
    CHECK(done->index.skipped == 7);

    const auto analysed = parseScanEvent(R"({"event":"analyse_done","analysed":9,"failed":1,"skipped":2})");
    CHECK(analysed->analysis.analysed == 9);
    CHECK(analysed->analysis.skipped == 2);

    const auto locked = parseScanEvent(R"({"event":"error","code":"locked","pid":4242})");
    CHECK(locked->kind == Kind::Error);
    CHECK(locked->code == "locked");
    CHECK(locked->pid == 4242);

    CHECK(parseScanEvent(R"({"event":"marked_failed","path":"x"})")->kind == Kind::MarkedFailed);
    CHECK(parseScanEvent(R"({"event":"marked_analysis_failed","path":"x"})")->kind == Kind::MarkedAnalysisFailed);
    CHECK(parseScanEvent(R"({"event":"analyse_start","path":"x"})")->kind == Kind::AnalyseStart);
    CHECK(parseScanEvent(R"({"event":"progress","done":1,"total":1,"path":"x"})")->kind == Kind::Progress);
}

TEST_CASE("parseScanEvent ignores what is not an event", "[scanevents]")
{
    CHECK_FALSE(parseScanEvent(""));
    CHECK_FALSE(parseScanEvent("warning: something"));
    CHECK_FALSE(parseScanEvent(R"({"path":"x"})"));
    CHECK_FALSE(parseScanEvent(R"({"event":5})"));
    CHECK_FALSE(parseScanEvent(R"({"event":"start","path":"cut off)"));
    CHECK(parseScanEvent(R"({"event":"from_the_future","x":1})")->kind == Kind::Unknown);
    CHECK(parseScanEvent(R"({"event":"done","added":-5})")->index.added == 0);
}

TEST_CASE("a run that reports done and analyse_done is finished", "[scanevents]")
{
    ScanRecovery r(4, true);
    feed(r, {R"({"event":"start","path":"a"})", R"({"event":"progress","done":1,"total":1,"path":"a"})",
             R"({"event":"done"})", R"({"event":"analyse_done"})"});
    CHECK(r.onExit() == Next::Finished);

    ScanRecovery indexOnly(4, false);
    feed(indexOnly, {R"({"event":"done"})"});
    CHECK(indexOnly.onExit() == Next::Finished);
}

TEST_CASE("errors end the scan without a retry", "[scanevents]")
{
    ScanRecovery locked(0, true);
    feed(locked, {R"({"event":"error","code":"locked","pid":7})"});
    CHECK(locked.onExit() == Next::Locked);
    CHECK(locked.error()->pid == 7);

    ScanRecovery failed(0, true);
    feed(failed, {R"({"event":"error","code":"failed","message":"disk I/O error"})"});
    CHECK(failed.onExit() == Next::Failed);
    CHECK(failed.error()->message == "disk I/O error");
}

TEST_CASE("a parallel crash retries on one thread, then marks the file", "[scanevents]")
{
    ScanRecovery r(4, true);
    CHECK(r.attempt().threads == 4);
    feed(r, {R"({"event":"start","path":"a"})", R"({"event":"start","path":"b"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().threads == 1);
    CHECK(r.attempt().fail.empty());

    feed(r, {R"({"event":"start","path":"a"})", R"({"event":"progress","done":1,"total":2,"path":"a"})",
             R"({"event":"start","path":"b"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().threads == 4); // back to full speed once the culprit is known
    CHECK(r.attempt().fail == Strings{"b"});
    REQUIRE(r.culprits().size() == 1);
    CHECK(r.culprits()[0] == std::make_pair(ScanPhase::Index, std::string("b")));

    feed(r, {R"({"event":"marked_failed","path":"b"})", R"({"event":"done"})", R"({"event":"analyse_done"})"});
    CHECK(r.onExit() == Next::Finished);
}

TEST_CASE("an analysis crash marks the file with --fail-analysis", "[scanevents]")
{
    ScanRecovery r(1, true);
    feed(r, {R"({"event":"done"})", R"({"event":"analyse_start","path":"x.wav"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().fail.empty());
    CHECK(r.attempt().failAnalysis == Strings{"x.wav"});
    CHECK(r.culprits()[0].first == ScanPhase::Analyse);
}

TEST_CASE("the same path in both phases is tracked separately", "[scanevents]")
{
    ScanRecovery r(1, true);
    // Indexed fine, then crashed while analysing the same file.
    feed(r, {R"({"event":"start","path":"x"})", R"({"event":"progress","done":1,"total":1,"path":"x"})",
             R"({"event":"done"})", R"({"event":"analyse_start","path":"x"})"});
    REQUIRE(r.onExit() == Next::Retry);
    CHECK(r.attempt().failAnalysis == Strings{"x"});
    CHECK(r.attempt().fail.empty());
}

TEST_CASE("crashes no file explains give up after two tries on one thread", "[scanevents]")
{
    ScanRecovery r(4, true);
    CHECK(r.onExit() == Next::Retry); // 4 threads, nothing in flight: try one thread
    CHECK(r.attempt().threads == 1);
    CHECK(r.onExit() == Next::Retry);
    CHECK(r.onExit() == Next::Crashed);
}

TEST_CASE("a file that crashes the worker again after being marked gives up", "[scanevents]")
{
    ScanRecovery r(1, true);
    feed(r, {R"({"event":"start","path":"evil"})"});
    REQUIRE(r.onExit() == Next::Retry);
    feed(r, {R"({"event":"start","path":"evil"})"});
    CHECK(r.onExit() == Next::Crashed);
}

TEST_CASE("unknown events and progress for unstarted files are harmless", "[scanevents]")
{
    ScanRecovery r(1, false);
    feed(r, {R"({"event":"from_the_future"})", R"({"event":"progress","done":1,"total":1,"path":"never-started"})",
             R"({"event":"done"})"});
    CHECK(r.onExit() == Next::Finished);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_scan_events.cpp:2:10: fatal error: 'asma/core/ScanEvents.h' file not found`

- [ ] **Step 3: Implement**

Create `core/include/asma/core/ScanEvents.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Analyser.h"
#include "asma/core/Scanner.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asma {

enum class ScanPhase { Index, Analyse };

// One line of asma-scan output (docs/scan-protocol.md).
struct ScanEvent {
    enum class Kind {
        MarkedFailed,
        MarkedAnalysisFailed,
        Start,
        Progress,
        Done,
        AnalyseStart,
        AnalyseProgress,
        AnalyseDone,
        Error,
        Unknown, // an event this build does not know; ignore it
    };
    Kind kind = Kind::Unknown;
    std::string path;              // root-relative, for the per-file events
    std::int64_t done = 0;         // Progress, AnalyseProgress
    std::int64_t total = 0;
    ScanStats index;               // Done
    AnalyseStats analysis;         // AnalyseDone
    std::string code;              // Error: "locked" or "failed"
    std::string message;           // Error
    std::optional<std::int64_t> pid; // Error "locked": the lock holder
};

// Nothing for a line that is not a JSON object with a string "event" field.
std::optional<ScanEvent> parseScanEvent(std::string_view line);

// Arguments for one asma-scan run of a supervised scan.
struct ScanAttempt {
    unsigned threads = 0; // 0: let the worker decide
    std::vector<std::string> fail;         // --fail
    std::vector<std::string> failAnalysis; // --fail-analysis
};

// The supervisor's side of the crash-recovery contract, without processes:
// feed it every event of a run, then ask what to do when the worker exits.
//
// A worker that exits without finishing, and without reporting an error,
// crashed. With several threads the crash cannot be pinned on one file, so
// the next run uses one thread; when a one-thread run crashes, the file it
// was working on is passed back with --fail or --fail-analysis and the thread
// count goes back to what was asked for. A crash that no file explains ends
// the scan after two tries, and so does a file that crashes the worker again
// after it was marked.
class ScanRecovery {
public:
    enum class Next { Finished, Retry, Locked, Failed, Crashed };

    ScanRecovery(unsigned threads, bool analyse);

    const ScanAttempt& attempt() const { return attempt_; }
    void onEvent(const ScanEvent& event);
    // Call once the worker has exited. On Retry, attempt() holds the new
    // arguments and the per-run state starts again.
    Next onExit();

    // Files marked as crashing the worker so far, in order.
    const std::vector<std::pair<ScanPhase, std::string>>& culprits() const { return culprits_; }
    const std::optional<ScanEvent>& error() const { return error_; }

private:
    void newRun();

    unsigned requestedThreads_;
    bool analyse_;
    ScanAttempt attempt_;
    std::vector<std::pair<ScanPhase, std::string>> inFlight_; // started, not finished, in start order
    std::vector<std::pair<ScanPhase, std::string>> culprits_;
    bool indexDone_ = false;
    bool analysisDone_ = false;
    std::optional<ScanEvent> error_;
    int unexplainedCrashes_ = 0;
};

} // namespace asma
```

Create `core/src/ScanEvents.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ScanEvents.h"

#include "asma/core/Json.h"

#include <algorithm>

namespace asma {

namespace {

std::string text(const JsonValue& doc, std::string_view key)
{
    const JsonValue* v = doc.get(key);
    return v && v->asString() ? *v->asString() : std::string();
}

std::int64_t integer(const JsonValue& doc, std::string_view key)
{
    const JsonValue* v = doc.get(key);
    return v ? v->asInt().value_or(0) : 0;
}

std::size_t count(const JsonValue& doc, std::string_view key)
{
    return static_cast<std::size_t>(std::max<std::int64_t>(0, integer(doc, key)));
}

} // namespace

std::optional<ScanEvent> parseScanEvent(std::string_view line)
{
    JsonValue doc;
    try {
        doc = parseJson(line);
    } catch (const JsonError&) {
        return std::nullopt;
    }
    const JsonValue* name = doc.get("event");
    if (!name || !name->asString()) return std::nullopt;

    using Kind = ScanEvent::Kind;
    static constexpr std::pair<std::string_view, Kind> kKinds[] = {
        {"marked_failed", Kind::MarkedFailed},
        {"marked_analysis_failed", Kind::MarkedAnalysisFailed},
        {"start", Kind::Start},
        {"progress", Kind::Progress},
        {"done", Kind::Done},
        {"analyse_start", Kind::AnalyseStart},
        {"analyse_progress", Kind::AnalyseProgress},
        {"analyse_done", Kind::AnalyseDone},
        {"error", Kind::Error},
    };
    ScanEvent e;
    for (const auto& [n, kind] : kKinds)
        if (n == *name->asString()) e.kind = kind;

    e.path = text(doc, "path");
    e.done = integer(doc, "done");
    e.total = integer(doc, "total");
    if (e.kind == Kind::Done) {
        e.index.added = count(doc, "added");
        e.index.updated = count(doc, "updated");
        e.index.unchanged = count(doc, "unchanged");
        e.index.relinked = count(doc, "relinked");
        e.index.missing = count(doc, "missing");
        e.index.failed = count(doc, "failed");
        e.index.skipped = count(doc, "skipped");
    }
    if (e.kind == Kind::AnalyseDone) {
        e.analysis.analysed = count(doc, "analysed");
        e.analysis.failed = count(doc, "failed");
        e.analysis.skipped = count(doc, "skipped");
    }
    e.code = text(doc, "code");
    e.message = text(doc, "message");
    if (const JsonValue* pid = doc.get("pid")) e.pid = pid->asInt();
    return e;
}

ScanRecovery::ScanRecovery(unsigned threads, bool analyse) : requestedThreads_(threads), analyse_(analyse)
{
    attempt_.threads = threads;
}

void ScanRecovery::newRun()
{
    inFlight_.clear();
    indexDone_ = false;
    analysisDone_ = false;
    error_.reset();
}

void ScanRecovery::onEvent(const ScanEvent& event)
{
    using Kind = ScanEvent::Kind;
    const auto finish = [&](ScanPhase phase) {
        const auto it = std::find(inFlight_.begin(), inFlight_.end(), std::make_pair(phase, event.path));
        if (it != inFlight_.end()) inFlight_.erase(it);
    };
    switch (event.kind) {
    case Kind::Start: inFlight_.emplace_back(ScanPhase::Index, event.path); break;
    case Kind::AnalyseStart: inFlight_.emplace_back(ScanPhase::Analyse, event.path); break;
    case Kind::Progress: finish(ScanPhase::Index); break;
    case Kind::AnalyseProgress: finish(ScanPhase::Analyse); break;
    case Kind::Done: indexDone_ = true; break;
    case Kind::AnalyseDone: analysisDone_ = true; break;
    case Kind::Error: error_ = event; break;
    default: break;
    }
}

ScanRecovery::Next ScanRecovery::onExit()
{
    if (error_) return error_->code == "locked" ? Next::Locked : Next::Failed;
    if (indexDone_ && (analysisDone_ || !analyse_)) return Next::Finished;

    // The worker crashed.
    if (attempt_.threads != 1) {
        attempt_.threads = 1;
        newRun();
        return Next::Retry;
    }
    if (inFlight_.empty()) {
        if (++unexplainedCrashes_ >= 2) return Next::Crashed;
        newRun();
        return Next::Retry;
    }
    const auto culprit = inFlight_.back();
    if (std::find(culprits_.begin(), culprits_.end(), culprit) != culprits_.end())
        return Next::Crashed; // marking it did not help
    culprits_.push_back(culprit);
    (culprit.first == ScanPhase::Index ? attempt_.fail : attempt_.failAnalysis).push_back(culprit.second);
    attempt_.threads = requestedThreads_;
    unexplainedCrashes_ = 0;
    newRun();
    return Next::Retry;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[scanevents]"`

Expected: no compiler warnings;
`All tests passed (54 assertions in 10 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 197`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: parse scanner events and decide how to recover from a crash"
```

---

### Task 8: The scan supervisor

`ScanSupervisor::run` loops `Subprocess` and `ScanRecovery` until the scan is
finished, failed or cancelled. The test child gains a fake `asma-scan`, driven
by `ASMA_FAKE_SCAN`, that crashes on cue and logs its arguments, so the tests
can assert the exact sequence of runs. `asma-scan` turns off the Windows crash
dialog.

**Files:**

- Create: `core/include/asma/core/ScanSupervisor.h`,
  `core/src/ScanSupervisor.cpp`, `tests/test_scan_supervisor.cpp`
- Modify: `apps/asma_scan_main.cpp`, `tests/child/asma_test_child.cpp`

**Interfaces:**

- Consumes: `Subprocess` (Task 6), `ScanRecovery`, `parseScanEvent` (Task 7).
- Produces:
  `struct asma::ScanRequest { worker; db; rootId; threads; analyse; }`;
  `struct asma::ScanReport { result; message; lockHolder; index; analysis; culprits; runs; }`
  with `Result { Finished, Locked, Failed, Crashed, Cancelled }`;
  `class asma::ScanSupervisor` with
  `ScanReport run(const ScanRequest&, const Listener& = {})` (blocking;
  `Listener` is `std::function<void(const ScanEvent&)>`) and `void cancel()`;
  `std::vector<std::string> asma::scanArguments(const ScanRequest&, const ScanAttempt&)`.

Behaviour the tests pin:

- A crash while indexing or analysing is pinned on its file and the scan
  completes; two bad files are both found.
- Locked and failed are reported without a retry; a missing worker executable is
  `Failed` after zero runs.
- `cancel()` stops a hanging worker within seconds, and a cancel that arrives
  before `run()` starts is not lost.
- The real `asma-scan` finishes in one run.

- [ ] **Step 1: Write the failing tests**

In `tests/child/asma_test_child.cpp`, replace:

```cpp
//   exit N        exit with code N
//   crash         die the way a real crash does
//   hang          print "ready", then sleep for a minute
#include "Args.h"

#include "asma/core/Json.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
```

with:

```cpp
//   exit N        exit with code N
//   crash         die the way a real crash does
//   hang          print "ready", then sleep for a minute
// Started with --db first, it stands in for asma-scan, driven by
// ASMA_FAKE_SCAN ("key=value;..."):
//   files=a,b,c        the root's files, indexed then analysed in this order
//   crash_index=b      crash when indexing b, unless --fail b was given
//   crash_analyse=c    crash when analysing c, unless --fail-analysis c
//   crash_start=1      crash before doing anything
//   error=locked|failed report that error and exit
//   hang=1             sleep for a minute after the first start event
// Each run appends its arguments, one line, to ASMA_FAKE_SCAN_LOG.
#include "Args.h"

#include "asma/core/Json.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
```

In `tests/child/asma_test_child.cpp`, replace:

```cpp
    std::abort();
}

int main(int argc, char** argv)
{
    asma::cli::Args parsed = asma::cli::Args::fromMain(argc, argv);
    std::vector<std::string> args = parsed.rest();
    if (args.empty()) return 2;
    const std::string mode = args.front();
    args.erase(args.begin());
```

with:

```cpp
    std::abort();
}

namespace {

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> out;
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, separator))
        if (!item.empty()) out.push_back(item);
    return out;
}

void emit(const asma::JsonLine& line) { std::cout << line.build() << "\n" << std::flush; }

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

// One phase: start events in groups of `threads`, then progress for each,
// crashing on `crashOn` unless it was marked.
void phase(const std::vector<std::string>& files, const std::vector<std::string>& marked, const std::string& crashOn,
           unsigned threads, const char* startEvent, const char* progressEvent, bool hang)
{
    std::vector<std::string> todo;
    for (const auto& f : files)
        if (!contains(marked, f)) todo.push_back(f);
    std::size_t done = 0;
    for (std::size_t i = 0; i < todo.size(); i += threads) {
        const std::size_t end = std::min(todo.size(), i + threads);
        for (std::size_t j = i; j < end; ++j) emit(asma::JsonLine().str("event", startEvent).str("path", todo[j]));
        if (hang) std::this_thread::sleep_for(std::chrono::seconds(60));
        for (std::size_t j = i; j < end; ++j) {
            if (todo[j] == crashOn) crash();
            emit(asma::JsonLine()
                     .str("event", progressEvent)
                     .num("done", static_cast<std::int64_t>(++done))
                     .num("total", static_cast<std::int64_t>(todo.size()))
                     .str("path", todo[j]));
        }
    }
}

int fakeScan(asma::cli::Args& args, const std::vector<std::string>& raw)
{
    if (const char* log = std::getenv("ASMA_FAKE_SCAN_LOG")) {
        std::ofstream out(log, std::ios::app);
        for (std::size_t i = 0; i < raw.size(); ++i) out << (i ? " " : "") << raw[i];
        out << "\n";
    }
    std::map<std::string, std::string> script;
    if (const char* text = std::getenv("ASMA_FAKE_SCAN"))
        for (const auto& pair : split(text, ';'))
            if (const auto eq = pair.find('='); eq != std::string::npos) script[pair.substr(0, eq)] = pair.substr(eq + 1);

    args.option("db");
    args.option("root");
    const auto threadsOption = args.option("threads");
    const unsigned threads = threadsOption ? static_cast<unsigned>(std::stoul(*threadsOption)) : 4u;
    const auto fail = args.options("fail");
    const auto failAnalysis = args.options("fail-analysis");
    const bool analyse = !args.flag("no-analysis");

    if (script["crash_start"] == "1") crash();
    if (script["error"] == "locked") {
        emit(asma::JsonLine().str("event", "error").str("code", "locked").num("pid", 4242));
        return 3;
    }
    if (script["error"] == "failed") {
        emit(asma::JsonLine().str("event", "error").str("code", "failed").str("message", "disk full"));
        return 1;
    }
    for (const auto& f : fail) emit(asma::JsonLine().str("event", "marked_failed").str("path", f));
    for (const auto& f : failAnalysis) emit(asma::JsonLine().str("event", "marked_analysis_failed").str("path", f));

    const auto files = split(script["files"], ',');
    const bool hang = script["hang"] == "1";
    phase(files, fail, script["crash_index"], threads, "start", "progress", hang);
    emit(asma::JsonLine()
             .str("event", "done")
             .num("added", static_cast<std::int64_t>(files.size() - fail.size()))
             .num("failed", static_cast<std::int64_t>(fail.size())));
    if (!analyse) return 0;
    std::vector<std::string> indexed;
    for (const auto& f : files)
        if (!contains(fail, f)) indexed.push_back(f);
    phase(indexed, failAnalysis, script["crash_analyse"], threads, "analyse_start", "analyse_progress", false);
    emit(asma::JsonLine()
             .str("event", "analyse_done")
             .num("analysed", static_cast<std::int64_t>(indexed.size() - failAnalysis.size())));
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    asma::cli::Args parsed = asma::cli::Args::fromMain(argc, argv);
    std::vector<std::string> args = parsed.rest();
    if (args.empty()) return 2;
    if (args.front() == "--db") return fakeScan(parsed, args);
    const std::string mode = args.front();
    args.erase(args.begin());
```

Create `tests/test_scan_supervisor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/ScanSupervisor.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <thread>

using namespace asma;
using asma::test::ScopedEnv;
using asma::test::TempDir;
using Result = ScanReport::Result;
using Strings = std::vector<std::string>;
namespace fs = std::filesystem;

namespace {

// Runs the fake worker in the test child with a script; the log records the
// arguments of every run.
struct FakeScan {
    TempDir dir;
    fs::path log = dir.path() / "runs.txt";
    ScopedEnv logEnv{"ASMA_FAKE_SCAN_LOG", toUtf8(log).c_str()};
    ScopedEnv script;
    ScanSupervisor supervisor;

    explicit FakeScan(const char* text) : script("ASMA_FAKE_SCAN", text) {}

    ScanReport run(unsigned threads = 4, bool analyse = true, ScanSupervisor::Listener listener = {})
    {
        ScanRequest request;
        request.worker = fromUtf8(ASMA_TEST_CHILD_PATH);
        request.db = dir.path() / "library.db";
        request.rootId = 1;
        request.threads = threads;
        request.analyse = analyse;
        return supervisor.run(request, listener);
    }

    Strings runs() const
    {
        Strings lines;
        std::ifstream in(log);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line.substr(line.find("--root 1")));
        return lines;
    }
};

} // namespace

TEST_CASE("scanArguments lays out one attempt", "[supervisor]")
{
    ScanRequest request;
    request.db = fromUtf8("/data/library.db");
    request.rootId = 3;
    request.analyse = false;
    ScanAttempt attempt;
    attempt.threads = 1;
    attempt.fail = {"a b.wav"};
    attempt.failAnalysis = {"c.wav"};
    CHECK(scanArguments(request, attempt) == Strings{"--db", toUtf8(request.db), "--root", "3", "--threads", "1",
                                                     "--no-analysis", "--fail", "a b.wav", "--fail-analysis",
                                                     "c.wav"});
    CHECK(scanArguments(request, {}) == Strings{"--db", toUtf8(request.db), "--root", "3", "--no-analysis"});
}

TEST_CASE("a clean run finishes in one go and passes every event on", "[supervisor]")
{
    FakeScan fake("files=a,b,c");
    int events = 0;
    const ScanReport report = fake.run(4, true, [&](const ScanEvent&) { ++events; });
    CHECK(report.result == Result::Finished);
    CHECK(report.runs == 1);
    CHECK(report.index.added == 3);
    CHECK(report.analysis.analysed == 3);
    CHECK(report.culprits.empty());
    CHECK(events == 3 * 2 * 2 + 2); // start and progress per file per phase, plus two summaries
}

TEST_CASE("an indexing crash is pinned on its file and the scan completes", "[supervisor]")
{
    FakeScan fake("files=a,b,c,d,e;crash_index=c");
    const ScanReport report = fake.run();
    CHECK(report.result == Result::Finished);
    REQUIRE(report.culprits.size() == 1);
    CHECK(report.culprits[0] == std::make_pair(ScanPhase::Index, std::string("c")));
    CHECK(report.index.added == 4);
    CHECK(report.index.failed == 1);
    CHECK(fake.runs() == Strings{"--root 1 --threads 4", "--root 1 --threads 1", "--root 1 --threads 4 --fail c"});
}

TEST_CASE("an analysis crash is pinned with --fail-analysis", "[supervisor]")
{
    FakeScan fake("files=a,b;crash_analyse=b");
    const ScanReport report = fake.run(2);
    CHECK(report.result == Result::Finished);
    CHECK(report.culprits == std::vector<std::pair<ScanPhase, std::string>>{{ScanPhase::Analyse, "b"}});
    CHECK(report.analysis.analysed == 1);
    CHECK(fake.runs().back() == "--root 1 --threads 2 --fail-analysis b");
}

TEST_CASE("two bad files in one root are both found", "[supervisor]")
{
    FakeScan fake("files=a,b,c;crash_index=a;crash_analyse=c");
    const ScanReport report = fake.run(1);
    CHECK(report.result == Result::Finished);
    CHECK(report.culprits.size() == 2);
    CHECK(fake.runs().back() == "--root 1 --threads 1 --fail a --fail-analysis c");
}

TEST_CASE("a worker that crashes before any file gives up", "[supervisor]")
{
    FakeScan fake("crash_start=1");
    const ScanReport report = fake.run();
    CHECK(report.result == Result::Crashed);
    CHECK(report.runs == 3); // four threads, then one thread twice
}

TEST_CASE("locked and failed are reported, not retried", "[supervisor]")
{
    FakeScan locked("error=locked");
    const ScanReport a = locked.run();
    CHECK(a.result == Result::Locked);
    CHECK(a.lockHolder == 4242);
    CHECK(a.runs == 1);

    FakeScan failed("error=failed");
    const ScanReport b = failed.run();
    CHECK(b.result == Result::Failed);
    CHECK(b.message == "disk full");
}

TEST_CASE("a missing worker is a failure, not a crash loop", "[supervisor]")
{
    ScanSupervisor supervisor;
    ScanRequest request;
    request.worker = fromUtf8(ASMA_TEST_CHILD_PATH).parent_path() / "no-such-worker";
    const ScanReport report = supervisor.run(request);
    CHECK(report.result == Result::Failed);
    CHECK(report.runs == 0);
    CHECK_FALSE(report.message.empty());
}

TEST_CASE("cancel stops a hanging worker promptly", "[supervisor]")
{
    FakeScan fake("files=a;hang=1");
    const auto started = std::chrono::steady_clock::now();
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        fake.supervisor.cancel();
    });
    const ScanReport report = fake.run();
    canceller.join();
    CHECK(report.result == Result::Cancelled);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));

    // The supervisor can run again afterwards.
    FakeScan again("files=a");
    CHECK(again.run().result == Result::Finished);
}

TEST_CASE("a cancel before run starts is not lost", "[supervisor]")
{
    FakeScan fake("files=a");
    fake.supervisor.cancel();
    CHECK(fake.run().result == Result::Cancelled);
    CHECK(fake.run().result == Result::Finished);
}

TEST_CASE("supervising the real asma-scan", "[supervisor][e2e]")
{
    TempDir dir;
    const fs::path lib = dir.path() / fromUtf8("Café");
    test::WavSpec spec;
    test::writeWav(lib / "Kick_01.wav", spec);
    spec.seed = 2;
    test::writeWav(lib / "Loops" / "Bass_Loop_Am_128.wav", spec);
    const fs::path dbPath = dir.path() / "data" / "library.db";
    std::int64_t rootId = 0;
    {
        Db db = Db::open(dbPath);
        rootId = Library(db).addRoot(lib);
    }

    ScanSupervisor supervisor;
    ScanRequest request;
    request.worker = fromUtf8(ASMA_SCAN_PATH);
    request.db = dbPath;
    request.rootId = rootId;
    const ScanReport report = supervisor.run(request);
    CHECK(report.result == Result::Finished);
    CHECK(report.runs == 1);
    CHECK(report.index.added == 2);
    CHECK(report.analysis.analysed == 2);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_scan_supervisor.cpp:6:10: fatal error: 'asma/core/ScanSupervisor.h' file not found`

- [ ] **Step 3: Implement**

In `apps/asma_scan_main.cpp`, replace:

```cpp
#include <iostream>
#include <string>

using namespace asma;
using namespace asma::cli;
```

with:

```cpp
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace asma;
using namespace asma::cli;
```

In `apps/asma_scan_main.cpp`, replace:

```cpp

int main(int argc, char** argv)
{
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
```

with:

```cpp

int main(int argc, char** argv)
{
#ifdef _WIN32
    // A crash must end the process at once so the supervisor can restart it,
    // not wait behind a Windows Error Reporting dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
```

Create `core/include/asma/core/ScanSupervisor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/ScanEvents.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace asma {

class Subprocess;

struct ScanRequest {
    std::filesystem::path worker; // the asma-scan executable
    std::filesystem::path db;
    std::int64_t rootId = 0;
    unsigned threads = 0; // 0: the worker's default
    bool analyse = true;
};

struct ScanReport {
    enum class Result { Finished, Locked, Failed, Crashed, Cancelled };
    Result result = Result::Finished;
    std::string message;                   // Failed: the worker's message; Crashed: what went wrong
    std::optional<std::int64_t> lockHolder; // Locked: the pid holding the writer lock, when known
    ScanStats index;                       // from the last run, so earlier runs' additions count as unchanged
    AnalyseStats analysis;
    std::vector<std::pair<ScanPhase, std::string>> culprits; // files marked for crashing the worker
    int runs = 0;
};

// Runs asma-scan for one root and sees it through crashes, following the
// contract in docs/scan-protocol.md. The UI runs it on a background thread.
class ScanSupervisor {
public:
    // Called on the thread that runs run(), for every event of every run.
    using Listener = std::function<void(const ScanEvent&)>;

    ScanReport run(const ScanRequest& request, const Listener& listener = {});

    // Stops the scan: kills the worker and makes run() return Cancelled. Safe
    // from any thread. A call while no run() is active cancels the next one
    // as soon as it starts, so a cancel that races the start is not lost.
    void cancel();

private:
    std::atomic<bool> cancelled_{false};
    std::mutex mutex_;
    Subprocess* current_ = nullptr; // guarded by mutex_
};

// The worker's arguments for one attempt.
std::vector<std::string> scanArguments(const ScanRequest& request, const ScanAttempt& attempt);

} // namespace asma
```

Create `core/src/ScanSupervisor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ScanSupervisor.h"

#include "asma/core/Fs.h"
#include "asma/core/Subprocess.h"

namespace asma {

std::vector<std::string> scanArguments(const ScanRequest& request, const ScanAttempt& attempt)
{
    std::vector<std::string> args = {"--db", toUtf8(request.db), "--root", std::to_string(request.rootId)};
    if (attempt.threads > 0) {
        args.push_back("--threads");
        args.push_back(std::to_string(attempt.threads));
    }
    if (!request.analyse) args.push_back("--no-analysis");
    for (const auto& path : attempt.fail) {
        args.push_back("--fail");
        args.push_back(path);
    }
    for (const auto& path : attempt.failAnalysis) {
        args.push_back("--fail-analysis");
        args.push_back(path);
    }
    return args;
}

void ScanSupervisor::cancel()
{
    cancelled_ = true;
    std::lock_guard lock(mutex_);
    if (current_) current_->kill();
}

ScanReport ScanSupervisor::run(const ScanRequest& request, const Listener& listener)
{
    using Result = ScanReport::Result;
    ScanReport report;
    ScanRecovery recovery(request.threads, request.analyse);

    for (;;) {
        if (cancelled_) {
            report.result = Result::Cancelled;
            break;
        }
        std::optional<Subprocess> worker;
        try {
            worker.emplace(Subprocess::start(request.worker, scanArguments(request, recovery.attempt())));
        } catch (const SubprocessError& e) {
            report.result = Result::Failed;
            report.message = e.what();
            break;
        }
        ++report.runs;
        {
            std::lock_guard lock(mutex_);
            current_ = &*worker;
        }
        if (cancelled_) worker->kill(); // cancel() came between the check above and here

        while (const auto line = worker->readLine()) {
            const auto event = parseScanEvent(*line);
            if (!event) continue;
            if (event->kind == ScanEvent::Kind::Done) report.index = event->index;
            if (event->kind == ScanEvent::Kind::AnalyseDone) report.analysis = event->analysis;
            recovery.onEvent(*event);
            if (listener) listener(*event);
        }
        worker->wait();
        {
            std::lock_guard lock(mutex_);
            current_ = nullptr;
        }
        if (cancelled_) {
            report.result = Result::Cancelled;
            break;
        }

        const auto next = recovery.onExit();
        if (next == ScanRecovery::Next::Retry) continue;
        if (next == ScanRecovery::Next::Finished) report.result = Result::Finished;
        if (next == ScanRecovery::Next::Locked) {
            report.result = Result::Locked;
            report.lockHolder = recovery.error()->pid;
        }
        if (next == ScanRecovery::Next::Failed) {
            report.result = Result::Failed;
            report.message = recovery.error()->message;
        }
        if (next == ScanRecovery::Next::Crashed) {
            report.result = Result::Crashed;
            report.message = "the scanner kept crashing";
        }
        break;
    }
    report.culprits = recovery.culprits();
    cancelled_ = false;
    return report;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[supervisor]"`

Expected: no compiler warnings;
`All tests passed (40 assertions in 11 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 208`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: supervise asma-scan through crashes and cancellation"
```

---

### Task 9: Read-only access and change notification

What the plugin opens inside a host, and what the UI polls to know when to
refresh. `SchemaMismatchError` tells the plugin whether to run the helper once
to migrate (older library) or to say it is out of date (newer).

**Files:**

- Create: `core/include/asma/core/ChangeWatcher.h`,
  `core/src/ChangeWatcher.cpp`, `tests/test_change_watcher.cpp`
- Modify: `core/include/asma/core/Db.h`, `core/include/asma/core/Schema.h`,
  `core/src/Db.cpp`, `core/src/Schema.cpp`

**Interfaces:**

- Consumes: `Db`, `migrate` (plan 1), `UserData` (Task 1).
- Produces: `static Db Db::openReadOnly(const std::filesystem::path&)`;
  `class asma::SchemaMismatchError : public DbError` with `int found() const`
  (now also thrown by `migrate` for a newer library);
  `class asma::ChangeWatcher(Db&)` with `bool changed()`.

Behaviour the tests pin:

- A read-only library refuses every write; opening neither creates a missing
  file nor migrates an old one.
- `changed()` is true once after other connections commit, not for rolled-back
  or uncommitted work, and not for the watched connection's own commits.
- Opening works after every writer has closed the library, and a writer that
  starts later is seen.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_change_watcher.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/ChangeWatcher.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

namespace {

struct Library2 {
    TempDir dir;
    std::filesystem::path file = dir.path() / "library.db";
    Db writer = Db::open(file);
    std::int64_t fileId = 0;

    Library2()
    {
        Library lib(writer);
        FileRecord f;
        f.rootId = lib.addRoot(dir.path() / "samples");
        f.relPath = "a.wav";
        f.format = "wav";
        fileId = lib.insertFile(f);
    }
};

} // namespace

TEST_CASE("openReadOnly reads a library and refuses to write to it", "[readonly]")
{
    Library2 lib;
    Db reader = Db::openReadOnly(lib.file);
    CHECK(Library(reader).fileById(lib.fileId));
    CHECK_THROWS_AS(UserData(reader).setRating(lib.fileId, 3), DbError);
    CHECK_THROWS_AS(reader.exec("INSERT INTO roots(path) VALUES ('/x')"), DbError);
}

TEST_CASE("openReadOnly neither creates nor migrates", "[readonly]")
{
    TempDir dir;
    CHECK_THROWS_AS(Db::openReadOnly(dir.path() / "missing.db"), DbError);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "missing.db"));

    const auto file = dir.path() / "old.db";
    {
        Db old = Db::openInMemory(2); // a library an older asma wrote
        auto copy = old.prepare("VACUUM INTO ?");
        copy.bind(1, std::string_view(toUtf8(file)));
        copy.run();
    }
    try {
        Db::openReadOnly(file);
        FAIL("an old library was opened");
    } catch (const SchemaMismatchError& e) {
        CHECK(e.found() == 2);
    }
    Db check = Db::open(file); // what the helper process does: migrate on open
    CHECK(check.schemaVersion() == currentSchemaVersion());
}

TEST_CASE("a newer library is a schema mismatch for writers too", "[readonly]")
{
    TempDir dir;
    const auto file = dir.path() / "new.db";
    {
        Db db = Db::open(file);
        db.exec("PRAGMA user_version = 999");
    }
    CHECK_THROWS_AS(Db::open(file), SchemaMismatchError);
    CHECK_THROWS_AS(Db::openReadOnly(file), SchemaMismatchError);
}

TEST_CASE("ChangeWatcher sees other connections' commits once", "[readonly]")
{
    Library2 lib;
    Db reader = Db::openReadOnly(lib.file);
    ChangeWatcher watcher(reader);
    CHECK_FALSE(watcher.changed());

    UserData(lib.writer).setRating(lib.fileId, 4);
    UserData(lib.writer).setFavourite(lib.fileId, true);
    CHECK(watcher.changed());
    CHECK_FALSE(watcher.changed());
    CHECK(UserData(reader).rating(lib.fileId) == 4);

    {
        Transaction tx(lib.writer);
        UserData(lib.writer).setRating(lib.fileId, 2);
        // Not committed yet: nothing to see.
        CHECK_FALSE(watcher.changed());
    } // rolled back
    CHECK_FALSE(watcher.changed());
}

TEST_CASE("ChangeWatcher ignores the watched connection's own commits", "[readonly]")
{
    Library2 lib;
    ChangeWatcher watcher(lib.writer);
    UserData(lib.writer).setRating(lib.fileId, 1);
    CHECK_FALSE(watcher.changed());
}

TEST_CASE("openReadOnly works after every writer has closed the library", "[readonly]")
{
    TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        Db writer = Db::open(file);
        writer.exec("INSERT INTO roots(path) VALUES ('/x')");
    } // the last connection checkpoints and removes the -wal and -shm files
    Db reader = Db::openReadOnly(file);
    {
        // Scoped: a statement left mid-step holds its read snapshot open.
        auto q = reader.prepare("SELECT count(*) FROM roots");
        REQUIRE(q.step());
        CHECK(q.getInt(0) == 1);
    }

    Db writer = Db::open(file); // a writer can still come along while the reader is open
    ChangeWatcher watcher(reader);
    writer.exec("INSERT INTO roots(path) VALUES ('/y')");
    CHECK(watcher.changed());
    auto again = reader.prepare("SELECT count(*) FROM roots");
    REQUIRE(again.step());
    CHECK(again.getInt(0) == 2);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_change_watcher.cpp:3:10: fatal error: 'asma/core/ChangeWatcher.h' file not found`

- [ ] **Step 3: Implement**

Create `core/include/asma/core/ChangeWatcher.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>

namespace asma {

// Tells a reader when another connection (the scanner, the app, the asma CLI
// run by a plugin) has committed to the library, so the UI can refresh what
// it shows. Polls SQLite's PRAGMA data_version, which is cheap enough to call
// from a UI timer. Commits made through the watched connection itself do not
// count.
class ChangeWatcher {
public:
    explicit ChangeWatcher(Db& db);

    // True when other connections have committed since the last call (or
    // since construction); several commits in between still give one true.
    bool changed();

private:
    std::int64_t dataVersion();

    Db& db_;
    std::int64_t last_;
};

} // namespace asma
```

In `core/include/asma/core/Db.h`, replace:

```cpp
    using std::runtime_error::runtime_error;
};

// A prepared statement. Bind indices are 1-based, column indices 0-based.
class Statement {
public:
```

with:

```cpp
    using std::runtime_error::runtime_error;
};

// A library written for a different schema than this build's. Older: a
// writer (the app or the asma CLI) must open it once to migrate it. Newer:
// this build is out of date.
class SchemaMismatchError : public DbError {
public:
    explicit SchemaMismatchError(int found);
    int found() const { return found_; }

private:
    int found_;
};

// A prepared statement. Bind indices are 1-based, column indices 0-based.
class Statement {
public:
```

In `core/include/asma/core/Db.h`, replace:

```cpp
    // Opens or creates the file (and its parent directories), enables WAL and
    // migrates to the current schema.
    static Db open(const std::filesystem::path& file);
    // Private in-memory database migrated to schemaVersion (default: the
    // current one), for tests.
    static Db openInMemory(int schemaVersion = -1);
```

with:

```cpp
    // Opens or creates the file (and its parent directories), enables WAL and
    // migrates to the current schema.
    static Db open(const std::filesystem::path& file);
    // Opens an existing library for reading only, as the plugin does inside a
    // host: nothing is created or migrated, and any write throws DbError.
    // Throws DbError when the file cannot be opened, SchemaMismatchError when
    // its schema is not this build's.
    static Db openReadOnly(const std::filesystem::path& file);
    // Private in-memory database migrated to schemaVersion (default: the
    // current one), for tests.
    static Db openInMemory(int schemaVersion = -1);
```

In `core/include/asma/core/Db.h`, replace:

```cpp

private:
    explicit Db(sqlite3* db) : db_(db) {}
    static Db openHandle(const std::string& utf8Name);

    sqlite3* db_ = nullptr;
};
```

with:

```cpp

private:
    explicit Db(sqlite3* db) : db_(db) {}
    static Db openHandle(const std::string& utf8Name, int flags);

    sqlite3* db_ = nullptr;
};
```

In `core/include/asma/core/Schema.h`, replace:

```cpp
int currentSchemaVersion();

// Applies pending migrations up to targetVersion (default: current), each in
// its own transaction. Throws DbError when the database was written by a newer
// asma.
void migrate(Db& db, int targetVersion = -1);

} // namespace asma
```

with:

```cpp
int currentSchemaVersion();

// Applies pending migrations up to targetVersion (default: current), each in
// its own transaction. Throws SchemaMismatchError when the database was
// written by a newer asma.
void migrate(Db& db, int targetVersion = -1);

} // namespace asma
```

Create `core/src/ChangeWatcher.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ChangeWatcher.h"

namespace asma {

ChangeWatcher::ChangeWatcher(Db& db) : db_(db), last_(dataVersion()) {}

std::int64_t ChangeWatcher::dataVersion()
{
    auto q = db_.prepare("PRAGMA data_version");
    q.step();
    return q.getInt(0);
}

bool ChangeWatcher::changed()
{
    const std::int64_t now = dataVersion();
    if (now == last_) return false;
    last_ = now;
    return true;
}

} // namespace asma
```

In `core/src/Db.cpp`, replace:

```cpp
    return data ? std::vector<unsigned char>(data, data + size) : std::vector<unsigned char>();
}

Db Db::openHandle(const std::string& utf8Name)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(utf8Name.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    Db db(raw); // owns the handle even on failure, so it gets closed
    if (rc != SQLITE_OK) fail(raw, "cannot open database " + utf8Name);
    sqlite3_busy_timeout(raw, 5000);
```

with:

```cpp
    return data ? std::vector<unsigned char>(data, data + size) : std::vector<unsigned char>();
}

Db Db::openHandle(const std::string& utf8Name, int flags)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(utf8Name.c_str(), &raw, flags, nullptr);
    Db db(raw); // owns the handle even on failure, so it gets closed
    if (rc != SQLITE_OK) fail(raw, "cannot open database " + utf8Name);
    sqlite3_busy_timeout(raw, 5000);
```

In `core/src/Db.cpp`, replace:

```cpp
Db Db::open(const std::filesystem::path& file)
{
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
    Db db = openHandle(toUtf8(file));
    db.exec("PRAGMA journal_mode = WAL");
    db.exec("PRAGMA synchronous = NORMAL");
    migrate(db);
    return db;
}

Db Db::openInMemory(int schemaVersion)
{
    Db db = openHandle(":memory:");
    migrate(db, schemaVersion < 0 ? currentSchemaVersion() : schemaVersion);
    return db;
}
```

with:

```cpp
Db Db::open(const std::filesystem::path& file)
{
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
    Db db = openHandle(toUtf8(file), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("PRAGMA journal_mode = WAL");
    db.exec("PRAGMA synchronous = NORMAL");
    migrate(db);
    return db;
}

Db Db::openReadOnly(const std::filesystem::path& file)
{
    Db db = openHandle(toUtf8(file), SQLITE_OPEN_READONLY);
    const int version = db.schemaVersion();
    if (version != currentSchemaVersion()) throw SchemaMismatchError(version);
    return db;
}

Db Db::openInMemory(int schemaVersion)
{
    Db db = openHandle(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    migrate(db, schemaVersion < 0 ? currentSchemaVersion() : schemaVersion);
    return db;
}
```

In `core/src/Db.cpp`, replace:

```cpp

std::int64_t Db::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }

int Db::schemaVersion()
{
    auto q = prepare("PRAGMA user_version");
```

with:

```cpp

std::int64_t Db::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }

SchemaMismatchError::SchemaMismatchError(int found)
    : DbError("library schema version " + std::to_string(found) + ", this asma build uses "
              + std::to_string(currentSchemaVersion())),
      found_(found)
{
}

int Db::schemaVersion()
{
    auto q = prepare("PRAGMA user_version");
```

In `core/src/Schema.cpp`, replace:

```cpp
        // open the same new database cannot both apply the same migration.
        Transaction tx(db);
        const int version = db.schemaVersion();
        if (version > currentSchemaVersion())
            throw DbError("database schema version " + std::to_string(version)
                          + " is newer than this asma build supports ("
                          + std::to_string(currentSchemaVersion()) + ")");
        if (version >= targetVersion) {
            tx.commit();
            return;
```

with:

```cpp
        // open the same new database cannot both apply the same migration.
        Transaction tx(db);
        const int version = db.schemaVersion();
        if (version > currentSchemaVersion()) throw SchemaMismatchError(version);
        if (version >= targetVersion) {
            tx.commit();
            return;
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[readonly]"`

Expected: no compiler warnings;
`All tests passed (21 assertions in 6 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 214`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: open a library read-only and notice other writers' commits"
```

---

### Task 10: Docs

The spec gets the two decisions this plan made (user data outside the writer
lock; the plugin's helper process), the scan protocol points at the supervisor,
and the README shows the new commands.

**Files:**

- Modify: `README.md`, `docs/scan-protocol.md`,
  `docs/superpowers/specs/2026-09-25-asma-design.md`

- [ ] **Step 1: Update the docs**

In `README.md`, replace:

```markdown
    asma query dusty --tag drums --json
    asma similar ~/Samples/Drums/Kick_01.wav

The library lives in the platform data directory (on macOS
`~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
`ASMA_DATA_DIR` environment variable override it.
```

with:

```markdown
    asma query dusty --tag drums --json
    asma similar ~/Samples/Drums/Kick_01.wav

Ratings, favourites, tags, collections and saved searches:

    asma rate 5 ~/Samples/Drums/Kick_01.wav     # 0 clears; --id N works too
    asma fav on ~/Samples/Drums/Kick_01.wav
    asma tag add punchy ~/Samples/Drums/Kick_01.wav
    asma collection create "Live set"
    asma collection add "Live set" ~/Samples/Drums/Kick_01.wav
    asma query --collection "Live set" --min-rating 4 --favourites
    asma search save "Fast loops" --type loop --bpm 140-180
    asma query --saved "Fast loops" --sort rating --desc

The library lives in the platform data directory (on macOS
`~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
`ASMA_DATA_DIR` environment variable override it.
```

In `docs/scan-protocol.md`, replace:

```markdown
`--threads 1`, and when a single-threaded worker dies, the path it started last
is passed back with `--fail` if it died while indexing, or `--fail-analysis` if
it died while analysing.
```

with:

```markdown
`--threads 1`, and when a single-threaded worker dies, the path it started last
is passed back with `--fail` if it died while indexing, or `--fail-analysis` if
it died while analysing.

`asma::ScanSupervisor` (`core/include/asma/core/ScanSupervisor.h`) implements
this contract; the recovery decisions live in `asma::ScanRecovery`, which is
tested without processes. After a single-threaded run pins a file, the next run
goes back to the requested thread count. A crash that no file explains ends the
scan after two single-threaded tries, and so does a file that crashes the worker
again after it was marked. `error` events end the scan without a retry.

On Windows `asma-scan` turns off the crash dialog (`SetErrorMode`), so a crash
ends the process at once instead of waiting for someone to dismiss a window.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```markdown
### Single writer

Only `asma-scan` and standalone file operations write. A lock file in the data
directory holds the writer's PID; a stale lock (dead PID) is taken over. File
operations wait for a running scan to finish, or pause it.

### File operations
```

with:

```markdown
### Single writer

Only `asma-scan` and standalone file operations take the writer lock. A lock
file in the data directory holds the writer's PID; a stale lock (dead PID) is
taken over. File operations wait for a running scan to finish, or pause it.

User data (ratings, favourites, user tags, collections, saved searches) is
written in short transactions without the writer lock, so rating a sample never
waits for a scan to end; SQLite's busy timeout covers the moment a scan batch is
committing. The plugin never writes inside the host: it runs the `asma` CLI
(`asma rate`, `asma fav`, `asma tag`, `asma collection`, `asma search`) as a
short-lived helper process, and opens the library read-only itself.

### File operations
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```markdown
Instrument plugin with MIDI input and audio output. Any number of instances
share the one database. The project saves only the selected sample, the search
model and view state, never the library itself.

## 10. Error handling
```

with:

```markdown
Instrument plugin with MIDI input and audio output. Any number of instances
share the one database. The project saves only the selected sample, the search
model (as the same JSON a saved search uses) and view state, never the library
itself.

## 10. Error handling
```

- [ ] **Step 2: Format and check**

```sh
prettier -w README.md docs/scan-protocol.md docs/superpowers/specs/2026-09-25-asma-design.md
```

Expected: prettier leaves the text as written above (no diff after the edit).

- [ ] **Step 3: Commit**

```sh
git add -A
git commit -m "docs: user-data writes, plugin helper and scan supervisor"
```

---

### Task 11: Verify and merge

- [ ] **Step 1: Full suite and the search timing**

```sh
ctest --test-dir build --output-on-failure
./build/tests/asma_tests "[.perf]"
```

Expected: all green; `search took` under 50 ms.

- [ ] **Step 2: Smoke test the organise commands**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
./build/apps/asma root add ~/Music
./build/apps/asma scan --no-analysis
f="$(./build/apps/asma query kick --limit 1 | cut -f1)"
./build/apps/asma rate 5 "$f" && ./build/apps/asma fav on "$f"
./build/apps/asma collection create "Smoke" && ./build/apps/asma collection add Smoke "$f"
./build/apps/asma search save Kicks kick --min-rating 4
./build/apps/asma query --saved Kicks --json
```

Expected: one JSON row with `"rating":5,"favourite":true`.

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

`Subprocess` has Linux and Windows code that no local build compiled. Pushing
the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-3a-organise
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green. On the Windows job, the
`[subprocess]` and `[supervisor]` crash tests must finish in seconds; a job that
stalls there means a crash dialog is waiting. Fix on the branch until all three
pass.

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-3a-organise
git -C product/asma worktree remove .worktrees/plan-3a
git -C product/asma branch -d plan-3a-organise
```

Push `main` once the user agrees.
