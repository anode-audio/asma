# asma Plan: Watching and the Safety Net Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** while any asma window is open, the library follows its folders (a
sample dropped into one appears in seconds; each folder is also scanned at
startup and every 15 minutes), and the library is kept safe: checked when asma
starts, rebuilt from a daily JSON backup of the user's data when it is damaged,
with the footer saying so.

**Architecture:** The core gains `FolderWatcher` (efsw over FSEvents,
ReadDirectoryChangesW and inotify, with a quiet period), `Backup` (the user's
data as JSON, restored by content hash) and `Repair` (`quick_check`, and a
rebuild under the writer lock that moves the damaged file aside); the CLI gains
`check`, `backup`, `restore` and `repair`. In the app and plugin code
`ScanSchedule` decides what to scan when, and `LibraryKeeper`, one per process
and held by the editors, watches, runs scans through `asma-scan` (now beside
every format) and runs the check, the rebuild and the backup through the `asma`
helper, so a plugin never writes the library inside its host. The library view
tells damage apart and follows a rebuilt file.

**Tech Stack:** C++20, JUCE 9.0.3, SQLite, efsw 1.7.2 (MIT), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (section 6
"Watching", section 10 "Database corruption" and "Daily backup", sections 11 and
12, as amended in `9e962da`). Task 9 adds two lines the prototype proved needed.

**How this plan was checked:** every task was built on a branch from `main`
(`9e962da`), then replayed commit by commit on a macOS Release build: with only
the task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The code below is taken from those commits: new files
in full, changed files as diffs against the task before (or in full where most
of the file changed), so applying the tasks in order to `main` gives the
prototype file for file. On the prototype, ctest passed 564 of 564, the plugin
tests passed in one process (206 test cases), pluginval said `SUCCESS` and
clap-validator `0 failed`. The prototype did not run on CI: Windows and Linux
(efsw's other two backends) are first built in Task 10.

## Where this sits

The order the user agreed on 2026-10-06: 3c2b2 organise (merged 2026-10-08),
**this plan**, then plan 4 (file manager) and plan 5 (packaging). Plan 5 must
also sign the helpers inside the bundles and settle where single-file CLAP and
LV2 installs put them.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only`. efsw is MIT.
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11), no
  floating-point `std::to_chars`/`std::from_chars` (macOS 12). The plugin build
  uses JUCE's warning set, which includes `-Wfloat-equal`.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE.
- The plugin never writes the library inside the host: scans run in `asma-scan`,
  the check, rebuild and backup in the `asma` helper (`asma-cli`). Only
  `asma-scan`, `asma retry`, `asma repair` and file operations take the writer
  lock.
- Nothing on the audio thread allocates, locks or blocks; nothing on the message
  thread waits for a helper process or a scan.
- A damaged library is never deleted: it is moved aside.
- Colours come from `theme`, never as hex in a component. UI text in the source
  is UTF-8.
- No em-dashes in code, comments, docs or commit messages; no attribution
  trailers; no mention of the tools used to write the code.

## Decisions made while prototyping

- **The keeper lives while an editor is open**, held by the editors through
  `LibraryKeeper::shared`, not by the processor: a host may make processors off
  the message thread and keep them headless, and the spec keeps the library in
  step "while any asma window is open".
- **No `asma-scan`, no scans.** A keeper whose `asma-scan` is missing does not
  scan (and says nothing), so the plugin test binary, which has none beside it,
  is left quiet; packaging puts it beside every format.
- **macOS merges change flags.** A folder is reported "modified" whenever
  anything inside it changes, hidden files too, so folder-modified notices are
  ignored; folders added, deleted or moved still count (a folder of samples
  dragged in arrives as one "added" notice). FSEvents can still mark a folder
  "added" again when a hidden file changes inside it, which costs one scan that
  finds nothing; the watcher tests use hidden files at a folder's top and inside
  hidden folders. They also wait 1.5 s after watching before acting, since
  FSEvents delivers notices for folders made just before.
- **efsw is called outside the watcher's lock**: its thread may be waiting for
  that lock to report a change while `removeWatch` waits for its thread.
- **Folders compare as the system reports them** (`weakly_canonical`): macOS
  reports `/private/var` for `/var`.
- **The library view probes on opening.** Opening reads only the header, so a
  damaged schema showed as Open until the first query; one statement at open
  tells. A file that is not a database is Damaged, not Unreadable (it is
  rebuilt); two older tests change accordingly.
- **A rebuilt library replaces the file**; the view compares the file's identity
  (device and inode) at each refresh and opens the new one. Windows never
  replaces a file a connection holds (the rebuild waits, `in_use`), so identity
  is not needed there.
- **The rebuild indexes but does not analyse**: the keeper then scans every
  folder afresh, which analyses.
- **Only news reaches the footer**: a scan that changed nothing says nothing,
  and one refused by the lock is skipped silently, as the spec says.
- **The validator runs with its own data directory**, since a plugin window now
  scans its library.
- **`backupFolders` read from a parsed document that had already gone**; it
  keeps the document now.

## Review Focus

The five inputs most likely to bite a person that the tasks' main tests do not
reach; each has its own test, in the task named:

- A folder of samples renamed inside a watched folder: reported, so its samples
  follow (Task 1).
- A backup from another asma version: unknown fields ignored, a non-backup
  refused (Task 2).
- A sample re-encoded since the backup (other content): counted as not found,
  not silently lost (Task 2).
- Two asma processes starting on one damaged library: only one rebuilds; the
  other is told the lock is taken, then finds a sound library (Task 4).
- A library a scan is writing when the check runs: never taken for damaged (Task
  3).

## File Structure

```
cmake/Dependencies.cmake, core/CMakeLists.txt             efsw 1.7.2
core/include/asma/core/FolderWatcher.h, core/src/...       change notices with a quiet period
core/include/asma/core/Backup.h, core/src/Backup.cpp       backupJson, writeBackup, restoreBackup
core/include/asma/core/Repair.h, core/src/Repair.cpp       checkLibrary, repairLibrary
core/include/asma/core/Fs.h, core/src/Fs.cpp               fileIdentity
apps/asma_main.cpp                                         check, backup, restore, repair
plugin/src/LibraryView.{h,cpp}                             Damaged, follows a replaced file
plugin/src/ScanSchedule.{h,cpp}                            what to scan when (JUCE-free)
plugin/src/ScanJob.{h,cpp}                                 ScanRunner; any folder; progress with its name
plugin/src/LibraryKeeper.{h,cpp}                           watching, scans, check, rebuild, backup
plugin/src/AsmaProcessor.{h,cpp}, AsmaEditor.{h,cpp}       the keeper replaces the processor's ScanJob
plugin/CMakeLists.txt, ci/validate-plugins.sh              asma-scan beside every format; own data dir
tests/test_{folder_watcher,backup,repair,cli_e2e}.cpp
tests/plugin/test_{library_view,scan_schedule,scan_job,library_keeper,editor}.cpp
README.md, docs/superpowers/specs/2026-09-25-asma-design.md
```

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-watch -b plan-watch
```

All paths below are relative to `product/asma/.worktrees/plan-watch`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
`cmake --build build`.

---

### Task 1: A folder watcher, through the operating system's change notices

Spec section 6 (Watching). efsw 1.7.2 (MIT) is fetched and linked into the core.
`FolderWatcher` watches folders recursively and reports one once it has been
quiet for a while (2 s by default; the tests use 1 s), on its own thread. Hidden
files and folders, ignored directories (asma's data directory) and
folder-modified notices never count. Folders are compared as the system reports
them, and efsw is never called while the watcher's lock is held.

**Files:**

- Modify: `cmake/Dependencies.cmake`
- Modify: `core/CMakeLists.txt`
- Create: `core/include/asma/core/FolderWatcher.h`
- Create: `core/src/FolderWatcher.cpp`
- Create: `tests/test_folder_watcher.cpp` (test)

**Interfaces:**

- Consumes: `fromUtf8`, `toUtf8`.
- Produces: `class FolderWatcher` with
  `struct Folder { std::int64_t id; std::filesystem::path path; }`,
  `using Changed = std::function<void(std::int64_t id)>`,
  `FolderWatcher(Changed, std::chrono::milliseconds quiet = 2s)`,
  `std::vector<std::int64_t> watch(const std::vector<Folder>&)` (gives the ids
  it could not watch), `void ignore(const std::filesystem::path&)`; the CMake
  target `efsw-static` (PIC).

- [ ] **Step 1: Write the failing test**

Create `tests/test_folder_watcher.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/FolderWatcher.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

using namespace asma;
using asma::test::TempDir;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// Counts reports per folder. Change notices can lag on a busy machine, so
// waits are generous and end as soon as the report comes.
struct Reports {
    std::mutex mutex;
    std::map<std::int64_t, int> counts;
    void add(std::int64_t id)
    {
        const std::lock_guard lock(mutex);
        ++counts[id];
    }
    void clear()
    {
        const std::lock_guard lock(mutex);
        counts.clear();
    }
    int count(std::int64_t id)
    {
        const std::lock_guard lock(mutex);
        return counts[id];
    }
    // Waits until folder `id` has `n` reports; false after `limit`.
    bool waitFor(std::int64_t id, int n, std::chrono::milliseconds limit = 10s)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (count(id) >= n) return true;
            std::this_thread::sleep_for(20ms);
        }
        return false;
    }
};

struct Rig {
    TempDir dir;
    fs::path a = dir.path() / "A";
    fs::path b = dir.path() / "B";
    Reports reports;
    FolderWatcher watcher{[this](std::int64_t id) { reports.add(id); }, 1s};
    Rig()
    {
        fs::create_directories(a / "Drums");
        fs::create_directories(b);
        CHECK(watcher.watch({{1, a}, {2, b}}).empty());
        // macOS can still deliver notices for the folders just made: let
        // them come and go before the test acts.
        std::this_thread::sleep_for(1500ms);
        reports.clear();
    }
};

} // namespace

TEST_CASE("a new, renamed or deleted file is reported once, for its folder", "[watcher]")
{
    Rig rig;
    test::writeBytes(rig.a / "Drums" / "kick.wav", "x");
    REQUIRE(rig.reports.waitFor(1, 1));
    std::this_thread::sleep_for(1500ms);
    CHECK(rig.reports.count(1) == 1);
    CHECK(rig.reports.count(2) == 0);

    fs::rename(rig.a / "Drums" / "kick.wav", rig.a / "Drums" / "kick_01.wav");
    CHECK(rig.reports.waitFor(1, 2));
    fs::remove(rig.a / "Drums" / "kick_01.wav");
    CHECK(rig.reports.waitFor(1, 3));
}

TEST_CASE("a burst of files is one report", "[watcher]")
{
    Rig rig;
    for (int i = 0; i < 200; ++i) test::writeBytes(rig.b / ("s" + std::to_string(i) + ".wav"), "x");
    REQUIRE(rig.reports.waitFor(2, 1));
    std::this_thread::sleep_for(1500ms);
    CHECK(rig.reports.count(2) == 1);
}

TEST_CASE("hidden files and ignored directories never count", "[watcher]")
{
    Rig rig;
    rig.watcher.ignore(rig.b / "data");
    fs::create_directories(rig.b / "data");
    test::writeBytes(rig.a / ".DS_Store", "x");
    test::writeBytes(rig.a / "._kick.wav", "x");
    fs::create_directories(rig.a / ".git");
    test::writeBytes(rig.a / ".git" / "index", "x");
    test::writeBytes(rig.b / "data" / "library.db", "x");
    std::this_thread::sleep_for(2500ms);
    CHECK(rig.reports.count(1) == 0);
    CHECK(rig.reports.count(2) == 0);
}

TEST_CASE("a folder no longer watched is not reported; a missing one is given back", "[watcher]")
{
    Rig rig;
    const auto failed = rig.watcher.watch({{1, rig.a}, {3, rig.dir.path() / "Unplugged"}});
    CHECK(failed == std::vector<std::int64_t>{3});
    test::writeBytes(rig.b / "new.wav", "x");
    test::writeBytes(rig.a / "new.wav", "x");
    REQUIRE(rig.reports.waitFor(1, 1));
    std::this_thread::sleep_for(1500ms);
    CHECK(rig.reports.count(2) == 0);
}

TEST_CASE("a folder of samples renamed inside a watched folder is reported", "[watcher]")
{
    Rig rig;
    fs::rename(rig.a / "Drums", rig.a / "Percussion");
    CHECK(rig.reports.waitFor(1, 1));
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[watcher]"`

Expected: the build stops:

```
tests/test_folder_watcher.cpp:3:10: fatal error: 'asma/core/FolderWatcher.h' file not found
```

- [ ] **Step 3: Implement**

In `cmake/Dependencies.cmake`, apply (`git apply` takes it as is):

```diff
diff --git a/cmake/Dependencies.cmake b/cmake/Dependencies.cmake
index 7fd51d5..a08dde4 100644
--- a/cmake/Dependencies.cmake
+++ b/cmake/Dependencies.cmake
@@ -30,10 +30,27 @@ FetchContent_Declare(signalsmith_stretch
   GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
   GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df # 1.4.0
   GIT_SHALLOW FALSE)
-FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128 signalsmith_stretch)
+# Folder change notices (FSEvents, ReadDirectoryChangesW, inotify); MIT.
+FetchContent_Declare(efsw
+  GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
+  GIT_TAG 41ddf6822f2d0dec7e14fafa09c4cef391137b20 # 1.7.2
+  GIT_SHALLOW FALSE)
+set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
+set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
+set(BUILD_TEST_APP OFF CACHE BOOL "" FORCE)
+set(EFSW_INSTALL OFF CACHE BOOL "" FORCE)
+FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128 signalsmith_stretch efsw)
 # Header-only; SYSTEM keeps their warnings out of ours.
 set_target_properties(signalsmith-stretch signalsmith-linear PROPERTIES SYSTEM TRUE)

+# The LV2 plugin is a shared library: everything it links must be PIC.
+set_target_properties(efsw-static PROPERTIES POSITION_INDEPENDENT_CODE ON SYSTEM TRUE)
+if(MSVC)
+  target_compile_options(efsw-static PRIVATE /w)
+else()
+  target_compile_options(efsw-static PRIVATE -w)
+endif()
+
 add_library(asma_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
 target_include_directories(asma_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
 target_compile_definitions(asma_sqlite PRIVATE
```

In `core/CMakeLists.txt`, apply (`git apply` takes it as is):

```diff
diff --git a/core/CMakeLists.txt b/core/CMakeLists.txt
index ae7364a..4a98e51 100644
--- a/core/CMakeLists.txt
+++ b/core/CMakeLists.txt
@@ -17,7 +17,7 @@ target_include_directories(asma_core
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
 target_link_libraries(asma_core
   PUBLIC asma_sqlite
-  PRIVATE asma_xxhash asma_dr_libs asma_stb asma_ebur128 Threads::Threads)
+  PRIVATE asma_xxhash asma_dr_libs asma_stb asma_ebur128 efsw-static Threads::Threads)
 if(WIN32)
   target_compile_definitions(asma_core PUBLIC
     NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
```

Create `core/include/asma/core/FolderWatcher.h`:

```cpp
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
```

Create `core/src/FolderWatcher.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/FolderWatcher.h"

#include "asma/core/Fs.h"

#include <efsw/efsw.hpp>

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
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
    // Last, so it goes first: its thread calls handleFileAction, which uses
    // everything above.
    efsw::FileWatcher watcher;

    Impl(Changed changed, std::chrono::milliseconds q) : onChanged(std::move(changed)), quiet(q)
    {
        watcher.watch(); // efsw's own thread delivers handleFileAction
        reporter = std::thread([this] { report(); });
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
        reporter.join();
        for (const auto w : watches) watcher.removeWatch(w);
    }

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

std::vector<std::int64_t> FolderWatcher::watch(const std::vector<Folder>& folders)
{
    std::vector<Folder> wanted;
    for (const auto& f : folders) wanted.push_back({f.id, real(f.path)});
    // What to drop and what to add, decided under the lock; efsw is called
    // outside it, since its thread may be waiting for the lock to report.
    std::vector<efsw::WatchID> drop;
    std::vector<Folder> add;
    {
        const std::lock_guard lock(impl_->mutex);
        for (auto it = impl_->folders.begin(); it != impl_->folders.end();) {
            const auto keep = std::find_if(wanted.begin(), wanted.end(), [&](const Folder& f) {
                return f.id == it->first && f.path == it->second.path;
            });
            if (keep != wanted.end()) {
                ++it;
                continue;
            }
            drop.push_back(it->second.watch);
            impl_->byWatch.erase(it->second.watch);
            impl_->pending.erase(it->first);
            it = impl_->folders.erase(it);
        }
        for (const auto& f : wanted)
            if (!impl_->folders.count(f.id)) add.push_back(f);
    }
    for (const auto w : drop) impl_->watcher.removeWatch(w);
    std::vector<std::int64_t> failed;
    for (const auto& f : add) {
        std::error_code ec;
        const efsw::WatchID id = fs::is_directory(f.path, ec) ? impl_->watcher.addWatch(toUtf8(f.path), impl_.get(), true)
                                                              : efsw::WatchID(-1);
        if (id < 0) {
            failed.push_back(f.id);
            continue;
        }
        const std::lock_guard lock(impl_->mutex);
        impl_->folders[f.id] = {id, f.path};
        impl_->byWatch[id] = f.id;
    }
    return failed;
}

void FolderWatcher::ignore(const std::filesystem::path& dir)
{
    const std::lock_guard lock(impl_->mutex);
    impl_->ignored.push_back(real(dir));
}

} // namespace asma
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[watcher]"`

Expected: `All tests passed (18 assertions in 5 test cases)`, every time (the
prototype ran it five times in a row).

- [ ] **Step 5: Commit**

```sh
git add cmake/Dependencies.cmake core/CMakeLists.txt core/include/asma/core/FolderWatcher.h core/src/FolderWatcher.cpp tests/test_folder_watcher.cpp
git commit -m "core: a folder watcher, through the operating system's change notices"
```

---

### Task 2: A backup of the user's data, restored by content

Spec section 10 (Daily backup). The backup holds the folders; each organised
sample by content hash and size with its rating, favourite and user tags; the
collections with members by hash; saved searches with their folder and
collection by path and name. It is written aside then renamed into place, the
one before kept. Restoring matches by content (moved and renamed files keep
their data, a file present twice gets it twice), counts what matches nothing,
ignores unknown fields and refuses a document that is not a backup.

**Files:**

- Create: `core/include/asma/core/Backup.h`
- Create: `core/src/Backup.cpp`
- Create: `tests/test_backup.cpp` (test)

**Interfaces:**

- Consumes: `UserData`, `Library`, `searchModelToJson`/`searchModelFromJson`,
  `parseJson`, `jsonEscape`.
- Produces: `std::string backupJson(Db&, std::string_view writtenAt)`;
  `void writeBackup(Db&, const std::filesystem::path& path, const std::filesystem::path& previous)`;
  `struct RestoreStats { std::size_t files, unmatched, collections, searches; }`;
  `RestoreStats restoreBackup(Db&, std::string_view)` (throws `JsonError`);
  `std::vector<std::string> backupFolders(std::string_view)`;
  `std::string backupWrittenAt(std::string_view)`; `std::string utcNow()`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_backup.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// A sample folder and a library of it.
struct Lib {
    fs::path root;
    Db db = Db::openInMemory();
    Library lib{db};
    UserData user{db};
    std::int64_t rootId = 0;
    explicit Lib(fs::path folder) : root(std::move(folder))
    {
        rootId = lib.addRoot(root);
        scanRoot(db, rootId);
    }
    std::int64_t id(const std::string& rel) { return lib.fileByPath(rootId, rel).value().id; }
};

void wav(const fs::path& path, std::uint32_t seed)
{
    test::WavSpec spec;
    spec.seed = seed;
    test::writeWav(path, spec);
}

std::string read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("a backup restores every kind of user data into a new library, by content", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "Drums" / "kick.wav", 1);
    wav(samples / "Loops" / "bass.wav", 2);
    wav(samples / "Loops" / "pad.wav", 3);
    std::string backup;
    {
        Lib old(samples);
        old.user.setRating(old.id("Drums/kick.wav"), 4);
        old.user.setFavourite(old.id("Loops/bass.wav"), true);
        old.lib.addUserTag(old.id("Loops/bass.wav"), "dusty");
        const auto set = old.user.createCollection("Live set");
        old.user.addToCollection(set, old.id("Drums/kick.wav"));
        old.user.addToCollection(set, old.id("Loops/pad.wav"));
        SearchModel scoped;
        scoped.text = "bass";
        scoped.rootId = old.rootId;
        old.user.saveSearch("Bass here", scoped);
        SearchModel inSet;
        inSet.collectionId = set;
        old.user.saveSearch("Set", inSet);
        backup = backupJson(old.db, "2026-10-07T09:00:00Z");
    }
    // The files moved and were renamed meanwhile, and the new library's ids
    // differ from the old one's.
    fs::rename(samples / "Drums" / "kick.wav", samples / "Loops" / "Kick_01.wav");
    Lib fresh(samples);
    fresh.user.createCollection("Something else"); // shifts collection ids
    const RestoreStats r = restoreBackup(fresh.db, backup);
    CHECK(r.files == 2); // the rated kick and the tagged favourite; pad is only in a collection
    CHECK(r.unmatched == 0);
    CHECK(r.collections == 1);
    CHECK(r.searches == 2);
    CHECK(fresh.user.rating(fresh.id("Loops/Kick_01.wav")) == 4);
    CHECK(fresh.user.isFavourite(fresh.id("Loops/bass.wav")));
    const auto tags = fresh.lib.tags(fresh.id("Loops/bass.wav"));
    CHECK(std::find(tags.begin(), tags.end(), std::pair<std::string, TagSource>{"dusty", TagSource::User}) != tags.end());
    const auto set = fresh.user.collectionByName("Live set");
    REQUIRE(set);
    CHECK(set->size == 2);
    const auto scoped = fresh.user.savedSearchByName("Bass here");
    REQUIRE(scoped);
    CHECK(scoped->model.rootId == fresh.rootId);
    CHECK(scoped->model.text == "bass");
    CHECK(fresh.user.savedSearchByName("Set")->model.collectionId == set->id);
    CHECK(backupWrittenAt(backup) == "2026-10-07T09:00:00Z");
    CHECK(backupFolders(backup) == std::vector<std::string>{fresh.lib.root(fresh.rootId)->path});
}

TEST_CASE("a sample present twice gets its data twice; one changed since is counted, not lost silently", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "kick.wav", 1);
    wav(samples / "snare.wav", 2);
    std::string backup;
    {
        Lib old(samples);
        old.user.setRating(old.id("kick.wav"), 5);
        old.user.setRating(old.id("snare.wav"), 2);
        backup = backupJson(old.db, "2026-10-07T09:00:00Z");
    }
    fs::copy_file(samples / "kick.wav", samples / "kick copy.wav");
    wav(samples / "snare.wav", 9); // re-encoded: other content
    Lib fresh(samples);
    const RestoreStats r = restoreBackup(fresh.db, backup);
    CHECK(r.files == 1);
    CHECK(r.unmatched == 1);
    CHECK(fresh.user.rating(fresh.id("kick.wav")) == 5);
    CHECK(fresh.user.rating(fresh.id("kick copy.wav")) == 5);
    CHECK_FALSE(fresh.user.rating(fresh.id("snare.wav")));
}

TEST_CASE("a backup from another asma version: unknown fields are ignored, a non-backup refused", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "kick.wav", 1);
    Lib fresh(samples);
    const std::string hash = fresh.lib.fileById(fresh.id("kick.wav"))->contentHash;
    const std::string size = std::to_string(fresh.lib.fileById(fresh.id("kick.wav"))->size);
    const std::string newer = "{\"asma_backup\":2,\"written\":\"2027-01-01T00:00:00Z\",\"mood\":\"calm\","
                              "\"files\":[{\"hash\":\"" + hash + "\",\"size\":" + size +
                              ",\"rating\":3,\"colour\":\"red\"}]}";
    CHECK(restoreBackup(fresh.db, newer).files == 1);
    CHECK(fresh.user.rating(fresh.id("kick.wav")) == 3);
    CHECK_THROWS_AS(restoreBackup(fresh.db, "{\"files\":[]}"), JsonError);
    CHECK_THROWS_AS(restoreBackup(fresh.db, "not json"), JsonError);
}

TEST_CASE("writing a backup keeps the one before, and leaves no half-written file", "[backup]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    wav(samples / "kick.wav", 1);
    Lib lib(samples);
    const fs::path path = dir.path() / "backup.json";
    const fs::path previous = dir.path() / "backup-previous.json";
    lib.user.setRating(lib.id("kick.wav"), 2);
    writeBackup(lib.db, path, previous);
    const std::string first = read(path);
    CHECK(first.find("\"rating\":2") != std::string::npos);
    lib.user.setRating(lib.id("kick.wav"), 5);
    writeBackup(lib.db, path, previous);
    CHECK(read(previous) == first);
    CHECK(read(path).find("\"rating\":5") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path() / "backup.json.tmp"));
    // A backup that cannot be written leaves both as they were.
    CHECK_THROWS(writeBackup(lib.db, dir.path() / "no-such-dir" / "backup.json", dir.path() / "no-such-dir" / "p.json"));
    CHECK(read(previous) == first);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[backup]"`

Expected: the build stops:

```
tests/test_backup.cpp:3:10: fatal error: 'asma/core/Backup.h' file not found
```

- [ ] **Step 3: Implement**

Create `core/include/asma/core/Backup.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

// The library's user data as one JSON document, so a rebuilt library can take
// it back: the folders; each organised sample (rated, favourite or tagged by
// the user) by content hash and size, with its rating, favourite and user
// tags; the collections, members by hash; the saved searches, their folder and
// collection by path and name since a new library gives new ids. `writtenAt`
// is RFC 3339 UTC ("2026-10-08T09:00:00Z").
std::string backupJson(Db& db, std::string_view writtenAt);

// Writes backupJson (stamped now) to `path`: through `path` + ".tmp" and a
// rename, so a crash never leaves half a backup, moving the file already at
// `path` to `previous` first. Throws std::runtime_error when it cannot.
void writeBackup(Db& db, const std::filesystem::path& path, const std::filesystem::path& previous);

struct RestoreStats {
    std::size_t files = 0;     // organised samples found and given their data
    std::size_t unmatched = 0; // organised samples whose content is nowhere in the library
    std::size_t collections = 0;
    std::size_t searches = 0;
};

// Gives the library's files the backup's user data, matched by content hash
// and size, so a moved or renamed file keeps its data and a file present twice
// gets it twice. Collections and saved searches are made, or added to, by
// name. Fields it does not know are ignored. One transaction. Throws JsonError
// for a document that is not an asma backup.
RestoreStats restoreBackup(Db& db, std::string_view json);

// What a backup says, without restoring it; empty when it does not say.
std::vector<std::string> backupFolders(std::string_view json);
std::string backupWrittenAt(std::string_view json);

// Now as RFC 3339 UTC, to the second.
std::string utcNow();

} // namespace asma
```

Create `core/src/Backup.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Backup.h"

#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace asma {

namespace {

std::string jsonString(std::string_view text) { return "\"" + jsonEscape(text) + "\""; }

// The members of an object as a JSON object, in order.
std::string object(const std::vector<std::pair<std::string, std::string>>& members)
{
    std::string out = "{";
    for (std::size_t i = 0; i < members.size(); ++i)
        out += (i ? "," : "") + jsonString(members[i].first) + ":" + members[i].second;
    return out + "}";
}

std::string array(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out + "]";
}

std::vector<std::string> userTags(Db& db, std::int64_t fileId)
{
    auto q = db.prepare("SELECT t.name FROM file_tags x JOIN tags t ON t.id = x.tag_id "
                        "WHERE x.file_id = ? AND x.source = 'user' ORDER BY t.name");
    q.bind(1, fileId);
    std::vector<std::string> out;
    while (q.step()) out.push_back(q.getText(0));
    return out;
}

// Parses a backup, or throws JsonError when it is not one.
JsonValue parseBackup(std::string_view json)
{
    JsonValue doc = parseJson(json);
    const JsonValue* version = doc.get("asma_backup");
    if (!version || !version->asInt()) throw JsonError("not an asma backup");
    return doc;
}

const JsonValue::Array& arrayOf(const JsonValue& doc, std::string_view key)
{
    static const JsonValue::Array kNone;
    const JsonValue* value = doc.get(key);
    const JsonValue::Array* items = value ? value->asArray() : nullptr;
    return items ? *items : kNone;
}

std::string text(const JsonValue& item, std::string_view key)
{
    const JsonValue* value = item.get(key);
    const std::string* s = value ? value->asString() : nullptr;
    return s ? *s : std::string();
}

// Files with this content.
std::vector<std::int64_t> filesWith(Db& db, const JsonValue& item)
{
    const std::string hash = text(item, "hash");
    const JsonValue* size = item.get("size");
    if (hash.empty() || !size || !size->asInt()) return {};
    auto q = db.prepare("SELECT id FROM files WHERE content_hash = ? AND size = ? ORDER BY id");
    q.bind(1, std::string_view(hash)).bind(2, *size->asInt());
    std::vector<std::int64_t> ids;
    while (q.step()) ids.push_back(q.getInt(0));
    return ids;
}

} // namespace

std::string backupJson(Db& db, std::string_view writtenAt)
{
    Library lib(db);
    UserData user(db);
    std::vector<std::string> folders;
    std::map<std::int64_t, std::string> rootPaths;
    for (const auto& r : lib.roots()) {
        folders.push_back(jsonString(r.path));
        rootPaths[r.id] = r.path;
    }

    std::vector<std::string> files;
    auto organised = db.prepare(
        "SELECT f.id, f.content_hash, f.size, f.root_id, f.rel_path, r.rating, x.file_id IS NOT NULL FROM files f "
        "LEFT JOIN ratings r ON r.file_id = f.id LEFT JOIN favourites x ON x.file_id = f.id "
        "WHERE f.content_hash IS NOT NULL AND f.content_hash != '' AND (r.rating IS NOT NULL OR x.file_id IS NOT NULL "
        "OR EXISTS (SELECT 1 FROM file_tags t WHERE t.file_id = f.id AND t.source = 'user')) ORDER BY f.id");
    while (organised.step()) {
        const std::int64_t id = organised.getInt(0);
        std::vector<std::pair<std::string, std::string>> m{
            {"hash", jsonString(organised.getText(1))},
            {"size", std::to_string(organised.getInt(2))},
            {"path", jsonString(rootPaths[organised.getInt(3)] + "/" + organised.getText(4))}, // for a person reading it
        };
        if (!organised.isNull(5)) m.emplace_back("rating", std::to_string(organised.getInt(5)));
        if (organised.getInt(6)) m.emplace_back("favourite", "true");
        std::vector<std::string> tags;
        for (const auto& t : userTags(db, id)) tags.push_back(jsonString(t));
        if (!tags.empty()) m.emplace_back("tags", array(tags));
        files.push_back(object(m));
    }

    std::vector<std::string> collections;
    std::map<std::int64_t, std::string> collectionNames;
    for (const auto& c : user.collections()) {
        collectionNames[c.id] = c.name;
        auto q = db.prepare("SELECT f.content_hash, f.size FROM collection_items i JOIN files f ON f.id = i.file_id "
                            "WHERE i.collection_id = ? AND f.content_hash IS NOT NULL AND f.content_hash != '' "
                            "ORDER BY f.id");
        q.bind(1, c.id);
        std::vector<std::string> members;
        while (q.step())
            members.push_back(object({{"hash", jsonString(q.getText(0))}, {"size", std::to_string(q.getInt(1))}}));
        collections.push_back(object({{"name", jsonString(c.name)}, {"files", array(members)}}));
    }

    std::vector<std::string> searches;
    for (const auto& s : user.savedSearches()) {
        std::vector<std::pair<std::string, std::string>> m{{"name", jsonString(s.name)},
                                                           {"model", jsonString(searchModelToJson(s.model))}};
        if (s.model.rootId && rootPaths.count(*s.model.rootId)) m.emplace_back("folder", jsonString(rootPaths[*s.model.rootId]));
        if (s.model.collectionId && collectionNames.count(*s.model.collectionId))
            m.emplace_back("collection", jsonString(collectionNames[*s.model.collectionId]));
        searches.push_back(object(m));
    }

    return object({{"asma_backup", "1"},
                   {"written", jsonString(writtenAt)},
                   {"folders", array(folders)},
                   {"files", array(files)},
                   {"collections", array(collections)},
                   {"searches", array(searches)}}) +
           "\n";
}

void writeBackup(Db& db, const fs::path& path, const fs::path& previous)
{
    const std::string json = backupJson(db, utcNow());
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << json;
        out.flush();
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    std::error_code ec;
    if (fs::exists(path, ec)) fs::rename(path, previous); // the day before's
    fs::rename(tmp, path);
}

RestoreStats restoreBackup(Db& db, std::string_view json)
{
    const JsonValue doc = parseBackup(json);
    Library lib(db);
    UserData user(db);
    RestoreStats stats;
    Transaction tx(db);
    for (const auto& item : arrayOf(doc, "files")) {
        const auto ids = filesWith(db, item);
        if (ids.empty()) {
            ++stats.unmatched;
            continue;
        }
        ++stats.files;
        const JsonValue* rating = item.get("rating");
        const JsonValue* favourite = item.get("favourite");
        for (const auto id : ids) {
            if (rating && rating->asInt() && *rating->asInt() >= 1 && *rating->asInt() <= 5)
                user.setRating(id, static_cast<int>(*rating->asInt()));
            if (favourite && favourite->asBool()) user.setFavourite(id, *favourite->asBool());
            for (const auto& tag : arrayOf(item, "tags"))
                if (const std::string* t = tag.asString(); t && !t->empty()) lib.addUserTag(id, *t);
        }
    }
    std::map<std::string, std::int64_t> collectionIds;
    for (const auto& item : arrayOf(doc, "collections")) {
        const std::string name = text(item, "name");
        if (name.empty()) continue;
        const auto existing = user.collectionByName(name);
        const std::int64_t id = existing ? existing->id : user.createCollection(name);
        collectionIds[name] = id;
        ++stats.collections;
        for (const auto& member : arrayOf(item, "files"))
            for (const auto file : filesWith(db, member)) user.addToCollection(id, file);
    }
    for (const auto& item : arrayOf(doc, "searches")) {
        const std::string name = text(item, "name");
        auto model = searchModelFromJson(text(item, "model"));
        if (name.empty() || !model) continue;
        // The scope by path and name: the new library has new ids.
        model->rootId.reset();
        model->collectionId.reset();
        if (const std::string folder = text(item, "folder"); !folder.empty())
            for (const auto& r : lib.roots())
                if (r.path == folder) model->rootId = r.id;
        if (const std::string collection = text(item, "collection"); !collection.empty())
            if (const auto c = user.collectionByName(collection)) model->collectionId = c->id;
        user.saveSearch(name, *model);
        ++stats.searches;
    }
    tx.commit();
    return stats;
}

std::vector<std::string> backupFolders(std::string_view json)
{
    std::vector<std::string> out;
    try {
        const JsonValue doc = parseBackup(json);
        for (const auto& f : arrayOf(doc, "folders"))
            if (const std::string* s = f.asString()) out.push_back(*s);
    } catch (const JsonError&) {
    }
    return out;
}

std::string backupWrittenAt(std::string_view json)
{
    try {
        return text(parseBackup(json), "written");
    } catch (const JsonError&) {
        return {};
    }
}

std::string utcNow()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char out[32];
    std::strftime(out, sizeof out, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return out;
}

} // namespace asma
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[backup]"`

Expected: `All tests passed (30 assertions in 4 test cases)`

- [ ] **Step 5: Commit**

```sh
git add core/include/asma/core/Backup.h core/src/Backup.cpp tests/test_backup.cpp
git commit -m "core: a backup of the user's data as JSON, restored by content into any library"
```

---

### Task 3: Check a library, and rebuild a damaged one

Spec section 10 (Database corruption). `checkLibrary` runs `PRAGMA quick_check`
read-only and never throws; a library a scan is writing is not damaged.
`repairLibrary` takes the writer lock, checks again (never moving a sound
library), moves the file and its `-wal` and `-shm` aside as `library.db.corrupt`
(dated when that exists), makes a new library with the backup's folders or the
damaged file's, scans them without analysis and restores the backup.

**Files:**

- Create: `core/include/asma/core/Repair.h`
- Create: `core/src/Repair.cpp`
- Create: `tests/test_repair.cpp` (test)

**Interfaces:**

- Consumes: task 2's backup; `WriterLock`, `scanRoot`, `Db::openReadOnly`.
- Produces: `enum class LibraryHealth { Ok, Missing, Damaged, Unreadable }`;
  `struct HealthReport { LibraryHealth health; std::string detail; }`;
  `HealthReport checkLibrary(const std::filesystem::path&)`;
  `struct RepairReport { enum class Result { Healthy, Repaired, Locked, InUse }; Result result; std::filesystem::path movedTo; std::size_t folders; std::string backupWrittenAt; RestoreStats restored; std::string detail; }`;
  `RepairReport repairLibrary(const std::filesystem::path& dbPath, const std::filesystem::path& backup, const ScanOptions& = {})`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_repair.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Repair.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// A scanned library with a rated kick, and its backup, in a data directory.
struct Data {
    TempDir dir;
    fs::path samples = dir.path() / "Samples";
    fs::path dbPath = dir.path() / "data" / "library.db";
    fs::path backup = dir.path() / "data" / "backup.json";
    Data()
    {
        test::WavSpec kick;
        kick.seed = 1;
        test::writeWav(samples / "Drums" / "kick.wav", kick);
        test::WavSpec loop;
        loop.seed = 2;
        test::writeWav(samples / "Loops" / "bass.wav", loop);
        Db db = Db::open(dbPath);
        Library lib(db);
        const auto root = lib.addRoot(samples);
        scanRoot(db, root);
        UserData(db).setRating(lib.fileByPath(root, "Drums/kick.wav")->id, 5);
    }
    void writeTheBackup()
    {
        Db db = Db::open(dbPath);
        writeBackup(db, backup, dir.path() / "data" / "backup-previous.json");
    }
    // Overwrites the library's pages after the first with garbage, as a disk
    // fault would; every connection is closed by now.
    void damage()
    {
        std::fstream f(dbPath, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(4096);
        const std::string junk(4096 * 3, '\xA5');
        f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    }
    std::optional<int> kickRating()
    {
        Db db = Db::open(dbPath);
        Library lib(db);
        const auto file = lib.fileByAbsolutePath(samples / "Drums" / "kick.wav");
        return file ? UserData(db).rating(file->id) : std::nullopt;
    }
};

} // namespace

TEST_CASE("the check tells a sound library from a damaged or missing one", "[repair]")
{
    Data d;
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Ok);
    CHECK(checkLibrary(d.dir.path() / "none.db").health == LibraryHealth::Missing);
    d.damage();
    const HealthReport damaged = checkLibrary(d.dbPath);
    CHECK(damaged.health == LibraryHealth::Damaged);
    CHECK_FALSE(damaged.detail.empty());
}

TEST_CASE("a library a scan is writing is never taken for a damaged one", "[repair]")
{
    Data d;
    Db writer = Db::open(d.dbPath);
    Transaction batch(writer); // a scan committing a batch
    writer.exec("UPDATE files SET mtime = mtime + 1");
    CHECK(checkLibrary(d.dbPath).health != LibraryHealth::Damaged);
}

TEST_CASE("repair leaves a sound library alone", "[repair]")
{
    Data d;
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Healthy);
    CHECK_FALSE(fs::exists(d.dir.path() / "data" / "library.db.corrupt"));
    CHECK(d.kickRating() == 5);
}

TEST_CASE("repair moves a damaged library aside, rebuilds it and restores the backup", "[repair]")
{
    Data d;
    d.writeTheBackup();
    d.damage();
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(r.movedTo == d.dir.path() / "data" / "library.db.corrupt");
    CHECK(fs::exists(r.movedTo));
    CHECK(r.folders == 1);
    CHECK(r.restored.files == 1);
    CHECK_FALSE(r.backupWrittenAt.empty());
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Ok);
    CHECK(d.kickRating() == 5);
}

TEST_CASE("a second damaged library is moved aside under a dated name", "[repair]")
{
    Data d;
    d.writeTheBackup();
    test::writeBytes(d.dir.path() / "data" / "library.db.corrupt", "an older one");
    d.damage();
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(r.movedTo.filename().string().rfind("library.db.corrupt-", 0) == 0);
    CHECK(fs::file_size(d.dir.path() / "data" / "library.db.corrupt") == 12); // the older one, untouched
}

TEST_CASE("repair with no backup still rebuilds, restoring nothing", "[repair]")
{
    Data d;
    d.damage();
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Repaired);
    CHECK(r.backupWrittenAt.empty()); // nothing restored
    CHECK(checkLibrary(d.dbPath).health == LibraryHealth::Ok);
}

TEST_CASE("repair waits its turn: with another writer holding the lock it moves nothing", "[repair]")
{
    Data d;
    d.damage();
    auto lock = WriterLock::tryAcquire(d.dbPath.parent_path());
    REQUIRE(lock);
    const RepairReport r = repairLibrary(d.dbPath, d.backup);
    CHECK(r.result == RepairReport::Result::Locked);
    CHECK_FALSE(fs::exists(d.dir.path() / "data" / "library.db.corrupt"));
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[repair]"`

Expected: the build stops:

```
tests/test_repair.cpp:6:10: fatal error: 'asma/core/Repair.h' file not found
```

- [ ] **Step 3: Implement**

Create `core/include/asma/core/Repair.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Backup.h"
#include "asma/core/Scanner.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace asma {

enum class LibraryHealth {
    Ok,
    Missing,    // no library yet
    Damaged,    // SQLite finds the file malformed
    Unreadable, // cannot be checked now (locked past the busy timeout, no permission): not damaged
};

struct HealthReport {
    LibraryHealth health = LibraryHealth::Ok;
    std::string detail; // what SQLite said, when not Ok
};

// Reads the library with PRAGMA quick_check: the page and record damage
// integrity_check finds, without verifying index contents, so seconds on a
// large library. Read-only; safe inside a host. Never throws.
HealthReport checkLibrary(const std::filesystem::path& dbPath);

struct RepairReport {
    enum class Result {
        Healthy,  // nothing to repair: the library was left as it is
        Repaired,
        Locked,   // another asma holds the writer lock; nothing was touched
        InUse,    // the damaged file could not be moved (Windows: another asma has it open)
    };
    Result result = Result::Healthy;
    std::filesystem::path movedTo; // where the damaged library went
    std::size_t folders = 0;       // folders the new library has
    std::string backupWrittenAt;   // the backup restored, empty when none was
    RestoreStats restored;
    std::string detail; // what the check found
};

// Rebuilds a damaged library. Takes the writer lock and checks again, so a
// sound library is never moved. Moves library.db and its -wal and -shm to
// library.db.corrupt (with the date and time when that exists; nothing is
// deleted), makes a new library with the backup's folders (or those the
// damaged file still yields), scans them without analysis (the next scan
// does that) and restores the backup at `backup`, when there is one.
RepairReport repairLibrary(const std::filesystem::path& dbPath, const std::filesystem::path& backup,
                           const ScanOptions& scan = {});

} // namespace asma
```

Create `core/src/Repair.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Repair.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/WriterLock.h"

#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace asma {

namespace {

bool says(const std::string& message, const char* what) { return message.find(what) != std::string::npos; }

bool damageMessage(const std::string& m)
{
    return says(m, "malformed") || says(m, "not a database") || says(m, "corrupt");
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The folders a damaged library still names; nothing when it cannot say.
std::vector<std::string> foldersOf(const fs::path& dbPath)
{
    std::vector<std::string> out;
    try {
        Db db = Db::openReadOnly(dbPath);
        auto q = db.prepare("SELECT path FROM roots ORDER BY id");
        while (q.step()) out.push_back(q.getText(0));
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

// Where the damaged library goes: library.db.corrupt, or with the date and
// time when that is taken.
fs::path asideName(const fs::path& dbPath)
{
    fs::path aside = dbPath;
    aside += ".corrupt";
    std::error_code ec;
    if (!fs::exists(aside, ec)) return aside;
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "-%Y%m%d-%H%M%S", &tm);
    aside += stamp;
    return aside;
}

} // namespace

HealthReport checkLibrary(const fs::path& dbPath)
{
    std::error_code ec;
    if (!fs::exists(dbPath, ec)) return {LibraryHealth::Missing, {}};
    try {
        Db db = Db::openReadOnly(dbPath);
        auto q = db.prepare("PRAGMA quick_check");
        std::string first;
        if (q.step()) first = q.getText(0);
        if (first == "ok") return {LibraryHealth::Ok, {}};
        return {LibraryHealth::Damaged, first.empty() ? std::string("quick_check found damage") : first};
    } catch (const SchemaMismatchError&) {
        return {LibraryHealth::Ok, {}}; // older or newer, not damaged
    } catch (const std::exception& e) {
        const std::string message = e.what();
        return {damageMessage(message) ? LibraryHealth::Damaged : LibraryHealth::Unreadable, message};
    }
}

RepairReport repairLibrary(const fs::path& dbPath, const fs::path& backup, const ScanOptions& scan)
{
    RepairReport report;
    auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        report.result = RepairReport::Result::Locked;
        return report;
    }
    const HealthReport health = checkLibrary(dbPath);
    report.detail = health.detail;
    if (health.health != LibraryHealth::Damaged) return report; // Healthy: never move a sound library

    const std::string backupText = readFile(backup);
    std::vector<std::string> folders = backupFolders(backupText);
    if (folders.empty()) folders = foldersOf(dbPath);

    // Move it aside, deleting nothing; its -wal and -shm go with it.
    const fs::path aside = asideName(dbPath);
    std::error_code ec;
    fs::rename(dbPath, aside, ec);
    if (ec) {
        report.result = RepairReport::Result::InUse;
        report.detail = ec.message();
        return report;
    }
    for (const char* suffix : {"-wal", "-shm"}) {
        fs::path from = dbPath, to = aside;
        from += suffix;
        to += suffix;
        if (fs::exists(from, ec)) fs::rename(from, to, ec);
    }
    report.movedTo = aside;

    Db db = Db::open(dbPath);
    Library lib(db);
    std::vector<std::int64_t> roots;
    for (const auto& f : folders) {
        Transaction tx(db);
        roots.push_back(lib.addRoot(fromUtf8(f)));
        tx.commit();
    }
    report.folders = roots.size();
    for (const auto id : roots) {
        std::error_code missing;
        if (fs::is_directory(fromUtf8(lib.root(id)->path), missing)) scanRoot(db, id, scan);
    }
    if (!backupWrittenAt(backupText).empty()) {
        report.restored = restoreBackup(db, backupText);
        report.backupWrittenAt = backupWrittenAt(backupText);
    }
    report.result = RepairReport::Result::Repaired;
    return report;
}

} // namespace asma
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[repair]"`

Expected: `All tests passed (25 assertions in 7 test cases)`

- [ ] **Step 5: Commit**

```sh
git add core/include/asma/core/Repair.h core/src/Repair.cpp tests/test_repair.cpp
git commit -m "core: check a library, and rebuild a damaged one from its backup"
```

---

### Task 4: The CLI: check, backup, restore, repair

Spec section 10. `check` and `repair` print one JSON line, for the helper's
caller, and never open the library as a writer first (a damaged one would fail
before they looked). `backup` writes `backup.json` beside the library
(`backup-previous.json` kept) or `--out FILE`; `restore FILE` says what it
restored. A repair refused by the lock exits 3.

**Files:**

- Modify: `apps/asma_main.cpp`
- Modify: `tests/test_cli_e2e.cpp` (test)

**Interfaces:**

- Consumes: tasks 2 and 3.
- Produces: `asma check`
  (`{"health":"ok"|"missing"|"damaged"|"unreadable"[,"detail":...]}`),
  `asma repair`
  (`{"result":"healthy"|"locked"|"in_use"|"repaired", "moved_to", "folders", "written", "files", "unmatched"}`),
  `asma backup [--out FILE]`, `asma restore FILE`.

- [ ] **Step 1: Write the failing test**

In `tests/test_cli_e2e.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_cli_e2e.cpp b/tests/test_cli_e2e.cpp
index a6bc014..8d756c8 100644
--- a/tests/test_cli_e2e.cpp
+++ b/tests/test_cli_e2e.cpp
@@ -419,3 +419,72 @@ TEST_CASE("asma render prints the file to drag", "[e2e]")
     CHECK(cli.runAsma("renders" + cache).out == "0 renders, 0 MB\n");
     CHECK(cli.runAsma("renders shrink" + cache).exitCode == 2);
 }
+
+namespace {
+
+// Overwrites a closed library's pages after the first, as a disk fault would.
+void damage(const fs::path& db)
+{
+    std::fstream f(db, std::ios::in | std::ios::out | std::ios::binary);
+    f.seekp(4096);
+    const std::string junk(4096 * 3, '\xA5');
+    f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
+}
+
+} // namespace
+
+TEST_CASE("check, backup, restore and repair from the CLI", "[e2e][safety]")
+{
+    Cli cli;
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+    const auto loop = quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav");
+    REQUIRE(cli.runAsma("rate 4 " + loop).exitCode == 0);
+
+    CHECK(cli.runAsma("check").out == "{\"health\":\"ok\"}\n");
+    const RunResult backup = cli.runAsma("backup");
+    CHECK(backup.exitCode == 0);
+    const fs::path data = cli.db.parent_path();
+    CHECK(fs::exists(data / "backup.json"));
+    CHECK(cli.runAsma("backup").exitCode == 0);
+    CHECK(fs::exists(data / "backup-previous.json"));
+    CHECK(cli.runAsma("backup --out " + quote(cli.dir.path() / "mine.json")).exitCode == 0);
+    CHECK(fs::exists(cli.dir.path() / "mine.json"));
+
+    CHECK(cli.runAsma("repair").out == "{\"result\":\"healthy\"}\n"); // a sound library stays
+
+    damage(cli.db);
+    CHECK(cli.runAsma("check").out.rfind("{\"health\":\"damaged\"", 0) == 0);
+    const RunResult repaired = cli.runAsma("repair");
+    CHECK(repaired.exitCode == 0);
+    CHECK(repaired.out.rfind("{\"result\":\"repaired\"", 0) == 0);
+    CHECK(repaired.out.find("\"files\":1,\"unmatched\":0") != std::string::npos);
+    CHECK(fs::exists(data / "library.db.corrupt"));
+    CHECK(cli.runAsma("query --min-rating 4").out.find("Bass_Loop") != std::string::npos);
+
+    REQUIRE(cli.runAsma("rate 0 " + loop).exitCode == 0);
+    const RunResult restored = cli.runAsma("restore " + quote(cli.dir.path() / "mine.json"));
+    CHECK(restored.exitCode == 0);
+    CHECK(restored.out == "restored 1 organised samples, 0 not found, 0 collections, 0 saved searches\n");
+    CHECK(cli.runAsma("query --min-rating 4").out.find("Bass_Loop") != std::string::npos);
+    CHECK(cli.runAsma("restore " + quote(cli.dir.path() / "nothing.json")).exitCode == 1);
+}
+
+TEST_CASE("two repairs at once on a damaged library: one rebuilds, the other waits its turn", "[e2e][safety]")
+{
+    Cli cli;
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+    REQUIRE(cli.runAsma("backup").exitCode == 0);
+    damage(cli.db);
+    // The second finds the lock taken, or, coming after, a sound library.
+    auto lock = std::make_unique<std::optional<asma::WriterLock>>(asma::WriterLock::tryAcquire(cli.db.parent_path()));
+    REQUIRE(lock->has_value());
+    const RunResult refused = cli.runAsma("repair");
+    CHECK(refused.exitCode == 3);
+    CHECK(refused.out == "{\"result\":\"locked\"}\n");
+    CHECK_FALSE(fs::exists(cli.db.parent_path() / "library.db.corrupt"));
+    lock.reset();
+    CHECK(cli.runAsma("repair").out.rfind("{\"result\":\"repaired\"", 0) == 0);
+    CHECK(cli.runAsma("repair").out == "{\"result\":\"healthy\"}\n");
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[safety]"`

Expected: the commands are unknown, so 16 of 21 assertions fail, the first:

```
tests/test_cli_e2e.cpp:459: FAILED:
  CHECK( repaired.exitCode == 0 )
with expansion:
  2 == 0
```

- [ ] **Step 3: Implement**

In `apps/asma_main.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/apps/asma_main.cpp b/apps/asma_main.cpp
index a11fc60..af5f074 100644
--- a/apps/asma_main.cpp
+++ b/apps/asma_main.cpp
@@ -7,18 +7,22 @@
 #include "asma/audio/Render.h"
 #include "asma/audio/Sync.h"
 #include "asma/core/Analyser.h"
+#include "asma/core/Backup.h"
 #include "asma/core/Db.h"
 #include "asma/core/Fs.h"
 #include "asma/core/Json.h"
 #include "asma/core/Library.h"
 #include "asma/core/NameParse.h"
 #include "asma/core/Query.h"
+#include "asma/core/Repair.h"
 #include "asma/core/Scanner.h"
 #include "asma/core/Similar.h"
 #include "asma/core/WriterLock.h"

 #include <cmath>
 #include <cstdio>
+#include <fstream>
+#include <sstream>
 #include <iostream>
 #include <string>

@@ -49,6 +53,10 @@ constexpr const char* kUsageText =
     "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--renders DIR]\n"
     "                          print the file to drag, rendering edits if any\n"
     "  renders [clear] [--renders DIR]   size of the kept renders, or delete them\n"
+    "  check                   is the library sound? (JSON)\n"
+    "  backup [--out FILE]     write the user data beside the library, or to FILE\n"
+    "  restore FILE            give the library a backup's user data\n"
+    "  repair                  rebuild a damaged library from its backup (JSON)\n"
     "  --version\n";

 // One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
@@ -263,6 +271,75 @@ int cmdRenders(Args& args, Db&)
     return kOk;
 }

+// FILE-previous.json for FILE.json: where the backup before goes.
+std::filesystem::path previousOf(const std::filesystem::path& file)
+{
+    return file.parent_path() / (toUtf8(file.stem()) + "-previous" + toUtf8(file.extension()));
+}
+
+int cmdCheck(Args& args, const std::filesystem::path& dbPath)
+{
+    rejectLeftovers(args);
+    const HealthReport r = checkLibrary(dbPath);
+    const char* health = r.health == LibraryHealth::Ok        ? "ok"
+                       : r.health == LibraryHealth::Missing   ? "missing"
+                       : r.health == LibraryHealth::Damaged   ? "damaged"
+                                                              : "unreadable";
+    JsonLine line;
+    line.str("health", health);
+    if (!r.detail.empty()) line.str("detail", r.detail);
+    std::cout << line.build() << "\n";
+    return kOk;
+}
+
+int cmdRepair(Args& args, const std::filesystem::path& dbPath)
+{
+    rejectLeftovers(args);
+    const RepairReport r = repairLibrary(dbPath, dbPath.parent_path() / "backup.json");
+    JsonLine line;
+    using Result = RepairReport::Result;
+    switch (r.result) {
+    case Result::Healthy: line.str("result", "healthy"); break;
+    case Result::Locked: line.str("result", "locked"); break;
+    case Result::InUse: line.str("result", "in_use").str("detail", r.detail); break;
+    case Result::Repaired:
+        line.str("result", "repaired")
+            .str("moved_to", toUtf8(r.movedTo))
+            .num("folders", static_cast<std::int64_t>(r.folders))
+            .str("written", r.backupWrittenAt)
+            .num("files", static_cast<std::int64_t>(r.restored.files))
+            .num("unmatched", static_cast<std::int64_t>(r.restored.unmatched));
+        break;
+    }
+    std::cout << line.build() << "\n";
+    if (r.result == Result::Locked) return kLocked;
+    return r.result == Result::InUse ? kError : kOk;
+}
+
+int cmdBackup(Args& args, Db& db, const std::filesystem::path& dbPath)
+{
+    const auto out = args.option("out");
+    rejectLeftovers(args);
+    const std::filesystem::path file = out ? fromUtf8(*out) : dbPath.parent_path() / "backup.json";
+    writeBackup(db, file, previousOf(file));
+    return kOk;
+}
+
+int cmdRestore(Args& args, Db& db)
+{
+    const auto file = args.positional();
+    rejectLeftovers(args);
+    if (!file) throw UsageError("restore needs a backup file");
+    std::ifstream in(fromUtf8(*file), std::ios::binary);
+    if (!in) throw std::runtime_error("cannot read " + *file);
+    std::stringstream text;
+    text << in.rdbuf();
+    const RestoreStats r = restoreBackup(db, text.str());
+    std::cout << "restored " << r.files << " organised samples, " << r.unmatched << " not found, " << r.collections
+              << " collections, " << r.searches << " saved searches\n";
+    return kOk;
+}
+
 int cmdQuery(Args& args, Db& db)
 {
     const bool json = args.flag("json");
@@ -303,11 +380,20 @@ int main(int argc, char** argv)
             {"search", cmdSearch},
             {"render", cmdRender},
             {"renders", cmdRenders},
+            {"restore", cmdRestore},
         };
         if (*command == "scan") {
             Db db = Db::open(dbPath);
             return cmdScan(args, db, dbPath);
         }
+        // The check and the repair must not open the library as a writer
+        // first: a damaged one would fail before they could look.
+        if (*command == "check") return cmdCheck(args, dbPath);
+        if (*command == "repair") return cmdRepair(args, dbPath);
+        if (*command == "backup") {
+            Db db = Db::open(dbPath);
+            return cmdBackup(args, db, dbPath);
+        }
         if (*command == "retry") {
             Db db = Db::open(dbPath);
             return cmdRetry(args, db, dbPath);
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[safety]"`

Expected: `All tests passed (31 assertions in 2 test cases)`

- [ ] **Step 5: Commit**

```sh
git add apps/asma_main.cpp tests/test_cli_e2e.cpp
git commit -m "cli: check, backup, restore and repair"
```

---

### Task 5: The library view tells damage apart and follows a rebuilt file

Spec section 10. A new state, `Damaged`, for SQLite's "malformed", "not a
database" and "corrupt" (a file that is not a database is now Damaged; two older
tests change). The view probes the library on opening, since opening reads only
the header. A rebuilt library replaces the file; the view compares the file's
identity at each refresh and opens the new one.

**Files:**

- Modify: `core/include/asma/core/Fs.h`
- Modify: `core/src/Fs.cpp`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Modify: `tests/plugin/test_library_view.cpp` (test)

**Interfaces:**

- Consumes: nothing new.
- Produces: `LibraryState::Damaged` (message "The library is damaged. asma is
  rebuilding it."); `std::string fileIdentity(const std::filesystem::path&)` in
  `Fs.h` (empty on Windows).

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_library_view.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_library_view.cpp b/tests/plugin/test_library_view.cpp
index cec9bbb..5db2f87 100644
--- a/tests/plugin/test_library_view.cpp
+++ b/tests/plugin/test_library_view.cpp
@@ -125,7 +125,7 @@ TEST_CASE("LibraryView explains a library it cannot read", "[libview]")
     const auto junk = other.path() / "library.db";
     test::writeBytes(junk, "this is not a database, not even close to one");
     LibraryView broken(junk);
-    CHECK(broken.refresh() == LibraryState::Unreadable);
+    CHECK(broken.refresh() == LibraryState::Damaged); // not a database: rebuilt like a damaged one
 }

 TEST_CASE("LibraryView gives up quietly on a library that breaks while open", "[libview]")
@@ -160,7 +160,7 @@ TEST_CASE("LibraryView gives up quietly on a library that breaks while open", "[

     CHECK_NOTHROW(view.changed());
     CHECK(view.search({}).empty());
-    CHECK(view.state() == LibraryState::Unreadable);
+    CHECK(view.state() == LibraryState::Damaged);
     CHECK_NOTHROW(view.info(loopId));
     CHECK(view.contentHash(loopId).empty());
     CHECK_NOTHROW(view.infoFor(f.lib / "Loops" / "Bass_Loop_Am_120.wav"));
@@ -184,3 +184,45 @@ TEST_CASE("the standalone updates an older library; a plugin only says so", "[li
     CHECK(standalone.changed());
     CHECK(plugin.refresh() == LibraryState::Open); // the plugin reads the updated library too
 }
+
+TEST_CASE("a damaged library is told apart from one that only cannot be read", "[libview]")
+{
+    Fixture f;
+    f.scan();
+    LibraryView view(f.dbPath);
+    REQUIRE(view.refresh() == LibraryState::Open);
+    {
+        // Garbage in the schema, through another connection, as above.
+        sqlite3* db = nullptr;
+        REQUIRE(sqlite3_open(toUtf8(f.dbPath).c_str(), &db) == SQLITE_OK);
+        const std::string sql = "PRAGMA writable_schema = ON;"
+                                "UPDATE sqlite_master SET sql = 'garbage' WHERE name = 'files';"
+                                "PRAGMA schema_version = 999;";
+        REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
+        sqlite3_close(db);
+    }
+    CHECK(view.search({}).empty());
+    CHECK(view.state() == LibraryState::Damaged);
+    CHECK(view.message() == "The library is damaged. asma is rebuilding it.");
+    CHECK(view.refresh() == LibraryState::Damaged); // still, until it is rebuilt
+}
+
+#ifndef _WIN32 // Windows never lets a file a connection holds be replaced
+TEST_CASE("a library file replaced by a rebuilt one is opened afresh", "[libview]")
+{
+    Fixture f;
+    f.scan();
+    LibraryView view(f.dbPath);
+    REQUIRE(view.refresh() == LibraryState::Open);
+    REQUIRE(view.search({}).size() == 2);
+    const fs::path rebuilt = f.dir.path() / "data" / "rebuilt.db";
+    {
+        Db db = Db::open(rebuilt); // an empty library, as a rebuild starts
+    }
+    fs::rename(f.dbPath, f.dir.path() / "data" / "library.db.corrupt");
+    fs::rename(rebuilt, f.dbPath);
+    view.refresh();
+    CHECK(view.changed());
+    CHECK(view.search({}).empty()); // the new file's rows, not the old one's
+}
+#endif
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[libview]"`

Expected: the build stops:

```
tests/plugin/test_library_view.cpp:128:45: error: no member named 'Damaged' in 'asma::app::LibraryState'
tests/plugin/test_library_view.cpp:163:41: error: no member named 'Damaged' in 'asma::app::LibraryState'
```

- [ ] **Step 3: Implement**

In `core/include/asma/core/Fs.h`, apply (`git apply` takes it as is):

```diff
diff --git a/core/include/asma/core/Fs.h b/core/include/asma/core/Fs.h
index a1f4f05..d987803 100644
--- a/core/include/asma/core/Fs.h
+++ b/core/include/asma/core/Fs.h
@@ -43,4 +43,10 @@ std::filesystem::path defaultDataDir();
 // meaningful for equality and ordering on the same machine.
 std::int64_t fileTimeToInt(std::filesystem::file_time_type time);

+// Which file a path names now, as an opaque string: two calls give the same
+// string unless the file was replaced (renamed over, deleted and made again).
+// Empty when it cannot tell, and always on Windows, which never lets a file
+// a program holds open be replaced.
+std::string fileIdentity(const std::filesystem::path& path);
+
 } // namespace asma
```

In `core/src/Fs.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/core/src/Fs.cpp b/core/src/Fs.cpp
index 0d11a8d..48fc63a 100644
--- a/core/src/Fs.cpp
+++ b/core/src/Fs.cpp
@@ -1,6 +1,10 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #include "asma/core/Fs.h"

+#ifndef _WIN32
+#include <sys/stat.h>
+#endif
+
 #include <cstdlib>
 #include <cstring>
 #include <optional>
@@ -82,3 +86,20 @@ std::int64_t fileTimeToInt(fs::file_time_type time)
 }

 } // namespace asma
+
+namespace asma {
+
+std::string fileIdentity(const std::filesystem::path& path)
+{
+#ifdef _WIN32
+    (void)path;
+    return {};
+#else
+    struct stat st {};
+    if (::stat(path.c_str(), &st) != 0) return {};
+    return std::to_string(static_cast<unsigned long long>(st.st_dev)) + ":"
+         + std::to_string(static_cast<unsigned long long>(st.st_ino));
+#endif
+}
+
+} // namespace asma
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index d3dd9f9..23bd79c 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -15,11 +15,22 @@ LibraryView::~LibraryView() = default;

 LibraryState LibraryView::refresh()
 {
-    if (state_ == LibraryState::Open) return state_;
+    if (state_ == LibraryState::Open) {
+        // A rebuilt library replaces the file: follow it to the new one.
+        const std::string now = fileIdentity(path_);
+        if (now.empty() || now == identity_) return state_;
+        watcher_.reset();
+        db_.reset();
+        state_ = LibraryState::Missing;
+    }
     std::error_code ec;
     if (!std::filesystem::exists(path_, ec)) return state_ = LibraryState::Missing;
     try {
+        identity_ = fileIdentity(path_);
         db_.emplace(Db::openReadOnly(path_));
+        // Opening reads only the header: a damaged schema shows at the first
+        // statement, so make one now rather than call the library open.
+        db_->prepare("SELECT id FROM files LIMIT 1").step();
         watcher_ = std::make_unique<ChangeWatcher>(*db_);
         opened_ = true;
         return state_ = LibraryState::Open;
@@ -36,14 +47,24 @@ LibraryState LibraryView::refresh()
                 state_ = LibraryState::Unreadable;
             }
         }
-    } catch (const DbError&) {
-        state_ = LibraryState::Unreadable;
+    } catch (const DbError& e) {
+        close(e.what());
+        return state_;
     }
     watcher_.reset();
     db_.reset();
     return state_;
 }

+void LibraryView::close(const std::string& why)
+{
+    watcher_.reset();
+    db_.reset();
+    const bool damaged = why.find("malformed") != std::string::npos || why.find("not a database") != std::string::npos
+                      || why.find("corrupt") != std::string::npos;
+    state_ = damaged ? LibraryState::Damaged : LibraryState::Unreadable;
+}
+
 std::string LibraryView::message() const
 {
     switch (state_) {
@@ -53,6 +74,7 @@ std::string LibraryView::message() const
         return "This library was made by an older asma. Open the asma app once, or run asma scan, to update it.";
     case LibraryState::TooNew: return "This library was made by a newer asma. Update asma to read it.";
     case LibraryState::Unreadable: return "The library file cannot be read: " + toUtf8(path_);
+    case LibraryState::Damaged: return "The library is damaged. asma is rebuilding it.";
     }
     return {};
 }
@@ -63,10 +85,8 @@ Result LibraryView::guarded(Query&& query, Result fallback)
     if (!db_) return fallback;
     try {
         return query();
-    } catch (const std::exception&) {
-        watcher_.reset();
-        db_.reset();
-        state_ = LibraryState::Unreadable;
+    } catch (const std::exception& e) {
+        close(e.what());
         return fallback;
     }
 }
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index e815890..0592770 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -22,6 +22,7 @@ enum class LibraryState {
     Outdated,   // an older schema: a writer must open it once to migrate it
     TooNew,     // written by a newer asma
     Unreadable, // not a library, or not readable
+    Damaged,    // SQLite finds it malformed: it is to be rebuilt
 };

 // The library as the UI sees it: opened read-only (so it works inside a
@@ -104,8 +105,10 @@ private:
     // refresh() tries to open it again.
     template <typename Query, typename Result>
     Result guarded(Query&& query, Result fallback);
+    void close(const std::string& why); // after an error: Damaged or Unreadable by what it says

     std::filesystem::path path_;
+    std::string identity_; // the file the connection is to, to notice it replaced
     Access access_;
     LibraryState state_ = LibraryState::Missing;
     std::optional<Db> db_;
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[libview]"`

Expected: `All tests passed (58 assertions in 8 test cases)`

- [ ] **Step 5: Commit**

```sh
git add core/include/asma/core/Fs.h core/src/Fs.cpp plugin/src/LibraryView.cpp plugin/src/LibraryView.h tests/plugin/test_library_view.cpp
git commit -m "app: the library view tells damage from unreadable, and follows a rebuilt library file"
```

---

### Task 6: When to scan which folder

Spec section 6 (Watching). JUCE-free: each folder once when first seen, a
reported folder as soon as possible, every folder every 15 minutes; queued at
most once; a change during a folder's scan queues it again; a refused scan waits
for the next change or poll.

**Files:**

- Create: `plugin/src/ScanSchedule.cpp`
- Create: `plugin/src/ScanSchedule.h`
- Create: `tests/plugin/test_scan_schedule.cpp` (test)

**Interfaces:**

- Consumes: nothing.
- Produces: `class ScanSchedule` (`using Clock = std::chrono::steady_clock`,
  `kPollEvery` 15 min,
  `setFolders(const std::vector<std::int64_t>&, Clock::time_point)`,
  `changed(std::int64_t)`,
  `std::optional<std::int64_t> next(Clock::time_point)`,
  `scanned(std::int64_t, Clock::time_point)`).

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_scan_schedule.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ScanSchedule.h"

#include <catch2/catch_test_macros.hpp>

using asma::app::ScanSchedule;
using namespace std::chrono_literals;

namespace {

const ScanSchedule::Clock::time_point t0{};

// Every folder next() gives at `now`, as a scan would take them in turn.
std::vector<std::int64_t> drain(ScanSchedule& s, ScanSchedule::Clock::time_point now)
{
    std::vector<std::int64_t> out;
    while (const auto id = s.next(now)) {
        out.push_back(*id);
        s.scanned(*id, now);
    }
    return out;
}

} // namespace

TEST_CASE("every folder is scanned once at startup, then every 15 minutes", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1, 2}, t0);
    CHECK(drain(s, t0) == std::vector<std::int64_t>{1, 2});
    CHECK(drain(s, t0 + 14min) .empty());
    CHECK(drain(s, t0 + 15min) == std::vector<std::int64_t>{1, 2});
}

TEST_CASE("a changed folder is scanned next, and only once however often it changes", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1, 2}, t0);
    drain(s, t0);
    s.changed(2);
    s.changed(2);
    CHECK(drain(s, t0 + 1min) == std::vector<std::int64_t>{2});
    // Its poll counts from that scan.
    CHECK(drain(s, t0 + 15min) == std::vector<std::int64_t>{1});
    CHECK(drain(s, t0 + 16min) == std::vector<std::int64_t>{2});
}

TEST_CASE("a change during a folder's scan scans it again after", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1}, t0);
    REQUIRE(s.next(t0) == 1); // scanning
    s.changed(1);             // the scan may already have passed the change
    s.scanned(1, t0 + 10s);
    CHECK(s.next(t0 + 10s) == 1);
}

TEST_CASE("a folder added elsewhere is scanned at once; one removed is not", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1}, t0);
    drain(s, t0);
    s.changed(1);
    s.setFolders({3}, t0 + 1min); // 1 removed, 3 added
    CHECK(drain(s, t0 + 1min) == std::vector<std::int64_t>{3});
    s.changed(1); // a stray report for a folder no longer there
    CHECK(drain(s, t0 + 2min).empty());
}

TEST_CASE("a refused scan just waits for the next change or poll", "[schedule]")
{
    ScanSchedule s;
    s.setFolders({1}, t0);
    REQUIRE(s.next(t0) == 1);
    s.scanned(1, t0); // refused by the writer lock: counts as done
    CHECK_FALSE(s.next(t0 + 1s));
    CHECK(s.next(t0 + 15min) == 1);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[schedule]"`

Expected: the build stops:

```
tests/plugin/test_scan_schedule.cpp:2:10: fatal error: 'ScanSchedule.h' file not found
```

- [ ] **Step 3: Implement**

Create `plugin/src/ScanSchedule.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ScanSchedule.h"

#include <algorithm>

namespace asma::app {

void ScanSchedule::setFolders(const std::vector<std::int64_t>& ids, Clock::time_point now)
{
    for (auto it = pollAt_.begin(); it != pollAt_.end();) {
        if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
            queue_.erase(std::remove(queue_.begin(), queue_.end(), it->first), queue_.end());
            it = pollAt_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto id : ids)
        if (pollAt_.emplace(id, now + kPollEvery).second) changed(id);
}

void ScanSchedule::changed(std::int64_t id)
{
    if (!pollAt_.count(id)) return;
    if (std::find(queue_.begin(), queue_.end(), id) == queue_.end()) queue_.push_back(id);
}

std::optional<std::int64_t> ScanSchedule::next(Clock::time_point now)
{
    if (!queue_.empty()) {
        const auto id = queue_.front();
        queue_.pop_front();
        return id;
    }
    for (const auto& [id, at] : pollAt_)
        if (at <= now) return id;
    return std::nullopt;
}

void ScanSchedule::scanned(std::int64_t id, Clock::time_point now)
{
    if (const auto it = pollAt_.find(id); it != pollAt_.end()) it->second = now + kPollEvery;
}

} // namespace asma::app
```

Create `plugin/src/ScanSchedule.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace asma::app {

// Which folder to scan when: each once when first seen (at startup, or added
// elsewhere), a folder the watcher reports as soon as it can, and every
// folder every 15 minutes, for what change notices miss. A folder is queued at
// most once; a change while it is being scanned queues it again, since the
// scan may have passed it. JUCE-free; the caller gives the time.
class ScanSchedule {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::minutes kPollEvery{15};

    // The library's folders now. New ones are due at once; gone ones are
    // forgotten, queued or not.
    void setFolders(const std::vector<std::int64_t>& ids, Clock::time_point now);
    void changed(std::int64_t id);
    // The next folder to scan, if one is due; it leaves the queue.
    std::optional<std::int64_t> next(Clock::time_point now);
    // A scan of the folder ended, however (a scan refused by the writer lock
    // counts: the next change or poll catches up).
    void scanned(std::int64_t id, Clock::time_point now);

private:
    std::map<std::int64_t, Clock::time_point> pollAt_; // the folders, and when each is next polled
    std::deque<std::int64_t> queue_;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[schedule]"`

Expected: `All tests passed (13 assertions in 5 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/ScanSchedule.cpp plugin/src/ScanSchedule.h tests/plugin/test_scan_schedule.cpp
git commit -m "app: when to scan which folder"
```

---

### Task 7: One keeper per process watches the folders and scans them, in plugins too

Spec section 6 (Watching). `ScanJob` becomes a `ScanRunner` that scans any
folder of the library, its progress naming the folder ("Scanning Samples: 1,200
of 8,000"). `LibraryKeeper`, shared by every editor in a process, reads the
folders from the library, watches them, feeds the schedule and runs one scan at
a time, telling the footer only news. The editors hold it (so it lives while a
window is open); the processor keeps only its binary's path. Every format gets
`asma-scan` beside it, and the validator runs with its own data directory.

**Files:**

- Modify: `ci/validate-plugins.sh`
- Modify: `plugin/CMakeLists.txt`
- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/AsmaProcessor.cpp`
- Modify: `plugin/src/AsmaProcessor.h`
- Create: `plugin/src/LibraryKeeper.cpp`
- Create: `plugin/src/LibraryKeeper.h`
- Modify: `plugin/src/ScanJob.cpp`
- Modify: `plugin/src/ScanJob.h`
- Modify: `tests/plugin/test_editor.cpp` (test)
- Create: `tests/plugin/test_library_keeper.cpp` (test)
- Modify: `tests/plugin/test_scan_job.cpp` (test)

**Interfaces:**

- Consumes: tasks 1 and 6; `ScanSupervisor`, `LibraryView`.
- Produces: `class ScanRunner`
  (`start(std::int64_t rootId, const std::string& label)`, `busy()`,
  `progress()`, `takeReport()`, `ready()`); `ScanJob : ScanRunner` (and
  `addAndScan`, `setWorker`, `cancel`, `workerNextTo`);
  `std::string groupDigits(std::int64_t)`; `class LibraryKeeper`
  (`LibraryKeeper(path dbPath, std::unique_ptr<ScanRunner>)`,
  `static shared(dbPath, worker)`, `addFolder`, `progress()`, `messageCount()`,
  `message()`, `folderChanged(std::int64_t)`, `tick(Clock::time_point)`,
  `runner()`); `AsmaEditor::keeper()`; `AsmaProcessor::binary()`. Removed:
  `AsmaProcessor::scans()`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index 3848712..7f25e08 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -131,12 +131,14 @@ TEST_CASE("the standalone adds a folder and shows the scan in the table", "[edit
     const auto more = rig.f.dir.path() / "More";
     test::writeWavFloat(more / "Pad_Cm.wav", 48000, {test::sine(261.6, 1.0, 0.3, 48000)});
     // The app would find asma-scan beside itself; the test points at the build's.
-    rig.p->scans()->setWorker(ASMA_SCAN_PATH);
+    dynamic_cast<app::ScanJob&>(rig.editor->keeper().runner()).setWorker(ASMA_SCAN_PATH);
     rig.editor->addFolder(more);
-    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
-    while (rig.p->scans()->busy() && std::chrono::steady_clock::now() < deadline)
-        std::this_thread::sleep_for(std::chrono::milliseconds(10));
-    rig.editor->poll();
+    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
+    // The row shows once the scan has indexed it; the news once it is done.
+    while (!rig.editor->footer().rightText().contains("Scan finished") && std::chrono::steady_clock::now() < deadline) {
+        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the keeper's timer
+        rig.editor->poll();
+    }
     CHECK(rig.editor->footer().rightText().contains("Scan finished: 1 added"));
     CHECK(rig.editor->table().getNumRows() == 4);
 }
```

Create `tests/plugin/test_library_keeper.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "LibraryKeeper.h"
#include "asma/core/Library.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace std::chrono_literals;
using app::LibraryKeeper;
namespace fs = std::filesystem;

namespace {

// Records what it is asked to scan and finishes when told.
struct FakeRunner final : app::ScanRunner {
    std::vector<std::int64_t> started;
    bool running = false;
    std::optional<ScanReport> report;
    bool start(std::int64_t rootId, const std::string&) override
    {
        if (running) return false;
        started.push_back(rootId);
        running = true;
        return true;
    }
    bool busy() const override { return running; }
    std::string progress() const override { return running ? "Scanning" : ""; }
    std::optional<ScanReport> takeReport() override
    {
        auto r = report;
        report.reset();
        return r;
    }
    bool ready() const override { return true; }
    void finish(ScanReport::Result result = ScanReport::Result::Finished, std::size_t added = 0)
    {
        ScanReport r;
        r.result = result;
        r.index.added = added;
        report = r;
        running = false;
    }
};

struct KeeperRig {
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    FakeRunner* runner = nullptr;
    std::unique_ptr<LibraryKeeper> keeper;
    std::int64_t samples = 0, other = 0;
    const LibraryKeeper::Clock::time_point t0 = LibraryKeeper::Clock::now();
    KeeperRig()
    {
        f.scan();
        fs::create_directories(f.dir.path() / "Other");
        {
            Db db = Db::open(f.dbPath);
            Library lib(db);
            samples = lib.roots().front().id;
            other = lib.addRoot(f.dir.path() / "Other");
        }
        auto fake = std::make_unique<FakeRunner>();
        runner = fake.get();
        keeper = std::make_unique<LibraryKeeper>(f.dbPath, std::move(fake));
    }
};

} // namespace

TEST_CASE("the keeper scans every folder once at startup, one at a time", "[keeper]")
{
    KeeperRig rig;
    rig.keeper->tick(rig.t0);
    CHECK(rig.runner->started == std::vector<std::int64_t>{rig.samples});
    rig.keeper->tick(rig.t0 + 1s); // still running: nothing more
    CHECK(rig.runner->started.size() == 1);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    CHECK(rig.runner->started == std::vector<std::int64_t>{rig.samples, rig.other});
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.size() == 2);
    rig.keeper->tick(rig.t0 + 16min); // the poll
    CHECK(rig.runner->started.size() == 3);
}

TEST_CASE("the keeper scans a folder the watcher reports", "[keeper]")
{
    KeeperRig rig;
    rig.keeper->tick(rig.t0);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 1s);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    rig.keeper->folderChanged(rig.other);
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.back() == rig.other);
    CHECK(rig.runner->started.size() == 3);
}

TEST_CASE("the keeper reports news, not quiet scans or a lock refused", "[keeper]")
{
    KeeperRig rig;
    rig.keeper->tick(rig.t0);
    rig.runner->finish(ScanReport::Result::Finished, 0);
    rig.keeper->tick(rig.t0 + 1s);
    CHECK(rig.keeper->messageCount() == 0);
    rig.runner->finish(ScanReport::Result::Locked);
    rig.keeper->tick(rig.t0 + 2s);
    CHECK(rig.keeper->messageCount() == 0);
    rig.keeper->folderChanged(rig.samples);
    rig.keeper->tick(rig.t0 + 3s);
    rig.runner->finish(ScanReport::Result::Finished, 3);
    rig.keeper->tick(rig.t0 + 4s);
    CHECK(rig.keeper->messageCount() == 1);
    CHECK(rig.keeper->message() == "Scan finished: 3 added");
}

TEST_CASE("a folder added elsewhere is scanned at the next tick", "[keeper]")
{
    KeeperRig rig;
    rig.keeper->tick(rig.t0);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 1s);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    fs::create_directories(rig.f.dir.path() / "Third");
    std::int64_t third = 0;
    {
        Db db = Db::open(rig.f.dbPath); // the app, or the CLI
        third = Library(db).addRoot(rig.f.dir.path() / "Third");
    }
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.back() == third);
}

TEST_CASE("every window in a process shares one keeper", "[keeper]")
{
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    auto a = LibraryKeeper::shared(f.dbPath, "asma-scan");
    auto b = LibraryKeeper::shared(f.dbPath, "asma-scan");
    CHECK(a == b);
    a.reset();
    b.reset();
    CHECK(LibraryKeeper::shared(f.dbPath, "asma-scan") != nullptr); // made again once none held it
}

TEST_CASE("a sample dropped into a watched folder appears in the table", "[keeper][watch]")
{
    test::EditorRig rig; // a plugin: it scans too, through asma-scan
    REQUIRE(rig.editor->table().getNumRows() == 3);
    dynamic_cast<app::ScanJob&>(rig.editor->keeper().runner()).setWorker(ASMA_SCAN_PATH);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(2000); // watching, and the startup scan under way
    test::writeWavFloat(rig.f.lib / "Drums" / "Hat_03.wav", 48000, {test::hatHit(48000, 5)});
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < deadline && rig.editor->table().getNumRows() < 4) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the keeper's timer
        rig.editor->poll();
    }
    CHECK(rig.editor->table().getNumRows() == 4);
}
```

In `tests/plugin/test_scan_job.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_scan_job.cpp b/tests/plugin/test_scan_job.cpp
index 97a2e3e..be079c9 100644
--- a/tests/plugin/test_scan_job.cpp
+++ b/tests/plugin/test_scan_job.cpp
@@ -3,6 +3,7 @@
 #include "LibraryView.h"
 #include "PluginTestUtil.h"
 #include "ScanJob.h"
+#include "asma/core/Library.h"

 #include <catch2/catch_test_macros.hpp>
 #include <thread>
@@ -83,10 +84,29 @@ TEST_CASE("the scanner is looked for next to the app", "[scanjob]")
 #endif
 }

-TEST_CASE("only the standalone scans", "[scanjob]")
+TEST_CASE("ScanJob scans a folder already in the library, saying which", "[scanjob]")
 {
-    app::AsmaProcessor plugin;
-    CHECK(plugin.scans() == nullptr);
-    app::AsmaProcessor standalone(app::AsmaProcessor::Mode::Standalone);
-    CHECK(standalone.scans() != nullptr);
+    test::LibraryFixture f;
+    f.scan();
+    test::writeWavFloat(f.lib / "Drums" / "Hat_03.wav", 48000, {test::hatHit(48000, 5)});
+    std::int64_t root = 0;
+    {
+        Db db = Db::open(f.dbPath);
+        root = Library(db).roots().front().id;
+    }
+    ScanJob job(f.dbPath, ASMA_SCAN_PATH);
+    CHECK(job.ready());
+    REQUIRE(job.start(root, "Samples"));
+    CHECK(job.progress().rfind("Scanning Samples", 0) == 0);
+    const auto report = finish(job);
+    REQUIRE(report);
+    CHECK(report->index.added == 1);
+    CHECK_FALSE(ScanJob(f.dbPath, f.dir.path() / "no-such-asma-scan").ready());
+}
+
+TEST_CASE("progress counts read with thousands grouped", "[scanjob]")
+{
+    CHECK(app::groupDigits(7) == "7");
+    CHECK(app::groupDigits(1200) == "1,200");
+    CHECK(app::groupDigits(1234567) == "1,234,567");
 }
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[keeper],[scanjob],[editor]"`

Expected: the build stops:

```
tests/plugin/test_library_keeper.cpp:3:10: fatal error: 'LibraryKeeper.h' file not found
tests/plugin/test_scan_job.cpp:98:15: error: no member named 'ready' in 'asma::app::ScanJob'
tests/plugin/test_scan_job.cpp:99:17: error: no member named 'start' in 'asma::app::ScanJob'
```

- [ ] **Step 3: Implement**

In `ci/validate-plugins.sh`, apply (`git apply` takes it as is):

```diff
diff --git a/ci/validate-plugins.sh b/ci/validate-plugins.sh
index 5f78bc4..253c9e1 100755
--- a/ci/validate-plugins.sh
+++ b/ci/validate-plugins.sh
@@ -6,6 +6,9 @@
 set -euo pipefail

 BUILD=${1:-build}
+# A plugin's window keeps its library in step with its folders; a validator
+# run must not touch the person's own library.
+export ASMA_DATA_DIR="${ASMA_DATA_DIR:-$(mktemp -d)}"
 ART="$BUILD/plugin/asma_plugin_artefacts/Release"
 TOOLS=${TOOLS:-"$BUILD/validators"}
 PLUGINVAL_VERSION=v1.0.4
```

In `plugin/CMakeLists.txt`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/CMakeLists.txt b/plugin/CMakeLists.txt
index a86940b..c82cc60 100644
--- a/plugin/CMakeLists.txt
+++ b/plugin/CMakeLists.txt
@@ -63,18 +63,16 @@ clap_juce_extensions_plugin(TARGET asma_plugin
   CLAP_ID "com.anodelabs.asma"
   CLAP_FEATURES instrument sampler)

-# The standalone runs asma-scan from beside its own executable.
-add_dependencies(asma_plugin_Standalone asma-scan)
-add_custom_command(TARGET asma_plugin_Standalone POST_BUILD
-  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma-scan> $<TARGET_FILE_DIR:asma_plugin_Standalone>)
 # Every format writes the library through the asma helper beside its binary
-# (the standalone only for a retry). It goes in as asma-cli: the app's and
-# the bundles' own binaries are called asma.
+# (the standalone only for a retry) and scans through asma-scan beside it.
+# The helper goes in as asma-cli: the app's and the bundles' own binaries are
+# called asma.
 foreach(format Standalone VST3 AU LV2 CLAP)
   if(TARGET asma_plugin_${format})
-    add_dependencies(asma_plugin_${format} asma)
+    add_dependencies(asma_plugin_${format} asma asma-scan)
     add_custom_command(TARGET asma_plugin_${format} POST_BUILD
       COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma>
-              $<TARGET_FILE_DIR:asma_plugin_${format}>/asma-cli${CMAKE_EXECUTABLE_SUFFIX})
+              $<TARGET_FILE_DIR:asma_plugin_${format}>/asma-cli${CMAKE_EXECUTABLE_SUFFIX}
+      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma-scan> $<TARGET_FILE_DIR:asma_plugin_${format}>)
   endif()
 endforeach()
```

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 9ae00e2..7722bac 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -44,6 +44,7 @@ juce::Font starFont() { return theme::font(theme::Face::Text, 11.0f).withExtraKe
 AsmaEditor::AsmaEditor(AsmaProcessor& owner)
     : juce::AudioProcessorEditor(owner), processor_(owner),
       library_(owner.libraryPath(), owner.isStandalone() ? LibraryView::Access::MayMigrate : LibraryView::Access::ReadOnly),
+      keeper_(LibraryKeeper::shared(owner.libraryPath(), ScanJob::workerNextTo(owner.binary()))),
       top_(owner.isStandalone())
 {
     setLookAndFeel(&lookAndFeel_);
@@ -555,10 +556,9 @@ void AsmaEditor::chooseFolder()

 void AsmaEditor::addFolder(const std::filesystem::path& folder)
 {
-    ScanJob* scans = processor_.scans();
-    if (!scans) return;
+    if (!processor_.isStandalone()) return;
     std::string why;
-    scanMessage_ = scans->addAndScan(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
+    scanMessage_ = keeper_->addFolder(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
     updateReadouts();
 }

@@ -596,19 +596,10 @@ void AsmaEditor::poll()
 {
     // A host or preset menu loaded state while the window was open.
     if (processor_.stateLoads() != loadedStates_) loadState();
-    if (ScanJob* scans = processor_.scans())
-        if (const auto report = scans->takeReport()) {
-            using Result = ScanReport::Result;
-            switch (report->result) {
-            case Result::Finished:
-                scanMessage_ = "Scan finished: " + juce::String(report->index.added) + " added";
-                break;
-            case Result::Locked: scanMessage_ = "Another asma is scanning this library; try again when it is done."; break;
-            case Result::Cancelled: scanMessage_ = "Scan cancelled."; break;
-            case Result::Failed:
-            case Result::Crashed: scanMessage_ = "Scan failed: " + juce::String(report->message); break;
-            }
-        }
+    if (keeper_->messageCount() != keeperMessages_) {
+        keeperMessages_ = keeper_->messageCount();
+        scanMessage_ = utf8(keeper_->message());
+    }
     if (browser_.poll()) {
         pending_.libraryChanged();
         table_.updateContent();
@@ -778,8 +769,8 @@ void AsmaEditor::updateReadouts()
     } else {
         footer_.setDrag({});
     }
-    ScanJob* scans = processor_.scans();
-    footer_.setStatus(scans && scans->busy() ? juce::String(scans->progress()) : scanMessage_);
+    const std::string scanning = keeper_->progress();
+    footer_.setStatus(!scanning.empty() ? utf8(scanning) : scanMessage_);
 }

 int AsmaEditor::getNumRows() { return browser_.count(); }
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index ad7599d..80f57da 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -2,6 +2,7 @@
 #pragma once

 #include "Browser.h"
+#include "LibraryKeeper.h"
 #include "LibraryView.h"
 #include "LibraryWriter.h"
 #include "Names.h"
@@ -78,6 +79,9 @@ public:
     void addFolder(const std::filesystem::path& folder);
     // Deletes every kept render; the footer's button asks first.
     void clearRenders();
+    // What keeps the library in step with its folders while this window,
+    // or any other asma window in the process, is open.
+    LibraryKeeper& keeper() { return *keeper_; }

     // Organising: the table shows each change at once and the library
     // confirms it; a write that fails rolls back and the footer says why.
@@ -145,6 +149,8 @@ private:
     AsmaProcessor& processor_;
     AsmaLookAndFeel lookAndFeel_; // first in, last out: every child uses it
     LibraryView library_;
+    std::shared_ptr<LibraryKeeper> keeper_;
+    std::uint64_t keeperMessages_ = 0; // the keeper's messages this window has shown
     Browser browser_{library_};
     TopBar top_;
     SidebarView sidebar_;
```

In `plugin/src/AsmaProcessor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.cpp b/plugin/src/AsmaProcessor.cpp
index f375d6a..f1dd968 100644
--- a/plugin/src/AsmaProcessor.cpp
+++ b/plugin/src/AsmaProcessor.cpp
@@ -23,14 +23,9 @@ AsmaProcessor::AsmaProcessor(Mode mode)
     }
     // In a plugin, the plugin's own binary (where JUCE can tell), so the
     // helpers are found inside its bundle.
-    const auto binary = fromUtf8(
-        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString());
-    if (standalone_) {
-        scans_ = std::make_unique<ScanJob>(libraryPath_, ScanJob::workerNextTo(binary));
-        writer_ = std::make_unique<DirectWriter>(libraryPath_, LibraryWriter::cliNextTo(binary));
-    } else {
-        writer_ = std::make_unique<CliWriter>(libraryPath_, LibraryWriter::cliNextTo(binary));
-    }
+    binary_ = fromUtf8(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString());
+    if (standalone_) writer_ = std::make_unique<DirectWriter>(libraryPath_, LibraryWriter::cliNextTo(binary_));
+    else writer_ = std::make_unique<CliWriter>(libraryPath_, LibraryWriter::cliNextTo(binary_));
     engine_.loader().start();
 }

```

In `plugin/src/AsmaProcessor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.h b/plugin/src/AsmaProcessor.h
index acfdf62..6c549cf 100644
--- a/plugin/src/AsmaProcessor.h
+++ b/plugin/src/AsmaProcessor.h
@@ -3,7 +3,6 @@

 #include "LibraryWriter.h"
 #include "PluginState.h"
-#include "ScanJob.h"
 #include "asma/audio/AuditionEngine.h"

 #include <juce_audio_processors/juce_audio_processors.h>
@@ -92,9 +91,8 @@ public:
     void setLinkEnabled(bool on);
     // Proposes a tempo to the Link session (peers may change it again).
     void setLinkTempo(double bpm);
-    // Adding folders and scanning: the standalone only; null in a plugin,
-    // which never writes the library from inside the host.
-    ScanJob* scans() { return scans_.get(); }
+    // The app's or the plugin's own binary, where its helpers sit beside it.
+    const std::filesystem::path& binary() const { return binary_; }
     // What the user adds to the library goes through here: written directly
     // in the standalone, by the asma helper in a plugin.
     LibraryWriter& writer() { return *writer_; }
@@ -108,7 +106,7 @@ private:
     std::atomic<double> hostBpm_{0.0};
     const bool standalone_;
     std::unique_ptr<ableton::Link> link_; // standalone only
-    std::unique_ptr<ScanJob> scans_;      // standalone only
+    std::filesystem::path binary_;
     std::unique_ptr<LibraryWriter> writer_;
     static constexpr double kDefaultBpm = 120.0; // the standalone's tempo until set
     std::atomic<bool> linkOn_{false};
```

Create `plugin/src/LibraryKeeper.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryKeeper.h"

#include "asma/core/Fs.h"

namespace asma::app {

namespace {

constexpr int kTickMs = 500;

} // namespace

LibraryKeeper::LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner)
    : dbPath_(std::move(dbPath)), runner_(std::move(runner)), view_(dbPath_)
{
    startTimer(kTickMs);
}

LibraryKeeper::~LibraryKeeper()
{
    stopTimer();
    watcher_.reset(); // no more reports from its thread
}

std::shared_ptr<LibraryKeeper> LibraryKeeper::shared(const std::filesystem::path& dbPath,
                                                      const std::filesystem::path& worker)
{
    static std::map<std::filesystem::path, std::weak_ptr<LibraryKeeper>> keepers; // message thread only
    if (auto existing = keepers[dbPath].lock()) return existing;
    auto made = std::make_shared<LibraryKeeper>(dbPath, std::make_unique<ScanJob>(dbPath, worker));
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
    if (news.empty()) return;
    message_ = news;
    ++messages_;
}

void LibraryKeeper::tick(Clock::time_point now)
{
    if (!runner_->ready()) return;
    if (!watcher_) {
        watcher_ = std::make_unique<FolderWatcher>([this](std::int64_t id) { folderChanged(id); });
        watcher_->ignore(dbPath_.parent_path());
    }
    view_.refresh();
    if (view_.changed()) readFolders(now);
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

} // namespace asma::app
```

Create `plugin/src/LibraryKeeper.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"
#include "ScanJob.h"
#include "ScanSchedule.h"
#include "asma/core/FolderWatcher.h"

#include <juce_events/juce_events.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace asma::app {

// Keeps the library in step with its folders while any asma window is open:
// it watches every folder, scans each once when it first sees it, a folder
// the watcher reports, and every folder every 15 minutes, one scan at a time
// through asma-scan. One per process (the app's, or one for every asma window
// in a host), shared through shared(). Across processes the writer lock
// decides: a scan refused by it is skipped and the next change or poll
// catches up. With no asma-scan to run it does nothing. Message thread only,
// but for folderChanged().
class LibraryKeeper : private juce::Timer {
public:
    using Clock = ScanSchedule::Clock;

    LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner);
    ~LibraryKeeper() override;
    LibraryKeeper(const LibraryKeeper&) = delete;
    LibraryKeeper& operator=(const LibraryKeeper&) = delete;

    // The process's keeper for this library, made with a ScanJob running
    // `worker` if there is none yet; it lives while anyone holds it.
    static std::shared_ptr<LibraryKeeper> shared(const std::filesystem::path& dbPath,
                                                 const std::filesystem::path& worker);

    // The standalone's Add folder: adds it to the library and scans it next.
    bool addFolder(const std::filesystem::path& folder, std::string* error = nullptr);
    // What the footer says while a scan runs; empty otherwise.
    std::string progress() const { return runner_->progress(); }
    // The last scan's news ("Scan finished: 3 added", "Scan failed: ..."),
    // counted, so each window shows each once.
    std::uint64_t messageCount() const { return messages_; }
    const std::string& message() const { return message_; }
    // The watcher saw a change; any thread.
    void folderChanged(std::int64_t rootId);
    // One step: the timer gives the time, tests their own.
    void tick(Clock::time_point now);
    ScanRunner& runner() { return *runner_; }

private:
    void timerCallback() override { tick(Clock::now()); }
    void readFolders(Clock::time_point now);
    void finished(const ScanReport& report);

    const std::filesystem::path dbPath_;
    std::unique_ptr<ScanRunner> runner_;
    LibraryView view_;
    ScanSchedule schedule_;
    std::vector<FolderWatcher::Folder> folders_;
    std::map<std::int64_t, std::string> names_; // what progress calls each folder
    std::optional<std::int64_t> scanning_;
    std::string message_;
    std::uint64_t messages_ = 0;
    std::mutex changedMutex_; // guards changed_, filled by the watcher's thread
    std::vector<std::int64_t> changed_;
    std::unique_ptr<FolderWatcher> watcher_; // last: its thread calls folderChanged
};

} // namespace asma::app
```

Replace the whole of `plugin/src/ScanJob.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ScanJob.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

namespace asma::app {

ScanJob::ScanJob(std::filesystem::path dbPath, std::filesystem::path worker)
    : dbPath_(std::move(dbPath)), worker_(std::move(worker))
{
}

ScanJob::~ScanJob()
{
    supervisor_.cancel();
    if (thread_.joinable()) thread_.join();
}

bool ScanJob::addAndScan(const std::filesystem::path& folder, std::string* error)
{
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (busy_.load()) return fail("a scan is already running");
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return fail("not a folder");
    std::int64_t rootId = 0;
    try {
        Db db = Db::open(dbPath_);
        rootId = Library(db).addRoot(folder);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    return start(rootId, toUtf8(folder.filename())) || fail("a scan is already running");
}

bool ScanJob::start(std::int64_t rootId, const std::string& label)
{
    if (busy_.load()) return false;
    if (thread_.joinable()) thread_.join(); // the previous scan, already finished
    busy_.store(true);
    std::filesystem::path worker;
    {
        const std::lock_guard lock(mutex_);
        progress_ = "Scanning " + label;
        report_.reset();
        worker = worker_;
    }
    thread_ = std::thread([this, rootId, worker, label] {
        ScanRequest request;
        request.worker = worker;
        request.db = dbPath_;
        request.rootId = rootId;
        const ScanReport report = supervisor_.run(request, [this, label](const ScanEvent& e) {
            const char* what = e.kind == ScanEvent::Kind::Progress          ? "Scanning"
                             : e.kind == ScanEvent::Kind::AnalyseProgress ? "Analysing"
                                                                            : nullptr;
            if (!what) return;
            const std::lock_guard lock(mutex_);
            progress_ = std::string(what) + " " + label + ": " + groupDigits(static_cast<std::int64_t>(e.done)) + " of "
                      + groupDigits(static_cast<std::int64_t>(e.total));
        });
        const std::lock_guard lock(mutex_);
        report_ = report;
        progress_.clear();
        busy_.store(false);
    });
    return true;
}

bool ScanJob::ready() const
{
    std::filesystem::path worker;
    {
        const std::lock_guard lock(mutex_);
        worker = worker_;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(worker, ec);
}

std::string groupDigits(std::int64_t n)
{
    std::string digits = std::to_string(n < 0 ? -n : n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<std::size_t>(i), ",");
    return (n < 0 ? "-" : "") + digits;
}

std::string ScanJob::progress() const
{
    const std::lock_guard lock(mutex_);
    return progress_;
}

std::optional<ScanReport> ScanJob::takeReport()
{
    const std::lock_guard lock(mutex_);
    std::optional<ScanReport> report;
    report.swap(report_);
    return report;
}

std::filesystem::path ScanJob::workerNextTo(const std::filesystem::path& executable)
{
#ifdef _WIN32
    return executable.parent_path() / "asma-scan.exe";
#else
    return executable.parent_path() / "asma-scan";
#endif
}

} // namespace asma::app
```

Replace the whole of `plugin/src/ScanJob.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/ScanSupervisor.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace asma::app {

// What runs a folder's scan, one at a time; the keeper drives it, and tests
// give the keeper a fake one. Control calls from the message thread.
class ScanRunner {
public:
    virtual ~ScanRunner() = default;
    // Starts scanning a folder of the library; `label` names it in progress
    // lines. False while a scan runs.
    virtual bool start(std::int64_t rootId, const std::string& label) = 0;
    virtual bool busy() const = 0;
    // "Scanning Samples: 1,200 of 8,000" while a scan runs; empty otherwise.
    virtual std::string progress() const = 0;
    // The finished scan's report, once.
    virtual std::optional<ScanReport> takeReport() = 0;
    // Whether it can scan at all (asma-scan is there).
    virtual bool ready() const = 0;
};

// Scans with asma-scan, which runs outside this process, on a background
// thread: the app's and the plugins' scans, so a plugin never writes the
// library inside its host.
class ScanJob final : public ScanRunner {
public:
    ScanJob(std::filesystem::path dbPath, std::filesystem::path worker);
    ~ScanJob() override; // cancels a running scan and waits for it
    ScanJob(const ScanJob&) = delete;
    ScanJob& operator=(const ScanJob&) = delete;

    bool start(std::int64_t rootId, const std::string& label) override;
    bool busy() const override { return busy_.load(); }
    std::string progress() const override;
    std::optional<ScanReport> takeReport() override;
    bool ready() const override;

    // Adds the folder to the library (creating the library on first use) and
    // starts scanning it: the standalone only. False, with the reason in
    // `error`, while another scan runs or when the folder cannot be added.
    bool addAndScan(const std::filesystem::path& folder, std::string* error = nullptr);
    void cancel() { supervisor_.cancel(); }
    // Where asma-scan is; takes effect from the next scan.
    void setWorker(std::filesystem::path worker)
    {
        const std::lock_guard lock(mutex_);
        worker_ = std::move(worker);
    }

    // Where the app and the plugins ship asma-scan: beside their own binary.
    static std::filesystem::path workerNextTo(const std::filesystem::path& executable);

private:
    std::filesystem::path dbPath_;
    std::filesystem::path worker_;
    ScanSupervisor supervisor_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    mutable std::mutex mutex_; // guards worker_, progress_ and report_
    std::string progress_;
    std::optional<ScanReport> report_;
};

// 8000 as "8,000": integer arithmetic, never the locale.
std::string groupDigits(std::int64_t n);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[keeper],[scanjob],[editor]"`

Expected: `All tests passed (216 assertions in 45 test cases)`

- [ ] **Step 5: Commit**

```sh
git add ci/validate-plugins.sh plugin/CMakeLists.txt plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/AsmaProcessor.cpp plugin/src/AsmaProcessor.h plugin/src/LibraryKeeper.cpp plugin/src/LibraryKeeper.h plugin/src/ScanJob.cpp plugin/src/ScanJob.h tests/plugin/test_editor.cpp tests/plugin/test_library_keeper.cpp tests/plugin/test_scan_job.cpp
git commit -m "app: one keeper per process watches the folders and scans them, in plugins too"
```

---

### Task 8: The keeper checks the library, has a damaged one rebuilt, and backs it up daily

Spec section 10. Through the helper on a `CliLane`, so a plugin never writes
inside its host: `asma check` when the keeper starts, `asma repair` at once for
a damaged library (or one the view finds damaged later; retried after 30 s when
another asma has it in hand), and `asma backup` when `backup.json` is a day old
or missing. No scans while checking or rebuilding; after a rebuild every folder
is scanned afresh. The footer says "The library was damaged and has been
rebuilt; your ratings and collections were restored from 7 October." (or that
there was no backup), and how many organised samples were not found.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/LibraryKeeper.cpp`
- Modify: `plugin/src/LibraryKeeper.h`
- Modify: `tests/plugin/test_library_keeper.cpp` (test)

**Interfaces:**

- Consumes: tasks 3, 4, 5 and 7; `CliLane`.
- Produces: `LibraryKeeper(path dbPath, std::unique_ptr<ScanRunner>, path cli)`,
  `shared(dbPath, worker, cli)`, `setCli`, `settled()`;
  `std::string backupDay(const std::string&)` ("7 October").

- [ ] **Step 1: Write the failing test**

Replace the whole of `tests/plugin/test_library_keeper.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "LibraryKeeper.h"
#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/UserData.h"

#include <fstream>
#include <sqlite3.h>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace std::chrono_literals;
using app::LibraryKeeper;
namespace fs = std::filesystem;

namespace {

// Records what it is asked to scan and finishes when told.
struct FakeRunner final : app::ScanRunner {
    std::vector<std::int64_t> started;
    bool running = false;
    std::optional<ScanReport> report;
    bool start(std::int64_t rootId, const std::string&) override
    {
        if (running) return false;
        started.push_back(rootId);
        running = true;
        return true;
    }
    bool busy() const override { return running; }
    std::string progress() const override { return running ? "Scanning" : ""; }
    std::optional<ScanReport> takeReport() override
    {
        auto r = report;
        report.reset();
        return r;
    }
    bool ready() const override { return true; }
    void finish(ScanReport::Result result = ScanReport::Result::Finished, std::size_t added = 0)
    {
        ScanReport r;
        r.result = result;
        r.index.added = added;
        report = r;
        running = false;
    }
};

struct KeeperRig {
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    FakeRunner* runner = nullptr;
    std::unique_ptr<LibraryKeeper> keeper;
    std::int64_t samples = 0, other = 0;
    const LibraryKeeper::Clock::time_point t0 = LibraryKeeper::Clock::now();
    KeeperRig()
    {
        f.scan();
        fs::create_directories(f.dir.path() / "Other");
        {
            Db db = Db::open(f.dbPath);
            Library lib(db);
            samples = lib.roots().front().id;
            other = lib.addRoot(f.dir.path() / "Other");
        }
        auto fake = std::make_unique<FakeRunner>();
        runner = fake.get();
        keeper = std::make_unique<LibraryKeeper>(f.dbPath, std::move(fake), ASMA_CLI_PATH);
    }
    // Ticks at t0 until the startup check (through the helper) is done; the
    // tick after it starts the first scan.
    void start()
    {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (std::chrono::steady_clock::now() < deadline) {
            keeper->tick(t0);
            if (!runner->started.empty()) return;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
    }
};

} // namespace

TEST_CASE("the keeper scans every folder once at startup, one at a time", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    CHECK(rig.runner->started == std::vector<std::int64_t>{rig.samples});
    rig.keeper->tick(rig.t0 + 1s); // still running: nothing more
    CHECK(rig.runner->started.size() == 1);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    CHECK(rig.runner->started == std::vector<std::int64_t>{rig.samples, rig.other});
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.size() == 2);
    rig.keeper->tick(rig.t0 + 16min); // the poll
    CHECK(rig.runner->started.size() == 3);
}

TEST_CASE("the keeper scans a folder the watcher reports", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 1s);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    rig.keeper->folderChanged(rig.other);
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.back() == rig.other);
    CHECK(rig.runner->started.size() == 3);
}

TEST_CASE("the keeper reports news, not quiet scans or a lock refused", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    rig.runner->finish(ScanReport::Result::Finished, 0);
    rig.keeper->tick(rig.t0 + 1s);
    CHECK(rig.keeper->messageCount() == 0);
    rig.runner->finish(ScanReport::Result::Locked);
    rig.keeper->tick(rig.t0 + 2s);
    CHECK(rig.keeper->messageCount() == 0);
    rig.keeper->folderChanged(rig.samples);
    rig.keeper->tick(rig.t0 + 3s);
    rig.runner->finish(ScanReport::Result::Finished, 3);
    rig.keeper->tick(rig.t0 + 4s);
    CHECK(rig.keeper->messageCount() == 1);
    CHECK(rig.keeper->message() == "Scan finished: 3 added");
}

TEST_CASE("a folder added elsewhere is scanned at the next tick", "[keeper]")
{
    KeeperRig rig;
    rig.start();
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 1s);
    rig.runner->finish();
    rig.keeper->tick(rig.t0 + 2s);
    fs::create_directories(rig.f.dir.path() / "Third");
    std::int64_t third = 0;
    {
        Db db = Db::open(rig.f.dbPath); // the app, or the CLI
        third = Library(db).addRoot(rig.f.dir.path() / "Third");
    }
    rig.keeper->tick(rig.t0 + 3s);
    CHECK(rig.runner->started.back() == third);
}

TEST_CASE("every window in a process shares one keeper", "[keeper]")
{
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    auto a = LibraryKeeper::shared(f.dbPath, "asma-scan", "asma-cli");
    auto b = LibraryKeeper::shared(f.dbPath, "asma-scan", "asma-cli");
    CHECK(a == b);
    a.reset();
    b.reset();
    CHECK(LibraryKeeper::shared(f.dbPath, "asma-scan", "asma-cli") != nullptr); // made again once none held it
}

TEST_CASE("a sample dropped into a watched folder appears in the table", "[keeper][watch]")
{
    test::EditorRig rig; // a plugin: it scans too, through asma-scan
    REQUIRE(rig.editor->table().getNumRows() == 3);
    dynamic_cast<app::ScanJob&>(rig.editor->keeper().runner()).setWorker(ASMA_SCAN_PATH);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(2000); // watching, and the startup scan under way
    test::writeWavFloat(rig.f.lib / "Drums" / "Hat_03.wav", 48000, {test::hatHit(48000, 5)});
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < deadline && rig.editor->table().getNumRows() < 4) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the keeper's timer
        rig.editor->poll();
    }
    CHECK(rig.editor->table().getNumRows() == 4);
}

namespace {

// Overwrites a closed library's pages after the first, as a disk fault would.
void damage(const fs::path& db)
{
    std::fstream f(db, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(4096);
    const std::string junk(4096 * 3, '\xA5');
    f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
}

// Ticks until the keeper has nothing in hand with the helper.
void settle(LibraryKeeper& keeper, LibraryKeeper::Clock::time_point now)
{
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    do {
        keeper.tick(now);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    } while (!keeper.settled() && std::chrono::steady_clock::now() < deadline);
}

// A backup of the library as it is, said to be written on 7 October.
void backupOn7October(const test::LibraryFixture& f)
{
    {
        Db db = Db::open(f.dbPath);
        writeBackup(db, f.dbPath.parent_path() / "backup.json", f.dbPath.parent_path() / "backup-previous.json");
    }
    std::ifstream in(f.dbPath.parent_path() / "backup.json");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    const auto at = text.find("\"written\":\"") + 11;
    text.replace(at, 20, "2026-10-07T09:00:00Z");
    std::ofstream(f.dbPath.parent_path() / "backup.json", std::ios::trunc) << text;
}

} // namespace

TEST_CASE("a damaged library at startup is rebuilt, and the footer says from when", "[keeper][safety]")
{
    KeeperRig rig;
    {
        Db db = Db::open(rig.f.dbPath);
        UserData(db).setRating(Library(db).fileByAbsolutePath(rig.f.kick)->id, 5);
    }
    backupOn7October(rig.f);
    damage(rig.f.dbPath);
    settle(*rig.keeper, rig.t0);
    CHECK(rig.keeper->messageCount() == 1);
    CHECK(rig.keeper->message() ==
          "The library was damaged and has been rebuilt; your ratings and collections were restored from 7 October.");
    CHECK(fs::exists(rig.f.dbPath.parent_path() / "library.db.corrupt"));
    Db db = Db::open(rig.f.dbPath);
    CHECK(UserData(db).rating(Library(db).fileByAbsolutePath(rig.f.kick)->id) == 5);
}

TEST_CASE("a sound library is checked and left alone", "[keeper][safety]")
{
    KeeperRig rig;
    settle(*rig.keeper, rig.t0);
    CHECK(rig.keeper->messageCount() == 0);
    CHECK_FALSE(fs::exists(rig.f.dbPath.parent_path() / "library.db.corrupt"));
}

TEST_CASE("damage met while reading is rebuilt too", "[keeper][safety]")
{
    KeeperRig rig;
    settle(*rig.keeper, rig.t0); // checked: sound
    {
        // Garbage in the schema, through another connection (Windows-safe).
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(toUtf8(rig.f.dbPath).c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db, "PRAGMA writable_schema = ON; UPDATE sqlite_master SET sql = 'garbage' "
                                 "WHERE name = 'files'; PRAGMA schema_version = 999;",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    settle(*rig.keeper, rig.t0 + 1s);
    settle(*rig.keeper, rig.t0 + 2s);
    CHECK(rig.keeper->message().rfind("The library was damaged and has been rebuilt", 0) == 0);
}

TEST_CASE("the user's data is backed up once a day", "[keeper][safety]")
{
    KeeperRig rig;
    const auto backup = rig.f.dbPath.parent_path() / "backup.json";
    settle(*rig.keeper, rig.t0);
    settle(*rig.keeper, rig.t0 + 1s);
    REQUIRE(fs::exists(backup)); // none yet: made now
    const auto written = fs::last_write_time(backup);
    settle(*rig.keeper, rig.t0 + 2min);
    const bool untouched = fs::last_write_time(backup) == written;
    CHECK(untouched); // a day has not passed
    fs::last_write_time(backup, written - std::chrono::hours(25));
    settle(*rig.keeper, rig.t0 + 4min);
    settle(*rig.keeper, rig.t0 + 4min + 1s);
    const bool rewritten = fs::last_write_time(backup) > written - std::chrono::hours(1);
    CHECK(rewritten);
    CHECK(fs::exists(rig.f.dbPath.parent_path() / "backup-previous.json"));
}

TEST_CASE("the backup's day reads as a person would say it", "[keeper]")
{
    CHECK(app::backupDay("2026-10-07T09:00:00Z") == "7 October");
    CHECK(app::backupDay("2027-01-31T23:59:59Z") == "31 January");
    CHECK(app::backupDay("").empty());
    CHECK(app::backupDay("yesterday").empty());
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[keeper]"`

Expected: the build stops:

```
tests/plugin/test_library_keeper.cpp:160:59: error: too many arguments to function call, expected 2, have 3
tests/plugin/test_library_keeper.cpp:201:22: error: no member named 'settled' in 'asma::app::LibraryKeeper'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 7722bac..262c185 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -44,7 +44,8 @@ juce::Font starFont() { return theme::font(theme::Face::Text, 11.0f).withExtraKe
 AsmaEditor::AsmaEditor(AsmaProcessor& owner)
     : juce::AudioProcessorEditor(owner), processor_(owner),
       library_(owner.libraryPath(), owner.isStandalone() ? LibraryView::Access::MayMigrate : LibraryView::Access::ReadOnly),
-      keeper_(LibraryKeeper::shared(owner.libraryPath(), ScanJob::workerNextTo(owner.binary()))),
+      keeper_(LibraryKeeper::shared(owner.libraryPath(), ScanJob::workerNextTo(owner.binary()),
+                                    LibraryWriter::cliNextTo(owner.binary()))),
       top_(owner.isStandalone())
 {
     setLookAndFeel(&lookAndFeel_);
```

Replace the whole of `plugin/src/LibraryKeeper.cpp` with:

```cpp
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
```

Replace the whole of `plugin/src/LibraryKeeper.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"
#include "LibraryWriter.h"
#include "ScanJob.h"
#include "ScanSchedule.h"
#include "asma/core/FolderWatcher.h"

#include <juce_events/juce_events.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace asma::app {

// Keeps the library in step with its folders while any asma window is open:
// it watches every folder, scans each once when it first sees it, a folder
// the watcher reports, and every folder every 15 minutes, one scan at a time
// through asma-scan. One per process (the app's, or one for every asma window
// in a host), shared through shared(). Across processes the writer lock
// decides: a scan refused by it is skipped and the next change or poll
// catches up. With no asma-scan to run it does not scan.
// It also keeps the library safe, through the asma helper so a plugin never
// writes inside its host: it checks the library when it starts, has a damaged
// one rebuilt (asma repair) at once, and has the user's data backed up once a
// day (asma backup). Message thread only, but for folderChanged().
class LibraryKeeper : private juce::Timer {
public:
    using Clock = ScanSchedule::Clock;

    LibraryKeeper(std::filesystem::path dbPath, std::unique_ptr<ScanRunner> runner, std::filesystem::path cli);
    ~LibraryKeeper() override;
    LibraryKeeper(const LibraryKeeper&) = delete;
    LibraryKeeper& operator=(const LibraryKeeper&) = delete;

    // The process's keeper for this library, made with a ScanJob running
    // `worker` if there is none yet; it lives while anyone holds it.
    static std::shared_ptr<LibraryKeeper> shared(const std::filesystem::path& dbPath, const std::filesystem::path& worker,
                                                 const std::filesystem::path& cli);
    // Where the asma helper is; takes effect from the next command.
    void setCli(std::filesystem::path cli) { helper_.setCli(std::move(cli)); }
    // Nothing being checked, rebuilt or backed up.
    bool settled() const { return safety_ == Safety::Idle && helper_.idle(); }

    // The standalone's Add folder: adds it to the library and scans it next.
    bool addFolder(const std::filesystem::path& folder, std::string* error = nullptr);
    // What the footer says while a scan runs; empty otherwise.
    std::string progress() const { return runner_->progress(); }
    // The last scan's news ("Scan finished: 3 added", "Scan failed: ..."),
    // counted, so each window shows each once.
    std::uint64_t messageCount() const { return messages_; }
    const std::string& message() const { return message_; }
    // The watcher saw a change; any thread.
    void folderChanged(std::int64_t rootId);
    // One step: the timer gives the time, tests their own.
    void tick(Clock::time_point now);
    ScanRunner& runner() { return *runner_; }

private:
    void timerCallback() override { tick(Clock::now()); }
    void readFolders(Clock::time_point now);
    void finished(const ScanReport& report);
    void keepSafe(Clock::time_point now);
    void repair();
    void tell(std::string news);

    const std::filesystem::path dbPath_;
    std::unique_ptr<ScanRunner> runner_;
    LibraryView view_;
    ScanSchedule schedule_;
    std::vector<FolderWatcher::Folder> folders_;
    std::map<std::int64_t, std::string> names_; // what progress calls each folder
    std::optional<std::int64_t> scanning_;
    std::string message_;
    std::uint64_t messages_ = 0;
    enum class Safety { Unchecked, Checking, Repairing, Idle };
    Safety safety_ = Safety::Unchecked;
    Clock::time_point retryAt_{};        // a repair or backup that could not run is tried again then
    Clock::time_point backupCheckAt_{};  // when to look at the backup's age again
    std::filesystem::path backupPath_;
    CliLane helper_;
    std::mutex changedMutex_; // guards changed_, filled by the watcher's thread
    std::vector<std::int64_t> changed_;
    std::unique_ptr<FolderWatcher> watcher_; // last: its thread calls folderChanged
};

// "7 October" for "2026-10-07T09:00:00Z"; empty when it is not a date.
std::string backupDay(const std::string& writtenAt);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[keeper]"`

Expected: `All tests passed (34 assertions in 11 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/LibraryKeeper.cpp plugin/src/LibraryKeeper.h tests/plugin/test_library_keeper.cpp
git commit -m "app: the keeper checks the library, has a damaged one rebuilt, and backs it up daily"
```

---

### Task 9: Docs

The README gains watching, the safety net and the four commands. The spec gains
that a file that is not a database counts as damaged, and that on Windows the
rebuild waits for a file another asma holds open.

**Files:**

- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`

**Interfaces:**

- Consumes: tasks 1 to 8.
- Produces: nothing code depends on.

- [ ] **Step 1: Write the docs**

In `README.md`, apply (`git apply` takes it as is):

```diff
diff --git a/README.md b/README.md
index c966fcb..d255ec6 100644
--- a/README.md
+++ b/README.md
@@ -36,6 +36,13 @@ Ratings, favourites, tags, collections and saved searches:
     asma query --saved "Fast loops" --sort rating --desc
     asma retry ~/Samples/Drums/broken.wav       # read a failed file again

+Keeping the library safe:
+
+    asma check                   # is the library sound?
+    asma backup                  # the user data, beside the library
+    asma restore backup.json     # give a library a backup's user data
+    asma repair                  # rebuild a damaged library from its backup
+
 The library lives in the platform data directory (on macOS
 `~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
 `ASMA_DATA_DIR` environment variable override it.
@@ -103,6 +110,15 @@ appears in the sidebar: its panel lists each with the reason, and Retry reads it
 again. Changes show at once; in a plugin, which never writes the library inside
 the host, the `asma-cli` helper shipped beside it makes them.

+While any asma window is open, the app's or a plugin's, the library follows its
+folders: a sample dropped into one appears within seconds, and one deleted or
+renamed outside asma goes or follows. Each folder is also rescanned at startup
+and every 15 minutes, for what the operating system's change notices miss. The
+library is checked when asma starts; a damaged one is moved aside as
+`library.db.corrupt` and rebuilt, and your ratings, favourites, tags,
+collections and saved searches come back from `backup.json`, which asma writes
+beside the library once a day.
+
 The look is checked against the approved design by `[fidelity]` in
 `asma_plugin_tests`, on macOS only; `tests/ui/reference/README.md` says how the
 reference picture is made.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, apply (`git apply` takes
it as is):

```diff
diff --git a/docs/superpowers/specs/2026-09-25-asma-design.md b/docs/superpowers/specs/2026-09-25-asma-design.md
index 020075e..bbbb414 100644
--- a/docs/superpowers/specs/2026-09-25-asma-design.md
+++ b/docs/superpowers/specs/2026-09-25-asma-design.md
@@ -543,7 +543,10 @@ without playing, with its tempo and key from the library.
   creates a new library with the backup's folders (or, with no backup, those the
   damaged file still yields), scans them and restores the backup. The footer
   says so: "The library was damaged and has been rebuilt; your ratings and
-  collections were restored from 7 October."
+  collections were restored from 7 October." A file at the library's path that
+  is not a database at all counts as damaged. On Windows a file another asma
+  process holds open cannot be moved: the rebuild waits and is tried again until
+  it is free.
 - **Daily backup:** once a day while asma runs, `asma backup` writes
   `backup.json` beside the library (written aside, then renamed into place),
   keeping the day before's as `backup-previous.json`. It holds the folders; each
```

- [ ] **Step 2: Check them**

Run:
`prettier --check README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: both files pass; no em-dashes.

- [ ] **Step 3: Commit**

```sh
git add README.md docs/superpowers/specs/2026-09-25-asma-design.md
git commit -m "docs: watching and the safety net in the README; the spec on non-databases and Windows"
```

---

### Task 10: Verify and merge

- [ ] **Step 1: Everything, locally**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests
TOOLS=build/validators ci/validate-plugins.sh build
ls build/plugin/asma_plugin_artefacts/Release/VST3/asma.vst3/Contents/MacOS/
```

Expected: no warnings from asma's code; `100% tests passed out of 564`; the
plugin tests also pass in one process (`All tests passed`, 206 test cases);
pluginval `SUCCESS` and clap-validator `0 failed`; the bundle holds `asma`,
`asma-cli` and `asma-scan`.

- [ ] **Step 2: Try the app by hand (macOS)**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
build/plugin/asma_plugin_artefacts/Release/Standalone/asma.app/Contents/MacOS/asma
```

Add a folder of samples. Expected: copy a WAV into it in Finder and it appears
in the table within a few seconds, the footer saying "Scan finished: 1 added";
rename it and it follows; delete it and it goes. Quit, rate a sample with
`asma rate`, then damage the library
(`dd if=/dev/urandom of="$ASMA_DATA_DIR/library.db" bs=4096 seek=1 count=3 conv=notrunc`
after `asma backup`), start the app again: the footer says the library was
damaged and rebuilt, the rating is back, and `library.db.corrupt` is beside the
new library.

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-watch
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green, including "Validate plugins". Watch
the `[watcher]` tests on Windows and Linux (efsw's ReadDirectoryChangesW and
inotify backends run there for the first time) and the `[repair]` tests on
Windows (renaming a file another connection holds fails there).

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-watch
git -C product/asma worktree remove .worktrees/plan-watch
git -C product/asma branch -d plan-watch
```

Push `main` once the user agrees, and delete the remote branch.
