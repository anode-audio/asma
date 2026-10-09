# asma Plan 4a: File Operations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** the standalone renames, moves and trashes samples and takes folders
out of the library, each journaled and undone as one with Cmd/Ctrl+Z, rolled
back after a crash, never replacing or deleting a file; samples keep their
ratings, tags and collections throughout; overlapping library folders are
refused or merged.

**Architecture:** The core gains `Trash` (the system's trash on each platform,
reporting where a file went so it can come back), `FileOps` (plan, preflight,
journal, execute, undo, recovery, pruning, over two new tables) and `Folders`
(overlap checks and merging); the CLI gains `rename`, `move`, `trash`,
`remove-folder`, `undo`, `history` and `root add --merge`. In the app,
`FileOpsJob`, owned by the standalone's processor, runs the operations one at a
time off the message thread under the writer lock, starting with recovery and
the merge of nested folders; the editor adds the row menu items, the keys, the
rename popover, Remove from Library, the merge question and, on macOS, the Edit
menu. A plugin never moves files.

**Tech Stack:** C++20, JUCE 9.0.3, SQLite, Foundation (macOS trash), the
Windows shell (`IFileOperation`), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (section 6 "File
operations", section 5's `journal_groups`, `journal` and `trashed_by`, section
10's file operation failures, as amended in `fd7dd95`). Task 8 adds what the
prototype settled: renaming with F2, one sample at a time in the window, and
removed folders coming back after a rebuild.

**How this plan was checked:** every task was built on a branch from `main`
(`fd7dd95`), then replayed commit by commit on a macOS Release build: with only
the task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The code below is taken from those commits: new files
in full, changed files as diffs against the task before (or in full where most
of the file changed), so applying the tasks in order to `main` gives the
prototype file for file. On the prototype, ctest passed 613 of 613, the plugin
tests passed in one process (226 test cases), pluginval said `SUCCESS` and
clap-validator `0 failed`. The prototype did not run on CI: the Windows trash
(`Trash_win.cpp`) and the Linux trash in its home are first built and run in
Task 9.

## Where this sits

Plan 4 was split on 2026-10-09: **4a, this plan** (the engine and everyday
operations), 4b (batch rename by pattern, export a collection), 4c (convert,
find duplicates). Then the playlist follower and processing (spec section 15),
then plan 5 (packaging).

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only`.
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11), no
  floating-point `std::to_chars`/`std::from_chars` (macOS 12). The plugin build
  uses JUCE's warning set, which includes `-Wfloat-equal` and `-Wswitch-enum`.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE.
- File operations are standalone only; a plugin never moves, renames or trashes
  a file. File operations take the writer lock.
- No file operation can lose data: delete always means the system's trash, a
  rename or move never replaces a file, and nothing in asma deletes a sample.
- Nothing on the audio thread allocates, locks or blocks; nothing on the
  message thread waits for a file operation or a scan.
- Colours come from `theme`, never as hex in a component. UI text in the source
  is UTF-8.
- No em-dashes in code, comments, docs or commit messages; no attribution
  trailers; no mention of the tools used to write the code.

## Decisions made while prototyping

- **A trashed sample keeps its row, off its path.** Its status becomes
  `missing`, `trashed_by` names the journal step, and its relative path becomes
  `/trashed/<step>`, which no file can have: `files` is unique on folder and
  path, and a new file put where the old one was is a new sample. Trashed rows
  are never relinked to a copy of their content.
- **Each step remembers the folder its sample was in** (`journal.root_id`), so
  undo puts the row back there even when a removed folder nested in it also
  holds the path.
- **Renames never replace**, even a file made a moment before:
  `renamex_np(RENAME_EXCL)` on macOS, `renameat2(RENAME_NOREPLACE)` on Linux
  (falling back to link and unlink), `MoveFileExW` without
  `MOVEFILE_REPLACE_EXISTING` on Windows. Restoring from the trash uses it too.
- **The freedesktop trash trusts only folders it owns.** Background security
  reviews of the first commit found that a shared volume's `.Trash/$uid` or
  `.Trash-$uid` could be a link planted by another user, and that checking a
  folder then writing into it by name leaves a gap. Every trash folder is opened
  with `O_NOFOLLOW`, checked to be this user's, and written through its
  descriptor (`openat`, `renameat`).
- **The macOS trash is Objective-C++.** `trashItemAtURL:resultingItemURL:` is
  the only call that says where the file went; CMake needs `OBJCXX` enabled at
  the top level for one `.mm` in the core, which links Foundation and no JUCE.
- **The Windows trash refuses a permanent delete.** On a drive with no Recycle
  Bin the shell deletes for good; the progress sink's `PreDeleteItem` aborts any
  delete that would not recycle.
- **A gone row with nothing of the user's gives way.** Undoing a trash where a
  scan has since recorded another file, now gone too, at the old path deletes
  that row if it holds no rating, favourite, user tag or collection; otherwise
  the step is skipped as "its old place is taken".
- **Merging keeps one row per sample.** A library with nested folders (allowed
  before) has two rows for each sample inside both: the outer row stays and
  takes the inner row's rating, favourite, user tags and collections. Journal
  steps that pointed at the inner folder point at the outer one.
- **Re-adding a removed folder that now holds library folders merges them**:
  the prototype found it took the "already there" path and left them nested.
- **One sample at a time, F2 to rename.** The table selects one row, so the
  window's operations act on one sample; Enter keeps playing it, as before.
- **The Edit menu is macOS only**, and set only when there is a JUCE
  application to own the menu bar (the test runner has none).
- **The processor owns `FileOpsJob`**, made when the standalone's window first
  looks at the library; its first request is recovery and the merge. Adding a
  folder goes through it, so `LibraryKeeper::addFolder` goes.
- **The CLI does not wait for the lock**: a file command exits 3 at once, as
  `repair` does; the app waits up to 30 seconds.
- **A rebuild brings removed folders back**: the backup lists every folder. The
  spec says so; carrying the removed flag is left for later.

## Review Focus

The five inputs most likely to bite a person that the tasks' main tests do not
reach; each has its own test, in the task named:

- A link planted in a trash folder on a shared volume: never followed, the file
  stays where it was (Task 1).
- A new file put where a trashed one was, then a scan: a new sample, and the
  trashed one is never relinked to it (Tasks 2 and 3).
- A crash between two steps of a group: rolled back at the next start, by the
  app and by any CLI write (Tasks 3, 5 and 6).
- A rename that only changes letter case, on a disk that ignores case (Task 3).
- A library made before overlaps were refused, with a folder inside another:
  merged once, one row per sample, the data of both kept (Tasks 4 and 6).

## File Structure

```
CMakeLists.txt, core/CMakeLists.txt                       OBJCXX; Foundation, ole32, shell32
core/include/asma/core/Trash.h, core/src/Trash*.{h,cpp,mm} the system trash, and freedesktop's
core/include/asma/core/Fs.h, core/src/Fs.cpp               renameNoReplace, sameVolume
core/src/Schema.cpp                                        migration 4: journal, trashed_by
core/include/asma/core/Library.h, core/src/Library.cpp     setRootEnabled, rootOf, folderPath, removeFile
core/include/asma/core/FileOps.h, core/src/FileOps.cpp     the engine
core/include/asma/core/Folders.h, core/src/Folders.cpp     overlap checks and merging
apps/FileCommands.{h,cpp}, apps/asma_main.cpp              the CLI's file commands
plugin/src/FileOpsJob.{h,cpp}                              the standalone's runner
plugin/src/AsmaProcessor.{h,cpp}, AsmaEditor.{h,cpp}       the job; menus, keys, popover, dialog
plugin/src/ui/EditMenu.{h,cpp}, ui/SidebarView.{h,cpp}     Edit menu; Remove from Library
plugin/src/LibraryKeeper.{h,cpp}                           addFolder goes
tests/test_{trash,library_folders,file_ops,folder_overlap,cli_e2e,user_data}.cpp
tests/plugin/test_{file_ops_job,file_manager,organise}.cpp, tests/plugin/EditorRig.h
README.md, docs/superpowers/specs/2026-09-25-asma-design.md
```

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-4a -b plan-4a
```

All paths below are relative to `product/asma/.worktrees/plan-4a`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
`cmake --build build`.

---

### Task 1: The system trash, which says where each file went, and renames that never replace

Spec section 6 (Trash). One backend per platform, each reporting where the file went so undo can bring it back: `trashItemAtURL:resultingItemURL:` on macOS (Objective-C++, Foundation only, so `OBJCXX` is enabled at the top level), `IFileOperation` with a progress sink on Windows (its `PreDeleteItem` aborts any delete that would not recycle), and the freedesktop.org specification on Linux, written directly and built on every POSIX system so it is tested everywhere. The freedesktop trash opens each of its folders with `O_NOFOLLOW`, checks it is this user's and writes through its descriptor, so a link planted on a shared volume is never followed. Restoring is a rename that never replaces: `renameNoReplace` refuses a destination that exists, even one made a moment before. Nothing here deletes a file.

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `core/CMakeLists.txt`
- Modify: `core/include/asma/core/Fs.h`
- Create: `core/include/asma/core/Trash.h`
- Modify: `core/src/Fs.cpp`
- Create: `core/src/TrashCommon.h`
- Create: `core/src/TrashFreedesktop.cpp`
- Create: `core/src/Trash_mac.mm`
- Create: `core/src/Trash_win.cpp`
- Create: `tests/test_trash.cpp` (test)

**Interfaces:**

- Consumes: nothing new.
- Produces: `struct TrashResult { bool ok; std::filesystem::path where; std::string error; }`; `bool trashAvailable(const path&)`; `TrashResult moveToTrash(const path&)`; `std::string restoreFromTrash(const path& where, const path& to)` (empty on success, else "it is no longer in the Trash" or "its old place is taken"); `namespace freedesktop { path homeTrash(); bool available(const path&, const path& home); TrashResult moveToTrash(const path&, const path& home); std::string restore(const path&, const path&); }` (not on Windows); `std::error_code renameNoReplace(const path& from, const path& to)` (`std::errc::file_exists` when taken) in `Fs.h`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_trash.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Trash.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

TEST_CASE("The system trash takes a file and gives it back", "[trash]")
{
    TempDir dir;
#ifdef __linux__
    asma::test::ScopedEnv xdg("XDG_DATA_HOME", (dir.path() / "share").string().c_str());
#endif
    // A name no other run uses, since the trash outlives the test.
    const fs::path file = dir.path() / ("asma trash test " + dir.path().filename().string() + ".wav");
    test::writeBytes(file, "RIFF kick");
    REQUIRE(trashAvailable(file));

    const TrashResult trashed = moveToTrash(file);
    REQUIRE(trashed.ok);
    CHECK(trashed.error.empty());
    CHECK_FALSE(fs::exists(file));
    REQUIRE(fs::exists(trashed.where));
    CHECK(slurp(trashed.where) == "RIFF kick");

    CHECK(restoreFromTrash(trashed.where, file).empty());
    CHECK(fs::exists(file));
    CHECK_FALSE(fs::exists(trashed.where));
    CHECK(slurp(file) == "RIFF kick");
}

TEST_CASE("Restoring from the trash says why it cannot", "[trash]")
{
    TempDir dir;
    test::writeBytes(dir.path() / "taken.wav", "someone else");
    test::writeBytes(dir.path() / "trash" / "kick.wav", "kick");
    CHECK(restoreFromTrash(dir.path() / "trash" / "gone.wav", dir.path() / "gone.wav") == "it is no longer in the Trash");
    CHECK(restoreFromTrash(dir.path() / "trash" / "kick.wav", dir.path() / "taken.wav") == "its old place is taken");
    CHECK(slurp(dir.path() / "taken.wav") == "someone else");
}

TEST_CASE("A rename that would replace a file fails and leaves both", "[trash]")
{
    TempDir dir;
    test::writeBytes(dir.path() / "a.wav", "a");
    test::writeBytes(dir.path() / "b.wav", "b");
    CHECK(renameNoReplace(dir.path() / "a.wav", dir.path() / "b.wav") == std::errc::file_exists);
    CHECK(slurp(dir.path() / "a.wav") == "a");
    CHECK(slurp(dir.path() / "b.wav") == "b");
    CHECK_FALSE(renameNoReplace(dir.path() / "a.wav", dir.path() / "c.wav"));
    CHECK(slurp(dir.path() / "c.wav") == "a");
    CHECK_FALSE(fs::exists(dir.path() / "a.wav"));
}

#ifndef _WIN32
TEST_CASE("The freedesktop trash keeps where each file came from", "[trash]")
{
    TempDir dir;
    const fs::path home = dir.path() / "share" / "Trash";
    const fs::path a = dir.path() / "Drums" / "kick 100%.wav";
    const fs::path b = dir.path() / "Other" / "kick 100%.wav";
    test::writeBytes(a, "a");
    test::writeBytes(b, "b");
    REQUIRE(freedesktop::available(a, home));

    const TrashResult first = freedesktop::moveToTrash(a, home);
    REQUIRE(first.ok);
    CHECK(first.where == home / "files" / "kick 100%.wav");
    const std::string info = slurp(home / "info" / "kick 100%.wav.trashinfo");
    CHECK(info.rfind("[Trash Info]\nPath=", 0) == 0);
    CHECK(info.find("/Drums/kick%20100%25.wav\n") != std::string::npos);
    CHECK(info.find("\nDeletionDate=") != std::string::npos);

    // A second file of the same name gets a number, not the first one's place.
    const TrashResult second = freedesktop::moveToTrash(b, home);
    REQUIRE(second.ok);
    CHECK(second.where == home / "files" / "kick 100% 2.wav");
    CHECK(fs::exists(home / "info" / "kick 100% 2.wav.trashinfo"));
    CHECK(slurp(second.where) == "b");

    CHECK(freedesktop::restore(first.where, a).empty());
    CHECK(slurp(a) == "a");
    CHECK_FALSE(fs::exists(home / "info" / "kick 100%.wav.trashinfo"));
    CHECK(fs::exists(second.where));
}

TEST_CASE("The freedesktop trash never follows a link planted in it", "[trash]")
{
    TempDir dir;
    const fs::path home = dir.path() / "share" / "Trash";
    fs::create_directories(home / "info");
    fs::create_directories(dir.path() / "elsewhere");
    fs::create_directory_symlink(dir.path() / "elsewhere", home / "files");
    const fs::path kick = dir.path() / "kick.wav";
    test::writeBytes(kick, "kick");

    const TrashResult result = freedesktop::moveToTrash(kick, home);
    CHECK_FALSE(result.ok);
    CHECK(fs::exists(kick));
    CHECK(fs::is_empty(dir.path() / "elsewhere"));
}

TEST_CASE("The freedesktop home trash is under XDG_DATA_HOME", "[trash]")
{
    asma::test::ScopedEnv xdg("XDG_DATA_HOME", "/somewhere/share");
    CHECK(freedesktop::homeTrash() == fs::path("/somewhere/share/Trash"));
}
#endif
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[trash]"`

Expected: the build stops:

```
tests/test_trash.cpp:4:10: fatal error: 'asma/core/Trash.h' file not found
```

- [ ] **Step 3: Implement**

In `CMakeLists.txt`, apply (`git apply` takes it as is):

````diff
diff --git a/CMakeLists.txt b/CMakeLists.txt
index a049902..f94aab7 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -4,6 +4,9 @@ cmake_minimum_required(VERSION 3.25)
 set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
 
 project(asma VERSION 0.1.0 LANGUAGES C CXX)
+if(APPLE)
+  enable_language(OBJCXX) # the core's trash speaks Foundation
+endif()
 
 set(CMAKE_CXX_STANDARD 20)
 set(CMAKE_CXX_STANDARD_REQUIRED ON)
````

Replace the whole of `core/CMakeLists.txt` with:

````cmake
# SPDX-License-Identifier: GPL-3.0-only
file(GLOB ASMA_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

set(ASMA_TOKENS_FILE ${PROJECT_SOURCE_DIR}/data/instrument_tokens.txt)
file(READ ${ASMA_TOKENS_FILE} ASMA_INSTRUMENT_TOKENS)
configure_file(src/InstrumentTokensData.cpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/InstrumentTokensData.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ASMA_TOKENS_FILE})
list(APPEND ASMA_CORE_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/InstrumentTokensData.cpp)

# The trash speaks each system's own API: Foundation on macOS, the shell on
# Windows (the freedesktop.org trash is portable C++).
if(APPLE)
  list(APPEND ASMA_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Trash_mac.mm)
endif()

add_library(asma_core STATIC ${ASMA_CORE_SOURCES})
add_library(asma::core ALIAS asma_core)
target_include_directories(asma_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_core
  PUBLIC asma_sqlite
  PRIVATE asma_xxhash asma_dr_libs asma_stb asma_ebur128 efsw-static Threads::Threads)
if(APPLE)
  target_link_libraries(asma_core PRIVATE "-framework Foundation")
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/Trash_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
if(WIN32)
  target_link_libraries(asma_core PRIVATE ole32 shell32)
  target_compile_definitions(asma_core PUBLIC
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
asma_set_warnings(asma_core)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor_dr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor_stb_vorbis.c
  PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
````

In `core/include/asma/core/Fs.h`, apply (`git apply` takes it as is):

````diff
diff --git a/core/include/asma/core/Fs.h b/core/include/asma/core/Fs.h
index d987803..857035f 100644
--- a/core/include/asma/core/Fs.h
+++ b/core/include/asma/core/Fs.h
@@ -7,6 +7,7 @@
 #include <memory>
 #include <string>
 #include <string_view>
+#include <system_error>
 
 namespace asma {
 
@@ -43,6 +44,10 @@ std::filesystem::path defaultDataDir();
 // meaningful for equality and ordering on the same machine.
 std::int64_t fileTimeToInt(std::filesystem::file_time_type time);
 
+// Renames a file or folder, never over another: a `to` that exists, even
+// one made a moment ago, fails with std::errc::file_exists. Same volume only.
+std::error_code renameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to);
+
 // Which file a path names now, as an opaque string: two calls give the same
 // string unless the file was replaced (renamed over, deleted and made again).
 // Empty when it cannot tell, and always on Windows, which never lets a file
````

Create `core/include/asma/core/Trash.h`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <string>

namespace asma {

// What moving a file to the trash did: where the file is now, or why it is
// still where it was.
struct TrashResult {
    bool ok = false;
    std::filesystem::path where; // the file inside the trash
    std::string error;
};

// The system's trash: the Trash on macOS, the Recycle Bin on Windows, the
// freedesktop.org trash on Linux. A file the trash cannot take stays where it
// is: nothing here ever deletes one.
//
// Whether the volume a file is on has a trash it can go to.
bool trashAvailable(const std::filesystem::path& file);
TrashResult moveToTrash(const std::filesystem::path& file);
// Moves a trashed file back to `to`. Empty on success, else why not ("it is no
// longer in the Trash", "its old place is taken").
std::string restoreFromTrash(const std::filesystem::path& where, const std::filesystem::path& to);

#ifndef _WIN32
// The freedesktop.org Trash specification, which Linux uses; built on every
// POSIX system so it is tested everywhere. A file on the same volume as
// `homeTrash` ($XDG_DATA_HOME/Trash) goes there; any other to its volume's
// $topdir/.Trash/$uid (when that is a real, sticky folder) or
// $topdir/.Trash-$uid. Each file gets an info/NAME.trashinfo saying where it
// came from and when.
namespace freedesktop {
std::filesystem::path homeTrash(); // $XDG_DATA_HOME/Trash, else ~/.local/share/Trash
bool available(const std::filesystem::path& file, const std::filesystem::path& homeTrash);
TrashResult moveToTrash(const std::filesystem::path& file, const std::filesystem::path& homeTrash);
std::string restore(const std::filesystem::path& where, const std::filesystem::path& to);
} // namespace freedesktop
#endif

} // namespace asma
````

In `core/src/Fs.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/core/src/Fs.cpp b/core/src/Fs.cpp
index 48fc63a..5dc0cb4 100644
--- a/core/src/Fs.cpp
+++ b/core/src/Fs.cpp
@@ -10,9 +10,17 @@
 #include <optional>
 
 #ifdef _WIN32
+#include <windows.h>
 #include <cwchar>
 #else
+#include <cerrno>
+#include <cstdio>
+#include <fcntl.h>
 #include <sys/types.h>
+#include <unistd.h>
+#endif
+#ifdef __linux__
+#include <sys/syscall.h>
 #endif
 
 namespace fs = std::filesystem;
@@ -102,4 +110,41 @@ std::string fileIdentity(const std::filesystem::path& path)
 #endif
 }
 
+std::error_code renameNoReplace(const fs::path& from, const fs::path& to)
+{
+#ifdef _WIN32
+    // Without MOVEFILE_REPLACE_EXISTING, Windows never replaces.
+    if (MoveFileExW(from.c_str(), to.c_str(), 0)) return {};
+    const DWORD error = GetLastError();
+    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return std::make_error_code(std::errc::file_exists);
+    return std::error_code(static_cast<int>(error), std::system_category());
+#else
+    int result = -1;
+#if defined(__APPLE__)
+    result = ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
+#elif defined(__linux__) && defined(SYS_renameat2)
+    result = static_cast<int>(::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1 /* RENAME_NOREPLACE */));
+    if (result != 0 && (errno == EINVAL || errno == ENOSYS)) {
+        // A file system without it: link then unlink never replaces either.
+        if (::link(from.c_str(), to.c_str()) == 0) {
+            ::unlink(from.c_str());
+            return {};
+        }
+        if (errno == EEXIST) return std::make_error_code(std::errc::file_exists);
+        // Nor links (FAT, some network shares): check, then rename.
+        struct stat st {};
+        if (::lstat(to.c_str(), &st) == 0) return std::make_error_code(std::errc::file_exists);
+        result = ::rename(from.c_str(), to.c_str());
+    }
+#else
+    struct stat st {};
+    if (::lstat(to.c_str(), &st) == 0) return std::make_error_code(std::errc::file_exists);
+    result = ::rename(from.c_str(), to.c_str());
+#endif
+    if (result == 0) return {};
+    if (errno == EEXIST) return std::make_error_code(std::errc::file_exists);
+    return std::error_code(errno, std::generic_category());
+#endif
+}
+
 } // namespace asma
````

Create `core/src/TrashCommon.h`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Fs.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace asma::detail {

// Puts a trashed file back by renaming it, never over another file. Empty on
// success, else why not.
inline std::string renameBack(const std::filesystem::path& where, const std::filesystem::path& to)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(where, ec))) return "it is no longer in the Trash";
    if (fs::exists(fs::symlink_status(to, ec))) return "its old place is taken";
    fs::create_directories(to.parent_path(), ec); // its folder may have gone since
    ec = renameNoReplace(where, to);
    if (ec == std::errc::file_exists) return "its old place is taken";
    return ec ? ec.message() : std::string();
}

} // namespace asma::detail
````

Create `core/src/TrashFreedesktop.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#ifndef _WIN32
#include "asma/core/Trash.h"

#include "TrashCommon.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <optional>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace asma::freedesktop {

namespace {

std::optional<dev_t> deviceOf(const fs::path& path)
{
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return std::nullopt;
    return st.st_dev;
}

// The nearest folder of `path` that exists: the home trash may not yet.
fs::path existingAncestor(fs::path path)
{
    std::error_code ec;
    while (!path.empty() && !fs::exists(path, ec)) {
        if (path == path.parent_path()) break;
        path = path.parent_path();
    }
    return path;
}

// The top of the volume a file is on: the last folder up from it on the same device.
fs::path topDir(const fs::path& file)
{
    const auto device = deviceOf(file);
    fs::path top = file.parent_path();
    while (top != top.parent_path()) {
        const auto up = deviceOf(top.parent_path());
        if (!up || !device || *up != *device) break;
        top = top.parent_path();
    }
    return top;
}

// An open folder, closed when it goes.
struct Dir {
    int fd = -1;
    Dir() = default;
    explicit Dir(int f) : fd(f) {}
    Dir(Dir&& o) noexcept : fd(std::exchange(o.fd, -1)) {}
    Dir& operator=(Dir&& o) noexcept
    {
        std::swap(fd, o.fd);
        return *this;
    }
    ~Dir()
    {
        if (fd >= 0) ::close(fd);
    }
    explicit operator bool() const { return fd >= 0; }
};

Dir openDir(int parent, const char* name)
{
    return Dir(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}

// A folder of ours inside `parent`, made when missing and `make`: a real
// folder (never a link someone planted on a shared volume) that this user
// owns. Everything after goes through the folder as opened, so it cannot be
// swapped for another between the check and the writing.
Dir ownDir(int parent, const char* name, bool make)
{
    if (make && ::mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return {};
    Dir dir = openDir(parent, name);
    struct stat st {};
    if (!dir || ::fstat(dir.fd, &st) != 0 || st.st_uid != ::getuid()) return {};
    return dir;
}

// A trash folder, opened, with its files/ and info/.
struct Trash {
    fs::path path;
    Dir files, info;
};

std::optional<Trash> opened(const fs::path& path, Dir trash, bool make)
{
    if (!trash) return std::nullopt;
    Trash t{path, ownDir(trash.fd, "files", make), ownDir(trash.fd, "info", make)};
    if (!t.files || !t.info) return std::nullopt;
    return t;
}

// The trash folder for a file, made if it has to be (`make`); nothing when it
// has none. Without `make` a trash that is not there yet but could be made
// counts: only its parent is checked.
std::optional<Trash> trashFor(const fs::path& file, const fs::path& home, bool make)
{
    const auto device = deviceOf(file);
    if (!device) return std::nullopt;
    if (deviceOf(existingAncestor(home)) == device) {
        std::error_code ec;
        if (make) fs::create_directories(home.parent_path(), ec); // $XDG_DATA_HOME may not be there yet
        const Dir parent = openDir(AT_FDCWD, home.parent_path().c_str());
        if (!make && !parent) return Trash{home, {}, {}};
        if (!parent) return std::nullopt;
        if (!make && ::faccessat(parent.fd, home.filename().c_str(), F_OK, AT_SYMLINK_NOFOLLOW) != 0)
            return Trash{home, {}, {}};
        Dir trash = ownDir(parent.fd, home.filename().c_str(), make);
        if (!make) return trash ? std::optional<Trash>(Trash{home, {}, {}}) : std::nullopt;
        return opened(home, std::move(trash), true);
    }

    const fs::path top = topDir(file);
    const Dir topFd = openDir(AT_FDCWD, top.c_str());
    if (!topFd) return std::nullopt;
    const std::string uid = std::to_string(::getuid());
    // A shared .Trash counts only as a real folder with the sticky bit.
    const Dir shared = openDir(topFd.fd, ".Trash");
    struct stat st {};
    if (shared && ::fstat(shared.fd, &st) == 0 && (st.st_mode & S_ISVTX)) {
        if (auto t = opened(top / ".Trash" / uid, ownDir(shared.fd, uid.c_str(), make), make)) return t;
        if (!make && ::faccessat(shared.fd, uid.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) != 0
            && ::faccessat(shared.fd, ".", W_OK, 0) == 0)
            return Trash{top / ".Trash" / uid, {}, {}};
    }
    const std::string own = ".Trash-" + uid;
    if (!make) {
        if (::faccessat(topFd.fd, own.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) == 0)
            return ownDir(topFd.fd, own.c_str(), false) ? std::optional<Trash>(Trash{top / own, {}, {}}) : std::nullopt;
        // Not made yet: say yes when it could be.
        return ::faccessat(topFd.fd, ".", W_OK, 0) == 0 ? std::optional<Trash>(Trash{top / own, {}, {}}) : std::nullopt;
    }
    return opened(top / own, ownDir(topFd.fd, own.c_str(), true), true);
}

std::string percentEncoded(const std::string& path)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : path) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'
                        || c == '_' || c == '.' || c == '~' || c == '/';
        if (plain) out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string now()
{
    const std::time_t t = std::time(nullptr);
    std::tm local {};
    ::localtime_r(&t, &local);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &local);
    return text;
}

// "kick.wav", then "kick 2.wav", "kick 3.wav"...
std::string numbered(const fs::path& name, int n)
{
    if (n == 1) return name.string();
    return name.stem().string() + " " + std::to_string(n) + name.extension().string();
}

} // namespace

fs::path homeTrash()
{
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return fs::path(xdg) / "Trash";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / ".local" / "share" / "Trash";
}

bool available(const fs::path& file, const fs::path& home) { return trashFor(file, home, false).has_value(); }

TrashResult moveToTrash(const fs::path& file, const fs::path& home)
{
    TrashResult result;
    std::error_code ec;
    const fs::path absolute = fs::absolute(file, ec);
    const auto trash = trashFor(absolute, home, true);
    if (!trash) {
        result.error = "there is no Trash on its disk";
        return result;
    }
    // The home trash records the full path; a volume's own, the path from its top.
    std::string original = absolute.string();
    if (trash->path != home) original = absolute.lexically_relative(topDir(absolute)).string();
    const std::string info = "[Trash Info]\nPath=" + percentEncoded(original) + "\nDeletionDate=" + now() + "\n";

    // Claim a name by making its info file first, as the specification asks:
    // O_EXCL makes two trashing processes pick different names.
    for (int n = 1; n < 10000; ++n) {
        const std::string name = numbered(absolute.filename(), n);
        const std::string infoName = name + ".trashinfo";
        if (::faccessat(trash->files.fd, name.c_str(), F_OK, AT_SYMLINK_NOFOLLOW) == 0) continue;
        const int fd = ::openat(trash->info.fd, infoName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            result.error = "cannot write to the Trash";
            return result;
        }
        const bool written = ::write(fd, info.data(), info.size()) == static_cast<ssize_t>(info.size());
        ::close(fd);
        const bool moved = written && ::renameat(AT_FDCWD, absolute.c_str(), trash->files.fd, name.c_str()) == 0;
        if (!moved) {
            ::unlinkat(trash->info.fd, infoName.c_str(), 0);
            result.error = written ? "cannot move it to the Trash" : "cannot write to the Trash";
            return result;
        }
        result.ok = true;
        result.where = trash->path / "files" / name;
        return result;
    }
    result.error = "the Trash has too many files of that name";
    return result;
}

std::string restore(const fs::path& where, const fs::path& to)
{
    const std::string error = detail::renameBack(where, to);
    if (error.empty()) {
        std::error_code ec;
        fs::remove(where.parent_path().parent_path() / "info" / (where.filename().string() + ".trashinfo"), ec);
    }
    return error;
}

} // namespace asma::freedesktop

#if !defined(__APPLE__)
namespace asma {

bool trashAvailable(const fs::path& file) { return freedesktop::available(file, freedesktop::homeTrash()); }
TrashResult moveToTrash(const fs::path& file) { return freedesktop::moveToTrash(file, freedesktop::homeTrash()); }
std::string restoreFromTrash(const fs::path& where, const fs::path& to) { return freedesktop::restore(where, to); }

} // namespace asma
#endif
#endif
````

Create `core/src/Trash_mac.mm`:

````text
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Trash.h"

#include "TrashCommon.h"

#import <Foundation/Foundation.h>

namespace fs = std::filesystem;

namespace asma {

namespace {

NSURL* urlOf(const fs::path& path)
{
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

} // namespace

bool trashAvailable(const fs::path& file)
{
    @autoreleasepool {
        NSError* error = nil;
        NSURL* trash = [[NSFileManager defaultManager] URLForDirectory:NSTrashDirectory
                                                             inDomain:NSUserDomainMask
                                                    appropriateForURL:urlOf(file)
                                                               create:NO
                                                                error:&error];
        return trash != nil;
    }
}

TrashResult moveToTrash(const fs::path& file)
{
    @autoreleasepool {
        TrashResult result;
        NSURL* where = nil;
        NSError* error = nil;
        if ([[NSFileManager defaultManager] trashItemAtURL:urlOf(file) resultingItemURL:&where error:&error] && where) {
            result.ok = true;
            result.where = fs::path([[where path] fileSystemRepresentation]);
        } else {
            result.error = error ? [[error localizedDescription] UTF8String] : "cannot move it to the Trash";
        }
        return result;
    }
}

std::string restoreFromTrash(const fs::path& where, const fs::path& to) { return detail::renameBack(where, to); }

} // namespace asma
````

Create `core/src/Trash_win.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#ifdef _WIN32
#include "asma/core/Trash.h"

#include "TrashCommon.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace fs = std::filesystem;

namespace asma {

namespace {

// COM for this thread, for as long as the object lives.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComScope()
    {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

template <class T> struct Ref {
    T* p = nullptr;
    ~Ref()
    {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

// Hears where the deleted item went, and stops a delete that would not go to
// the Recycle Bin: the shell deletes for good when a drive has none.
class Sink final : public IFileOperationProgressSink {
public:
    fs::path where;
    bool refused = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *out = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs_; } // lives on the stack

    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem*) override
    {
        if (flags & TSF_DELETE_RECYCLE_IF_POSSIBLE) return S_OK;
        refused = true;
        return E_ABORT;
    }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT result, IShellItem* created) override
    {
        if (SUCCEEDED(result) && created) {
            PWSTR path = nullptr;
            if (SUCCEEDED(created->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                where = fs::path(path);
                CoTaskMemFree(path);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

private:
    ULONG refs_ = 1;
};

std::wstring volumeOf(const fs::path& file)
{
    wchar_t volume[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(fs::absolute(file).c_str(), volume, MAX_PATH)) return {};
    return volume;
}

} // namespace

bool trashAvailable(const fs::path& file)
{
    const std::wstring volume = volumeOf(file);
    if (volume.empty()) return false;
    // Only fixed drives have a Recycle Bin by default; asking it answers for the rest.
    if (GetDriveTypeW(volume.c_str()) == DRIVE_FIXED) return true;
    SHQUERYRBINFO info{};
    info.cbSize = sizeof info;
    return SUCCEEDED(SHQueryRecycleBinW(volume.c_str(), &info));
}

TrashResult moveToTrash(const fs::path& file)
{
    TrashResult result;
    ComScope com;
    Ref<IFileOperation> op;
    Ref<IShellItem> item;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))
        || FAILED(SHCreateItemFromParsingName(fs::absolute(file).c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        result.error = "cannot reach the Recycle Bin";
        return result;
    }
    op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE
                          | FOFX_EARLYFAILURE);
    Sink sink;
    DWORD cookie = 0;
    op->Advise(&sink, &cookie);
    const HRESULT queued = op->DeleteItem(item.p, nullptr);
    const HRESULT done = SUCCEEDED(queued) ? op->PerformOperations() : queued;
    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    op->Unadvise(cookie);
    std::error_code ec;
    if (SUCCEEDED(done) && !aborted && !sink.where.empty() && !fs::exists(file, ec)) {
        result.ok = true;
        result.where = sink.where;
    } else {
        result.error = sink.refused ? "there is no Recycle Bin on its drive" : "cannot move it to the Recycle Bin";
    }
    return result;
}

std::string restoreFromTrash(const fs::path& where, const fs::path& to)
{
    const std::string error = detail::renameBack(where, to);
    if (error.empty()) {
        // The Recycle Bin keeps what it knows of $Rxxxx.ext in $Ixxxx.ext.
        const std::wstring name = where.filename().wstring();
        if (name.size() > 2 && name[0] == L'$' && name[1] == L'R') {
            std::error_code ec;
            fs::remove(where.parent_path() / (L"$I" + name.substr(2)), ec);
        }
    }
    return error;
}

} // namespace asma
#endif
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[trash]"`

Expected: `All tests passed (37 assertions in 6 test cases)`. On macOS one test round-trips a uniquely named file through your real Trash.

- [ ] **Step 5: Commit**

````sh
git add CMakeLists.txt core/CMakeLists.txt core/include/asma/core/Fs.h core/include/asma/core/Trash.h core/src/Fs.cpp core/src/TrashCommon.h core/src/TrashFreedesktop.cpp core/src/Trash_mac.mm core/src/Trash_win.cpp tests/test_trash.cpp
git commit -m "core: the system trash on macOS, Windows and Linux, with restore; renames that never replace"
````

---

### Task 2: A journal in the library; removed folders and trashed samples keep their data

Spec section 5. Migration 4 adds `journal_groups` and `journal` (each step with its sample, the folder it was in, where it was and where it goes) and `files.trashed_by`. A folder is removed by disabling it: every query already passes through enabled folders, so its samples vanish and keep their data, and adding the folder again enables it. `rootOf` finds the deepest folder holding a path. A trashed row (missing, marked, at a path no file can have) is never relinked to a copy of its content.

**Files:**

- Modify: `core/include/asma/core/Library.h`
- Modify: `core/src/Library.cpp`
- Modify: `core/src/Schema.cpp`
- Create: `tests/test_library_folders.cpp` (test)
- Modify: `tests/test_user_data.cpp` (test)

**Interfaces:**

- Consumes: nothing new.
- Produces: `currentSchemaVersion() == 4`; `Library::setRootEnabled(std::int64_t, bool)`; `Library::addRoot` enables an existing folder again; `std::optional<std::pair<Root, std::string>> Library::rootOf(const path&, bool enabledOnly = true)`; `relinkCandidates` skips trashed rows.

- [ ] **Step 1: Write the failing test**

Create `tests/test_library_folders.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

TEST_CASE("The library has a journal and marks trashed samples", "[folders]")
{
    Db db = Db::openInMemory();
    CHECK(currentSchemaVersion() == 4);
    db.exec("INSERT INTO journal_groups(label, at, state) VALUES ('Move kick.wav', '2026-10-09T10:00:00Z', 'running')");
    db.exec("INSERT INTO journal(group_id, op, file_id, src, dst, state, at) "
            "VALUES (1, 'move', 7, '/a/kick.wav', '/b/kick.wav', 'planned', '2026-10-09T10:00:00Z')");
    auto q = db.prepare("SELECT COUNT(*) FROM files WHERE trashed_by IS NULL");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
}

TEST_CASE("A removed folder's samples are hidden and keep their data until it is added again", "[folders]")
{
    TempDir dir;
    const fs::path samples = dir.path() / "Samples";
    test::WavSpec spec;
    test::writeWav(samples / "kick.wav", spec);
    spec.seed = 2;
    test::writeWav(samples / "snare.wav", spec);
    Db db = Db::open(dir.path() / "library.db");
    Library lib(db);
    const auto root = lib.addRoot(samples);
    scanRoot(db, root);
    const auto kick = lib.fileByPath(root, "kick.wav")->id;
    UserData user(db);
    user.setRating(kick, 4);
    const auto drums = user.createCollection("Drums");
    user.addToCollection(drums, kick);
    REQUIRE(countSearch(db, SearchModel{}) == 2);

    lib.setRootEnabled(root, false);
    CHECK_FALSE(lib.root(root)->enabled);
    CHECK(countSearch(db, SearchModel{}) == 0);
    SearchModel inDrums;
    inDrums.collectionId = drums;
    CHECK(countSearch(db, inDrums) == 0);
    CHECK(lib.problems().empty());

    CHECK(lib.addRoot(samples) == root); // the same folder, enabled again
    CHECK(lib.root(root)->enabled);
    CHECK(countSearch(db, SearchModel{}) == 2);
    CHECK(countSearch(db, inDrums) == 1);
    CHECK(user.rating(kick) == 4);
}

TEST_CASE("rootOf finds the deepest folder holding a path", "[folders]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    fs::create_directories(dir.path() / "Samples" / "Drums");
    const auto samples = lib.addRoot(dir.path() / "Samples");
    const auto drums = lib.addRoot(dir.path() / "Samples" / "Drums");
    lib.setRootEnabled(drums, false);
    const fs::path kick = dir.path() / "Samples" / "Drums" / "kick.wav";

    const auto any = lib.rootOf(kick, false);
    REQUIRE(any);
    CHECK(any->first.id == drums);
    CHECK(any->second == "kick.wav");
    const auto enabled = lib.rootOf(kick);
    REQUIRE(enabled);
    CHECK(enabled->first.id == samples);
    CHECK(enabled->second == "Drums/kick.wav");
    CHECK(lib.rootOf(dir.path() / "Samples")->second.empty()); // the folder itself
    CHECK_FALSE(lib.rootOf(dir.path() / "Elsewhere" / "kick.wav"));
    CHECK_FALSE(lib.rootOf(dir.path() / "Samples2" / "kick.wav")); // a prefix of the name is not inside
}

TEST_CASE("A trashed sample is never relinked to a copy of it", "[folders]")
{
    TempDir dir;
    test::WavSpec spec;
    test::writeWav(dir.path() / "A" / "kick.wav", spec);
    Db db = Db::open(dir.path() / "library.db");
    Library lib(db);
    const auto a = lib.addRoot(dir.path() / "A");
    scanRoot(db, a);
    const auto kick = lib.fileByPath(a, "kick.wav")->id;
    // As the trash leaves it: missing, marked, and off its path.
    db.exec("UPDATE files SET status = 'missing', trashed_by = 1, rel_path = '/trashed/1' WHERE id = "
            + std::to_string(kick));
    fs::remove(dir.path() / "A" / "kick.wav");

    test::writeWav(dir.path() / "B" / "kick.wav", spec); // the same content elsewhere
    const auto b = lib.addRoot(dir.path() / "B");
    const auto stats = scanRoot(db, b);
    CHECK(stats.added == 1);
    CHECK(stats.relinked == 0);
    CHECK(lib.fileById(kick)->relPath == "/trashed/1");
    test::writeWav(dir.path() / "A" / "kick.wav", spec); // and a new file where it was
    CHECK(scanRoot(db, a).added == 1);
}
````

In `tests/test_user_data.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/tests/test_user_data.cpp b/tests/test_user_data.cpp
index 14f1a7a..d93161c 100644
--- a/tests/test_user_data.cpp
+++ b/tests/test_user_data.cpp
@@ -47,7 +47,7 @@ TEST_CASE("migration 3 adds the user-data tables to a version 2 library", "[user
     db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
     db.exec("INSERT INTO files(id, root_id, rel_path, name, size, mtime, format, status) VALUES "
             "(1, 1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok')");
-    migrate(db);
+    migrate(db, 3);
     CHECK(db.schemaVersion() == 3);
     UserData user(db);
     user.setRating(1, 4);
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[folders]"`

Expected: the build stops:

```
tests/test_library_folders.cpp:46:9: error: no member named 'setRootEnabled' in 'asma::Library'
tests/test_library_folders.cpp:72:26: error: no member named 'rootOf' in 'asma::Library'
```

- [ ] **Step 3: Implement**

In `core/include/asma/core/Library.h`, apply (`git apply` takes it as is):

````diff
diff --git a/core/include/asma/core/Library.h b/core/include/asma/core/Library.h
index a8e2daf..f50b624 100644
--- a/core/include/asma/core/Library.h
+++ b/core/include/asma/core/Library.h
@@ -75,17 +75,25 @@ class Library {
 public:
     explicit Library(Db& db) : db_(db) {}
 
-    // Returns the existing id when the directory is already a root.
+    // Returns the existing id when the directory is already a root, enabling
+    // it again if it was removed.
     std::int64_t addRoot(const std::filesystem::path& dir);
     std::vector<Root> roots();
     std::optional<Root> root(std::int64_t id);
+    // A removed folder is disabled: its samples are hidden everywhere and keep
+    // their data, which comes back when it is enabled again.
+    void setRootEnabled(std::int64_t id, bool enabled);
+    // The deepest folder holding a path (enabled ones only, unless not
+    // `enabledOnly`) and the path inside it, '/' separated; empty for the
+    // folder itself.
+    std::optional<std::pair<Root, std::string>> rootOf(const std::filesystem::path& path, bool enabledOnly = true);
 
     std::vector<FileRecord> filesInRoot(std::int64_t rootId);
     std::optional<FileRecord> fileById(std::int64_t id);
     std::optional<FileRecord> fileByPath(std::int64_t rootId, std::string_view relPath);
     // The file at an absolute path, looked up through the root that contains it.
     std::optional<FileRecord> fileByAbsolutePath(const std::filesystem::path& path);
-    // Missing rows in any root whose content matches.
+    // Missing rows in any root whose content matches; never a trashed one.
     std::vector<FileRecord> relinkCandidates(std::string_view contentHash, std::int64_t size);
     // Ok rows in any root whose content matches: a file moved from a root that
     // has not been rescanned yet still looks present there.
````

In `core/src/Library.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/core/src/Library.cpp b/core/src/Library.cpp
index ddb7d82..4b7adb4 100644
--- a/core/src/Library.cpp
+++ b/core/src/Library.cpp
@@ -148,7 +148,11 @@ std::int64_t Library::addRoot(const fs::path& dir)
 
     auto select = db_.prepare("SELECT id FROM roots WHERE path = ?");
     select.bind(1, std::string_view(path));
-    if (select.step()) return select.getInt(0);
+    if (select.step()) {
+        const auto id = select.getInt(0);
+        setRootEnabled(id, true);
+        return id;
+    }
 
     auto insert = db_.prepare("INSERT INTO roots(path) VALUES (?)");
     insert.bind(1, std::string_view(path));
@@ -172,6 +176,38 @@ std::optional<Root> Library::root(std::int64_t id)
     return Root{q.getInt(0), q.getText(1), q.getInt(2) != 0};
 }
 
+void Library::setRootEnabled(std::int64_t id, bool enabled)
+{
+    auto q = db_.prepare("UPDATE roots SET enabled = ? WHERE id = ?");
+    q.bind(1, enabled ? 1 : 0).bind(2, id);
+    q.run();
+}
+
+std::optional<std::pair<Root, std::string>> Library::rootOf(const fs::path& path, bool enabledOnly)
+{
+    if (path.empty()) return std::nullopt;
+    std::error_code ec;
+    const fs::path absolute = fs::absolute(path, ec);
+    if (ec) return std::nullopt;
+    fs::path canonical = fs::weakly_canonical(absolute, ec);
+    if (ec) canonical = absolute;
+    std::string full = toUtf8(canonical);
+    while (full.size() > 1 && full.back() == '/') full.pop_back();
+    std::optional<std::pair<Root, std::string>> best;
+    for (const auto& r : roots()) {
+        if (enabledOnly && !r.enabled) continue;
+        if (best && r.path.size() <= best->first.path.size()) continue;
+        if (full == r.path) {
+            best = std::make_pair(r, std::string());
+            continue;
+        }
+        const std::string prefix = r.path.back() == '/' ? r.path : r.path + "/";
+        if (full.size() > prefix.size() && full.compare(0, prefix.size(), prefix) == 0)
+            best = std::make_pair(r, full.substr(prefix.size()));
+    }
+    return best;
+}
+
 std::vector<FileRecord> Library::filesInRoot(std::int64_t rootId)
 {
     auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE root_id = ?");
@@ -217,7 +253,8 @@ std::optional<FileRecord> Library::fileByAbsolutePath(const fs::path& path)
 std::vector<FileRecord> Library::relinkCandidates(std::string_view contentHash, std::int64_t size)
 {
     auto q = db_.prepare("SELECT " + std::string(kFileColumns)
-                         + " FROM files WHERE status = 'missing' AND content_hash = ? AND size = ? ORDER BY id");
+                         + " FROM files WHERE status = 'missing' AND trashed_by IS NULL AND content_hash = ? AND size = ? "
+                           "ORDER BY id");
     q.bind(1, contentHash).bind(2, size);
     return readFiles(q);
 }
````

In `core/src/Schema.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/core/src/Schema.cpp b/core/src/Schema.cpp
index fdc15d2..7b622fe 100644
--- a/core/src/Schema.cpp
+++ b/core/src/Schema.cpp
@@ -12,7 +12,7 @@ namespace asma {
 namespace {
 
 // Append-only. Never edit a migration that has shipped; add a new one.
-constexpr std::array<std::string_view, 3> kMigrations = {
+constexpr std::array<std::string_view, 4> kMigrations = {
     R"SQL(
 CREATE TABLE roots (
     id INTEGER PRIMARY KEY,
@@ -119,6 +119,34 @@ CREATE TABLE saved_searches (
     name TEXT NOT NULL UNIQUE COLLATE NOCASE,
     model TEXT NOT NULL
 );
+)SQL",
+    R"SQL(
+-- A trashed sample keeps its row, and with it the user's data, until its
+-- journal group is pruned: trashed_by is the journal step that trashed it.
+ALTER TABLE files ADD COLUMN trashed_by INTEGER;
+CREATE INDEX files_trashed ON files(trashed_by) WHERE trashed_by IS NOT NULL;
+
+-- File operations, each group undoable as one.
+CREATE TABLE journal_groups (
+    id INTEGER PRIMARY KEY,
+    label TEXT NOT NULL,
+    at TEXT NOT NULL,
+    state TEXT NOT NULL CHECK (state IN ('running', 'done', 'undone', 'rolled_back'))
+);
+
+CREATE TABLE journal (
+    id INTEGER PRIMARY KEY,
+    group_id INTEGER NOT NULL REFERENCES journal_groups(id) ON DELETE CASCADE,
+    op TEXT NOT NULL CHECK (op IN ('rename', 'move', 'trash', 'remove_root')),
+    file_id INTEGER,
+    root_id INTEGER,
+    src TEXT NOT NULL,
+    dst TEXT,
+    trash_ref TEXT,
+    state TEXT NOT NULL CHECK (state IN ('planned', 'done', 'undone', 'failed')),
+    at TEXT NOT NULL
+);
+CREATE INDEX journal_group ON journal(group_id);
 )SQL",
 };
 
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[folders]"`

Expected: `All tests passed (26 assertions in 4 test cases)`; the whole of `asma_tests` passes too (the version-3 migration test now asks for version 3).

- [ ] **Step 5: Commit**

````sh
git add core/include/asma/core/Library.h core/src/Library.cpp core/src/Schema.cpp tests/test_library_folders.cpp tests/test_user_data.cpp
git commit -m "core: a journal in the library; removed folders keep their samples' data, and trashed samples are never relinked"
````

---

### Task 3: File operations: rename, move, trash and removing a folder, journaled, undone as a group

Spec section 6 (The engine, Undo, Recovery). `FileOps` plans each request as steps, checks them all before touching anything (the sample is in the library, in an enabled folder and unchanged on disk; the destination is free on disk and in the library and inside an enabled folder, on the same disk; the name is allowed and keeps its extension; a trash exists), journals the group, then carries out each step and updates the row in the same transaction, so a sample keeps its id and its data. A step that fails rolls back the group and names the file. Undo reverses the newest group step by step, skipping and naming what cannot be put back. Recovery rolls back a group a crash left running. The newest 50 groups are kept; a sample still in the trash when its group goes is deleted from the library for good. The messages are the footer's.

**Files:**

- Create: `core/include/asma/core/FileOps.h`
- Modify: `core/include/asma/core/Fs.h`
- Create: `core/src/FileOps.cpp`
- Modify: `core/src/Fs.cpp`
- Create: `tests/test_file_ops.cpp` (test)

**Interfaces:**

- Consumes: task 1's trash and `renameNoReplace`, task 2's journal and `rootOf`; `utcNow()`.
- Produces: `class OperationRefused`; `class SimulatedCrash`; `enum class Operation { Rename, Move, Trash, RemoveFolder }`; `struct OpResult { group, op, label, count, name, files, skipped }`; `struct JournalGroup { id, label, at, state }`; `struct TrashBackend { available, move, restore; static TrashBackend system(); }`; `class FileOps` (`FileOps(Db&, TrashBackend = system())`, `rename(id, name)`, `move(ids, folder)`, `trash(ids)`, `removeFolder(rootId)`, `std::optional<OpResult> undo()`, `std::optional<JournalGroup> undoable()`, `history(limit = 50)`, `recover()`, `crashAfter(n)`, `kKeptGroups = 50`); `std::optional<std::string> renameProblem(current, name)`; `std::string interruptedText(const OpResult&)`; `std::string undoneText(const OpResult&)`; `bool sameVolume(const path&, const path&)` in `Fs.h`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_file_ops.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/FileOps.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// A scanned library of three samples in Samples, with an empty Samples/Drums
// and a trash of its own (the tests never touch the system's).
struct Lib {
    TempDir dir;
    fs::path samples = dir.path() / "Samples";
    fs::path trashDir = dir.path() / "Trash";
    Db db;
    std::int64_t root = 0;
    std::map<std::string, std::int64_t> id;
    bool trashWorks = true;
    int trashCalls = 0;
    int failTrashCall = 0; // the call that fails (0: none)

    Lib() : db(open(dir.path()))
    {
        std::uint32_t seed = 1;
        for (const char* name : {"kick.wav", "snare.wav", "hat.wav"}) {
            test::WavSpec spec;
            spec.seed = seed++;
            test::writeWav(samples / name, spec);
        }
        fs::create_directories(samples / "Drums");
        fs::create_directories(trashDir);
        Library lib(db);
        root = lib.addRoot(samples);
        scanRoot(db, root);
        for (const auto& f : lib.filesInRoot(root)) id[f.relPath] = f.id;
        UserData(db).setRating(id["kick.wav"], 5);
    }
    static Db open(const fs::path& dir) { return Db::open(dir / "library.db"); }

    TrashBackend trash()
    {
        TrashBackend t;
        t.available = [this](const fs::path&) { return trashWorks; };
        t.move = [this](const fs::path& file) {
            TrashResult r;
            if (++trashCalls == failTrashCall) {
                r.error = "the disk said no";
                return r;
            }
            r.where = trashDir / (std::to_string(trashCalls) + "-" + file.filename().string());
            r.ok = !renameNoReplace(file, r.where);
            return r;
        };
        t.restore = [](const fs::path& where, const fs::path& to) {
            if (!fs::exists(where)) return std::string("it is no longer in the Trash");
            return renameNoReplace(where, to) ? std::string("its old place is taken") : std::string();
        };
        return t;
    }
    FileOps ops() { return FileOps(db, trash()); }
    std::optional<FileRecord> file(const std::string& name) { return Library(db).fileById(id.at(name)); }
    std::int64_t shown() { return countSearch(db, SearchModel{}); }
};

std::string refusal(const std::function<void()>& f)
{
    try {
        f();
    } catch (const OperationRefused& e) {
        return e.what();
    }
    return "(not refused)";
}

} // namespace

TEST_CASE("Renaming a sample keeps its data, and undo renames it back", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const OpResult done = ops.rename(lib.id["kick.wav"], "Kick 01.wav");
    CHECK(done.label == "Rename kick.wav");
    CHECK(done.files == std::vector<std::int64_t>{lib.id["kick.wav"]});
    CHECK(fs::exists(lib.samples / "Kick 01.wav"));
    CHECK_FALSE(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.file("kick.wav")->relPath == "Kick 01.wav");
    CHECK(UserData(lib.db).rating(lib.id["kick.wav"]) == 5);
    REQUIRE(ops.undoable());
    CHECK(ops.undoable()->label == "Rename kick.wav");

    const auto undone = ops.undo();
    REQUIRE(undone);
    CHECK(undone->skipped.empty());
    CHECK(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");
    CHECK_FALSE(ops.undoable());
    CHECK_FALSE(ops.undo());
}

TEST_CASE("A rename is refused before anything is touched", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const auto kick = lib.id["kick.wav"];
    CHECK(refusal([&] { ops.rename(kick, "snare.wav"); }) == "snare.wav already exists in Samples.");
    CHECK(refusal([&] { ops.rename(kick, "kick.aif"); }) == "A rename keeps the extension: .wav.");
    CHECK(refusal([&] { ops.rename(kick, "a/b.wav"); }) == "A name cannot contain / \\ : * ? \" < > |.");
    CHECK(refusal([&] { ops.rename(kick, ".wav"); }) == "A name is needed.");
    CHECK(refusal([&] { ops.rename(kick, "kick.wav"); }) == "kick.wav has that name already.");
    test::writeBytes(lib.samples / "kick.wav", "changed behind asma's back");
    CHECK(refusal([&] { ops.rename(kick, "boom.wav"); })
          == "kick.wav has changed since asma last read it; try again after the next scan.");
    CHECK(fs::exists(lib.samples / "kick.wav"));
    CHECK_FALSE(ops.undoable());
}

TEST_CASE("A rename that only changes letter case works", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    ops.rename(lib.id["kick.wav"], "KICK.wav");
    CHECK(lib.file("kick.wav")->relPath == "KICK.wav");
    bool listed = false;
    for (const auto& e : fs::directory_iterator(lib.samples)) listed |= e.path().filename() == "KICK.wav";
    CHECK(listed);
    ops.undo();
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");
}

TEST_CASE("Moving samples keeps their ids, and a collision moves none", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const OpResult done = ops.move({lib.id["kick.wav"], lib.id["snare.wav"]}, lib.samples / "Drums");
    CHECK(done.label == "Move 2 Samples");
    CHECK(fs::exists(lib.samples / "Drums" / "kick.wav"));
    CHECK(lib.file("kick.wav")->relPath == "Drums/kick.wav");
    CHECK(lib.file("snare.wav")->relPath == "Drums/snare.wav");
    CHECK(lib.shown() == 3);
    ops.undo();
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");

    test::writeBytes(lib.samples / "Drums" / "kick.wav", "another kick");
    test::writeBytes(lib.samples / "Drums" / "hat.wav", "another hat");
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"], lib.id["snare.wav"], lib.id["hat.wav"]}, lib.samples / "Drums"); })
          == "2 of 3 samples already exist in Drums: hat.wav, kick.wav.");
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"]}, lib.samples / "Drums"); })
          == "kick.wav already exists in Drums.");
    CHECK(fs::exists(lib.samples / "snare.wav"));
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"]}, lib.dir.path()); }) == "That folder is outside the library's folders.");
    CHECK(refusal([&] { ops.move({lib.id["kick.wav"]}, lib.samples); }) == "kick.wav is already in Samples.");
}

TEST_CASE("Trashed samples leave the library with their data kept, and undo brings them back", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    const OpResult done = ops.trash({lib.id["kick.wav"], lib.id["hat.wav"]});
    CHECK(done.label == "Move 2 Samples to the Trash");
    CHECK_FALSE(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.shown() == 1);
    CHECK(lib.file("kick.wav")->status == FileStatus::Missing);
    // A scan now finds them gone, and a new file where one was is new.
    test::WavSpec other;
    other.seed = 99;
    test::writeWav(lib.samples / "kick.wav", other);
    CHECK(scanRoot(lib.db, lib.root).added == 1);
    fs::remove(lib.samples / "kick.wav");
    scanRoot(lib.db, lib.root);

    const auto undone = ops.undo();
    REQUIRE(undone);
    CHECK(undone->skipped.empty());
    CHECK(fs::exists(lib.samples / "kick.wav"));
    CHECK(lib.file("kick.wav")->status == FileStatus::Ok);
    CHECK(lib.file("kick.wav")->relPath == "kick.wav");
    CHECK(UserData(lib.db).rating(lib.id["kick.wav"]) == 5);
    CHECK(lib.shown() == 3);

    lib.trashWorks = false;
    CHECK(refusal([&] { ops.trash({lib.id["kick.wav"]}); })
          == "kick.wav can't go to the Trash: its disk has none. Nothing was moved.");
    CHECK(fs::exists(lib.samples / "kick.wav"));
}

TEST_CASE("Undo skips what it cannot put back and says why", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    ops.move({lib.id["kick.wav"], lib.id["snare.wav"]}, lib.samples / "Drums");
    test::writeBytes(lib.samples / "kick.wav", "a new kick where the old one was");
    const auto undone = ops.undo();
    REQUIRE(undone);
    CHECK(undone->skipped == std::vector<std::string>{"kick.wav could not be moved back: its old place is taken"});
    CHECK(lib.file("snare.wav")->relPath == "snare.wav");
    CHECK(lib.file("kick.wav")->relPath == "Drums/kick.wav");
    CHECK_FALSE(ops.undoable());

    ops.trash({lib.id["hat.wav"]});
    fs::remove_all(lib.trashDir); // emptied
    CHECK(ops.undo()->skipped == std::vector<std::string>{"hat.wav could not be brought back: it is no longer in the Trash"});
}

TEST_CASE("A group a crash interrupted is rolled back when asma starts", "[fileops]")
{
    Lib lib;
    {
        auto ops = lib.ops();
        ops.crashAfter(2);
        CHECK_THROWS_AS(ops.move({lib.id["kick.wav"], lib.id["snare.wav"], lib.id["hat.wav"]}, lib.samples / "Drums"),
                        SimulatedCrash);
    }
    CHECK(fs::exists(lib.samples / "Drums" / "snare.wav"));
    CHECK(fs::exists(lib.samples / "hat.wav")); // the third step never ran

    auto ops = lib.ops();
    const auto recovered = ops.recover();
    REQUIRE(recovered.size() == 1);
    CHECK(interruptedText(recovered[0]) == "asma was interrupted while moving 3 samples; they are back where they were.");
    for (const char* name : {"kick.wav", "snare.wav", "hat.wav"}) CHECK(fs::exists(lib.samples / name));
    CHECK(lib.file("snare.wav")->relPath == "snare.wav");
    CHECK(ops.recover().empty());
    CHECK_FALSE(ops.undoable());
    CHECK(ops.history().front().state == "rolled_back");
}

TEST_CASE("A step that fails rolls back the group and names the file", "[fileops]")
{
    Lib lib;
    lib.failTrashCall = 2;
    auto ops = lib.ops();
    CHECK(refusal([&] { ops.trash({lib.id["hat.wav"], lib.id["kick.wav"], lib.id["snare.wav"]}); })
          == "Could not move kick.wav to the Trash: the disk said no. Nothing was moved.");
    for (const char* name : {"kick.wav", "snare.wav", "hat.wav"}) CHECK(fs::exists(lib.samples / name));
    CHECK(lib.shown() == 3);
    CHECK_FALSE(ops.undoable());
}

TEST_CASE("Removing a folder hides its samples, and undo puts it back", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    CHECK(ops.removeFolder(lib.root).label == "Remove Samples from the Library");
    CHECK(lib.shown() == 0);
    CHECK_FALSE(Library(lib.db).root(lib.root)->enabled);
    CHECK(refusal([&] { ops.removeFolder(lib.root); }) == "Samples is not in the library.");
    ops.undo();
    CHECK(lib.shown() == 3);
    CHECK(UserData(lib.db).rating(lib.id["kick.wav"]) == 5);
}

TEST_CASE("The journal keeps the newest 50 groups; older trashed samples go for good", "[fileops]")
{
    Lib lib;
    auto ops = lib.ops();
    ops.trash({lib.id["kick.wav"]});
    for (int i = 0; i < 50; ++i) {
        ops.rename(lib.id["snare.wav"], i % 2 ? "snare.wav" : "snare 2.wav");
    }
    CHECK(ops.history().size() == 50);
    CHECK(ops.history().front().label == "Rename snare 2.wav");
    CHECK_FALSE(Library(lib.db).fileById(lib.id["kick.wav"])); // its row went with its group
    CHECK_FALSE(UserData(lib.db).rating(lib.id["kick.wav"]));
    CHECK(Library(lib.db).fileById(lib.id["hat.wav"]));
}
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[fileops]"`

Expected: the build stops:

```
tests/test_file_ops.cpp:3:10: fatal error: 'asma/core/FileOps.h' file not found
```

- [ ] **Step 3: Implement**

Create `core/include/asma/core/FileOps.h`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"
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
````

In `core/include/asma/core/Fs.h`, apply (`git apply` takes it as is):

````diff
diff --git a/core/include/asma/core/Fs.h b/core/include/asma/core/Fs.h
index 857035f..bf94c68 100644
--- a/core/include/asma/core/Fs.h
+++ b/core/include/asma/core/Fs.h
@@ -48,6 +48,10 @@ std::int64_t fileTimeToInt(std::filesystem::file_time_type time);
 // one made a moment ago, fails with std::errc::file_exists. Same volume only.
 std::error_code renameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to);
 
+// Whether two existing paths are on the same volume, where a rename can move
+// one to the other.
+bool sameVolume(const std::filesystem::path& a, const std::filesystem::path& b);
+
 // Which file a path names now, as an opaque string: two calls give the same
 // string unless the file was replaced (renamed over, deleted and made again).
 // Empty when it cannot tell, and always on Windows, which never lets a file
````

Create `core/src/FileOps.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/FileOps.h"

#include "asma/core/Backup.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fs = std::filesystem;

namespace asma {

struct FileOps::Step {
    std::string op;           // rename, move, trash, remove_root
    std::int64_t fileId = 0;  // the sample (file steps)
    std::int64_t rootId = 0;  // the folder it was in, or the one removed
    std::string src;          // UTF-8, where it was
    std::string dst;          // UTF-8, where it goes (rename, move)
    std::string trashRef;     // where the trash put it
    std::string state;
    std::int64_t id = 0;      // the journal row
};

namespace {

constexpr std::string_view kTrashedPrefix = "/trashed/"; // a relative path no file can have

std::string nameOf(std::string_view utf8Path)
{
    const auto slash = utf8Path.rfind('/');
    return std::string(slash == std::string_view::npos ? utf8Path : utf8Path.substr(slash + 1));
}

std::string parentOf(std::string_view relPath)
{
    const auto slash = relPath.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(relPath.substr(0, slash));
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string extensionOf(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos || dot == 0 ? std::string() : std::string(name.substr(dot));
}

// "kick.wav, snare.wav, hat.wav and 9 more"
std::string listed(std::vector<std::string> names)
{
    std::sort(names.begin(), names.end());
    std::string out;
    const std::size_t shown = std::min<std::size_t>(names.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) out += (i ? ", " : "") + names[i];
    if (names.size() > shown) out += " and " + std::to_string(names.size() - shown) + " more";
    return out;
}

std::string samples(std::size_t n) { return std::to_string(n) + (n == 1 ? " sample" : " samples"); }

// The folder's name as the user knows it: "Drums" for .../Samples/Drums.
std::string folderName(const Root& root, std::string_view rel)
{
    return nameOf(rel.empty() ? std::string_view(root.path) : rel);
}

bool hasUserData(Db& db, std::int64_t fileId)
{
    auto q = db.prepare("SELECT EXISTS (SELECT 1 FROM ratings WHERE file_id = ?1) "
                        "OR EXISTS (SELECT 1 FROM favourites WHERE file_id = ?1) "
                        "OR EXISTS (SELECT 1 FROM file_tags WHERE file_id = ?1 AND source = 'user') "
                        "OR EXISTS (SELECT 1 FROM collection_items WHERE file_id = ?1)");
    q.bind(1, fileId);
    return q.step() && q.getInt(0) != 0;
}

// Deletes a sample's row, its index entry and (by cascade) all it held.
void forget(Db& db, std::int64_t fileId)
{
    auto fts = db.prepare("DELETE FROM fts_files WHERE rowid = ?");
    fts.bind(1, fileId);
    fts.run();
    auto row = db.prepare("DELETE FROM files WHERE id = ?");
    row.bind(1, fileId);
    row.run();
}

bool sameFile(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

// A rename only changes letter case, which a case-blind disk sees as the
// same name: it goes through a name of its own.
std::error_code moveFile(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec); // a folder that went since
    const bool caseOnly = from != to && from.parent_path() == to.parent_path()
                       && lower(toUtf8(from.filename())) == lower(toUtf8(to.filename())) && sameFile(from, to);
    if (!caseOnly) return renameNoReplace(from, to);
    const fs::path aside = from.parent_path() / (".asma-renaming-" + toUtf8(from.filename()));
    if ((ec = renameNoReplace(from, aside))) return ec;
    if ((ec = renameNoReplace(aside, to))) renameNoReplace(aside, from);
    return ec;
}

Operation opFrom(std::string_view s)
{
    if (s == "rename") return Operation::Rename;
    if (s == "trash") return Operation::Trash;
    if (s == "remove_root") return Operation::RemoveFolder;
    return Operation::Move;
}

std::string label(Operation op, std::size_t count, const std::string& name)
{
    const std::string what = count == 1 ? name : std::to_string(count) + " Samples";
    switch (op) {
    case Operation::Rename: return "Rename " + name;
    case Operation::Move: return "Move " + what;
    case Operation::Trash: return "Move " + what + " to the Trash";
    case Operation::RemoveFolder: return "Remove " + name + " from the Library";
    }
    return what;
}

// A sample as the operation finds it: in the library, in an enabled folder,
// and on disk as the library last saw it. Throws OperationRefused otherwise.
struct Source {
    FileRecord file;
    Root root;
    fs::path full;
    std::string name;
};

Source source(Library& lib, std::int64_t id)
{
    auto file = lib.fileById(id);
    if (!file || file->status == FileStatus::Missing) throw OperationRefused("A sample is no longer in the library.");
    auto root = lib.root(file->rootId);
    const std::string name = nameOf(file->relPath);
    if (!root || !root->enabled) throw OperationRefused(name + "'s folder is not in the library.");
    const fs::path full = fromUtf8(root->path + "/" + file->relPath);
    std::error_code ec;
    if (!fs::is_regular_file(full, ec))
        throw OperationRefused(name + " is no longer in its folder; the next scan will notice.");
    const auto size = fs::file_size(full, ec);
    const auto mtime = ec ? fs::file_time_type{} : fs::last_write_time(full, ec);
    if (ec || static_cast<std::int64_t>(size) != file->size || fileTimeToInt(mtime) != file->mtime)
        throw OperationRefused(name + " has changed since asma last read it; try again after the next scan.");
    return {std::move(*file), std::move(*root), full, name};
}

// Whether a destination is taken, on disk or in the library, by anything
// but the sample itself.
bool taken(Library& lib, std::int64_t rootId, const std::string& rel, const fs::path& to, const Source& s)
{
    std::error_code ec;
    if (fs::exists(fs::symlink_status(to, ec)) && !sameFile(to, s.full)) return true;
    const auto row = lib.fileByPath(rootId, rel);
    return row && row->id != s.file.id;
}

} // namespace

std::optional<std::string> renameProblem(std::string_view current, std::string_view newName)
{
    const std::string name(newName);
    if (name.find_first_of("/\\:*?\"<>|") != std::string::npos
        || std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
        return "A name cannot contain / \\ : * ? \" < > |.";
    const std::string ext = extensionOf(current);
    const bool keepsExt = name.size() >= ext.size() && lower(name.substr(name.size() - ext.size())) == lower(ext);
    const std::string base = keepsExt ? name.substr(0, name.size() - ext.size()) : name;
    if (base.find_first_not_of(' ') == std::string::npos || base == "." || base == "..") return "A name is needed.";
    if (!keepsExt || lower(extensionOf(name)) != lower(ext)) return "A rename keeps the extension: " + ext + ".";
    if (name == current) return std::string(current) + " has that name already.";
    return std::nullopt;
}

TrashBackend TrashBackend::system()
{
    TrashBackend t;
    t.available = [](const fs::path& f) { return trashAvailable(f); };
    t.move = [](const fs::path& f) { return moveToTrash(f); };
    t.restore = [](const fs::path& where, const fs::path& to) { return restoreFromTrash(where, to); };
    return t;
}

FileOps::FileOps(Db& db, TrashBackend trash) : db_(db), trash_(std::move(trash)) {}

OpResult FileOps::rename(std::int64_t fileId, std::string_view newName)
{
    Library lib(db_);
    const Source s = source(lib, fileId);
    const std::string name(newName);
    if (const auto problem = renameProblem(s.name, name)) throw OperationRefused(*problem);

    const std::string folderRel = parentOf(s.file.relPath);
    const std::string rel = folderRel.empty() ? name : folderRel + "/" + name;
    const fs::path to = fromUtf8(s.root.path + "/" + rel);
    if (taken(lib, s.root.id, rel, to, s))
        throw OperationRefused(name + " already exists in " + folderName(s.root, folderRel) + ".");

    Step step;
    step.op = "rename";
    step.fileId = fileId;
    step.rootId = s.root.id;
    step.src = toUtf8(s.full);
    step.dst = toUtf8(to);
    return run(Operation::Rename, {step}, label(Operation::Rename, 1, s.name), s.name);
}

OpResult FileOps::move(const std::vector<std::int64_t>& fileIds, const fs::path& folder)
{
    Library lib(db_);
    std::error_code ec;
    const auto place = lib.rootOf(folder);
    if (!place || !fs::is_directory(folder, ec)) throw OperationRefused("That folder is outside the library's folders.");
    const auto& [root, folderRel] = *place;
    const std::string where = folderName(root, folderRel);

    std::vector<Step> steps;
    std::vector<std::string> already, clash, elsewhere;
    std::set<std::string> going;
    std::string firstName;
    for (const auto id : fileIds) {
        const Source s = source(lib, id);
        if (s.root.id == root.id && parentOf(s.file.relPath) == folderRel) {
            already.push_back(s.name);
            continue;
        }
        const std::string rel = folderRel.empty() ? s.name : folderRel + "/" + s.name;
        const fs::path to = fromUtf8(root.path + "/" + rel);
        if (!sameVolume(s.full, folder)) elsewhere.push_back(s.name);
        if (taken(lib, root.id, rel, to, s) || !going.insert(lower(s.name)).second) clash.push_back(s.name);
        if (firstName.empty()) firstName = s.name;
        Step step;
        step.op = "move";
        step.fileId = id;
        step.rootId = s.root.id;
        step.src = toUtf8(s.full);
        step.dst = toUtf8(to);
        steps.push_back(std::move(step));
    }
    if (steps.empty()) {
        if (already.size() == 1) throw OperationRefused(already[0] + " is already in " + where + ".");
        throw OperationRefused("Those samples are already in " + where + ".");
    }
    if (!elsewhere.empty())
        throw OperationRefused((elsewhere.size() == 1 ? elsewhere[0] + " is" : listed(elsewhere) + " are")
                               + " on another disk than " + where + "; moving between disks comes with export.");
    if (!clash.empty()) {
        if (steps.size() == 1) throw OperationRefused(clash[0] + " already exists in " + where + ".");
        throw OperationRefused(std::to_string(clash.size()) + " of " + samples(steps.size()) + " already exist in " + where
                               + ": " + listed(clash) + ".");
    }
    const std::size_t count = steps.size();
    return run(Operation::Move, std::move(steps), label(Operation::Move, count, firstName), firstName);
}

OpResult FileOps::trash(const std::vector<std::int64_t>& fileIds)
{
    Library lib(db_);
    std::vector<Step> steps;
    std::vector<std::string> none;
    for (const auto id : fileIds) {
        const Source s = source(lib, id);
        if (!trash_.available(s.full)) none.push_back(s.name);
        Step step;
        step.op = "trash";
        step.fileId = id;
        step.rootId = s.root.id;
        step.src = toUtf8(s.full);
        steps.push_back(std::move(step));
    }
    if (steps.empty()) throw OperationRefused("Nothing to move to the Trash.");
    if (!none.empty()) {
        if (none.size() == 1) throw OperationRefused(none[0] + " can't go to the Trash: its disk has none. Nothing was moved.");
        throw OperationRefused(samples(none.size()) + " can't go to the Trash: their disk has none (" + listed(none)
                               + "). Nothing was moved.");
    }
    const std::string first = nameOf(steps.front().src);
    const std::size_t count = steps.size();
    return run(Operation::Trash, std::move(steps), label(Operation::Trash, count, first), first);
}

OpResult FileOps::removeFolder(std::int64_t rootId)
{
    Library lib(db_);
    const auto root = lib.root(rootId);
    if (!root) throw OperationRefused("That folder is not in the library.");
    const std::string name = nameOf(root->path);
    if (!root->enabled) throw OperationRefused(name + " is not in the library.");
    Step step;
    step.op = "remove_root";
    step.rootId = rootId;
    step.src = root->path;
    return run(Operation::RemoveFolder, {step}, label(Operation::RemoveFolder, 1, name), name);
}

OpResult FileOps::run(Operation op, std::vector<Step> steps, std::string groupLabel, std::string name)
{
    OpResult result;
    result.op = op;
    result.label = std::move(groupLabel);
    result.count = steps.size();
    result.name = std::move(name);
    const std::string at = utcNow();
    {
        Transaction tx(db_);
        auto group = db_.prepare("INSERT INTO journal_groups(label, at, state) VALUES (?, ?, 'running')");
        group.bind(1, std::string_view(result.label)).bind(2, std::string_view(at));
        group.run();
        result.group = db_.lastInsertId();
        auto insert = db_.prepare("INSERT INTO journal(group_id, op, file_id, root_id, src, dst, state, at) "
                                  "VALUES (?, ?, ?, ?, ?, ?, 'planned', ?)");
        for (auto& s : steps) {
            insert.bind(1, result.group).bind(2, std::string_view(s.op));
            if (s.fileId) insert.bind(3, s.fileId);
            else insert.bindNull(3);
            insert.bind(4, s.rootId).bind(5, std::string_view(s.src));
            if (s.dst.empty()) insert.bindNull(6);
            else insert.bind(6, std::string_view(s.dst));
            insert.bind(7, std::string_view(at));
            insert.run();
            insert.reset();
            s.id = db_.lastInsertId();
        }
        tx.commit();
    }

    Library lib(db_);
    std::size_t done = 0;
    for (const auto& s : steps) {
        if (crashAfter_ && done == *crashAfter_) {
            crashAfter_.reset();
            throw SimulatedCrash();
        }
        std::string error;
        if (s.op == "rename" || s.op == "move") {
            if (const auto ec = moveFile(fromUtf8(s.src), fromUtf8(s.dst))) {
                error = ec == std::errc::file_exists ? "its new place is taken" : ec.message();
            } else {
                Transaction tx(db_);
                auto file = lib.fileById(s.fileId);
                const auto place = lib.rootOf(fromUtf8(s.dst));
                if (file && place) {
                    file->rootId = place->first.id;
                    file->relPath = place->second;
                    lib.updateFile(*file);
                }
                auto mark = db_.prepare("UPDATE journal SET state = 'done' WHERE id = ?");
                mark.bind(1, s.id);
                mark.run();
                tx.commit();
            }
        } else if (s.op == "trash") {
            const TrashResult trashed = trash_.move(fromUtf8(s.src));
            if (!trashed.ok) {
                error = trashed.error.empty() ? "the Trash did not take it" : trashed.error;
            } else {
                Transaction tx(db_);
                if (auto file = lib.fileById(s.fileId)) {
                    file->status = FileStatus::Missing;
                    file->relPath = std::string(kTrashedPrefix) + std::to_string(s.id);
                    lib.updateFile(*file);
                    auto mark = db_.prepare("UPDATE files SET trashed_by = ? WHERE id = ?");
                    mark.bind(1, s.id).bind(2, s.fileId);
                    mark.run();
                }
                auto mark = db_.prepare("UPDATE journal SET state = 'done', trash_ref = ? WHERE id = ?");
                mark.bind(1, std::string_view(toUtf8(trashed.where))).bind(2, s.id);
                mark.run();
                tx.commit();
            }
        } else {
            Transaction tx(db_);
            lib.setRootEnabled(s.rootId, false);
            auto mark = db_.prepare("UPDATE journal SET state = 'done' WHERE id = ?");
            mark.bind(1, s.id);
            mark.run();
            tx.commit();
        }
        if (!error.empty()) {
            OpResult rolled = result;
            rollBack(result.group, "rolled_back", rolled);
            const std::string what = nameOf(s.src);
            switch (op) {
            case Operation::Rename: throw OperationRefused("Could not rename " + what + ": " + error + ".");
            case Operation::Trash:
                throw OperationRefused("Could not move " + what + " to the Trash: " + error + ". Nothing was moved.");
            default: throw OperationRefused("Could not move " + what + ": " + error + ". Nothing was moved.");
            }
        }
        if (s.fileId) result.files.push_back(s.fileId);
        ++done;
    }
    auto finish = db_.prepare("UPDATE journal_groups SET state = 'done' WHERE id = ?");
    finish.bind(1, result.group);
    finish.run();
    prune();
    return result;
}

void FileOps::rollBack(std::int64_t group, const char* state, OpResult& result)
{
    Library lib(db_);
    std::vector<Step> steps;
    {
        auto q = db_.prepare("SELECT id, op, COALESCE(file_id, 0), COALESCE(root_id, 0), src, COALESCE(dst, ''), "
                             "COALESCE(trash_ref, ''), state FROM journal WHERE group_id = ? ORDER BY id DESC");
        q.bind(1, group);
        while (q.step()) {
            Step s;
            s.id = q.getInt(0);
            s.op = q.getText(1);
            s.fileId = q.getInt(2);
            s.rootId = q.getInt(3);
            s.src = q.getText(4);
            s.dst = q.getText(5);
            s.trashRef = q.getText(6);
            s.state = q.getText(7);
            steps.push_back(std::move(s));
        }
    }
    result.files.clear();
    result.skipped.clear();
    const auto mark = [&](std::int64_t id, const char* stepState) {
        auto q = db_.prepare("UPDATE journal SET state = ? WHERE id = ?");
        q.bind(1, std::string_view(stepState)).bind(2, id);
        q.run();
    };
    for (const auto& s : steps) {
        if (s.state != "done") {
            if (s.state == "planned") mark(s.id, "undone"); // never happened
            continue;
        }
        const std::string name = nameOf(s.src);
        if (s.op == "remove_root") {
            if (!lib.root(s.rootId)) {
                result.skipped.push_back(name + " could not be put back: it is no longer in the library");
                mark(s.id, "failed");
                continue;
            }
            Transaction tx(db_);
            lib.setRootEnabled(s.rootId, true);
            mark(s.id, "undone");
            tx.commit();
            continue;
        }
        // Back to the folder it was in, at the path it had.
        const auto root = lib.root(s.rootId);
        const std::string prefix = root ? root->path + "/" : std::string();
        const bool inRoot = root && s.src.compare(0, prefix.size(), prefix) == 0;
        const std::string rel = inRoot ? s.src.substr(prefix.size()) : std::string();
        const auto occupant = inRoot ? lib.fileByPath(s.rootId, rel) : std::nullopt;
        const bool trashed = s.op == "trash";
        const std::string failed = name + (trashed ? " could not be brought back: " : " could not be moved back: ");
        // A row whose file has gone and that holds nothing of the user's
        // gives way; any other keeps its place.
        if (occupant && occupant->id != s.fileId && occupant->status == FileStatus::Missing
            && !hasUserData(db_, occupant->id)) {
            forget(db_, occupant->id);
        } else if (occupant && occupant->id != s.fileId) {
            result.skipped.push_back(failed + "its old place is taken");
            mark(s.id, "failed");
            continue;
        }
        std::string error;
        if (trashed) {
            error = trash_.restore(fromUtf8(s.trashRef), fromUtf8(s.src));
        } else {
            std::error_code ec;
            if (!fs::exists(fs::symlink_status(fromUtf8(s.dst), ec))) error = "it is no longer where it was moved";
            else if (const auto moved = moveFile(fromUtf8(s.dst), fromUtf8(s.src)))
                error = moved == std::errc::file_exists ? "its old place is taken" : moved.message();
        }
        if (!error.empty()) {
            result.skipped.push_back(failed + error);
            mark(s.id, "failed");
            continue;
        }
        Transaction tx(db_);
        if (auto file = lib.fileById(s.fileId); file && inRoot) {
            file->rootId = s.rootId;
            file->relPath = rel;
            if (trashed) file->status = file->failureReason.empty() ? FileStatus::Ok : FileStatus::Failed;
            lib.updateFile(*file);
            auto clear = db_.prepare("UPDATE files SET trashed_by = NULL WHERE id = ?");
            clear.bind(1, s.fileId);
            clear.run();
            result.files.push_back(s.fileId);
        }
        mark(s.id, "undone");
        tx.commit();
    }
    auto finish = db_.prepare("UPDATE journal_groups SET state = ? WHERE id = ?");
    finish.bind(1, std::string_view(state)).bind(2, group);
    finish.run();
}

namespace {

// The operation, count and name of a journaled group.
OpResult described(Db& db, const JournalGroup& g)
{
    OpResult r;
    r.group = g.id;
    r.label = g.label;
    auto q = db.prepare("SELECT op, src FROM journal WHERE group_id = ? ORDER BY id");
    q.bind(1, g.id);
    while (q.step()) {
        if (r.count++ == 0) {
            r.op = opFrom(q.getText(0));
            r.name = nameOf(q.getText(1));
        }
    }
    return r;
}

std::vector<JournalGroup> groups(Db& db, std::string_view where, std::size_t limit)
{
    auto q = db.prepare("SELECT id, label, at, state FROM journal_groups " + std::string(where)
                        + " ORDER BY id DESC LIMIT ?");
    q.bind(1, static_cast<std::int64_t>(limit));
    std::vector<JournalGroup> out;
    while (q.step()) out.push_back({q.getInt(0), q.getText(1), q.getText(2), q.getText(3)});
    return out;
}

} // namespace

std::optional<OpResult> FileOps::undo()
{
    const auto g = undoable();
    if (!g) return std::nullopt;
    OpResult r = described(db_, *g);
    rollBack(g->id, "undone", r);
    return r;
}

std::optional<JournalGroup> FileOps::undoable()
{
    auto found = groups(db_, "WHERE state = 'done'", 1);
    if (found.empty()) return std::nullopt;
    return found.front();
}

std::vector<JournalGroup> FileOps::history(std::size_t limit) { return groups(db_, "", limit); }

std::vector<OpResult> FileOps::recover()
{
    std::vector<OpResult> out;
    for (const auto& g : groups(db_, "WHERE state = 'running'", 1000)) {
        OpResult r = described(db_, g);
        rollBack(g.id, "rolled_back", r);
        out.push_back(std::move(r));
    }
    return out;
}

void FileOps::prune()
{
    std::vector<std::int64_t> old;
    {
        auto q = db_.prepare("SELECT id FROM journal_groups WHERE state != 'running' ORDER BY id DESC LIMIT -1 OFFSET ?");
        q.bind(1, static_cast<std::int64_t>(kKeptGroups));
        while (q.step()) old.push_back(q.getInt(0));
    }
    if (old.empty()) return;
    Transaction tx(db_);
    for (const auto group : old) {
        // A sample still in the Trash can no longer come back through asma:
        // its row goes, and with it its data.
        auto gone = db_.prepare("SELECT f.id FROM files f JOIN journal j ON j.id = f.trashed_by WHERE j.group_id = ?");
        gone.bind(1, group);
        std::vector<std::int64_t> ids;
        while (gone.step()) ids.push_back(gone.getInt(0));
        for (const auto id : ids) forget(db_, id);
        auto steps = db_.prepare("DELETE FROM journal WHERE group_id = ?");
        steps.bind(1, group);
        steps.run();
        auto g = db_.prepare("DELETE FROM journal_groups WHERE id = ?");
        g.bind(1, group);
        g.run();
    }
    tx.commit();
}

std::string interruptedText(const OpResult& r)
{
    const std::string what = r.count == 1 ? r.name : std::to_string(r.count) + " samples";
    std::string doing;
    switch (r.op) {
    case Operation::Rename: doing = "renaming " + r.name; break;
    case Operation::Move: doing = "moving " + what; break;
    case Operation::Trash: doing = "moving " + what + " to the Trash"; break;
    case Operation::RemoveFolder: doing = "removing " + r.name + " from the library"; break;
    }
    std::string text = "asma was interrupted while " + doing + "; ";
    if (r.op == Operation::RemoveFolder) text += "it is back in the library.";
    else text += r.count == 1 ? "it is back where it was." : "they are back where they were.";
    for (const auto& s : r.skipped) text += " " + s + ".";
    return text;
}

std::string undoneText(const OpResult& r)
{
    std::string text = "Undid " + r.label;
    for (const auto& s : r.skipped) text += "; " + s;
    return text + ".";
}

} // namespace asma
````

In `core/src/Fs.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/core/src/Fs.cpp b/core/src/Fs.cpp
index 5dc0cb4..af18f7d 100644
--- a/core/src/Fs.cpp
+++ b/core/src/Fs.cpp
@@ -147,4 +147,17 @@ std::error_code renameNoReplace(const fs::path& from, const fs::path& to)
 #endif
 }
 
+bool sameVolume(const fs::path& a, const fs::path& b)
+{
+#ifdef _WIN32
+    wchar_t va[MAX_PATH + 1] = {}, vb[MAX_PATH + 1] = {};
+    if (!GetVolumePathNameW(fs::absolute(a).c_str(), va, MAX_PATH)) return false;
+    if (!GetVolumePathNameW(fs::absolute(b).c_str(), vb, MAX_PATH)) return false;
+    return _wcsicmp(va, vb) == 0;
+#else
+    struct stat sa {}, sb {};
+    return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
+#endif
+}
+
 } // namespace asma
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[fileops]"`

Expected: `All tests passed (85 assertions in 10 test cases)`

- [ ] **Step 5: Commit**

````sh
git add core/include/asma/core/FileOps.h core/include/asma/core/Fs.h core/src/FileOps.cpp core/src/Fs.cpp tests/test_file_ops.cpp
git commit -m "core: file operations: rename, move, trash and removing a folder, journaled, undone as a group, rolled back after a crash"
````

---

### Task 4: Overlapping folders: refused inside, merged when holding

Spec section 6 (Overlapping folders). `checkAddFolder` says what adding a folder would do: new, again (a removed folder enabled), inside an enabled library folder (refused, "Drums is already in the library, inside Samples."), or holding library folders ("Samples contains 2 folders already in the library (Bass, Drums). Add Samples in their place?"). `addFolder` with `merge` moves the inner folders' samples into it with their ids; a sample with a row in both keeps the outer row with the data of both. `mergeNestedFolders` does it once for libraries made before overlaps were refused.

**Files:**

- Create: `core/include/asma/core/Folders.h`
- Modify: `core/include/asma/core/Library.h`
- Modify: `core/src/FileOps.cpp`
- Create: `core/src/Folders.cpp`
- Modify: `core/src/Library.cpp`
- Create: `tests/test_folder_overlap.cpp` (test)

**Interfaces:**

- Consumes: task 2's `addRoot`, `setRootEnabled`; task 3's `OperationRefused`.
- Produces: `struct AddCheck { enum class Result { New, Again, Inside, Contains }; result; std::vector<Root> contained; std::string message; }`; `AddCheck checkAddFolder(Db&, const path&)`; `std::int64_t addFolder(Db&, const path&, bool merge)`; `std::vector<std::string> mergeNestedFolders(Db&)`; `static std::string Library::folderPath(const path&)`; `void Library::removeFile(std::int64_t)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_folder_overlap.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/FileOps.h"
#include "asma/core/Folders.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// Samples/Drums/kick.wav, Samples/Bass/sub.wav and Samples/pad.wav.
struct Tree {
    TempDir dir;
    fs::path samples = dir.path() / "Samples";
    Db db = Db::open(dir.path() / "library.db");
    Tree()
    {
        test::WavSpec spec;
        test::writeWav(samples / "Drums" / "kick.wav", spec);
        spec.seed = 2;
        test::writeWav(samples / "Bass" / "sub.wav", spec);
        spec.seed = 3;
        test::writeWav(samples / "pad.wav", spec);
    }
    std::int64_t idOf(std::int64_t root, const std::string& rel) { return Library(db).fileByPath(root, rel)->id; }
    std::int64_t shown() { return countSearch(db, SearchModel{}); }
};

std::string refusal(const std::function<void()>& f)
{
    try {
        f();
    } catch (const OperationRefused& e) {
        return e.what();
    }
    return "(not refused)";
}

} // namespace

TEST_CASE("A folder inside one already in the library is refused", "[overlap]")
{
    Tree t;
    const auto samples = addFolder(t.db, t.samples, false);
    const AddCheck check = checkAddFolder(t.db, t.samples / "Drums");
    CHECK(check.result == AddCheck::Result::Inside);
    CHECK(check.message == "Drums is already in the library, inside Samples.");
    CHECK(refusal([&] { addFolder(t.db, t.samples / "Drums", true); }) == check.message);
    CHECK(checkAddFolder(t.db, t.samples).result == AddCheck::Result::Again);
    CHECK(checkAddFolder(t.db, t.dir.path() / "Other").result == AddCheck::Result::New);

    // Inside a removed folder is fine: that one is not scanned. Adding the
    // removed one again then takes the new one's place.
    Library(t.db).setRootEnabled(samples, false);
    CHECK(checkAddFolder(t.db, t.samples / "Drums").result == AddCheck::Result::New);
    addFolder(t.db, t.samples / "Drums", false);
    CHECK(checkAddFolder(t.db, t.samples).result == AddCheck::Result::Contains);
    CHECK(addFolder(t.db, t.samples, true) == samples);
    CHECK(Library(t.db).roots().size() == 1);
    CHECK(Library(t.db).root(samples)->enabled);
}

TEST_CASE("A folder holding library folders takes their place, their samples keeping their data", "[overlap]")
{
    Tree t;
    const auto drums = addFolder(t.db, t.samples / "Drums", false);
    const auto bass = addFolder(t.db, t.samples / "Bass", false);
    scanRoot(t.db, drums);
    scanRoot(t.db, bass);
    const auto kick = t.idOf(drums, "kick.wav");
    UserData(t.db).setRating(kick, 3);
    Library(t.db).setRootEnabled(bass, false); // a removed one is absorbed too

    const AddCheck check = checkAddFolder(t.db, t.samples);
    CHECK(check.result == AddCheck::Result::Contains);
    CHECK(check.message == "Samples contains 2 folders already in the library (Bass, Drums). Add Samples in their place?");
    CHECK(refusal([&] { addFolder(t.db, t.samples, false); }) == check.message);

    const auto samples = addFolder(t.db, t.samples, true);
    Library lib(t.db);
    CHECK(lib.roots().size() == 1);
    CHECK(lib.fileById(kick)->rootId == samples);
    CHECK(lib.fileById(kick)->relPath == "Drums/kick.wav");
    CHECK(UserData(t.db).rating(kick) == 3);
    CHECK(t.shown() == 2); // kick and sub: pad is not scanned yet
    const auto stats = scanRoot(t.db, samples);
    CHECK(stats.added == 1);
    CHECK(stats.missing == 0);
    CHECK(t.shown() == 3);
    SearchModel query;
    query.text = "drums";
    CHECK(countSearch(t.db, query) == 1); // the index knows its new folder
}

TEST_CASE("Nested folders already in a library merge once, into one row per sample", "[overlap]")
{
    Tree t;
    // As older versions allowed: Samples, and Drums inside it, both scanned.
    Library lib(t.db);
    const auto samples = lib.addRoot(t.samples);
    const auto drums = lib.addRoot(t.samples / "Drums");
    scanRoot(t.db, samples);
    scanRoot(t.db, drums);
    REQUIRE(t.shown() == 4);
    const auto outerKick = t.idOf(samples, "Drums/kick.wav");
    const auto innerKick = t.idOf(drums, "kick.wav");
    UserData user(t.db);
    user.setRating(innerKick, 4);
    user.setFavourite(outerKick, true);
    const auto c = user.createCollection("Hits");
    user.addToCollection(c, innerKick);

    const auto merged = mergeNestedFolders(t.db);
    CHECK(merged == std::vector<std::string>{"Merged Drums into Samples, which contains it."});
    CHECK(lib.roots().size() == 1);
    CHECK(t.shown() == 3);
    CHECK(user.rating(outerKick) == 4);
    CHECK(user.isFavourite(outerKick));
    SearchModel hits;
    hits.collectionId = c;
    CHECK(countSearch(t.db, hits) == 1);
    CHECK(mergeNestedFolders(t.db).empty());
}
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[overlap]"`

Expected: the build stops:

```
tests/test_folder_overlap.cpp:4:10: fatal error: 'asma/core/Folders.h' file not found
```

- [ ] **Step 3: Implement**

Create `core/include/asma/core/Folders.h`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"
#include "asma/core/Library.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace asma {

// What adding a folder would do, given the folders already in the library.
struct AddCheck {
    enum class Result {
        New,      // added as it is
        Again,    // already a library folder: enabled again if it was removed
        Inside,   // inside an (enabled) library folder: refused
        Contains, // holds library folders: it can take their place
    };
    Result result = Result::New;
    std::vector<Root> contained; // Contains: the folders it holds, by name
    // Inside: "Drums is already in the library, inside Samples."
    // Contains: "Samples contains 2 folders already in the library (Bass,
    // Drums). Add Samples in their place?"
    std::string message;
};

AddCheck checkAddFolder(Db& db, const std::filesystem::path& dir);

// Adds a folder. One holding library folders takes their place only with
// `merge`: their samples join it with their ids, so their data stays, and a
// sample both had a row for keeps one, with the data of both. Throws
// OperationRefused when the folder is inside another, or holds some without
// `merge`.
std::int64_t addFolder(Db& db, const std::filesystem::path& dir, bool merge);

// Merges library folders nested in an enabled one into it, as addFolder
// would have: what libraries made before overlaps were refused may hold.
// Says what it merged ("Merged Drums and Bass into Samples, which contains
// them.").
std::vector<std::string> mergeNestedFolders(Db& db);

} // namespace asma
````

In `core/include/asma/core/Library.h`, apply (`git apply` takes it as is):

````diff
diff --git a/core/include/asma/core/Library.h b/core/include/asma/core/Library.h
index f50b624..9cdacb2 100644
--- a/core/include/asma/core/Library.h
+++ b/core/include/asma/core/Library.h
@@ -75,6 +75,8 @@ class Library {
 public:
     explicit Library(Db& db) : db_(db) {}
 
+    // A folder as roots store it: absolute, canonical, UTF-8 with '/'.
+    static std::string folderPath(const std::filesystem::path& dir);
     // Returns the existing id when the directory is already a root, enabling
     // it again if it was removed.
     std::int64_t addRoot(const std::filesystem::path& dir);
@@ -100,6 +102,9 @@ public:
     std::vector<FileRecord> okFilesWithContent(std::string_view contentHash, std::int64_t size);
 
     std::int64_t insertFile(const FileRecord& file);
+    // Deletes a file's row and everything it held: features, tags, rating,
+    // favourite and collection membership.
+    void removeFile(std::int64_t id);
     void updateFile(const FileRecord& file); // by file.id, every column
     void setStatus(std::int64_t fileId, FileStatus status, std::string_view reason = {});
     // Content changed: forget analysis output and queue the file again.
````

In `core/src/FileOps.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/core/src/FileOps.cpp b/core/src/FileOps.cpp
index 95bba14..38fb115 100644
--- a/core/src/FileOps.cpp
+++ b/core/src/FileOps.cpp
@@ -82,17 +82,6 @@ bool hasUserData(Db& db, std::int64_t fileId)
     return q.step() && q.getInt(0) != 0;
 }
 
-// Deletes a sample's row, its index entry and (by cascade) all it held.
-void forget(Db& db, std::int64_t fileId)
-{
-    auto fts = db.prepare("DELETE FROM fts_files WHERE rowid = ?");
-    fts.bind(1, fileId);
-    fts.run();
-    auto row = db.prepare("DELETE FROM files WHERE id = ?");
-    row.bind(1, fileId);
-    row.run();
-}
-
 bool sameFile(const fs::path& a, const fs::path& b)
 {
     std::error_code ec;
@@ -471,7 +460,7 @@ void FileOps::rollBack(std::int64_t group, const char* state, OpResult& result)
         // gives way; any other keeps its place.
         if (occupant && occupant->id != s.fileId && occupant->status == FileStatus::Missing
             && !hasUserData(db_, occupant->id)) {
-            forget(db_, occupant->id);
+            lib.removeFile(occupant->id);
         } else if (occupant && occupant->id != s.fileId) {
             result.skipped.push_back(failed + "its old place is taken");
             mark(s.id, "failed");
@@ -587,7 +576,8 @@ void FileOps::prune()
         gone.bind(1, group);
         std::vector<std::int64_t> ids;
         while (gone.step()) ids.push_back(gone.getInt(0));
-        for (const auto id : ids) forget(db_, id);
+        Library lib(db_);
+        for (const auto id : ids) lib.removeFile(id);
         auto steps = db_.prepare("DELETE FROM journal WHERE group_id = ?");
         steps.bind(1, group);
         steps.run();
````

Create `core/src/Folders.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Folders.h"

#include "asma/core/FileOps.h"
#include "asma/core/Fs.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace asma {

namespace {

std::string nameOf(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool inside(const std::string& path, const std::string& folder)
{
    const std::string prefix = folder.back() == '/' ? folder : folder + "/";
    return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
}

// "Drums", "Drums and Bass", "Bass, Drums and Keys"
std::string joined(std::vector<std::string> names)
{
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i)
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return out;
}

void run(Db& db, std::string_view sql, std::int64_t a, std::int64_t b)
{
    auto q = db.prepare(sql);
    q.bind(1, a).bind(2, b);
    q.run();
}

// Moves the inner folder's samples into the outer one and drops the inner
// folder. A sample with a row in each keeps the outer row, which takes the
// inner row's data too.
void absorb(Db& db, Library& lib, const Root& outer, const Root& inner)
{
    const std::string prefix = inner.path.substr(outer.path.size() + (outer.path.back() == '/' ? 0 : 1));
    for (auto file : lib.filesInRoot(inner.id)) {
        // A trashed row's path is its own and stays unique.
        const bool trashed = file.relPath.rfind("/trashed/", 0) == 0;
        const std::string rel = trashed ? file.relPath : prefix + "/" + file.relPath;
        if (const auto twin = lib.fileByPath(outer.id, rel)) {
            run(db, "INSERT OR IGNORE INTO ratings(file_id, rating) SELECT ?1, rating FROM ratings WHERE file_id = ?2",
                twin->id, file.id);
            run(db, "INSERT OR IGNORE INTO favourites(file_id) SELECT ?1 FROM favourites WHERE file_id = ?2", twin->id,
                file.id);
            run(db,
                "INSERT INTO file_tags(file_id, tag_id, source) SELECT ?1, tag_id, 'user' FROM file_tags "
                "WHERE file_id = ?2 AND source = 'user' ON CONFLICT(file_id, tag_id) DO UPDATE SET source = 'user'",
                twin->id, file.id);
            run(db,
                "INSERT OR IGNORE INTO collection_items(collection_id, file_id) SELECT collection_id, ?1 "
                "FROM collection_items WHERE file_id = ?2",
                twin->id, file.id);
            lib.removeFile(file.id);
            lib.updateFile(*twin); // its index entry, with the tags it took
            continue;
        }
        file.rootId = outer.id;
        file.relPath = rel;
        lib.updateFile(file);
    }
    // Undo of what moved its samples puts them back into the outer folder.
    run(db, "UPDATE journal SET root_id = ?1 WHERE root_id = ?2 AND op != 'remove_root'", outer.id, inner.id);
    auto drop = db.prepare("DELETE FROM roots WHERE id = ?");
    drop.bind(1, inner.id);
    drop.run();
}

} // namespace

AddCheck checkAddFolder(Db& db, const fs::path& dir)
{
    Library lib(db);
    const std::string path = Library::folderPath(dir);
    AddCheck check;
    std::vector<std::string> names;
    bool again = false;
    for (const auto& r : lib.roots()) {
        if (r.path == path) {
            again = true;
            continue;
        }
        if (r.enabled && inside(path, r.path)) {
            check.result = AddCheck::Result::Inside;
            check.message = nameOf(path) + " is already in the library, inside " + nameOf(r.path) + ".";
            return check;
        }
        if (inside(r.path, path)) {
            check.contained.push_back(r);
            names.push_back(nameOf(r.path));
        }
    }
    if (check.contained.empty()) {
        if (again) check.result = AddCheck::Result::Again;
        return check;
    }
    std::sort(names.begin(), names.end());
    check.result = AddCheck::Result::Contains;
    const std::string name = nameOf(path);
    if (names.size() == 1)
        check.message = name + " contains a folder already in the library (" + names[0] + "). Add " + name
                      + " in its place?";
    else {
        std::string list;
        for (std::size_t i = 0; i < names.size(); ++i) list += (i ? ", " : "") + names[i];
        check.message = name + " contains " + std::to_string(names.size()) + " folders already in the library (" + list
                      + "). Add " + name + " in their place?";
    }
    return check;
}

std::int64_t addFolder(Db& db, const fs::path& dir, bool merge)
{
    const AddCheck check = checkAddFolder(db, dir);
    if (check.result == AddCheck::Result::Inside || (check.result == AddCheck::Result::Contains && !merge))
        throw OperationRefused(check.message);
    Transaction tx(db);
    Library lib(db);
    const auto id = lib.addRoot(dir);
    const auto outer = lib.root(id);
    for (const auto& inner : check.contained) absorb(db, lib, *outer, inner);
    tx.commit();
    return id;
}

std::vector<std::string> mergeNestedFolders(Db& db)
{
    Library lib(db);
    std::vector<std::string> said;
    for (;;) {
        auto roots = lib.roots();
        // The outermost first, so a folder three deep joins the top one.
        std::sort(roots.begin(), roots.end(), [](const Root& a, const Root& b) { return a.path.size() < b.path.size(); });
        bool merged = false;
        for (const auto& outer : roots) {
            if (!outer.enabled) continue;
            std::vector<Root> nested;
            std::vector<std::string> names;
            for (const auto& r : roots)
                if (inside(r.path, outer.path)) {
                    nested.push_back(r);
                    names.push_back(nameOf(r.path));
                }
            if (nested.empty()) continue;
            Transaction tx(db);
            for (const auto& inner : nested) absorb(db, lib, outer, inner);
            tx.commit();
            said.push_back("Merged " + joined(names) + " into " + nameOf(outer.path) + ", which contains "
                           + (names.size() == 1 ? "it." : "them."));
            merged = true;
            break;
        }
        if (!merged) return said;
    }
}

} // namespace asma
````

In `core/src/Library.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/core/src/Library.cpp b/core/src/Library.cpp
index 4b7adb4..ab7d4b4 100644
--- a/core/src/Library.cpp
+++ b/core/src/Library.cpp
@@ -138,13 +138,18 @@ void bindFileColumns(Statement& s, int first, const FileRecord& f)
 
 } // namespace
 
-std::int64_t Library::addRoot(const fs::path& dir)
+std::string Library::folderPath(const fs::path& dir)
 {
     std::error_code ec;
     fs::path canonical = fs::weakly_canonical(fs::absolute(dir), ec);
     if (ec) canonical = fs::absolute(dir);
     if (!canonical.has_filename() && canonical != canonical.root_path()) canonical = canonical.parent_path();
-    const std::string path = toUtf8(canonical);
+    return toUtf8(canonical);
+}
+
+std::int64_t Library::addRoot(const fs::path& dir)
+{
+    const std::string path = folderPath(dir);
 
     auto select = db_.prepare("SELECT id FROM roots WHERE path = ?");
     select.bind(1, std::string_view(path));
@@ -279,6 +284,16 @@ std::int64_t Library::insertFile(const FileRecord& file)
     return id;
 }
 
+void Library::removeFile(std::int64_t id)
+{
+    auto fts = db_.prepare("DELETE FROM fts_files WHERE rowid = ?");
+    fts.bind(1, id);
+    fts.run();
+    auto row = db_.prepare("DELETE FROM files WHERE id = ?");
+    row.bind(1, id);
+    row.run();
+}
+
 void Library::updateFile(const FileRecord& file)
 {
     auto q = db_.prepare("UPDATE files SET root_id = ?, rel_path = ?, name = ?, size = ?, mtime = ?, "
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[overlap]"`

Expected: `All tests passed (30 assertions in 3 test cases)`

- [ ] **Step 5: Commit**

````sh
git add core/include/asma/core/Folders.h core/include/asma/core/Library.h core/src/FileOps.cpp core/src/Folders.cpp core/src/Library.cpp tests/test_folder_overlap.cpp
git commit -m "core: a folder inside a library folder is refused; one holding library folders takes their place, and nested ones merge"
````

---

### Task 5: The CLI: rename, move, trash, remove-folder, undo, history, root add --merge

Spec section 6 (CLI). Each file command takes the writer lock (exit 3 at once when another process holds it), first rolls back what an interrupted process left (saying so), and prints the group's label, or with `--json` one line. A refusal exits 1 with its reason. `root add` refuses a folder inside another and needs `--merge` for one holding some.

**Files:**

- Modify: `apps/CMakeLists.txt`
- Create: `apps/FileCommands.cpp`
- Create: `apps/FileCommands.h`
- Modify: `apps/asma_main.cpp`
- Modify: `tests/test_cli_e2e.cpp` (test)

**Interfaces:**

- Consumes: tasks 3 and 4.
- Produces: `asma rename <file>|--id N <name>`, `asma move <file>...|--id N... --to FOLDER`, `asma trash <file>...|--id N...`, `asma remove-folder <folder>`, `asma undo`, `asma history`, each with `--json`; `asma root add [--merge] <dir>`; `cmdFileOperation`, `cmdHistory`, `cmdRootAdd` in `apps/FileCommands.h`.

- [ ] **Step 1: Write the failing test**

In `tests/test_cli_e2e.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/tests/test_cli_e2e.cpp b/tests/test_cli_e2e.cpp
index 8d756c8..aea922c 100644
--- a/tests/test_cli_e2e.cpp
+++ b/tests/test_cli_e2e.cpp
@@ -2,10 +2,13 @@
 #include "TestUtil.h"
 #include "asma/audio/SampleSource.h"
 #include "asma/core/Db.h"
+#include "asma/core/FileOps.h"
+#include "asma/core/Library.h"
 #include "asma/core/Fs.h"
 #include "asma/core/WriterLock.h"
 
 #include <catch2/catch_test_macros.hpp>
+#include <algorithm>
 #include <chrono>
 #include <cstdlib>
 #include <fstream>
@@ -488,3 +491,95 @@ TEST_CASE("two repairs at once on a damaged library: one rebuilds, the other wai
     CHECK(cli.runAsma("repair").out.rfind("{\"result\":\"repaired\"", 0) == 0);
     CHECK(cli.runAsma("repair").out == "{\"result\":\"healthy\"}\n");
 }
+
+TEST_CASE("rename, move, trash, undo and history from the CLI", "[e2e][files]")
+{
+    Cli cli;
+#ifdef __linux__
+    asma::test::ScopedEnv xdg("XDG_DATA_HOME", (cli.dir.path() / "share").string().c_str());
+#endif
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+    const fs::path loop = cli.lib / "Loops" / "Bass_Loop_Am_128.wav";
+    const fs::path kick = cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav");
+    REQUIRE(cli.runAsma("rate 4 " + quote(loop)).exitCode == 0);
+
+    const RunResult renamed = cli.runAsma("rename " + quote(loop) + " Bass_Loop_Am_120.wav");
+    CHECK(renamed.exitCode == 0);
+    CHECK(renamed.out == "Rename Bass_Loop_Am_128.wav\n");
+    const fs::path loop120 = cli.lib / "Loops" / "Bass_Loop_Am_120.wav";
+    CHECK(fs::exists(loop120));
+    CHECK(cli.runAsma("query --min-rating 4").out.find("Bass_Loop_Am_120.wav") != std::string::npos);
+
+    const RunResult moved = cli.runAsma("move " + quote(loop120) + " --to " + quote(cli.lib / "Drums") + " --json");
+    CHECK(moved.exitCode == 0);
+    CHECK(moved.out.find("\"label\":\"Move Bass_Loop_Am_120.wav\"") != std::string::npos);
+    CHECK(fs::exists(cli.lib / "Drums" / "Bass_Loop_Am_120.wav"));
+    const RunResult refused = cli.runAsma("--errors-to-stdout move " + quote(kick) + " --to " + quote(cli.lib / "Drums"));
+    CHECK(refused.exitCode == 1);
+    CHECK(refused.out == "error: Kick Ü_01.wav is already in Drums.\n");
+
+    const RunResult trashed = cli.runAsma("trash " + quote(kick));
+    CHECK(trashed.exitCode == 0);
+    CHECK(trashed.out == "Move Kick Ü_01.wav to the Trash\n");
+    CHECK_FALSE(fs::exists(kick));
+    const RunResult history = cli.runAsma("history");
+    CHECK(history.out.find("done\tMove Kick Ü_01.wav to the Trash\n") != std::string::npos);
+
+    CHECK(cli.runAsma("undo").out == "Undid Move Kick Ü_01.wav to the Trash.\n");
+    CHECK(fs::exists(kick));
+    CHECK(cli.runAsma("undo").exitCode == 0);
+    CHECK(cli.runAsma("undo").out == "Undid Rename Bass_Loop_Am_128.wav.\n");
+    CHECK(fs::exists(loop));
+    CHECK(cli.runAsma("undo").out == "nothing to undo\n");
+
+    CHECK(cli.runAsma("remove-folder " + quote(cli.lib)).out == "Remove Café Samples from the Library\n");
+    CHECK(cli.runAsma("query").out.empty());
+    CHECK(cli.runAsma("undo").exitCode == 0);
+    CHECK(cli.runAsma("query").out.find("Bass_Loop_Am_128.wav") != std::string::npos);
+
+    const auto lock = asma::WriterLock::tryAcquire(cli.db.parent_path());
+    REQUIRE(lock);
+    CHECK(cli.runAsma("trash " + quote(kick)).exitCode == 3);
+    CHECK(fs::exists(kick));
+}
+
+TEST_CASE("root add refuses a folder inside one, and takes the place of those inside with --merge", "[e2e][files]")
+{
+    Cli cli;
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib / "Drums")).exitCode == 0);
+    const RunResult refused = cli.runAsma("--errors-to-stdout root add " + quote(cli.lib));
+    CHECK(refused.exitCode == 1);
+    CHECK(refused.out
+          == "error: Café Samples contains a folder already in the library (Drums). Add Café Samples in its place? "
+             "(root add --merge does)\n");
+    CHECK(cli.runAsma("root add --merge " + quote(cli.lib)).exitCode == 0);
+    const RunResult list = cli.runAsma("root list");
+    CHECK(std::count(list.out.begin(), list.out.end(), '\n') == 1);
+    CHECK(cli.runAsma("--errors-to-stdout root add " + quote(cli.lib / "Loops")).out
+          == "error: Loops is already in the library, inside Café Samples.\n");
+}
+
+TEST_CASE("a CLI write first rolls back a group an interrupted process left", "[e2e][files]")
+{
+    Cli cli;
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+    const fs::path loop = cli.lib / "Loops" / "Bass_Loop_Am_128.wav";
+    {
+        asma::Db db = asma::Db::open(cli.db);
+        asma::Library lib(db);
+        std::vector<std::int64_t> ids;
+        for (const auto& f : lib.filesInRoot(lib.roots().front().id)) ids.push_back(f.id);
+        asma::FileOps ops(db);
+        ops.crashAfter(1);
+        CHECK_THROWS_AS(ops.move(ids, cli.lib), asma::SimulatedCrash);
+    }
+    const bool loopMoved = fs::exists(cli.lib / "Bass_Loop_Am_128.wav");
+    const bool kickMoved = fs::exists(cli.lib / asma::fromUtf8("Kick Ü_01.wav"));
+    CHECK(loopMoved != kickMoved); // one step of two ran
+    CHECK(cli.runAsma("undo").out
+          == "asma was interrupted while moving 2 samples; they are back where they were.\nnothing to undo\n");
+    CHECK(fs::exists(loop));
+    CHECK(fs::exists(cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav")));
+}
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[files]"`

Expected: the commands are unknown and `root add` takes any folder, so 28 of 42 assertions fail, for example:

```
tests/test_cli_e2e.cpp:552: FAILED:
  CHECK( refused.exitCode == 1 )
with expansion:
  0 == 1
```

- [ ] **Step 3: Implement**

Replace the whole of `apps/CMakeLists.txt` with:

````cmake
# SPDX-License-Identifier: GPL-3.0-only
add_library(asma_cli_support STATIC Args.cpp CliCommon.cpp SearchArgs.cpp)
target_include_directories(asma_cli_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asma_cli_support PUBLIC asma::core)
if(WIN32)
  target_link_libraries(asma_cli_support PUBLIC shell32)
endif()
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp FileCommands.cpp OrganiseCommands.cpp)
target_link_libraries(asma PRIVATE asma_cli_support asma::audio)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)

add_executable(asma-scan asma_scan_main.cpp)
target_link_libraries(asma-scan PRIVATE asma_cli_support)
asma_set_warnings(asma-scan)
````

Create `apps/FileCommands.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "FileCommands.h"

#include "CliCommon.h"
#include "SearchArgs.h"

#include "asma/core/FileOps.h"
#include "asma/core/Folders.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/UserData.h"
#include "asma/core/WriterLock.h"

#include <iostream>

namespace asma::cli {

namespace {

std::int64_t fileAt(Library& lib, const std::string& path)
{
    const auto file = lib.fileByAbsolutePath(fromUtf8(path));
    if (!file) throw UserDataError("not in the library: " + path);
    return file->id;
}

// The --id values, then every path left.
std::vector<std::int64_t> files(Args& args, Db& db, const std::vector<std::string>& idOptions)
{
    std::vector<std::int64_t> ids;
    for (const auto& id : idOptions) ids.push_back(static_cast<std::int64_t>(toDouble(id, "--id")));
    Library lib(db);
    while (const auto path = args.positional()) ids.push_back(fileAt(lib, *path));
    rejectLeftovers(args);
    if (ids.empty()) throw UsageError("name at least one file, by path or --id N");
    return ids;
}

void print(const OpResult& r, bool json)
{
    if (!json) {
        std::cout << r.label << "\n";
        return;
    }
    std::string list;
    for (const auto id : r.files) list += (list.empty() ? "" : ",") + std::to_string(id);
    std::string line = JsonLine().num("group", r.group).str("label", r.label).build();
    line.pop_back(); // the closing brace: the files go in before it
    std::cout << line << ",\"files\":[" << list << "]}\n";
}

} // namespace

int cmdFileOperation(const std::string& command, Args& args, Db& db, const std::filesystem::path& dbPath)
{
    const bool json = args.flag("json");
    const auto idOptions = args.options("id");
    const auto to = args.option("to");

    const auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        std::cerr << "asma: another asma process is writing to this library\n";
        return kLocked;
    }
    FileOps ops(db);
    for (const auto& r : ops.recover()) {
        if (json) std::cout << JsonLine().str("interrupted", interruptedText(r)).build() << "\n";
        else std::cout << interruptedText(r) << "\n";
    }

    if (command == "undo") {
        rejectLeftovers(args);
        const auto r = ops.undo();
        if (json) {
            JsonLine line;
            if (r) line.str("undone", r->label).strings("skipped", r->skipped);
            else line.null("undone");
            std::cout << line.build() << "\n";
        } else {
            std::cout << (r ? undoneText(*r) : "nothing to undo") << "\n";
        }
        return kOk;
    }
    if (command == "rename") {
        std::int64_t id = 0;
        if (!idOptions.empty()) id = static_cast<std::int64_t>(toDouble(idOptions.front(), "--id"));
        else if (const auto path = args.positional()) {
            Library lib(db);
            id = fileAt(lib, *path);
        }
        const auto name = args.positional();
        rejectLeftovers(args);
        if (!id || !name) throw UsageError("rename needs a file (or --id N) and its new name");
        print(ops.rename(id, *name), json);
        return kOk;
    }
    if (command == "move") {
        if (!to) throw UsageError("move needs --to FOLDER");
        print(ops.move(files(args, db, idOptions), fromUtf8(*to)), json);
        return kOk;
    }
    if (command == "trash") {
        print(ops.trash(files(args, db, idOptions)), json);
        return kOk;
    }
    // remove-folder
    const auto folder = args.positional();
    rejectLeftovers(args);
    if (!folder) throw UsageError("remove-folder needs a folder");
    Library lib(db);
    const auto place = lib.rootOf(fromUtf8(*folder));
    if (!place || !place->second.empty()) throw UserDataError("not a library folder: " + *folder);
    print(ops.removeFolder(place->first.id), json);
    return kOk;
}

int cmdHistory(Args& args, Db& db)
{
    const bool json = args.flag("json");
    rejectLeftovers(args);
    for (const auto& g : FileOps(db).history()) {
        if (json)
            std::cout << JsonLine().num("group", g.id).str("at", g.at).str("state", g.state).str("label", g.label).build()
                      << "\n";
        else
            std::cout << g.id << "\t" << g.at << "\t" << g.state << "\t" << g.label << "\n";
    }
    return kOk;
}

int cmdRootAdd(Args& args, Db& db)
{
    const bool merge = args.flag("merge");
    const auto dir = args.positional();
    if (!dir) throw UsageError("root add needs a directory");
    rejectLeftovers(args);
    const AddCheck check = checkAddFolder(db, fromUtf8(*dir));
    if (check.result == AddCheck::Result::Contains && !merge)
        throw OperationRefused(check.message + " (root add --merge does)");
    const auto id = addFolder(db, fromUtf8(*dir), merge);
    std::cout << "root " << id << " " << Library(db).root(id)->path << "\n";
    return kOk;
}

} // namespace asma::cli
````

Create `apps/FileCommands.h`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"
#include "asma/core/Db.h"

#include <filesystem>

namespace asma::cli {

// File operations: rename, move, trash, remove-folder and undo, each under
// the writer lock (exit 3 when another process holds it), each first rolling
// back what an interrupted process left. Each prints the group's label, or
// with --json one line: {"group":N,"label":"...","files":[...]}.
int cmdFileOperation(const std::string& command, Args& args, Db& db, const std::filesystem::path& dbPath);
// The newest groups: "id<TAB>at<TAB>state<TAB>label", or JSON lines.
int cmdHistory(Args& args, Db& db);
// root add, refusing a folder inside one and merging those inside it with --merge.
int cmdRootAdd(Args& args, Db& db);

} // namespace asma::cli
````

In `apps/asma_main.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/apps/asma_main.cpp b/apps/asma_main.cpp
index af5f074..d0094ca 100644
--- a/apps/asma_main.cpp
+++ b/apps/asma_main.cpp
@@ -1,6 +1,7 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #include "Args.h"
 #include "CliCommon.h"
+#include "FileCommands.h"
 #include "OrganiseCommands.h"
 #include "SearchArgs.h"
 
@@ -33,7 +34,7 @@ namespace {
 
 constexpr const char* kUsageText =
     "usage: asma [--db PATH] [--errors-to-stdout] <command>\n"
-    "  root add <dir>          add a sample folder\n"
+    "  root add [--merge] <dir>  add a sample folder (--merge: in place of those inside it)\n"
     "  root list               list sample folders\n"
     "  scan [--root ID] [--threads N] [--no-analysis]\n"
     "  query [words...] [--saved NAME] [--type loop|oneshot|any] [--bpm N|MIN-MAX]\n"
@@ -53,6 +54,12 @@ constexpr const char* kUsageText =
     "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--renders DIR]\n"
     "                          print the file to drag, rendering edits if any\n"
     "  renders [clear] [--renders DIR]   size of the kept renders, or delete them\n"
+    "  rename <file> | --id N <new name> [--json]\n"
+    "  move <file>... | --id N... --to FOLDER [--json]\n"
+    "  trash <file>... | --id N... [--json]   to the system's trash\n"
+    "  remove-folder <folder> [--json]   take a folder out of the library (its data is kept)\n"
+    "  undo [--json]           undo the last file operation\n"
+    "  history [--json]        the last file operations\n"
     "  check                   is the library sound? (JSON)\n"
     "  backup [--out FILE]     write the user data beside the library, or to FILE\n"
     "  restore FILE            give the library a backup's user data\n"
@@ -101,14 +108,7 @@ int cmdRoot(Args& args, Db& db)
 {
     Library lib(db);
     const auto sub = args.positional();
-    if (sub == "add") {
-        const auto dir = args.positional();
-        if (!dir) throw UsageError("root add needs a directory");
-        rejectLeftovers(args);
-        const auto id = lib.addRoot(fromUtf8(*dir));
-        std::cout << "root " << id << " " << lib.root(id)->path << "\n";
-        return kOk;
-    }
+    if (sub == "add") return cmdRootAdd(args, db);
     if (sub == "list") {
         rejectLeftovers(args);
         for (const auto& r : lib.roots()) std::cout << r.id << "\t" << r.path << "\t" << (r.enabled ? "on" : "off") << "\n";
@@ -381,6 +381,7 @@ int main(int argc, char** argv)
             {"render", cmdRender},
             {"renders", cmdRenders},
             {"restore", cmdRestore},
+            {"history", cmdHistory},
         };
         if (*command == "scan") {
             Db db = Db::open(dbPath);
@@ -394,6 +395,11 @@ int main(int argc, char** argv)
             Db db = Db::open(dbPath);
             return cmdBackup(args, db, dbPath);
         }
+        for (const char* op : {"rename", "move", "trash", "remove-folder", "undo"}) {
+            if (*command != op) continue;
+            Db db = Db::open(dbPath);
+            return cmdFileOperation(*command, args, db, dbPath);
+        }
         if (*command == "retry") {
             Db db = Db::open(dbPath);
             return cmdRetry(args, db, dbPath);
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_tests && ./build/tests/asma_tests "[files]"`

Expected: `All tests passed (42 assertions in 3 test cases)` (the lock test prints "asma: another asma process is writing to this library").

- [ ] **Step 5: Commit**

````sh
git add apps/CMakeLists.txt apps/FileCommands.cpp apps/FileCommands.h apps/asma_main.cpp tests/test_cli_e2e.cpp
git commit -m "cli: rename, move, trash, remove-folder, undo and history; root add refuses overlaps and merges with --merge"
````

---

### Task 6: The standalone's file operations run one at a time off the message thread

Spec section 6 (In the standalone, Watching and scans). `FileOpsJob` queues requests on its own thread, takes the writer lock for each (waiting up to 30 seconds for a scan, then "The library is busy scanning; try again in a moment."), runs it, and posts the outcome to the message thread: the footer's words, counted like the keeper's, and the samples to select. Its first request rolls back what an interrupted asma left and merges nested folders. It keeps what undo would undo, for the Edit menu.

**Files:**

- Create: `plugin/src/FileOpsJob.cpp`
- Create: `plugin/src/FileOpsJob.h`
- Create: `tests/plugin/test_file_ops_job.cpp` (test)

**Interfaces:**

- Consumes: tasks 3 and 4.
- Produces: `struct FileRequest` (`rename`, `move`, `trash`, `removeFolder`, `undo`, `addFolder(folder, merge)`); `struct FileOutcome { bool done; std::string text; std::vector<std::int64_t> files; }`; `class FileOpsJob` (`FileOpsJob(path dbPath, TrashBackend = system(), std::chrono::milliseconds lockWait = 30s)`, `run(FileRequest, Done)`, `idle()`, `undoLabel()`, `messageCount()`, `message()`, `static undoKey()`); `std::string doneText(const FileRequest&, const OpResult&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_file_ops_job.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "FileOpsJob.h"
#include "LibraryFixture.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_events/juce_events.h>

#include <chrono>

using namespace asma;
namespace fs = std::filesystem;
using app::FileOpsJob;
using app::FileOutcome;
using app::FileRequest;

namespace {

void settle(FileOpsJob& job)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!job.idle() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

// A trash in a folder of its own.
TrashBackend folderTrash(const fs::path& dir)
{
    fs::create_directories(dir);
    TrashBackend t;
    t.available = [](const fs::path&) { return true; };
    t.move = [dir](const fs::path& file) {
        TrashResult r;
        r.where = dir / file.filename();
        r.ok = !renameNoReplace(file, r.where);
        return r;
    };
    t.restore = [](const fs::path& where, const fs::path& to) {
        return renameNoReplace(where, to) ? std::string("its old place is taken") : std::string();
    };
    return t;
}

std::int64_t idOf(const fs::path& db, const fs::path& file)
{
    Db d = Db::open(db);
    return Library(d).fileByAbsolutePath(file)->id;
}

} // namespace

TEST_CASE("The file operations job trashes and undoes off the message thread", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"));
    settle(job);
    CHECK(job.messageCount() == 0); // a clean start says nothing
    CHECK(job.undoLabel().empty());

    const auto kick = idOf(f.dbPath, f.kick);
    FileOutcome got;
    job.run(FileRequest::trash({kick}), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.done);
    CHECK(got.text == "Moved Kick_01.wav to the Trash. " + FileOpsJob::undoKey() + " to undo.");
    CHECK(got.files == std::vector<std::int64_t>{kick});
    CHECK(job.message() == got.text);
    CHECK(job.messageCount() == 1);
    CHECK_FALSE(fs::exists(f.kick));
    CHECK(job.undoLabel() == "Move Kick_01.wav to the Trash");

    job.run(FileRequest::undo(), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.text == "Undid Move Kick_01.wav to the Trash.");
    CHECK(got.files == std::vector<std::int64_t>{kick});
    CHECK(fs::exists(f.kick));
    CHECK(job.undoLabel().empty());

    job.run(FileRequest::move({kick}, f.lib / "Drums"), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK_FALSE(got.done);
    CHECK(got.text == "Kick_01.wav is already in Drums.");
}

TEST_CASE("The file operations job waits for the writer lock, then gives up", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    auto lock = WriterLock::tryAcquire(f.dbPath.parent_path());
    REQUIRE(lock);
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"), std::chrono::milliseconds(300));
    FileOutcome got;
    job.run(FileRequest::trash({idOf(f.dbPath, f.kick)}), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK_FALSE(got.done);
    CHECK(got.text == "The library is busy scanning; try again in a moment.");
    CHECK(fs::exists(f.kick));
}

TEST_CASE("The file operations job starts by rolling back and merging, and says so", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        Library lib(db);
        lib.addRoot(f.lib / "Drums"); // nested, as older versions allowed
        FileOps ops(db, folderTrash(f.dir.path() / "Trash"));
        ops.crashAfter(1);
        CHECK_THROWS_AS(ops.trash({lib.fileByAbsolutePath(f.loop)->id, lib.fileByAbsolutePath(f.kick)->id}),
                        SimulatedCrash);
    }
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"));
    settle(job);
    CHECK(job.messageCount() == 1);
    CHECK(job.message()
          == "asma was interrupted while moving 2 samples to the Trash; they are back where they were. "
             "Merged Drums into Samples, which contains it.");
    CHECK(fs::exists(f.loop));
}

TEST_CASE("The file operations job adds a folder in place of those inside it", "[fileopsjob]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        Library lib(db);
        // Samples was removed, then two folders inside it were added.
        lib.setRootEnabled(lib.roots().front().id, false);
        lib.addRoot(f.lib / "Drums");
        lib.addRoot(f.lib / "Loops");
    }
    FileOpsJob job(f.dbPath, folderTrash(f.dir.path() / "Trash"));
    settle(job);
    FileOutcome got;
    job.run(FileRequest::addFolder(f.lib, true), [&](const FileOutcome& o) { got = o; });
    settle(job);
    CHECK(got.done);
    CHECK(got.text == "Added Samples in place of Drums and Loops.");
    Db db = Db::open(f.dbPath);
    CHECK(Library(db).roots().size() == 1);
}
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fileopsjob]"`

Expected: the build stops:

```
tests/plugin/test_file_ops_job.cpp:2:10: fatal error: 'FileOpsJob.h' file not found
```

- [ ] **Step 3: Implement**

Create `plugin/src/FileOpsJob.cpp`:

````cpp
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
    try {
        Db db = Db::open(dbPath_);
        FileOps ops(db, trash_);
        switch (request.kind) {
        case Kind::Startup: {
            std::string said;
            for (const auto& r : ops.recover()) said += (said.empty() ? "" : " ") + interruptedText(r);
            for (const auto& m : mergeNestedFolders(db)) said += (said.empty() ? "" : " ") + m;
            outcome.text = said;
            break;
        }
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
        const auto next = ops.undoable();
        const std::lock_guard guard(mutex_);
        undoLabel_ = next ? next->label : std::string();
    } catch (const OperationRefused& e) {
        outcome.text = e.what();
    } catch (const std::exception& e) {
        outcome.text = std::string("Could not do that: ") + e.what();
    }
    return outcome;
}

} // namespace asma::app
````

Create `plugin/src/FileOpsJob.h`:

````cpp
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
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fileopsjob]"`

Expected: `All tests passed (26 assertions in 4 test cases)`

- [ ] **Step 5: Commit**

````sh
git add plugin/src/FileOpsJob.cpp plugin/src/FileOpsJob.h tests/plugin/test_file_ops_job.cpp
git commit -m "app: the standalone's file operations run one at a time off the message thread, under the writer lock, starting with recovery"
````

---

### Task 7: Rename, Move to, Move to Trash and undo in the standalone; Remove from Library; the merge question

Spec section 6 (In the standalone). The processor owns the job in the standalone (none in a plugin). The row menu gains Rename…, Move to… and Move to Trash; F2 renames, Delete or Backspace trashes, Cmd/Ctrl+Z undoes, outside text fields. The rename popover refuses as you type (a taken name, a character no system takes, another extension). After a trash the next row is selected, after an undo the sample brought back; renamed and moved samples stay selected. The sidebar's folder menu gains Remove from Library. Adding a folder checks for overlaps first: inside one is refused in the footer, holding some asks. On macOS the Edit menu names what undo would undo. `LibraryKeeper::addFolder` goes: adding goes through the job.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/AsmaProcessor.cpp`
- Modify: `plugin/src/AsmaProcessor.h`
- Modify: `plugin/src/LibraryKeeper.cpp`
- Modify: `plugin/src/LibraryKeeper.h`
- Create: `plugin/src/ui/EditMenu.cpp`
- Create: `plugin/src/ui/EditMenu.h`
- Modify: `plugin/src/ui/SidebarView.cpp`
- Modify: `plugin/src/ui/SidebarView.h`
- Modify: `tests/plugin/EditorRig.h` (test)
- Create: `tests/plugin/test_file_manager.cpp` (test)
- Modify: `tests/plugin/test_organise.cpp` (test)

**Interfaces:**

- Consumes: task 6.
- Produces: `AsmaProcessor::fileOps()` (null in a plugin), `AsmaProcessor::setTrash(TrashBackend)`; `AsmaEditor::renamePopover(row)`, `moveSample(row, folder)`, `trashSample(row)`, `undoFileOperation()`, `confirmMerge`, `RowMenuItem::{kRename, kMoveTo, kTrash}`; `SidebarView::onRemoveFolder`, `SidebarView::kRemoveFolder`; `class EditMenu : juce::MenuBarModel`; `EditorRig(mode, setUp)`. Removed: `LibraryKeeper::addFolder`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/EditorRig.h`, apply (`git apply` takes it as is):

````diff
diff --git a/tests/plugin/EditorRig.h b/tests/plugin/EditorRig.h
index 3387f2b..6cf327b 100644
--- a/tests/plugin/EditorRig.h
+++ b/tests/plugin/EditorRig.h
@@ -10,6 +10,7 @@
 #include <juce_gui_basics/juce_gui_basics.h>
 
 #include <chrono>
+#include <functional>
 #include <memory>
 #include <thread>
 
@@ -20,10 +21,13 @@ struct EditorRig {
     const juce::ScopedJuceInitialiser_GUI gui;
     std::unique_ptr<app::AsmaProcessor> p;
     std::unique_ptr<app::AsmaEditor> editor;
-    explicit EditorRig(app::AsmaProcessor::Mode mode = app::AsmaProcessor::Mode::FromWrapper)
+    // `setUp` sees the processor before the window opens.
+    explicit EditorRig(app::AsmaProcessor::Mode mode = app::AsmaProcessor::Mode::FromWrapper,
+                       const std::function<void(app::AsmaProcessor&)>& setUp = {})
     {
         f.scan();
         p = std::make_unique<app::AsmaProcessor>(mode);
+        if (setUp) setUp(*p);
         p->prepareToPlay(48000.0, 512);
         editor.reset(dynamic_cast<app::AsmaEditor*>(p->createEditorAndMakeActive()));
         REQUIRE(editor);
````

Create `tests/plugin/test_file_manager.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "FileOpsJob.h"
#include "ui/EditMenu.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::app;
using asma::test::EditorRig;
namespace fs = std::filesystem;

namespace {

// A trash in a folder of its own: the tests never touch the system's.
TrashBackend folderTrash(const fs::path& dir)
{
    fs::create_directories(dir);
    TrashBackend t;
    t.available = [](const fs::path&) { return true; };
    t.move = [dir](const fs::path& file) {
        TrashResult r;
        r.where = dir / file.filename();
        r.ok = !renameNoReplace(file, r.where);
        return r;
    };
    t.restore = [](const fs::path& where, const fs::path& to) {
        return renameNoReplace(where, to) ? std::string("its old place is taken") : std::string();
    };
    return t;
}

// The standalone over the fixture's library, with a trash of its own.
struct App : EditorRig {
    App()
        : EditorRig(AsmaProcessor::Mode::Standalone,
                    [this](AsmaProcessor& processor) { processor.setTrash(folderTrash(f.dir.path() / "Trash")); })
    {
        settle();
    }
    // Until the file operations are done and the window has looked.
    void settle()
    {
        auto* job = p->fileOps();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (job && !job->idle() && std::chrono::steady_clock::now() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        editor->poll();
    }
    // Selects the row showing this file name.
    SearchRow select(const std::string& name)
    {
        for (int i = 0; i < editor->table().getNumRows(); ++i)
            if (editor->shownRow(i)->name == name) {
                editor->table().selectRow(i);
                return *editor->shownRow(i);
            }
        FAIL("no row " << name);
        return {};
    }
    std::string selectedName()
    {
        const auto row = editor->shownRow(editor->table().getSelectedRow());
        return row ? row->name : std::string();
    }
    juce::String status() { return editor->footer().rightText(); }
};

std::vector<std::string> items(const juce::PopupMenu& menu)
{
    std::vector<std::string> out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (!it.getItem().isSeparator) out.push_back(it.getItem().text.toStdString());
    return out;
}

int folderEntry(EditorRig& rig)
{
    for (int i = 0;; ++i) {
        const auto menu = rig.editor->sidebar().entryMenu(i);
        if (i > 20) return -1;
        if (!items(menu).empty() && items(menu).front() == "Remove from Library") return i;
    }
}

const std::string undoHint = " " + FileOpsJob::undoKey() + " to undo.";

} // namespace

TEST_CASE("A row's menu offers the file operations in the standalone only", "[filemanager]")
{
    EditorRig plugin;
    const SearchRow row = *plugin.editor->shownRow(0);
    const auto pluginItems = items(plugin.editor->rowMenu(row));
    CHECK(std::find(pluginItems.begin(), pluginItems.end(), "Move to Trash") == pluginItems.end());
    CHECK(plugin.p->fileOps() == nullptr);
    CHECK_FALSE(plugin.editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));

    App app;
    const auto appItems = items(app.editor->rowMenu(*app.editor->shownRow(0)));
    CHECK(std::vector<std::string>(appItems.end() - 3, appItems.end())
          == std::vector<std::string>{"Rename…", "Move to…", "Move to Trash"});
}

TEST_CASE("Renaming from the popover renames the sample, which stays selected", "[filemanager]")
{
    App app;
    const SearchRow kick = app.select("Kick_01.wav");
    auto popover = app.editor->renamePopover(kick);
    CHECK(popover->field().getText() == "Kick_01.wav");
    popover->field().setText("Snare_02.wav", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(popover->refusalText() == "Snare_02.wav already exists in Drums.");
    popover->field().setText("Kick_01.aif", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(popover->refusalText() == "A rename keeps the extension: .wav.");
    popover->field().setText("Kick_09.wav", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(popover->refusalText().isEmpty());
    popover->saveButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    app.settle();
    CHECK(fs::exists(app.f.lib / "Drums" / "Kick_09.wav"));
    CHECK(app.selectedName() == "Kick_09.wav");
    CHECK(app.status().contains("Renamed Kick_01.wav to Kick_09.wav." + undoHint));
}

TEST_CASE("Delete trashes the selected sample, the next is selected, and undo brings it back", "[filemanager]")
{
    App app;
    app.select("Kick_01.wav");
    CHECK(app.editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    app.settle();
    CHECK_FALSE(fs::exists(app.f.kick));
    CHECK(app.editor->table().getNumRows() == 2);
    CHECK(app.selectedName() == "Snare_02.wav");
    CHECK(app.status().contains("Moved Kick_01.wav to the Trash." + undoHint));
    CHECK(app.p->fileOps()->undoLabel() == "Move Kick_01.wav to the Trash");

    CHECK(app.editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    app.settle();
    CHECK(fs::exists(app.f.kick));
    CHECK(app.editor->table().getNumRows() == 3);
    CHECK(app.selectedName() == "Kick_01.wav");
    CHECK(app.status().contains("Undid Move Kick_01.wav to the Trash."));
    CHECK(app.p->fileOps()->undoLabel().empty());
}

TEST_CASE("Move to puts the sample in the folder chosen, or says why not", "[filemanager]")
{
    App app;
    const SearchRow kick = app.select("Kick_01.wav");
    app.editor->moveSample(kick, app.f.lib / "Loops");
    app.settle();
    CHECK(fs::exists(app.f.lib / "Loops" / "Kick_01.wav"));
    CHECK(app.status().contains("Moved Kick_01.wav to Loops." + undoHint));
    CHECK(app.selectedName() == "Kick_01.wav");
    app.editor->moveSample(*app.editor->shownRow(app.editor->table().getSelectedRow()), app.f.dir.path());
    app.settle();
    CHECK(app.status().contains("That folder is outside the library's folders."));
}

TEST_CASE("Remove from Library takes a folder out, and undo puts it back", "[filemanager]")
{
    EditorRig plugin;
    CHECK(folderEntry(plugin) == -1);

    App app;
    const int folder = folderEntry(app);
    REQUIRE(folder >= 0);
    app.editor->sidebar().entryMenuChosen(folder, SidebarView::kRemoveFolder);
    app.settle();
    CHECK(app.editor->table().getNumRows() == 0);
    CHECK(app.status().contains("Removed Samples from the library." + undoHint));
    CHECK(fs::exists(app.f.kick));
    CHECK(app.editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    app.settle();
    CHECK(app.editor->table().getNumRows() == 3);
}

TEST_CASE("Adding a folder inside one is refused; one holding some asks first", "[filemanager]")
{
    App app;
    app.editor->addFolder(app.f.lib / "Drums");
    app.settle();
    CHECK(app.status().contains("Drums is already in the library, inside Samples."));

    juce::String asked;
    app.editor->confirmMerge = [&](const juce::String& question, std::function<void(bool)> answer) {
        asked = question;
        answer(true);
    };
    const auto outer = app.f.dir.path();
    const std::string name = toUtf8(outer.filename());
    app.editor->addFolder(outer);
    app.settle();
    CHECK(asked.toStdString()
          == name + " contains a folder already in the library (Samples). Add " + name + " in its place?");
    CHECK(app.status().contains("Added " + name + " in place of Samples."));
    Db db = Db::open(app.f.dbPath);
    REQUIRE(Library(db).roots().size() == 1);
    CHECK(Library(db).roots().front().path == Library::folderPath(outer));
}

TEST_CASE("The Edit menu names what undo would undo", "[filemanager]")
{
    std::string label = "Move Kick_01.wav to the Trash";
    int undone = 0;
    EditMenu menu([&] { return label; }, [&] { ++undone; });
    CHECK(menu.getMenuBarNames() == juce::StringArray{"Edit"});
    auto edit = menu.getMenuForIndex(0, "Edit");
    juce::PopupMenu::MenuItemIterator it(edit);
    REQUIRE(it.next());
    CHECK(it.getItem().text == "Undo Move Kick_01.wav to the Trash");
    CHECK(it.getItem().isEnabled);
    menu.menuItemSelected(it.getItem().itemID, 0);
    CHECK(undone == 1);
    label.clear();
    edit = menu.getMenuForIndex(0, "Edit");
    juce::PopupMenu::MenuItemIterator again(edit);
    REQUIRE(again.next());
    CHECK(again.getItem().text == "Undo");
    CHECK_FALSE(again.getItem().isEnabled);
}
````

In `tests/plugin/test_organise.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/tests/plugin/test_organise.cpp b/tests/plugin/test_organise.cpp
index 2fd6381..5a2125a 100644
--- a/tests/plugin/test_organise.cpp
+++ b/tests/plugin/test_organise.cpp
@@ -316,7 +316,8 @@ TEST_CASE("a row's menu ticks the collections it is in, and picking one adds or
     const SearchRow kick = *rig.editor->shownRow(0);
     const auto menu = rig.editor->rowMenu(kick);
     CHECK(topItems(menu) == std::vector<std::string>{"Add to collection", "Tags\u2026",
-                                                      AsmaEditor::revealText().toStdString()});
+                                                      AsmaEditor::revealText().toStdString(), "Rename\u2026",
+                                                      "Move to\u2026", "Move to Trash"});
     CHECK(collectionItems(menu) == std::vector<std::string>{"Album", "+Live set", "New collection\u2026"});
 
     rig.editor->rowMenuChosen(kick, itemId(menu, "Album"));
````

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[filemanager],[organise]"`

Expected: the build stops:

```
tests/plugin/test_file_manager.cpp:4:10: fatal error: 'ui/EditMenu.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 60e33a8..6afe2a6 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -5,6 +5,8 @@
 #include "DragOut.h"
 #include "TempoChip.h"
 #include "asma/audio/Render.h"
+#include "asma/core/FileOps.h"
+#include "asma/core/Folders.h"
 #include "asma/core/Fs.h"
 #include "ui/TableRows.h"
 #include "ui/Theme.h"
@@ -89,6 +91,28 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
         top_.linkChip().onClick = [this] { processor_.setLinkEnabled(top_.linkChip().getToggleState()); };
         top_.addFolderButton().onClick = [this] { chooseFolder(); };
         emptyAddFolder_.onClick = [this] { chooseFolder(); };
+        sidebar_.onRemoveFolder = [this](int index) {
+            if (index < 0 || index >= static_cast<int>(entries_.size())) return;
+            const auto& entry = entries_[static_cast<std::size_t>(index)];
+            if (entry.kind == EntryKind::Folder) runFileOperation(FileRequest::removeFolder(entry.id));
+        };
+        confirmMerge = [this](const juce::String& question, std::function<void(bool)> answer) {
+            juce::NativeMessageBox::showOkCancelBox(
+                juce::MessageBoxIconType::QuestionIcon, "Add folder", question, this,
+                juce::ModalCallbackFunction::create([answer](int ok) { answer(ok != 0); }));
+        };
+#if JUCE_MAC
+        // Only the app has a menu bar to put it in; the tests have no app.
+        if (juce::JUCEApplicationBase::getInstance() != nullptr) {
+            editMenu_ = std::make_unique<EditMenu>(
+                [this] {
+                    auto* job = processor_.fileOps();
+                    return job ? job->undoLabel() : std::string();
+                },
+                [this] { undoFileOperation(); });
+            juce::MenuBarModel::setMacMainMenu(editMenu_.get());
+        }
+#endif
     }
 
     auto& header = table_.getHeader();
@@ -171,6 +195,9 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
 AsmaEditor::~AsmaEditor()
 {
     stopTimer();
+#if JUCE_MAC
+    if (editMenu_) juce::MenuBarModel::setMacMainMenu(nullptr);
+#endif
     setLookAndFeel(nullptr);
 }
 
@@ -262,6 +289,25 @@ bool AsmaEditor::keyPressed(const juce::KeyPress& key)
     // in it; this is for keys that reach the window from anywhere else.
     const int c = key.getTextCharacter() != 0 ? static_cast<int>(key.getTextCharacter()) : key.getKeyCode();
     const bool typing = dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
+    // The standalone's file operations: Cmd/Ctrl+Z undoes the last, F2
+    // renames the selection, Delete or Backspace trashes it.
+    if (!typing && processor_.fileOps()) {
+        const auto mods = key.getModifiers();
+        if ((c == 'z' || c == 'Z') && mods.isCommandDown() && !mods.isShiftDown()) {
+            undoFileOperation();
+            return true;
+        }
+        if (selected_ && !mods.isCommandDown()) {
+            if (key.getKeyCode() == juce::KeyPress::F2Key) {
+                showRename(*selected_);
+                return true;
+            }
+            if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) {
+                trashSample(*selected_);
+                return true;
+            }
+        }
+    }
     if (!typing && selected_ && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown()) {
         const auto row = pending_.apply(*selected_);
         if (c == 'f' || c == 'F') {
@@ -438,6 +484,12 @@ juce::PopupMenu AsmaEditor::rowMenu(const SearchRow& row)
     menu.addItem(kEditTags, juce::String::fromUTF8("Tags…"));
     menu.addSeparator();
     menu.addItem(kReveal, revealText());
+    if (processor_.fileOps()) {
+        menu.addSeparator();
+        menu.addItem(kRename, juce::String::fromUTF8("Rename…"));
+        menu.addItem(kMoveTo, juce::String::fromUTF8("Move to…"));
+        menu.addItem(kTrash, "Move to Trash");
+    }
     return menu;
 }
 
@@ -452,6 +504,12 @@ void AsmaEditor::rowMenuChosen(const SearchRow& row, int result)
         juce::CallOutBox::launchAsynchronously(tagsPopover(row), area, this);
     } else if (result == kReveal) {
         juce::File(utf8(toUtf8(LibraryView::pathOf(row)))).revealToUser();
+    } else if (result == kRename) {
+        showRename(row);
+    } else if (result == kMoveTo) {
+        chooseDestination(row);
+    } else if (result == kTrash) {
+        trashSample(row);
     } else if (result >= kFirstCollection) {
         const auto index = static_cast<std::size_t>(result - kFirstCollection);
         if (index >= menuCollections_.size()) return;
@@ -476,6 +534,92 @@ std::unique_ptr<TagsPopover> AsmaEditor::tagsPopover(const SearchRow& row)
                                          });
 }
 
+std::unique_ptr<NamePopover> AsmaEditor::renamePopover(const SearchRow& row)
+{
+    const auto folder = LibraryView::pathOf(row).parent_path();
+    auto refusal = [current = row.name, folder](const juce::String& text) -> std::optional<juce::String> {
+        const std::string name = text.toStdString();
+        if (name.empty() || name == current) return juce::String(); // nothing to say yet
+        if (const auto problem = renameProblem(current, name)) return utf8(*problem);
+        std::error_code ec;
+        const bool caseOnly = juce::String(name).equalsIgnoreCase(juce::String(current));
+        if (!caseOnly && std::filesystem::exists(std::filesystem::symlink_status(folder / fromUtf8(name), ec)))
+            return utf8(name + " already exists in " + toUtf8(folder.filename()) + ".");
+        return std::nullopt;
+    };
+    auto popover = std::make_unique<NamePopover>("Rename", utf8(row.name), std::move(refusal),
+                                                 [this, id = row.id](const juce::String& name) {
+                                                     runFileOperation(FileRequest::rename(id, name.toStdString()));
+                                                 });
+    // The name selected up to its extension, ready to type over.
+    const auto dot = row.name.rfind('.');
+    const int stem = static_cast<int>(utf8(row.name.substr(0, dot == std::string::npos ? row.name.size() : dot)).length());
+    popover->field().setHighlightedRegion({0, stem});
+    return popover;
+}
+
+void AsmaEditor::showRename(const SearchRow& row)
+{
+    const int at = browser_.rowOf(LibraryView::pathOf(row));
+    const auto area = at >= 0 ? getLocalArea(&table_, table_.getRowPosition(at, true)) : table_.getBounds();
+    juce::CallOutBox::launchAsynchronously(renamePopover(row), area, this);
+}
+
+void AsmaEditor::chooseDestination(const SearchRow& row)
+{
+    chooser_ = std::make_unique<juce::FileChooser>("Move to",
+                                                   juce::File(utf8(toUtf8(LibraryView::pathOf(row).parent_path()))));
+    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
+                          [this, row](const juce::FileChooser& chooser) {
+                              const juce::File folder = chooser.getResult();
+                              if (folder != juce::File()) moveSample(row, fromUtf8(folder.getFullPathName().toStdString()));
+                          });
+}
+
+void AsmaEditor::moveSample(const SearchRow& row, const std::filesystem::path& folder)
+{
+    runFileOperation(FileRequest::move({row.id}, folder));
+}
+
+void AsmaEditor::trashSample(const SearchRow& row)
+{
+    runFileOperation(FileRequest::trash({row.id}), browser_.rowOf(LibraryView::pathOf(row)));
+}
+
+void AsmaEditor::undoFileOperation() { runFileOperation(FileRequest::undo()); }
+
+void AsmaEditor::runFileOperation(FileRequest request, int selectRowAfter)
+{
+    auto* job = processor_.fileOps();
+    if (!job) return;
+    const auto kind = request.kind;
+    job->run(std::move(request), [safe = juce::Component::SafePointer<AsmaEditor>(this), kind,
+                                  selectRowAfter](const FileOutcome& outcome) {
+        if (!safe) return;
+        safe->poll(); // the rows follow the library before anything is selected
+        if (safe->editMenu_) safe->editMenu_->menuItemsChanged();
+        if (!outcome.done) return;
+        if (kind == FileRequest::Kind::Trash && selectRowAfter >= 0) {
+            // The row that took the trashed sample's place.
+            const int rows = safe->table_.getNumRows();
+            if (const SearchRow* next = rows > 0 ? safe->browser_.row(std::min(selectRowAfter, rows - 1)) : nullptr)
+                safe->selectFile(next->id);
+        } else if (kind == FileRequest::Kind::Undo && !outcome.files.empty()) {
+            safe->selectFile(outcome.files.front());
+        }
+    });
+}
+
+void AsmaEditor::selectFile(std::int64_t id)
+{
+    const auto row = library_.row(id);
+    if (!row) return;
+    const std::string path = toUtf8(LibraryView::pathOf(*row));
+    processor_.updateState([&](PluginState& s) { s.selected = path; });
+    showSelection();
+    updateReadouts();
+}
+
 void AsmaEditor::changeTag(std::int64_t fileId, const std::string& tag, bool added)
 {
     // The table's tags column shows the change at once.
@@ -559,9 +703,28 @@ void AsmaEditor::chooseFolder()
 void AsmaEditor::addFolder(const std::filesystem::path& folder)
 {
     if (!processor_.isStandalone()) return;
-    std::string why;
-    scanMessage_ = keeper_->addFolder(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
-    updateReadouts();
+    const auto refuse = [this](const juce::String& why) {
+        scanMessage_ = why;
+        updateReadouts();
+    };
+    std::error_code ec;
+    if (!std::filesystem::is_directory(folder, ec)) return refuse("Cannot add that folder: not a folder");
+    AddCheck check;
+    try {
+        Db db = Db::open(processor_.libraryPath());
+        check = checkAddFolder(db, folder);
+    } catch (const std::exception& e) {
+        return refuse(juce::String("Cannot add that folder: ") + e.what());
+    }
+    if (check.result == AddCheck::Result::Inside) return refuse(utf8(check.message));
+    if (check.result == AddCheck::Result::Contains) {
+        // It takes the place of the folders inside it, if the user says so.
+        confirmMerge(utf8(check.message), [safe = juce::Component::SafePointer<AsmaEditor>(this), folder](bool yes) {
+            if (safe && yes) safe->runFileOperation(FileRequest::addFolder(folder, true));
+        });
+        return;
+    }
+    runFileOperation(FileRequest::addFolder(folder, false));
 }
 
 void AsmaEditor::clearRenders()
@@ -609,6 +772,10 @@ void AsmaEditor::poll()
         keeperMessages_ = keeper_->messageCount();
         scanMessage_ = utf8(keeper_->message());
     }
+    if (auto* job = processor_.fileOps(); job && job->messageCount() != fileOpsMessages_) {
+        fileOpsMessages_ = job->messageCount();
+        scanMessage_ = utf8(job->message());
+    }
     if (browser_.poll()) {
         pending_.libraryChanged();
         // The selection follows its sample: renamed or moved outside asma, it
````

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index 80f57da..9cddd0f 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -2,6 +2,7 @@
 #pragma once
 
 #include "Browser.h"
+#include "FileOpsJob.h"
 #include "LibraryKeeper.h"
 #include "LibraryView.h"
 #include "LibraryWriter.h"
@@ -10,6 +11,7 @@
 #include "Sidebar.h"
 #include "ui/AsmaLookAndFeel.h"
 #include "ui/ChipRow.h"
+#include "ui/EditMenu.h"
 #include "ui/FilterPopovers.h"
 #include "ui/Footer.h"
 #include "ui/NamePopover.h"
@@ -101,13 +103,25 @@ public:
     // A row's right-click menu: Add to collection (ticking those it is in,
     // and New collection…), Tags… and Show in Finder (Explorer, the file
     // manager); and what picking an item does.
+    // In the standalone it also offers Rename…, Move to… and Move to Trash.
     juce::PopupMenu rowMenu(const SearchRow& row);
     void rowMenuChosen(const SearchRow& row, int result);
-    enum RowMenuItem { kNewCollection = 1, kEditTags, kReveal, kFirstCollection = 100 };
+    enum RowMenuItem { kNewCollection = 1, kEditTags, kReveal, kRename, kMoveTo, kTrash, kFirstCollection = 100 };
     static juce::String revealText();
     // A sample's tags to change, as Tags… opens it.
     std::unique_ptr<TagsPopover> tagsPopover(const SearchRow& row);
 
+    // The standalone's file operations (spec section 6), each run by the
+    // processor's FileOpsJob; the footer says what came of it. F2 renames,
+    // Delete or Backspace trashes, Cmd/Ctrl+Z undoes (outside text fields).
+    std::unique_ptr<NamePopover> renamePopover(const SearchRow& row);
+    void moveSample(const SearchRow& row, const std::filesystem::path& folder); // the chooser ends here
+    void trashSample(const SearchRow& row);
+    void undoFileOperation();
+    // Asks whether a folder holding library folders takes their place; the
+    // app asks in a dialog, tests answer themselves.
+    std::function<void(const juce::String& question, std::function<void(bool)> answer)> confirmMerge;
+
 private:
     enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
     // TableListBoxModel
@@ -128,6 +142,12 @@ private:
     void showSort(const SearchModel& model); // the header's arrow on the search's sort
     void syncChanged(const audio::SyncSettings& sync);
     void chooseFolder();
+    void chooseDestination(const SearchRow& row);
+    void showRename(const SearchRow& row);
+    // Runs a file operation; when it is done, selects what it names (or,
+    // after a trash, the row that took the sample's place).
+    void runFileOperation(FileRequest request, int selectRowAfter = -1);
+    void selectFile(std::int64_t id);
     void showSelection();   // selects the saved file's row without playing it
     void loadState();       // every control from the processor's state
     void selectionChanged(); // re-reads what the readouts need about the selection
@@ -151,6 +171,8 @@ private:
     LibraryView library_;
     std::shared_ptr<LibraryKeeper> keeper_;
     std::uint64_t keeperMessages_ = 0; // the keeper's messages this window has shown
+    std::uint64_t fileOpsMessages_ = 0; // the file operations' messages it has shown
+    std::unique_ptr<EditMenu> editMenu_; // the standalone's, on macOS
     Browser browser_{library_};
     TopBar top_;
     SidebarView sidebar_;
````

In `plugin/src/AsmaProcessor.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/AsmaProcessor.cpp b/plugin/src/AsmaProcessor.cpp
index f1dd968..580cfe7 100644
--- a/plugin/src/AsmaProcessor.cpp
+++ b/plugin/src/AsmaProcessor.cpp
@@ -31,6 +31,13 @@ AsmaProcessor::AsmaProcessor(Mode mode)
 
 AsmaProcessor::~AsmaProcessor() = default;
 
+FileOpsJob* AsmaProcessor::fileOps()
+{
+    if (!standalone_) return nullptr;
+    if (!fileOps_) fileOps_ = std::make_unique<FileOpsJob>(libraryPath_, trash_);
+    return fileOps_.get();
+}
+
 void AsmaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
 {
     // Kept rather than read back with getSampleRate(), which is 0 until a
````

In `plugin/src/AsmaProcessor.h`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/AsmaProcessor.h b/plugin/src/AsmaProcessor.h
index 6c549cf..9a765c8 100644
--- a/plugin/src/AsmaProcessor.h
+++ b/plugin/src/AsmaProcessor.h
@@ -1,6 +1,7 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #pragma once
 
+#include "FileOpsJob.h"
 #include "LibraryWriter.h"
 #include "PluginState.h"
 #include "asma/audio/AuditionEngine.h"
@@ -96,6 +97,12 @@ public:
     // What the user adds to the library goes through here: written directly
     // in the standalone, by the asma helper in a plugin.
     LibraryWriter& writer() { return *writer_; }
+    // The standalone's file operations, made on first use; null in a plugin,
+    // which never moves files. Message thread.
+    FileOpsJob* fileOps();
+    // The trash file operations use, before the first fileOps(): tests keep
+    // to a folder of their own.
+    void setTrash(TrashBackend trash) { trash_ = std::move(trash); }
 
 private:
     // One preview cache for every instance in the process.
@@ -108,6 +115,8 @@ private:
     std::unique_ptr<ableton::Link> link_; // standalone only
     std::filesystem::path binary_;
     std::unique_ptr<LibraryWriter> writer_;
+    TrashBackend trash_ = TrashBackend::system();
+    std::unique_ptr<FileOpsJob> fileOps_; // standalone only, once asked for
     static constexpr double kDefaultBpm = 120.0; // the standalone's tempo until set
     std::atomic<bool> linkOn_{false};
     std::atomic<double> manualBpm_{0.0};
````

In `plugin/src/LibraryKeeper.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/LibraryKeeper.cpp b/plugin/src/LibraryKeeper.cpp
index 3ad6648..8d6d9e8 100644
--- a/plugin/src/LibraryKeeper.cpp
+++ b/plugin/src/LibraryKeeper.cpp
@@ -67,23 +67,6 @@ std::shared_ptr<LibraryKeeper> LibraryKeeper::shared(const std::filesystem::path
     return made;
 }
 
-bool LibraryKeeper::addFolder(const std::filesystem::path& folder, std::string* error)
-{
-    std::error_code ec;
-    if (!std::filesystem::is_directory(folder, ec)) {
-        if (error) *error = "not a folder";
-        return false;
-    }
-    try {
-        Db db = Db::open(dbPath_);
-        Library(db).addRoot(folder); // seen at the next tick, and scanned first
-    } catch (const std::exception& e) {
-        if (error) *error = e.what();
-        return false;
-    }
-    return true;
-}
-
 void LibraryKeeper::folderChanged(std::int64_t rootId)
 {
     const std::lock_guard lock(changedMutex_);
````

In `plugin/src/LibraryKeeper.h`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/LibraryKeeper.h b/plugin/src/LibraryKeeper.h
index 0b5a8dd..a556928 100644
--- a/plugin/src/LibraryKeeper.h
+++ b/plugin/src/LibraryKeeper.h
@@ -55,8 +55,6 @@ public:
         return safety_ == Safety::Idle && helper_.idle() && !(damageSuspected_ && lastTick_ >= retryAt_);
     }
 
-    // The standalone's Add folder: adds it to the library and scans it next.
-    bool addFolder(const std::filesystem::path& folder, std::string* error = nullptr);
     // What the footer says while a scan runs; empty otherwise.
     std::string progress() const { return runner_->progress(); }
     // The last scan's news ("Scan finished: 3 added", "Scan failed: ..."),
````

Create `plugin/src/ui/EditMenu.cpp`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "EditMenu.h"

namespace asma::app {

EditMenu::EditMenu(std::function<std::string()> label, std::function<void()> undo)
    : label_(std::move(label)), undo_(std::move(undo))
{
}

juce::StringArray EditMenu::getMenuBarNames() { return {"Edit"}; }

juce::PopupMenu EditMenu::getMenuForIndex(int, const juce::String&)
{
    const std::string label = label_ ? label_() : std::string();
    juce::PopupMenu menu;
    juce::PopupMenu::Item undo(label.empty() ? juce::String("Undo") : "Undo " + juce::String::fromUTF8(label.c_str()));
    undo.setID(kUndo).setEnabled(!label.empty());
    undo.shortcutKeyDescription = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons();
    menu.addItem(std::move(undo));
    return menu;
}

void EditMenu::menuItemSelected(int itemId, int)
{
    if (itemId == kUndo && undo_) undo_();
}

} // namespace asma::app
````

Create `plugin/src/ui/EditMenu.h`:

````cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>

namespace asma::app {

// The standalone's Edit menu on macOS: "Undo Move 5 Samples", greyed when
// there is nothing to undo.
class EditMenu final : public juce::MenuBarModel {
public:
    // `label`: what undo would undo, empty for nothing.
    EditMenu(std::function<std::string()> label, std::function<void()> undo);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int itemId, int index) override;

private:
    enum { kUndo = 1 };
    std::function<std::string()> label_;
    std::function<void()> undo_;
};

} // namespace asma::app
````

In `plugin/src/ui/SidebarView.cpp`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/ui/SidebarView.cpp b/plugin/src/ui/SidebarView.cpp
index da4f68e..6109c08 100644
--- a/plugin/src/ui/SidebarView.cpp
+++ b/plugin/src/ui/SidebarView.cpp
@@ -178,7 +178,7 @@ void SidebarView::setEntries(std::vector<SidebarEntry> entries)
         row->onClick = [this, i] {
             if (onPick) onPick(static_cast<int>(i));
         };
-        if (e.kind == EntryKind::Collection || e.kind == EntryKind::SavedSearch)
+        if (e.kind == EntryKind::Collection || e.kind == EntryKind::SavedSearch || (e.kind == EntryKind::Folder && onRemoveFolder))
             static_cast<Row*>(row)->onMenu = [this, i, row] {
                 entryMenu(static_cast<int>(i)).showMenuAsync(
                     juce::PopupMenu::Options().withTargetComponent(row).withParentComponent(getTopLevelComponent()),
@@ -295,6 +295,7 @@ juce::PopupMenu SidebarView::entryMenu(int index) const
     juce::PopupMenu menu;
     if (index < 0 || index >= static_cast<int>(entries_.size())) return menu;
     const auto kind = entries_[static_cast<std::size_t>(index)].kind;
+    if (kind == EntryKind::Folder && onRemoveFolder) menu.addItem(kRemoveFolder, "Remove from Library");
     if (kind != EntryKind::Collection && kind != EntryKind::SavedSearch) return menu;
     menu.addItem(kRename, juce::String::fromUTF8("Rename…"));
     menu.addItem(kDelete, "Delete");
@@ -305,6 +306,7 @@ void SidebarView::entryMenuChosen(int index, int result)
 {
     if (result == kRename) startRename(index);
     if (result == kDelete && onDelete) onDelete(index);
+    if (result == kRemoveFolder && onRemoveFolder) onRemoveFolder(index);
 }
 
 std::function<void(int)> SidebarView::entryMenuHandler(int index)
````

In `plugin/src/ui/SidebarView.h`, apply (`git apply` takes it as is):

````diff
diff --git a/plugin/src/ui/SidebarView.h b/plugin/src/ui/SidebarView.h
index 8510a2d..6a043d0 100644
--- a/plugin/src/ui/SidebarView.h
+++ b/plugin/src/ui/SidebarView.h
@@ -35,6 +35,8 @@ public:
     std::function<std::optional<juce::String>(int index, const juce::String& name)> nameRefusal;
     std::function<void(int index, const juce::String& name)> onNamed;
     std::function<void(int index)> onDelete;
+    // Set in the standalone: a folder's menu offers Remove from Library.
+    std::function<void(int index)> onRemoveFolder;
     // A new collection's field was dropped (Escape, or another field opened).
     std::function<void()> onNewCancelled;
 
@@ -52,7 +54,7 @@ public:
     // What the menu's choice does once it is made.
     std::function<void(int result)> entryMenuHandler(int index);
     juce::Label& refusalLabel() { return refusalLabel_; }
-    enum MenuItem { kRename = 1, kDelete };
+    enum MenuItem { kRename = 1, kDelete, kRemoveFolder };
 
     int rowCount() const { return rows_.size(); }
     juce::Button& row(int index) { return *rows_[index]; }
````

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[filemanager],[organise]"`

Expected: `All tests passed (195 assertions in 33 test cases)`

- [ ] **Step 5: Commit**

````sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/AsmaProcessor.cpp plugin/src/AsmaProcessor.h plugin/src/LibraryKeeper.cpp plugin/src/LibraryKeeper.h plugin/src/ui/EditMenu.cpp plugin/src/ui/EditMenu.h plugin/src/ui/SidebarView.cpp plugin/src/ui/SidebarView.h tests/plugin/EditorRig.h tests/plugin/test_file_manager.cpp tests/plugin/test_organise.cpp
git commit -m "app: rename, move to, trash and undo in the standalone; Remove from Library; adding a folder holding library folders asks first; the Edit menu on macOS"
````

---

### Task 8: Docs

The README gains file operations, in the app and on the command line. The spec gains what the prototype settled: renaming with F2 (Enter keeps playing), one sample at a time in the window, `asma root add --merge`, the Edit menu on macOS, and removed folders coming back after a rebuild.

**Files:**

- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`

**Interfaces:**

- Consumes: tasks 1 to 7.
- Produces: nothing code depends on.

- [ ] **Step 1: Write the docs**

In `README.md`, apply (`git apply` takes it as is):

````diff
diff --git a/README.md b/README.md
index d255ec6..5b11f1b 100644
--- a/README.md
+++ b/README.md
@@ -36,6 +36,16 @@ Ratings, favourites, tags, collections and saved searches:
     asma query --saved "Fast loops" --sort rating --desc
     asma retry ~/Samples/Drums/broken.wav       # read a failed file again
 
+Moving files, each undoable as one, and taking folders out of the library:
+
+    asma rename ~/Samples/Drums/Kick_01.wav Kick_Dusty.wav
+    asma move ~/Samples/Drums/Kick_Dusty.wav --to ~/Samples/Keep
+    asma trash ~/Samples/Drums/Snare_03.wav      # to the system's trash
+    asma remove-folder ~/Samples/Old            # its samples' data is kept
+    asma undo                                   # the last of these, as a whole
+    asma history
+    asma root add --merge ~/Samples             # in place of the folders inside it
+
 Keeping the library safe:
 
     asma check                   # is the library sound?
@@ -110,6 +120,17 @@ appears in the sidebar: its panel lists each with the reason, and Retry reads it
 again. Changes show at once; in a plugin, which never writes the library inside
 the host, the `asma-cli` helper shipped beside it makes them.
 
+The app also manages the files themselves. F2 or Rename… in a row's menu renames
+the selected sample, Move to… puts it in another library folder, and Delete,
+Backspace or Move to Trash sends it to the system's trash; Cmd+Z (Ctrl+Z off
+macOS) undoes the last of these, and the footer says what each did. A sample
+keeps its ratings, tags and collections wherever it goes, and asma never
+replaces a file or deletes one. Remove from Library, in a folder's menu in the
+sidebar, takes a folder out without touching its files, and its samples' data
+comes back if you add it again. Adding a folder inside one already in the
+library is refused; adding one that holds library folders asks to take their
+place. A plugin window leaves files alone.
+
 While any asma window is open, the app's or a plugin's, the library follows its
 folders: a sample dropped into one appears within seconds, and one deleted or
 renamed outside asma goes or follows. Each folder is also rescanned at startup
````

In `docs/superpowers/specs/2026-09-25-asma-design.md`, apply (`git apply` takes it as is):

````diff
diff --git a/docs/superpowers/specs/2026-09-25-asma-design.md b/docs/superpowers/specs/2026-09-25-asma-design.md
index 05b51b2..aab0d6f 100644
--- a/docs/superpowers/specs/2026-09-25-asma-design.md
+++ b/docs/superpowers/specs/2026-09-25-asma-design.md
@@ -270,9 +270,9 @@ stages under the writer lock:
 In 4a every step is a rename on one volume, so a crash never leaves half a copy:
 moving between volumes is refused ("moving between disks comes with export").
 
-**Undo:** Cmd/Ctrl+Z (outside text fields) and the Edit menu ("Undo Move 5
-Samples") reverse the newest `done` group, step by step in reverse order. A
-trashed file is restored from its `trash_ref` and its `trashed_by` cleared. A
+**Undo:** Cmd/Ctrl+Z (outside text fields) and, on macOS, the Edit menu ("Undo
+Move 5 Samples") reverse the newest `done` group, step by step in reverse order.
+A trashed file is restored from its `trash_ref` and its `trashed_by` cleared. A
 step that cannot be reversed (the file is no longer where the step left it, its
 old place is taken, the trash was emptied) is marked `failed`, skipped and
 reported; the rest of the group is undone. The group becomes `undone`. The
@@ -316,10 +316,10 @@ them"). In the CLI, `asma add` on a containing folder needs `--merge`.
 
 **In the standalone:**
 
-- **Rename:** F2, Enter on a single selection, or "Rename…" in the row menu
-  opens the name popover with the name selected up to its extension. A taken
-  name, an invalid character or another extension turns the field red with the
-  reason.
+- **Rename:** F2 or "Rename…" in the row menu opens the name popover with the
+  name selected up to its extension (Enter keeps playing the selection, as
+  before). A taken name, an invalid character or another extension turns the
+  field red with the reason.
 - **Move:** "Move to…" in the row menu, on the selection, opens a native folder
   chooser at the first sample's folder. A destination outside the library's
   folders, or a refusal from preflight, is said in the footer.
@@ -328,6 +328,9 @@ them"). In the CLI, `asma add` on a containing folder needs `--merge`.
   macOS).
 - **Remove from Library:** in a sidebar folder's menu, with no confirmation:
   "Removed Samples from the library. Cmd+Z to undo."
+- **One sample at a time:** the table selects one row, so the window's
+  operations act on that sample; the CLI takes many, and 4b brings batch work to
+  the window.
 - **Selection:** moved and renamed samples stay selected; after a trash the next
   row is selected; after an undo, the restored samples.
 - Operations run one at a time on a background thread; the window is never
@@ -658,7 +661,9 @@ without playing, with its tempo and key from the library.
   crash; the footer names the file. A group never ends half done.
 - **Damage during a file operation:** the operation stops; files already moved
   stay moved. A rebuilt library has no journal (it is not in the backup), so
-  undo history is lost; the scan finds the files where they are.
+  undo history is lost; the scan finds the files where they are. The backup
+  lists every folder, removed ones too, so a rebuild brings removed folders
+  back.
 - **Trash unavailable:** delete refused, no fallback.
 - **Streaming failure:** anything reading ahead in a streamed file throws ends
   streaming for that file only: it plays silence where it could not read, and
````

- [ ] **Step 2: Check them**

Run: `prettier --check README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: both files pass; no em-dashes.

- [ ] **Step 3: Commit**

````sh
git add README.md docs/superpowers/specs/2026-09-25-asma-design.md
git commit -m "docs: file operations in the README; the spec on renaming with F2, one sample at a time, and removed folders after a rebuild"
````

---

### Task 9: Verify and merge

- [ ] **Step 1: Everything, locally**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests
TOOLS=build/validators ci/validate-plugins.sh build
```

Expected: no warnings from asma's code; `100% tests passed out of 613`; the
plugin tests also pass in one process (`All tests passed`, 226 test cases);
pluginval `SUCCESS` and clap-validator `0 failed`.

- [ ] **Step 2: Try the app by hand (macOS)**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
build/plugin/asma_plugin_artefacts/Release/Standalone/asma.app/Contents/MacOS/asma
```

Add a folder of samples (a copy: these are real file operations). Expected:
F2 on a sample opens the name popover with the name selected up to the
extension, a taken name turns it red, Save renames the file in Finder and the
row follows; "Move to…" in the row menu moves it to a subfolder; Delete sends
it to the Trash (Finder's Trash shows it) and the next row is selected; Cmd+Z,
and Edit > Undo, which names the operation, bring each back in turn, rating
included; right-click the folder in the sidebar, Remove from Library empties
the table, Cmd+Z restores it; add a subfolder of it: refused in the footer; add
its parent: asks to take its place, and Add leaves one folder in the sidebar.
Quit during nothing in particular and start again: the footer says nothing.

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-4a
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green, including "Validate plugins". Watch
the `[trash]` tests on Windows (`IFileOperation` and the Recycle Bin run there
for the first time, and `Trash_win.cpp` is first compiled there) and on Ubuntu
(the freedesktop trash in `$XDG_DATA_HOME`, and `renameat2`), and the `[files]`
CLI tests, which use each system's own trash.

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-4a
git -C product/asma worktree remove .worktrees/plan-4a
git -C product/asma branch -d plan-4a
```

Push `main` once the user agrees, and delete the remote branch.
