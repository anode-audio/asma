# asma Plan 3c2b2: Organise Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** asma organises its library from the window: favourites and ratings
from the table (clicks and keys), a row's menu for collections, tags and showing
the file, collections and saved searches made, renamed and deleted from the
sidebar, Save search in the chip row, and a Problems panel that retries files
that failed; shown at once and confirmed by the library, written directly in the
standalone and through the `asma` helper in a plugin.

**Architecture:** The core trims user tags, lists problems and retries files
(re-probed as a scan would a new file, then analysed); the CLI gains `retry`,
`search rename`, `search save --json` and `--errors-to-stdout`. In the plugin
code a `Write` describes each change; `LibraryWriter` makes it, `DirectWriter`
in the standalone (short transactions) and `CliWriter` in a plugin (one `asma`
command at a time on a background thread, `asma-cli` beside the binary), and
both run retries through `asma retry`. `PendingEdits` holds what the table shows
before the library has it. The UI gains `NamePopover`, `TagsPopover` and
`ProblemsView`, and the sidebar a "+", a name field and a menu.

**Tech Stack:** C++20, JUCE 9.0.3, SQLite, Catch2; headless Chrome for the
reference pictures.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (section 9
"Organising (3c2b2)", sections 6 and 12, as amended in `6fe7d05`). Task 10
corrects two of its lines the prototype proved wrong.

**How this plan was checked:** every task was built on a branch from `main`
(`6fe7d05`), then replayed commit by commit on a macOS Release build: with only
the task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The code below is taken from those commits: new files
in full, changed files as diffs against the task before (or in full where most
of the file changed), so applying the tasks in order to `main` gives the
prototype file for file. The reference pictures come from the pages the plan
gives, rendered by Chrome as stated. On the prototype, ctest passed 513 of 513,
the plugin tests passed in one process (178 test cases), pluginval said
`SUCCESS` and clap-validator `0 failed`. The prototype did not run on CI:
Windows and Linux are first built in Task 11.

## Where this sits

Plan 3c2b was split in two (the user's call, 2026-10-02): 3c2b1 browsed and only
read the library (merged 2026-10-06); **3c2b2 (this plan)** writes it. Next, in
the order the user agreed: the file watcher with the library's safety net
(integrity check at startup, a corrupt library moved aside and rebuilt, a
nightly JSON sidecar of user data), plan 4 (file manager) and plan 5
(packaging).

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (HTML: `<!-- SPDX-... -->`).
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11), no
  floating-point `std::to_chars`/`std::from_chars` (macOS 12). The plugin build
  uses JUCE's warning set, which includes `-Wfloat-equal`: compare floats with
  `<`/`>`, never `==`/`!=`.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE.
- The plugin never writes the library inside the host: it runs the `asma` CLI as
  a helper process. The standalone writes user data directly, in short
  transactions without the writer lock. Only `asma retry` (and scans) take the
  writer lock.
- Nothing on the audio thread allocates, locks or blocks; nothing on the message
  thread waits for a helper process.
- UI text in the source is UTF-8; `asma_ui` compiles with `/utf-8` on MSVC.
- Colours come from `theme`, never as hex in a component.
- No em-dashes in code, comments, docs or commit messages; no attribution
  trailers; no mention of the tools used to write the code.

## Decisions made while prototyping

- **The helper ships as `asma-cli`.** Copied as `asma` it overwrote the app's
  and the plugin bundles' own binaries, which are also called `asma` (the plugin
  tests could not see it; `file` on the bundle could). Every format's build
  copies it beside its binary under that name.
- **Errors come back on stdout.** The helper runs with stderr closed (a host may
  have closed its own), so `--errors-to-stdout` prints one `error: ` line; the
  writer turns "database is locked" into "the library is busy".
- **A retry has its own lane.** It may wait a whole scan for the writer lock;
  ratings queued behind it would wait too, which the spec forbids. The writer
  runs retries and writes on separate threads, each in order.
- **The standalone retries through the helper too**, as the spec says, so the
  re-read runs out of process under the writer lock in both.
- **A pending edit ends at the library's next change after its write**, not at
  any change: a scan's commit before the helper's must not flash the old value.
  A failed write drops its edit at once.
- **"New collection…" in a row's menu uses the sidebar's name field**, with the
  sample added once named, rather than another popover; the field is where the
  "+" puts it too.
- **COLLECTIONS always shows**, even with none, since its "+" is how the first
  is made.
- **The keys F and 0 to 5** act unless a text field has focus (the headless
  tests cannot give the table focus; a field keeps what is typed in it anyway).
- **Save search and Retry all are primary buttons** (amber, dimmed while they
  cannot act): the LookAndFeel's `asma.accent` only lights a toggled button.
- **The row menu has no reference picture.** It is a native popup menu, which
  cannot be drawn headless; Task 10 says so in the spec. The Tags popover and
  the Problems panel have pictures.
- **A text field reports Return, Escape and changes through the message loop**,
  and `triggerClick` clicks later too: the tests run the loop after each.
- **A Tags popover button deletes itself** when its tag goes, so the tag is
  passed by value (by reference it was read after it was freed).

## Review Focus

The five inputs most likely to bite a person that the tasks' main tests do not
reach; each has its own test, in the task named:

- Renaming a collection or saved search to the name it already has: no change,
  not "that name exists" (Task 6).
- Deleting the collection the table is showing: back to All samples, not an
  empty table scoped to nothing (Task 6).
- A tag typed with stray spaces or capitals: " Bass" adds nothing to a sample
  tagged "bass" (Tasks 1 and 7).
- Retrying a file deleted since it failed: it stays, saying the file is gone
  (Task 8).
- Several quick rating clicks in a plugin: the last one wins, not whichever
  helper process finished last (Task 4).

## File Structure

```
core/include/asma/core/Library.h, core/src/Library.cpp   trimmed tags, Problem, problems()
core/include/asma/core/Scanner.h, core/src/Scanner.cpp   RetryStats, retryFiles
core/include/asma/core/Analyser.h, core/src/Analyser.cpp AnalyseOptions::fileIds
core/include/asma/core/UserData.h, core/src/UserData.cpp renameSavedSearch
apps/OrganiseCommands.{h,cpp}, apps/asma_main.cpp       retry, search rename/save --json, --errors-to-stdout
plugin/CMakeLists.txt                                   asma-cli beside every format
plugin/src/LibraryWriter.{h,cpp}                         Write, CliLane, LibraryWriter, DirectWriter, CliWriter
plugin/src/PendingEdits.{h,cpp}                          what the table shows before the library has it
plugin/src/Names.{h,cpp}                                 name checks (JUCE-free)
plugin/src/AsmaProcessor.{h,cpp}                         writer()
plugin/src/AsmaEditor.{h,cpp}                            clicks, keys, menus, naming, Problems wiring
plugin/src/LibraryView.{h,cpp}                           collectionsOf, tagsOf, problems
plugin/src/ui/NamePopover.{h,cpp}                        Save search's name
plugin/src/ui/TagsPopover.{h,cpp}                        a sample's tags
plugin/src/ui/ProblemsView.{h,cpp}                       the Problems panel
plugin/src/ui/SidebarView.{h,cpp}                        "+", name field, entry menu, lit Problems
plugin/src/ui/ChipRow.{h,cpp}                            Save search
plugin/src/ui/AsmaLookAndFeel.cpp, ui/Theme.h            primary buttons, refusal red
tests/test_{library,scanner,analyser,user_data,cli_e2e}.cpp
tests/plugin/EditorRig.h (from test_editor.cpp), PluginTestUtil.h
tests/plugin/test_{library_writer,pending_edits,organise,names,tags_popover,problems_view,
  sidebar_view,ui_fidelity}.cpp
tests/ui/reference/{main,tags-popover,problems}.{html,png}, README.md
README.md, docs/superpowers/specs/2026-09-25-asma-design.md
```

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-3c2b2 -b plan-3c2b2
```

All paths below are relative to `product/asma/.worktrees/plan-3c2b2`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
`cmake --build build`.

---

### Task 1: Trimmed tags, and the library's list of problems

Spec section 9 (Organising: Tags, Problems). A user tag is stored trimmed and
lower case, so " Bass" is the tag "bass", and an empty one is refused.
`Library::problems()` lists what the Problems panel shows: files that could not
be read, then files whose analysis failed, in enabled folders; a missing file is
not a problem.

**Files:**

- Modify: `core/include/asma/core/Library.h`
- Modify: `core/src/Library.cpp`
- Modify: `tests/test_library.cpp` (test)

**Interfaces:**

- Consumes: `Library::addUserTag`, `removeUserTag`, `UserDataError`.
- Produces:
  `struct Problem { enum class Kind { Read, Analysis }; std::int64_t id; std::string rootPath, relPath; Kind kind; std::string reason; }`;
  `std::vector<Problem> Library::problems()`; `addUserTag`/`removeUserTag` throw
  `UserDataError` for an empty tag.

- [ ] **Step 1: Write the failing test**

In `tests/test_library.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_library.cpp b/tests/test_library.cpp
index f2816f4..dfde27c 100644
--- a/tests/test_library.cpp
+++ b/tests/test_library.cpp
@@ -2,6 +2,7 @@
 #include "TestUtil.h"
 #include "asma/core/Fs.h"
 #include "asma/core/Library.h"
+#include "asma/core/UserData.h"

 #include <catch2/catch_test_macros.hpp>

@@ -326,3 +327,49 @@ TEST_CASE("an empty path is in no folder, and looking it up is not an error", "[
     CHECK_NOTHROW(found = lib.fileByAbsolutePath({}));
     CHECK_FALSE(found);
 }
+
+TEST_CASE("user tags are trimmed and compared ignoring case", "[library][tags]")
+{
+    TempDir dir;
+    Db db = Db::openInMemory();
+    Library lib(db);
+    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
+    lib.addUserTag(id, "bass");
+    lib.addUserTag(id, " Bass\t");
+    REQUIRE(lib.tags(id).size() == 1);
+    CHECK(lib.tags(id)[0].first == "bass");
+    lib.removeUserTag(id, "  BASS ");
+    CHECK(lib.tags(id).empty());
+    CHECK_THROWS_AS(lib.addUserTag(id, "   "), UserDataError);
+    CHECK_THROWS_AS(lib.removeUserTag(id, ""), UserDataError);
+}
+
+TEST_CASE("problems lists failed files, then failed analyses, in enabled folders", "[library][problems]")
+{
+    TempDir dir;
+    Db db = Db::openInMemory();
+    Library lib(db);
+    const auto root = lib.addRoot(dir.path());
+    const auto ok = lib.insertFile(sampleRecord(root, "ok.wav", "01"));
+    const auto silent = lib.insertFile(sampleRecord(root, "b/silent.wav", "02"));
+    lib.setAnalysisError(silent, "the file is silent");
+    const auto broken = lib.insertFile(sampleRecord(root, "z/broken.wav", "03"));
+    lib.setStatus(broken, FileStatus::Failed, "not a valid WAV header");
+    const auto gone = lib.insertFile(sampleRecord(root, "gone.wav", "04"));
+    lib.setStatus(gone, FileStatus::Missing, "not a valid WAV header"); // missing is not a problem
+
+    const auto problems = lib.problems();
+    REQUIRE(problems.size() == 2);
+    CHECK(problems[0].id == broken);
+    CHECK(problems[0].kind == Problem::Kind::Read);
+    CHECK(problems[0].relPath == "z/broken.wav");
+    CHECK(problems[0].reason == "not a valid WAV header");
+    CHECK(problems[1].id == silent);
+    CHECK(problems[1].kind == Problem::Kind::Analysis);
+    CHECK(problems[1].reason == "the file is silent");
+    CHECK(problems[1].rootPath == lib.root(root)->path);
+    (void)ok;
+
+    db.exec("UPDATE roots SET enabled = 0");
+    CHECK(lib.problems().empty());
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[tags],[problems]"`

Expected: the build stops:

```
tests/test_library.cpp:361:31: error: no member named 'problems' in 'asma::Library'
tests/test_library.cpp:364:31: error: use of undeclared identifier 'Problem'
```

- [ ] **Step 3: Implement**

In `core/include/asma/core/Library.h`, apply (`git apply` takes it as is):

```diff
diff --git a/core/include/asma/core/Library.h b/core/include/asma/core/Library.h
index c231d94..a8e2daf 100644
--- a/core/include/asma/core/Library.h
+++ b/core/include/asma/core/Library.h
@@ -43,6 +43,17 @@ struct FileRecord {
     std::string failureReason;
 };

+// A file in an enabled folder that could not be read, or read but not
+// analysed: what the Problems panel lists.
+struct Problem {
+    enum class Kind { Read, Analysis };
+    std::int64_t id = 0;
+    std::string rootPath; // UTF-8, '/' separators
+    std::string relPath;
+    Kind kind = Kind::Read;
+    std::string reason;
+};
+
 // Metadata derived from headers and file names; derived() also reports what
 // analysis filled in.
 struct DerivedInfo {
@@ -100,12 +111,17 @@ public:
     // Peak and LUFS from analysis; nullopt until the file has been analysed.
     std::optional<Loudness> loudness(std::int64_t fileId);

+    // Tags are trimmed and kept lower case, so " Bass" is the tag "bass".
+    // Throws UserDataError for a tag that is empty once trimmed.
     void addUserTag(std::int64_t fileId, std::string_view tag);
     // Removes the tag only where the user added it; auto and embedded tags
     // belong to the scanner and would come back on the next scan.
     void removeUserTag(std::int64_t fileId, std::string_view tag);
     std::vector<std::pair<std::string, TagSource>> tags(std::int64_t fileId);

+    // Failed files, then files whose analysis failed, each by path.
+    std::vector<Problem> problems();
+
 private:
     std::int64_t ensureTag(std::string_view name);
     void refreshFts(std::int64_t fileId);
```

In `core/src/Library.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/core/src/Library.cpp b/core/src/Library.cpp
index 0a0b3b4..ddb7d82 100644
--- a/core/src/Library.cpp
+++ b/core/src/Library.cpp
@@ -2,6 +2,7 @@
 #include "asma/core/Library.h"

 #include "asma/core/Fs.h"
+#include "asma/core/UserData.h"

 #include <cctype>

@@ -73,6 +74,16 @@ std::string lower(std::string_view s)
     return out;
 }

+// A tag as stored: trimmed and lower case. Throws for one that is empty.
+std::string tagName(std::string_view tag)
+{
+    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
+    while (!tag.empty() && isSpace(tag.front())) tag.remove_prefix(1);
+    while (!tag.empty() && isSpace(tag.back())) tag.remove_suffix(1);
+    if (tag.empty()) throw UserDataError("a tag needs a name");
+    return lower(tag);
+}
+
 std::string baseName(std::string_view relPath)
 {
     const auto slash = relPath.rfind('/');
@@ -426,14 +437,14 @@ void Library::addUserTag(std::int64_t fileId, std::string_view tag)
 {
     auto q = db_.prepare("INSERT INTO file_tags(file_id, tag_id, source) VALUES (?, ?, 'user') "
                          "ON CONFLICT(file_id, tag_id) DO UPDATE SET source = 'user'");
-    q.bind(1, fileId).bind(2, ensureTag(tag));
+    q.bind(1, fileId).bind(2, ensureTag(tagName(tag)));
     q.run();
     refreshFts(fileId);
 }

 void Library::removeUserTag(std::int64_t fileId, std::string_view tag)
 {
-    const std::string normalised = lower(tag);
+    const std::string normalised = tagName(tag);
     auto q = db_.prepare("DELETE FROM file_tags WHERE file_id = ? AND source = 'user' "
                          "AND tag_id = (SELECT id FROM tags WHERE name = ?)");
     q.bind(1, fileId).bind(2, std::string_view(normalised));
@@ -451,6 +462,26 @@ std::vector<std::pair<std::string, TagSource>> Library::tags(std::int64_t fileId
     return out;
 }

+std::vector<Problem> Library::problems()
+{
+    auto q = db_.prepare(
+        "SELECT f.id, r.path, f.rel_path, f.status, COALESCE(f.failure_reason, ''), COALESCE(f.analysis_error, '') "
+        "FROM files f JOIN roots r ON r.id = f.root_id WHERE r.enabled = 1 "
+        "AND (f.status = 'failed' OR (f.status = 'ok' AND f.analysis_error IS NOT NULL)) "
+        "ORDER BY f.status = 'ok', r.path, f.rel_path");
+    std::vector<Problem> out;
+    while (q.step()) {
+        Problem p;
+        p.id = q.getInt(0);
+        p.rootPath = q.getText(1);
+        p.relPath = q.getText(2);
+        p.kind = q.getText(3) == "failed" ? Problem::Kind::Read : Problem::Kind::Analysis;
+        p.reason = q.getText(p.kind == Problem::Kind::Read ? 4 : 5);
+        out.push_back(std::move(p));
+    }
+    return out;
+}
+
 std::int64_t Library::ensureTag(std::string_view name)
 {
     const std::string normalised = lower(name);
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[tags],[problems]"`

Expected: `All tests passed (25 assertions in 6 test cases)`

- [ ] **Step 5: Commit**

```sh
git add core/include/asma/core/Library.h core/src/Library.cpp tests/test_library.cpp
git commit -m "core: user tags are trimmed; the library lists its problems"
```

---

### Task 2: Retry: read chosen files again and queue them for analysis

Spec section 9 (Organising: Problems). `retryFiles` reads each file as a scan
reads a new one, whatever its size and mtime: readable again, it is ok with its
analysis forgotten; still unreadable, it keeps the new reason; gone, it says
"The file is gone". `AnalyseOptions::fileIds` then analyses only those files.
One transaction; an unknown id writes nothing.

**Files:**

- Modify: `core/include/asma/core/Analyser.h`
- Modify: `core/include/asma/core/Scanner.h`
- Modify: `core/src/Analyser.cpp`
- Modify: `core/src/Scanner.cpp`
- Modify: `tests/test_analyser.cpp` (test)
- Modify: `tests/test_scanner.cpp` (test)

**Interfaces:**

- Consumes: the scanner's `process`, `recordFrom`, `derive`;
  `Library::resetAnalysis`, `setDerived`; `analysePending`.
- Produces:
  `struct RetryStats { std::vector<std::int64_t> readable; std::size_t failed, gone; }`;
  `RetryStats retryFiles(Db&, const std::vector<std::int64_t>&)` (throws
  `std::invalid_argument` for an unknown id);
  `std::optional<std::vector<std::int64_t>> AnalyseOptions::fileIds`.

- [ ] **Step 1: Write the failing test**

In `tests/test_analyser.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_analyser.cpp b/tests/test_analyser.cpp
index 05bd48b..f52e2b3 100644
--- a/tests/test_analyser.cpp
+++ b/tests/test_analyser.cpp
@@ -182,3 +182,36 @@ TEST_CASE("renaming away a file-name BPM hands the file back to analysis", "[ana
     CHECK(d.bpmSource == FeatureSource::Analysis);
     CHECK(d.isLoop == true);
 }
+
+TEST_CASE("fileIds limits the run to those files, and none to nothing", "[analyser][retry]")
+{
+    Fixture f;
+    f.write("a.wav", test::kickHit(kRate));
+    f.write("b.wav", test::kickHit(kRate));
+    scanRoot(f.db, f.rootId);
+
+    AnalyseOptions none;
+    none.fileIds = std::vector<std::int64_t>{};
+    CHECK(analysePending(f.db, none).analysed == 0);
+
+    AnalyseOptions one;
+    one.fileIds = std::vector<std::int64_t>{f.file("b.wav").id};
+    CHECK(analysePending(f.db, one).analysed == 1);
+    CHECK(f.version("a.wav") == 0);
+    CHECK(f.version("b.wav") == kAnalysisVersion);
+}
+
+TEST_CASE("a retried file whose analysis failed is analysed again", "[analyser][retry]")
+{
+    Fixture f;
+    f.write("a.wav", test::kickHit(kRate));
+    scanRoot(f.db, f.rootId);
+    markAnalysisFailed(f.db, f.rootId, "a.wav", "crashed the analyser");
+
+    const RetryStats r = retryFiles(f.db, {f.file("a.wav").id});
+    REQUIRE(r.readable.size() == 1);
+    AnalyseOptions options;
+    options.fileIds = r.readable;
+    CHECK(analysePending(f.db, options).analysed == 1);
+    CHECK(f.lib.problems().empty());
+}
```

In `tests/test_scanner.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_scanner.cpp b/tests/test_scanner.cpp
index 8ab59ff..042c5c3 100644
--- a/tests/test_scanner.cpp
+++ b/tests/test_scanner.cpp
@@ -367,3 +367,57 @@ TEST_CASE("an unknown root id throws", "[scanner]")
     Db db = Db::openInMemory();
     CHECK_THROWS_AS(scanRoot(db, 42), std::invalid_argument);
 }
+
+TEST_CASE("retry reads a fixed file again and queues it for analysis", "[scanner][retry]")
+{
+    Fixture f;
+    test::writeBytes(f.root / "broken.wav", "not audio");
+    f.scan();
+    REQUIRE(f.file("broken.wav").status == FileStatus::Failed);
+
+    f.wav("broken.wav", 5); // fixed in place; a scan would only see it if size or mtime changed
+    const RetryStats r = retryFiles(f.db, {f.file("broken.wav").id});
+    CHECK(r.readable == std::vector<std::int64_t>{f.file("broken.wav").id});
+    CHECK(r.failed == 0);
+    CHECK(f.file("broken.wav").status == FileStatus::Ok);
+    CHECK(f.file("broken.wav").failureReason.empty());
+    CHECK_FALSE(f.file("broken.wav").contentHash.empty());
+}
+
+TEST_CASE("retry keeps a file that still fails, with the new reason", "[scanner][retry]")
+{
+    Fixture f;
+    test::writeBytes(f.root / "broken.wav", "not audio");
+    f.scan();
+    f.db.exec("UPDATE files SET failure_reason = 'an old reason'");
+
+    const RetryStats r = retryFiles(f.db, {f.file("broken.wav").id});
+    CHECK(r.readable.empty());
+    CHECK(r.failed == 1);
+    CHECK(f.file("broken.wav").status == FileStatus::Failed);
+    CHECK(f.file("broken.wav").failureReason != "an old reason");
+    CHECK_FALSE(f.file("broken.wav").failureReason.empty());
+}
+
+TEST_CASE("retry says a file that has gone is gone", "[scanner][retry]")
+{
+    Fixture f;
+    test::writeBytes(f.root / "broken.wav", "not audio");
+    f.scan();
+    fs::remove(f.root / "broken.wav");
+
+    const RetryStats r = retryFiles(f.db, {f.file("broken.wav").id});
+    CHECK(r.gone == 1);
+    CHECK(f.file("broken.wav").status == FileStatus::Failed);
+    CHECK(f.file("broken.wav").failureReason == "The file is gone");
+}
+
+TEST_CASE("retry refuses an unknown id and writes nothing", "[scanner][retry]")
+{
+    Fixture f;
+    test::writeBytes(f.root / "broken.wav", "not audio");
+    f.scan();
+    f.wav("broken.wav", 5);
+    CHECK_THROWS_AS(retryFiles(f.db, {f.file("broken.wav").id, 9999}), std::invalid_argument);
+    CHECK(f.file("broken.wav").status == FileStatus::Failed);
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[retry]"`

Expected: the build stops:

```
tests/test_analyser.cpp:194:10: error: no member named 'fileIds' in 'asma::AnalyseOptions'
tests/test_analyser.cpp:211:11: error: unknown type name 'RetryStats'
tests/test_analyser.cpp:211:26: error: use of undeclared identifier 'retryFiles'
```

- [ ] **Step 3: Implement**

In `core/include/asma/core/Analyser.h`, apply (`git apply` takes it as is):

```diff
diff --git a/core/include/asma/core/Analyser.h b/core/include/asma/core/Analyser.h
index ec997b9..c65ca37 100644
--- a/core/include/asma/core/Analyser.h
+++ b/core/include/asma/core/Analyser.h
@@ -8,6 +8,7 @@
 #include <functional>
 #include <optional>
 #include <string_view>
+#include <vector>

 namespace asma {

@@ -21,6 +22,7 @@ struct AnalyseOptions {
     unsigned threads = 0;              // 0 = std::thread::hardware_concurrency()
     std::size_t batchSize = 50;        // files per write transaction
     std::optional<std::int64_t> rootId; // only this root; all enabled roots otherwise
+    std::optional<std::vector<std::int64_t>> fileIds; // only these files (a retry)
     double maxSeconds = 30.0;          // analyse at most this much of each file
     // Both callbacks may be called from worker threads; calls are serialised.
     std::function<void(std::string_view relPath)> onFileStart;
```

Replace the whole of `core/include/asma/core/Scanner.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace asma {

struct ScanStats {
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t unchanged = 0;
    std::size_t relinked = 0;
    std::size_t missing = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0; // could not be read this time; retried next scan
};

struct ScanOptions {
    unsigned threads = 0;         // 0 = std::thread::hardware_concurrency()
    std::size_t batchSize = 200;  // files per write transaction
    // Both callbacks may be called from worker threads; calls are serialised.
    std::function<void(std::string_view relPath)> onFileStart;
    std::function<void(std::size_t done, std::size_t total, std::string_view relPath)> onProgress;
};

// Brings the database in line with the files under one root. Read-only on
// disk. Throws std::invalid_argument for an unknown root, DbError on database
// failure.
ScanStats scanRoot(Db& db, std::int64_t rootId, const ScanOptions& options = {});

struct RetryStats {
    std::vector<std::int64_t> readable; // read again: queued for analysis
    std::size_t failed = 0;             // still cannot be read; the reason is the new one
    std::size_t gone = 0;               // no longer where the library says
};

// The Problems panel's retry: reads the files again whatever their size and
// mtime, as a scan would a new file. A file read again is ok, its analysis
// forgotten so analysePending (with fileIds) does it again; one that still
// fails keeps the new reason; one that is gone is failed with "The file is
// gone" (the next scan of its folder marks it missing). One transaction.
// Throws std::invalid_argument, writing nothing, for an id the library does
// not know.
RetryStats retryFiles(Db& db, const std::vector<std::int64_t>& fileIds);

// Records that relPath crashed the scanner, so later scans skip it until its
// size or mtime changes. Creates the row if the file is not known yet.
void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

} // namespace asma
```

In `core/src/Analyser.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/core/src/Analyser.cpp b/core/src/Analyser.cpp
index 2a5e105..88f6a40 100644
--- a/core/src/Analyser.cpp
+++ b/core/src/Analyser.cpp
@@ -31,11 +31,19 @@ struct Outcome {
     bool unreadable = false;
 };

-std::vector<Pending> pendingFiles(Db& db, const std::optional<std::int64_t>& rootId)
+std::vector<Pending> pendingFiles(Db& db, const AnalyseOptions& options)
 {
+    const auto& rootId = options.rootId;
     std::string sql = "SELECT f.id, r.path, f.rel_path FROM files f JOIN roots r ON r.id = f.root_id "
                       "WHERE f.status = 'ok' AND r.enabled = 1 AND f.analysis_version < ?";
     if (rootId) sql += " AND f.root_id = ?";
+    if (options.fileIds) {
+        if (options.fileIds->empty()) return {};
+        sql += " AND f.id IN (";
+        for (std::size_t i = 0; i < options.fileIds->size(); ++i)
+            sql += (i ? "," : "") + std::to_string((*options.fileIds)[i]);
+        sql += ")";
+    }
     sql += " ORDER BY f.id";
     auto q = db.prepare(sql);
     q.bind(1, kAnalysisVersion);
@@ -69,7 +77,7 @@ Outcome analyseOne(const Pending& file, double maxSeconds)

 AnalyseStats analysePending(Db& db, const AnalyseOptions& options)
 {
-    const std::vector<Pending> files = pendingFiles(db, options.rootId);
+    const std::vector<Pending> files = pendingFiles(db, options);
     const unsigned threads = detail::threadCount(options.threads);
     const std::size_t batchSize = std::max<std::size_t>(1, options.batchSize);
     Library lib(db);
```

In `core/src/Scanner.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/core/src/Scanner.cpp b/core/src/Scanner.cpp
index 9820a9c..abc2102 100644
--- a/core/src/Scanner.cpp
+++ b/core/src/Scanner.cpp
@@ -322,6 +322,51 @@ ScanStats scanRoot(Db& db, std::int64_t rootId, const ScanOptions& options)
     return stats;
 }

+RetryStats retryFiles(Db& db, const std::vector<std::int64_t>& fileIds)
+{
+    Library lib(db);
+    struct Target {
+        FileRecord file;
+        fs::path root;
+    };
+    std::vector<Target> targets;
+    for (const auto id : fileIds) {
+        auto file = lib.fileById(id);
+        if (!file) throw std::invalid_argument("no file with id " + std::to_string(id));
+        const auto root = lib.root(file->rootId);
+        targets.push_back({std::move(*file), fromUtf8(root->path)});
+    }
+
+    RetryStats stats;
+    Transaction tx(db);
+    for (const auto& [file, root] : targets) {
+        const fs::path full = root / fromUtf8(file.relPath);
+        std::error_code ec;
+        const auto size = fs::file_size(full, ec);
+        const auto mtime = ec ? fs::file_time_type{} : fs::last_write_time(full, ec);
+        if (ec) {
+            lib.setStatus(file.id, FileStatus::Failed, "The file is gone");
+            ++stats.gone;
+            continue;
+        }
+        const Job job{JobKind::Changed, {file.relPath, static_cast<std::int64_t>(size), fileTimeToInt(mtime)}, file};
+        const JobResult r = process(root, job);
+        if (!r.probe) {
+            lib.setStatus(file.id, FileStatus::Failed, r.error);
+            ++stats.failed;
+            continue;
+        }
+        FileRecord rec = recordFrom(file.rootId, job.disk, r);
+        rec.id = file.id;
+        lib.updateFile(rec);
+        lib.resetAnalysis(rec.id);
+        lib.setDerived(rec.id, derive(r));
+        stats.readable.push_back(rec.id);
+    }
+    tx.commit();
+    return stats;
+}
+
 void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason)
 {
     Library lib(db);
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[retry]"`

Expected: `All tests passed (23 assertions in 6 test cases)`

- [ ] **Step 5: Commit**

```sh
git add core/include/asma/core/Analyser.h core/include/asma/core/Scanner.h core/src/Analyser.cpp core/src/Scanner.cpp tests/test_analyser.cpp tests/test_scanner.cpp
git commit -m "core: retry reads chosen files again and queues them for analysis"
```

---

### Task 3: The CLI: retry, search rename, search save --json, errors on stdout

Spec sections 6 and 9. `asma retry` takes the writer lock, saying "waiting for
the scan" once while a scan holds it, then retries and analyses.
`search save NAME --json MODEL` saves the app's whole search (options cannot say
a folder scope or the sort); `search rename` renames one. `--errors-to-stdout`
prints `error: ` lines on stdout, for a helper whose stderr is closed.

**Files:**

- Modify: `apps/OrganiseCommands.cpp`
- Modify: `apps/OrganiseCommands.h`
- Modify: `apps/asma_main.cpp`
- Modify: `core/include/asma/core/UserData.h`
- Modify: `core/src/UserData.cpp`
- Modify: `tests/test_cli_e2e.cpp` (test)
- Modify: `tests/test_user_data.cpp` (test)

**Interfaces:**

- Consumes: task 2's `retryFiles`, `AnalyseOptions::fileIds`; `WriterLock`.
- Produces: `UserData::renameSavedSearch(std::int64_t, std::string_view)`;
  `cli::cmdRetry(Args&, Db&, const std::filesystem::path&)`; the commands
  `asma retry <file>... | --id N...`, `asma search rename <name> <new>`,
  `asma search save <name> --json MODEL`, the flag `--errors-to-stdout`.

- [ ] **Step 1: Write the failing test**

In `tests/test_cli_e2e.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_cli_e2e.cpp b/tests/test_cli_e2e.cpp
index 09ee082..3c911f3 100644
--- a/tests/test_cli_e2e.cpp
+++ b/tests/test_cli_e2e.cpp
@@ -299,6 +299,82 @@ TEST_CASE("collections and saved searches from the CLI", "[e2e]")
     CHECK(cli.runAsma("query").out.find("Bass_Loop") != std::string::npos); // files stay
 }

+TEST_CASE("search save takes the app's JSON, and saved searches rename", "[e2e]")
+{
+    Cli cli;
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+
+#ifdef _WIN32
+    const std::string json = "\"{\"\"v\"\":1,\"\"sort\"\":\"\"bpm\"\",\"\"desc\"\":true}\"";
+#else
+    const std::string json = "'{\"v\":1,\"sort\":\"bpm\",\"desc\":true}'";
+#endif
+    CHECK(cli.runAsma("search save Fast --json " + json).exitCode == 0);
+    CHECK(cli.runAsma("search list").out.find("Fast\t{\"v\":1,\"sort\":\"bpm\",\"desc\":true}") != std::string::npos);
+    CHECK(cli.runAsma("search save Bad --json nope").exitCode == 2);
+    CHECK(cli.runAsma("search save Kicks kick").exitCode == 0);
+    CHECK(cli.runAsma("search rename Fast Faster").exitCode == 0);
+    CHECK(cli.runAsma("search list").out.find("Faster\t") != std::string::npos);
+    CHECK(cli.runAsma("search rename Faster kicks").exitCode == 1);
+    CHECK(cli.runAsma("search rename Nope Other").exitCode == 1);
+}
+
+TEST_CASE("asked to, asma says what went wrong on stdout", "[e2e]")
+{
+    Cli cli;
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+    const RunResult r = cli.runAsma("--errors-to-stdout collection rename Nope Other");
+    CHECK(r.exitCode == 1);
+    CHECK(r.out == "error: no collection called 'Nope'\n");
+    const RunResult usage = cli.runAsma("--errors-to-stdout rate");
+    CHECK(usage.exitCode == 2);
+    CHECK(usage.out.rfind("error: missing rating", 0) == 0);
+}
+
+TEST_CASE("retry reads a fixed file again and analyses it", "[e2e][retry]")
+{
+    Cli cli;
+    asma::test::writeBytes(cli.lib / "Drums" / "broken.wav", "not audio");
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan").exitCode == 0);
+    const auto broken = quote(cli.lib / "Drums" / "broken.wav");
+    const RunResult still = cli.runAsma("retry " + broken);
+    CHECK(still.exitCode == 0);
+    CHECK(still.out == "retried 1: readable 0, failed 1, gone 0; analysed 0, failed 0\n");
+
+    asma::test::WavSpec spec;
+    spec.seed = 9;
+    asma::test::writeWav(cli.lib / "Drums" / "broken.wav", spec);
+    const RunResult fixed = cli.runAsma("retry " + broken);
+    CHECK(fixed.exitCode == 0);
+    CHECK(fixed.out == "retried 1: readable 1, failed 0, gone 0; analysed 1, failed 0\n");
+    CHECK(cli.runAsma("query broken").out.find("broken.wav") != std::string::npos);
+}
+
+TEST_CASE("retry waits while a scan holds the library", "[e2e][retry]")
+{
+    Cli cli;
+    asma::test::writeBytes(cli.lib / "Drums" / "broken.wav", "not audio");
+    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
+    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
+
+    auto lock = std::make_unique<std::optional<asma::WriterLock>>(asma::WriterLock::tryAcquire(cli.db.parent_path()));
+    REQUIRE(lock->has_value());
+    std::thread release([&] {
+        std::this_thread::sleep_for(std::chrono::milliseconds(600));
+        lock.reset();
+    });
+    const auto started = std::chrono::steady_clock::now();
+    const RunResult r = cli.runAsma("retry " + quote(cli.lib / "Drums" / "broken.wav"));
+    const auto waited = std::chrono::steady_clock::now() - started;
+    release.join();
+    CHECK(r.exitCode == 0);
+    CHECK(r.out.rfind("waiting for the scan\n", 0) == 0);
+    CHECK(waited >= std::chrono::milliseconds(500));
+}
+
 TEST_CASE("asma render prints the file to drag", "[e2e]")
 {
     Cli cli;
```

In `tests/test_user_data.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_user_data.cpp b/tests/test_user_data.cpp
index 03c74a8..14f1a7a 100644
--- a/tests/test_user_data.cpp
+++ b/tests/test_user_data.cpp
@@ -205,3 +205,19 @@ TEST_CASE("user data follows a file moved to another root, whichever root is sca
     CHECK(user.rating(id) == 4);
     CHECK_FALSE(lib.fileByPath(rootB, "Kick.wav"));
 }
+
+TEST_CASE("saved searches rename, refusing a name another has", "[userdata]")
+{
+    Fixture f;
+    SearchModel loops;
+    loops.type = SampleType::Loop;
+    const auto a = f.user.saveSearch("Loops", loops);
+    const auto b = f.user.saveSearch("Kicks", {});
+    f.user.renameSavedSearch(a, "  Dark loops ");
+    CHECK(f.user.savedSearchByName("Dark loops")->model.type == SampleType::Loop);
+    CHECK_FALSE(f.user.savedSearchByName("Loops"));
+    f.user.renameSavedSearch(a, "DARK LOOPS"); // its own name, in other case
+    CHECK_THROWS_AS(f.user.renameSavedSearch(b, "dark loops"), UserDataError);
+    CHECK_THROWS_AS(f.user.renameSavedSearch(b, " "), UserDataError);
+    CHECK_THROWS_AS(f.user.renameSavedSearch(999, "x"), UserDataError);
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[e2e],[userdata]"`

Expected: the build stops:

```
tests/test_user_data.cpp:216:12: error: no member named 'renameSavedSearch' in 'asma::UserData'
```

- [ ] **Step 3: Implement**

In `apps/OrganiseCommands.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/apps/OrganiseCommands.cpp b/apps/OrganiseCommands.cpp
index f213b08..ce02e7a 100644
--- a/apps/OrganiseCommands.cpp
+++ b/apps/OrganiseCommands.cpp
@@ -4,14 +4,19 @@
 #include "CliCommon.h"
 #include "SearchArgs.h"

+#include "asma/core/Analyser.h"
 #include "asma/core/Fs.h"
 #include "asma/core/Library.h"
 #include "asma/core/Query.h"
+#include "asma/core/Scanner.h"
 #include "asma/core/UserData.h"
+#include "asma/core/WriterLock.h"

 #include <cstdint>
+#include <chrono>
 #include <iostream>
 #include <string>
+#include <thread>
 #include <vector>

 namespace asma::cli {
@@ -160,6 +165,15 @@ int cmdSearch(Args& args, Db& db)
         if (args.rest().empty() || args.rest().front().rfind("--", 0) == 0)
             throw UsageError("search save needs the name before any option");
         const std::string name = required(args, "search name");
+        // --json: the whole model as the app holds it, which options cannot
+        // all say (the sort, a folder scope).
+        if (const auto json = args.option("json")) {
+            rejectLeftovers(args);
+            const auto model = searchModelFromJson(*json);
+            if (!model) throw UsageError("--json needs a search as JSON");
+            user.saveSearch(name, *model);
+            return kOk;
+        }
         const SearchModel model = modelFromArgs(args, db);
         user.saveSearch(name, model);
         return kOk;
@@ -172,7 +186,36 @@ int cmdSearch(Args& args, Db& db)
         user.deleteSavedSearch(saved->id);
         return kOk;
     }
-    throw UsageError("search needs list, save or delete");
+    if (sub == "rename") {
+        const std::string from = required(args, "search name");
+        const std::string to = required(args, "new name");
+        rejectLeftovers(args);
+        const auto saved = user.savedSearchByName(from);
+        if (!saved) throw UserDataError("no saved search called '" + from + "'");
+        user.renameSavedSearch(saved->id, to);
+        return kOk;
+    }
+    throw UsageError("search needs list, save, rename or delete");
+}
+
+int cmdRetry(Args& args, Db& db, const std::filesystem::path& dbPath)
+{
+    const auto idOptions = args.options("id");
+    const auto ids = targetFiles(args, db, idOptions);
+    auto lock = WriterLock::tryAcquire(dbPath.parent_path());
+    if (!lock) {
+        std::cout << "waiting for the scan" << std::endl;
+        while (!(lock = WriterLock::tryAcquire(dbPath.parent_path())))
+            std::this_thread::sleep_for(std::chrono::milliseconds(200));
+    }
+    const RetryStats retried = retryFiles(db, ids);
+    AnalyseOptions analysis;
+    analysis.fileIds = retried.readable;
+    const AnalyseStats analysed = analysePending(db, analysis);
+    std::cout << "retried " << ids.size() << ": readable " << retried.readable.size() << ", failed "
+              << retried.failed << ", gone " << retried.gone << "; analysed " << analysed.analysed << ", failed "
+              << analysed.failed << "\n";
+    return kOk;
 }

 } // namespace asma::cli
```

Replace the whole of `apps/OrganiseCommands.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include "asma/core/Db.h"

#include <filesystem>

namespace asma::cli {

// User-data commands. The plugin runs these as its out-of-process writer, so
// each one is a single transaction and prints nothing on success.
int cmdRate(Args& args, Db& db);
int cmdFav(Args& args, Db& db);
int cmdTag(Args& args, Db& db);
int cmdCollection(Args& args, Db& db);
int cmdSearch(Args& args, Db& db);
// Reads failed files again and re-analyses them, under the writer lock,
// waiting while a scan holds it (saying so once on stdout).
int cmdRetry(Args& args, Db& db, const std::filesystem::path& dbPath);

} // namespace asma::cli
```

In `apps/asma_main.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/apps/asma_main.cpp b/apps/asma_main.cpp
index f172d55..b15e6a9 100644
--- a/apps/asma_main.cpp
+++ b/apps/asma_main.cpp
@@ -28,7 +28,7 @@ using namespace asma::cli;
 namespace {

 constexpr const char* kUsageText =
-    "usage: asma [--db PATH] <command>\n"
+    "usage: asma [--db PATH] [--errors-to-stdout] <command>\n"
     "  root add <dir>          add a sample folder\n"
     "  root list               list sample folders\n"
     "  scan [--root ID] [--threads N] [--no-analysis]\n"
@@ -42,7 +42,9 @@ constexpr const char* kUsageText =
     "  tag add|remove <tag> <file>... | --id N...\n"
     "  collection list | create <name> | rename <name> <new> | delete <name>\n"
     "  collection add|remove <name> <file>... | --id N...\n"
-    "  search list | save <name> [query options] [words...] | delete <name>\n"
+    "  search list | save <name> [query options] [words...] | save <name> --json MODEL\n"
+    "  search rename <name> <new> | delete <name>\n"
+    "  retry <file>... | --id N...   read failed files again and re-analyse them\n"
     "  render <file> [--trim-start S] [--trim-end S] [--reverse | --ping-pong]\n"
     "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--renders DIR]\n"
     "                          print the file to drag, rendering edits if any\n"
@@ -273,8 +275,12 @@ int cmdQuery(Args& args, Db& db)
 int main(int argc, char** argv)
 {
     setupConsole();
+    // The app runs asma as a helper with stderr closed: it asks for errors on
+    // stdout, one line starting "error: ".
+    bool errorsToStdout = false;
     try {
         Args args = Args::fromMain(argc, argv);
+        errorsToStdout = args.flag("errors-to-stdout");
         if (args.flag("version")) {
             std::cout << "asma " << ASMA_VERSION << "\n";
             return kOk;
@@ -299,6 +305,10 @@ int main(int argc, char** argv)
             Db db = Db::open(dbPath);
             return cmdScan(args, db, dbPath);
         }
+        if (*command == "retry") {
+            Db db = Db::open(dbPath);
+            return cmdRetry(args, db, dbPath);
+        }
         for (const auto& [name, run] : commands) {
             if (*command != name) continue;
             Db db = Db::open(dbPath);
@@ -306,10 +316,12 @@ int main(int argc, char** argv)
         }
         throw UsageError("unknown command: " + *command);
     } catch (const UsageError& e) {
-        std::cerr << "asma: " << e.what() << "\n" << kUsageText;
+        if (errorsToStdout) std::cout << "error: " << e.what() << "\n";
+        else std::cerr << "asma: " << e.what() << "\n" << kUsageText;
         return kUsage;
     } catch (const std::exception& e) {
-        std::cerr << "asma: " << e.what() << "\n";
+        if (errorsToStdout) std::cout << "error: " << e.what() << "\n";
+        else std::cerr << "asma: " << e.what() << "\n";
         return kError;
     }
 }
```

In `core/include/asma/core/UserData.h`, apply (`git apply` takes it as is):

```diff
diff --git a/core/include/asma/core/UserData.h b/core/include/asma/core/UserData.h
index 9b06f16..3d10e44 100644
--- a/core/include/asma/core/UserData.h
+++ b/core/include/asma/core/UserData.h
@@ -61,6 +61,8 @@ public:
     std::int64_t saveSearch(std::string_view name, const SearchModel& model);
     std::vector<SavedSearch> savedSearches(); // by name
     std::optional<SavedSearch> savedSearchByName(std::string_view name);
+    // Keeps the search; refuses a name another saved search has.
+    void renameSavedSearch(std::int64_t id, std::string_view name);
     void deleteSavedSearch(std::int64_t id);

 private:
```

In `core/src/UserData.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/core/src/UserData.cpp b/core/src/UserData.cpp
index 6960e7c..87094df 100644
--- a/core/src/UserData.cpp
+++ b/core/src/UserData.cpp
@@ -174,6 +174,19 @@ std::optional<SavedSearch> UserData::savedSearchByName(std::string_view name)
     return SavedSearch{q.getInt(0), q.getText(1), searchModelFromJson(q.getText(2)).value_or(SearchModel{})};
 }

+void UserData::renameSavedSearch(std::int64_t id, std::string_view name)
+{
+    auto exists = db_.prepare("SELECT 1 FROM saved_searches WHERE id = ?");
+    exists.bind(1, id);
+    if (!exists.step()) throw UserDataError("no saved search with id " + std::to_string(id));
+    const std::string valid = validName(name, "saved search");
+    if (const auto existing = savedSearchByName(valid); existing && existing->id != id)
+        throw UserDataError("a saved search called '" + valid + "' already exists");
+    auto q = db_.prepare("UPDATE saved_searches SET name = ? WHERE id = ?");
+    q.bind(1, std::string_view(valid)).bind(2, id);
+    q.run();
+}
+
 void UserData::deleteSavedSearch(std::int64_t id)
 {
     auto exists = db_.prepare("SELECT 1 FROM saved_searches WHERE id = ?");
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[e2e],[userdata]"`

Expected: `All tests passed (213 assertions in 30 test cases)`

- [ ] **Step 5: Commit**

```sh
git add apps/OrganiseCommands.cpp apps/OrganiseCommands.h apps/asma_main.cpp core/include/asma/core/UserData.h core/src/UserData.cpp tests/test_cli_e2e.cpp tests/test_user_data.cpp
git commit -m "cli: retry, search rename, search save --json, and errors on stdout for the app"
```

---

### Task 4: One writer: directly in the standalone, through asma-cli in a plugin

Spec sections 6 and 9 (Organising: one way to write). A `Write` says one change;
`DirectWriter` makes it at once in a short transaction, `CliWriter` queues it
for the helper, one command at a time in order on a `CliLane`. Retries run
`asma retry` on a lane of their own in both. The processor owns the writer.
Every format's build copies the CLI beside its binary as `asma-cli`.

**Files:**

- Modify: `plugin/CMakeLists.txt`
- Modify: `plugin/src/AsmaProcessor.cpp`
- Modify: `plugin/src/AsmaProcessor.h`
- Create: `plugin/src/LibraryWriter.cpp`
- Create: `plugin/src/LibraryWriter.h`
- Create: `tests/plugin/test_library_writer.cpp` (test)

**Interfaces:**

- Consumes: task 3's commands and flag; `Subprocess`; `UserData`, `Library`.
- Produces: `struct Write` (with `rate`, `favourite`, `addTag`, `removeTag`,
  `createCollection(name, withFile = 0)`, `renameCollection`,
  `deleteCollection`, `addToCollection`, `removeFromCollection`, `saveSearch`,
  `renameSearch`, `deleteSearch`); `cliCommands(const Write&)`,
  `reasonText(const std::string&)`,
  `failureText(const Write&, const std::string&)`;
  `using WriteDone = std::function<void(const std::string& error)>`;
  `struct RetryEvent { enum class Kind { Waiting, Finished, Failed }; Kind kind; std::string error; }`;
  `class CliLane`; `class LibraryWriter` (`write(const Write&, WriteDone = {})`,
  `retry(std::vector<std::int64_t>, RetryUpdate)`, `setCli`, `idle()`,
  `static cliNextTo(path)`); `DirectWriter`, `CliWriter`;
  `AsmaProcessor::writer()`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_library_writer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"
#include "LibraryFixture.h"
#include "LibraryWriter.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <memory>
#include <sstream>

using namespace asma;
namespace fs = std::filesystem;
using app::CliWriter;
using app::DirectWriter;
using app::LibraryWriter;
using app::RetryEvent;
using app::Write;

namespace {

// Runs the message loop until the writer has nothing queued or running, and
// the outcomes it posted have been delivered.
void settle(LibraryWriter& writer)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!writer.idle() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

// Everything the user has added to the library, as text.
std::string userData(const fs::path& dbPath)
{
    Db db = Db::open(dbPath);
    std::ostringstream out;
    const auto dump = [&](const char* title, int columns, const char* sql) {
        out << title << ":";
        auto q = db.prepare(sql);
        while (q.step()) {
            out << " (";
            for (int c = 0; c < columns; ++c) out << (c ? "," : "") << q.getText(c);
            out << ")";
        }
        out << "\n";
    };
    dump("ratings", 2, "SELECT f.rel_path, r.rating FROM ratings r JOIN files f ON f.id = r.file_id ORDER BY 1");
    dump("favourites", 1, "SELECT f.rel_path FROM favourites x JOIN files f ON f.id = x.file_id ORDER BY 1");
    dump("tags", 2, "SELECT f.rel_path, t.name FROM file_tags x JOIN files f ON f.id = x.file_id JOIN tags t ON t.id = "
                 "x.tag_id WHERE x.source = 'user' ORDER BY 1, 2");
    dump("collections", 2, "SELECT c.name, COALESCE(f.rel_path, '') FROM collections c LEFT JOIN collection_items i ON "
                        "i.collection_id = c.id LEFT JOIN files f ON f.id = i.file_id ORDER BY 1, 2");
    dump("searches", 2, "SELECT name, model FROM saved_searches ORDER BY 1");
    return out.str();
}

std::int64_t idOf(const fs::path& dbPath, const fs::path& file)
{
    Db db = Db::open(dbPath);
    return Library(db).fileByAbsolutePath(file).value().id;
}

// The same writes, whichever writer makes them; errors collected in order.
std::vector<std::string> writeEverything(LibraryWriter& writer, const test::LibraryFixture& f)
{
    const auto loop = idOf(f.dbPath, f.loop);
    const auto kick = idOf(f.dbPath, f.kick);
    SearchModel fast;
    fast.sort = SortField::Bpm;
    fast.descending = true;
    std::vector<std::string> errors;
    const auto collect = [&](const std::string& error) { errors.push_back(error); };
    writer.write(Write::rate(loop, 4), collect);
    writer.write(Write::rate(kick, 2), collect);
    writer.write(Write::rate(kick, 0), collect);
    writer.write(Write::favourite(kick, true), collect);
    writer.write(Write::addTag(loop, " Dusty "), collect);
    writer.write(Write::addTag(loop, "warm"), collect);
    writer.write(Write::removeTag(loop, "WARM"), collect);
    writer.write(Write::createCollection("Live set", loop), collect);
    writer.write(Write::createCollection("Album"), collect);
    writer.write(Write::addToCollection("Album", kick), collect);
    writer.write(Write::addToCollection("live set", kick), collect);
    writer.write(Write::removeFromCollection("Live set", loop), collect);
    writer.write(Write::renameCollection("Album", "Album 2"), collect);
    writer.write(Write::createCollection("Gone"), collect);
    writer.write(Write::deleteCollection("Gone"), collect);
    writer.write(Write::saveSearch("Fast", fast), collect);
    writer.write(Write::saveSearch("Old", {}), collect);
    writer.write(Write::renameSearch("Old", "Older"), collect);
    writer.write(Write::deleteSearch("Older"), collect);
    writer.write(Write::createCollection("album 2"), collect); // taken
    writer.write(Write::rate(loop, 6), collect);               // out of range
    settle(writer);
    return errors;
}

constexpr const char* kEverything = "ratings: (Loops/Bass_Loop_Am_120.wav,4)\n"
                                    "favourites: (Drums/Kick_01.wav)\n"
                                    "tags: (Loops/Bass_Loop_Am_120.wav,dusty)\n"
                                    "collections: (Album 2,Drums/Kick_01.wav) (Live set,Drums/Kick_01.wav)\n"
                                    "searches: (Fast,{\"v\":1,\"sort\":\"bpm\",\"desc\":true})\n";

} // namespace

TEST_CASE("cliCommands says each write as asma commands", "[writer]")
{
    using V = std::vector<std::vector<std::string>>;
    CHECK(app::cliCommands(Write::rate(7, 3)) == V{{"rate", "3", "--id", "7"}});
    CHECK(app::cliCommands(Write::favourite(7, false)) == V{{"fav", "off", "--id", "7"}});
    CHECK(app::cliCommands(Write::addTag(7, "dusty")) == V{{"tag", "add", "dusty", "--id", "7"}});
    CHECK(app::cliCommands(Write::createCollection("Set")) == V{{"collection", "create", "Set"}});
    CHECK(app::cliCommands(Write::createCollection("Set", 7)) ==
          V{{"collection", "create", "Set"}, {"collection", "add", "Set", "--id", "7"}});
    CHECK(app::cliCommands(Write::renameSearch("A", "B")) == V{{"search", "rename", "A", "B"}});
    SearchModel model;
    model.text = "kick";
    CHECK(app::cliCommands(Write::saveSearch("Kicks", model)) ==
          V{{"search", "save", "Kicks", "--json", "{\"v\":1,\"text\":\"kick\"}"}});
}

TEST_CASE("a failed write says what could not be done and why", "[writer]")
{
    CHECK(app::reasonText("database is locked") == "the library is busy");
    CHECK(app::reasonText("no collection called 'X'") == "no collection called 'X'");
    CHECK(app::failureText(Write::rate(1, 3), "the library is busy") == "Could not save the rating: the library is busy");
    CHECK(app::failureText(Write::addTag(1, "x"), "why") == "Could not change the tags: why");
}

TEST_CASE("both writers leave the library the same", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<LibraryWriter> writer;
    SECTION("directly")
    {
        writer = std::make_unique<DirectWriter>(f.dbPath, ASMA_CLI_PATH);
    }
    SECTION("through the helper")
    {
        writer = std::make_unique<CliWriter>(f.dbPath, ASMA_CLI_PATH);
    }
    const auto errors = writeEverything(*writer, f);
    REQUIRE(errors.size() == 21);
    for (std::size_t i = 0; i < 19; ++i) CHECK(errors[i].empty());
    CHECK(errors[19] == "a collection called 'album 2' already exists");
    CHECK(errors[20] == "a rating is 1 to 5, or 0 to clear it");
    CHECK(userData(f.dbPath) == kEverything);
}

TEST_CASE("the helper makes writes in the order given, so the last click wins", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    CliWriter writer(f.dbPath, ASMA_CLI_PATH);
    const auto loop = idOf(f.dbPath, f.loop);
    for (int rating : {1, 5, 2, 4, 3}) writer.write(Write::rate(loop, rating));
    settle(writer);
    CHECK(userData(f.dbPath).rfind("ratings: (Loops/Bass_Loop_Am_120.wav,3)\n", 0) == 0);
}

TEST_CASE("a missing helper is reported, not hidden", "[writer]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    CliWriter writer(f.dbPath, f.dir.path() / "no-such-asma");
    std::string error;
    writer.write(Write::rate(idOf(f.dbPath, f.loop), 3), [&](const std::string& e) { error = e; });
    settle(writer);
    CHECK(error == "asma's command-line helper is missing");
}

TEST_CASE("a retry waits for the scan, then reports that it finished", "[writer][retry]")
{
    test::LibraryFixture f;
    test::writeBytes(f.lib / "Drums" / "broken.wav", "not audio");
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    DirectWriter writer(f.dbPath, ASMA_CLI_PATH);
    std::vector<RetryEvent::Kind> events;
    {
        auto lock = WriterLock::tryAcquire(f.dbPath.parent_path());
        REQUIRE(lock);
        writer.retry({idOf(f.dbPath, f.lib / "Drums" / "broken.wav")},
                     [&](const RetryEvent& e) { events.push_back(e.kind); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (events.empty() && std::chrono::steady_clock::now() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    settle(writer);
    CHECK(events == std::vector<RetryEvent::Kind>{RetryEvent::Kind::Waiting, RetryEvent::Kind::Finished});
}

TEST_CASE("the helper ships as asma-cli, beside the binary", "[writer]")
{
#ifdef _WIN32
    CHECK(LibraryWriter::cliNextTo("C:/Plugins/asma.vst3/Contents/x86_64-win/asma.vst3") ==
          fs::path("C:/Plugins/asma.vst3/Contents/x86_64-win/asma-cli.exe"));
#else
    CHECK(LibraryWriter::cliNextTo("/Library/asma.vst3/Contents/MacOS/asma") ==
          fs::path("/Library/asma.vst3/Contents/MacOS/asma-cli"));
#endif
}

TEST_CASE("a plugin writes through the helper, the standalone directly", "[writer]")
{
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaProcessor plugin;
    app::AsmaProcessor standalone(app::AsmaProcessor::Mode::Standalone);
    CHECK(dynamic_cast<CliWriter*>(&plugin.writer()) != nullptr);
    CHECK(dynamic_cast<DirectWriter*>(&standalone.writer()) != nullptr);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[writer]"`

Expected: the build stops:

```
tests/plugin/test_library_writer.cpp:4:10: fatal error: 'LibraryWriter.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/CMakeLists.txt`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/CMakeLists.txt b/plugin/CMakeLists.txt
index 8a98260..a86940b 100644
--- a/plugin/CMakeLists.txt
+++ b/plugin/CMakeLists.txt
@@ -67,3 +67,14 @@ clap_juce_extensions_plugin(TARGET asma_plugin
 add_dependencies(asma_plugin_Standalone asma-scan)
 add_custom_command(TARGET asma_plugin_Standalone POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma-scan> $<TARGET_FILE_DIR:asma_plugin_Standalone>)
+# Every format writes the library through the asma helper beside its binary
+# (the standalone only for a retry). It goes in as asma-cli: the app's and
+# the bundles' own binaries are called asma.
+foreach(format Standalone VST3 AU LV2 CLAP)
+  if(TARGET asma_plugin_${format})
+    add_dependencies(asma_plugin_${format} asma)
+    add_custom_command(TARGET asma_plugin_${format} POST_BUILD
+      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma>
+              $<TARGET_FILE_DIR:asma_plugin_${format}>/asma-cli${CMAKE_EXECUTABLE_SUFFIX})
+  endif()
+endforeach()
```

In `plugin/src/AsmaProcessor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.cpp b/plugin/src/AsmaProcessor.cpp
index bf76da4..f375d6a 100644
--- a/plugin/src/AsmaProcessor.cpp
+++ b/plugin/src/AsmaProcessor.cpp
@@ -20,8 +20,16 @@ AsmaProcessor::AsmaProcessor(Mode mode)
         state_.sync.hostBpm = kDefaultBpm;
         manualBpm_.store(kDefaultBpm, std::memory_order_relaxed);
         link_ = std::make_unique<ableton::Link>(kDefaultBpm);
-        const auto app = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
-        scans_ = std::make_unique<ScanJob>(libraryPath_, ScanJob::workerNextTo(fromUtf8(app.getFullPathName().toStdString())));
+    }
+    // In a plugin, the plugin's own binary (where JUCE can tell), so the
+    // helpers are found inside its bundle.
+    const auto binary = fromUtf8(
+        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString());
+    if (standalone_) {
+        scans_ = std::make_unique<ScanJob>(libraryPath_, ScanJob::workerNextTo(binary));
+        writer_ = std::make_unique<DirectWriter>(libraryPath_, LibraryWriter::cliNextTo(binary));
+    } else {
+        writer_ = std::make_unique<CliWriter>(libraryPath_, LibraryWriter::cliNextTo(binary));
     }
     engine_.loader().start();
 }
```

In `plugin/src/AsmaProcessor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.h b/plugin/src/AsmaProcessor.h
index 8b5c2db..acfdf62 100644
--- a/plugin/src/AsmaProcessor.h
+++ b/plugin/src/AsmaProcessor.h
@@ -1,6 +1,7 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #pragma once

+#include "LibraryWriter.h"
 #include "PluginState.h"
 #include "ScanJob.h"
 #include "asma/audio/AuditionEngine.h"
@@ -94,6 +95,9 @@ public:
     // Adding folders and scanning: the standalone only; null in a plugin,
     // which never writes the library from inside the host.
     ScanJob* scans() { return scans_.get(); }
+    // What the user adds to the library goes through here: written directly
+    // in the standalone, by the asma helper in a plugin.
+    LibraryWriter& writer() { return *writer_; }

 private:
     // One preview cache for every instance in the process.
@@ -105,6 +109,7 @@ private:
     const bool standalone_;
     std::unique_ptr<ableton::Link> link_; // standalone only
     std::unique_ptr<ScanJob> scans_;      // standalone only
+    std::unique_ptr<LibraryWriter> writer_;
     static constexpr double kDefaultBpm = 120.0; // the standalone's tempo until set
     std::atomic<bool> linkOn_{false};
     std::atomic<double> manualBpm_{0.0};
```

Create `plugin/src/LibraryWriter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryWriter.h"

#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Subprocess.h"
#include "asma/core/UserData.h"

#include <juce_events/juce_events.h>

namespace asma::app {

namespace {

constexpr const char* kHelperMissing = "asma's command-line helper is missing";

Write make(Write::Kind kind)
{
    Write w;
    w.kind = kind;
    return w;
}

// Message thread: runs `call` there, from any thread.
void onMessageThread(std::function<void()> call)
{
    juce::MessageManager::callAsync(std::move(call));
}

Collection collectionNamed(UserData& user, const std::string& name)
{
    const auto collection = user.collectionByName(name);
    if (!collection) throw UserDataError("no collection called '" + name + "'");
    return *collection;
}

SavedSearch searchNamed(UserData& user, const std::string& name)
{
    const auto saved = user.savedSearchByName(name);
    if (!saved) throw UserDataError("no saved search called '" + name + "'");
    return *saved;
}

void apply(Db& db, const Write& w)
{
    Library lib(db);
    UserData user(db);
    Transaction tx(db);
    using Kind = Write::Kind;
    switch (w.kind) {
    case Kind::Rate: user.setRating(w.fileId, w.rating); break;
    case Kind::Favourite: user.setFavourite(w.fileId, w.on); break;
    case Kind::AddTag:
    case Kind::RemoveTag:
        if (!lib.fileById(w.fileId)) throw UserDataError("no file with id " + std::to_string(w.fileId));
        if (w.kind == Kind::AddTag) lib.addUserTag(w.fileId, w.name);
        else lib.removeUserTag(w.fileId, w.name);
        break;
    case Kind::CreateCollection: {
        const auto id = user.createCollection(w.name);
        if (w.fileId) user.addToCollection(id, w.fileId);
        break;
    }
    case Kind::RenameCollection: user.renameCollection(collectionNamed(user, w.name).id, w.newName); break;
    case Kind::DeleteCollection: user.deleteCollection(collectionNamed(user, w.name).id); break;
    case Kind::AddToCollection: user.addToCollection(collectionNamed(user, w.name).id, w.fileId); break;
    case Kind::RemoveFromCollection: user.removeFromCollection(collectionNamed(user, w.name).id, w.fileId); break;
    case Kind::SaveSearch: user.saveSearch(w.name, w.model); break;
    case Kind::RenameSearch: user.renameSavedSearch(searchNamed(user, w.name).id, w.newName); break;
    case Kind::DeleteSearch: user.deleteSavedSearch(searchNamed(user, w.name).id); break;
    }
    tx.commit();
}

} // namespace

Write Write::rate(std::int64_t fileId, int rating)
{
    Write w = make(Kind::Rate);
    w.fileId = fileId;
    w.rating = rating;
    return w;
}

Write Write::favourite(std::int64_t fileId, bool on)
{
    Write w = make(Kind::Favourite);
    w.fileId = fileId;
    w.on = on;
    return w;
}

Write Write::addTag(std::int64_t fileId, std::string tag)
{
    Write w = make(Kind::AddTag);
    w.fileId = fileId;
    w.name = std::move(tag);
    return w;
}

Write Write::removeTag(std::int64_t fileId, std::string tag)
{
    Write w = make(Kind::RemoveTag);
    w.fileId = fileId;
    w.name = std::move(tag);
    return w;
}

Write Write::createCollection(std::string name, std::int64_t withFile)
{
    Write w = make(Kind::CreateCollection);
    w.name = std::move(name);
    w.fileId = withFile;
    return w;
}

Write Write::renameCollection(std::string name, std::string newName)
{
    Write w = make(Kind::RenameCollection);
    w.name = std::move(name);
    w.newName = std::move(newName);
    return w;
}

Write Write::deleteCollection(std::string name)
{
    Write w = make(Kind::DeleteCollection);
    w.name = std::move(name);
    return w;
}

Write Write::addToCollection(std::string collection, std::int64_t fileId)
{
    Write w = make(Kind::AddToCollection);
    w.name = std::move(collection);
    w.fileId = fileId;
    return w;
}

Write Write::removeFromCollection(std::string collection, std::int64_t fileId)
{
    Write w = make(Kind::RemoveFromCollection);
    w.name = std::move(collection);
    w.fileId = fileId;
    return w;
}

Write Write::saveSearch(std::string name, SearchModel model)
{
    Write w = make(Kind::SaveSearch);
    w.name = std::move(name);
    w.model = std::move(model);
    return w;
}

Write Write::renameSearch(std::string name, std::string newName)
{
    Write w = make(Kind::RenameSearch);
    w.name = std::move(name);
    w.newName = std::move(newName);
    return w;
}

Write Write::deleteSearch(std::string name)
{
    Write w = make(Kind::DeleteSearch);
    w.name = std::move(name);
    return w;
}

std::vector<std::vector<std::string>> cliCommands(const Write& w)
{
    const std::string id = std::to_string(w.fileId);
    using Kind = Write::Kind;
    switch (w.kind) {
    case Kind::Rate: return {{"rate", std::to_string(w.rating), "--id", id}};
    case Kind::Favourite: return {{"fav", w.on ? "on" : "off", "--id", id}};
    case Kind::AddTag: return {{"tag", "add", w.name, "--id", id}};
    case Kind::RemoveTag: return {{"tag", "remove", w.name, "--id", id}};
    case Kind::CreateCollection:
        if (!w.fileId) return {{"collection", "create", w.name}};
        return {{"collection", "create", w.name}, {"collection", "add", w.name, "--id", id}};
    case Kind::RenameCollection: return {{"collection", "rename", w.name, w.newName}};
    case Kind::DeleteCollection: return {{"collection", "delete", w.name}};
    case Kind::AddToCollection: return {{"collection", "add", w.name, "--id", id}};
    case Kind::RemoveFromCollection: return {{"collection", "remove", w.name, "--id", id}};
    case Kind::SaveSearch: return {{"search", "save", w.name, "--json", searchModelToJson(w.model)}};
    case Kind::RenameSearch: return {{"search", "rename", w.name, w.newName}};
    case Kind::DeleteSearch: return {{"search", "delete", w.name}};
    }
    return {};
}

std::string reasonText(const std::string& error)
{
    if (error.find("locked") != std::string::npos || error.find("busy") != std::string::npos)
        return "the library is busy";
    return error;
}

std::string failureText(const Write& w, const std::string& reason)
{
    using Kind = Write::Kind;
    const char* what = "";
    switch (w.kind) {
    case Kind::Rate: what = "save the rating"; break;
    case Kind::Favourite: what = "save the favourite"; break;
    case Kind::AddTag:
    case Kind::RemoveTag: what = "change the tags"; break;
    case Kind::CreateCollection: what = "make the collection"; break;
    case Kind::RenameCollection: what = "rename the collection"; break;
    case Kind::DeleteCollection: what = "delete the collection"; break;
    case Kind::AddToCollection:
    case Kind::RemoveFromCollection: what = "change the collection"; break;
    case Kind::SaveSearch: what = "save the search"; break;
    case Kind::RenameSearch: what = "rename the search"; break;
    case Kind::DeleteSearch: what = "delete the search"; break;
    }
    return std::string("Could not ") + what + ": " + reason;
}

// --- CliLane ---------------------------------------------------------------

struct CliLane::Running {
    Subprocess* process = nullptr; // guarded by the lane's mutex
};

CliLane::CliLane(std::filesystem::path dbPath, std::filesystem::path cli)
    : dbPath_(std::move(dbPath)), cli_(std::move(cli)), current_(std::make_shared<Running>())
{
    thread_ = std::thread([this] { loop(); });
}

CliLane::~CliLane()
{
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
        if (current_->process) current_->process->kill(); // a retry may be waiting for a scan
    }
    wake_.notify_all();
    thread_.join();
}

void CliLane::run(Command command)
{
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(std::move(command));
    }
    wake_.notify_all();
}

void CliLane::setCli(std::filesystem::path cli)
{
    const std::lock_guard lock(mutex_);
    cli_ = std::move(cli);
}

bool CliLane::idle() const
{
    const std::lock_guard lock(mutex_);
    return queue_.empty() && !running_;
}

void CliLane::loop()
{
    for (;;) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            command = std::move(queue_.front());
            queue_.pop_front();
            running_ = true;
        }
        std::string error;
        for (const auto& step : command.steps) {
            error = runStep(step, command.onLine);
            if (!error.empty()) break;
        }
        {
            const std::lock_guard lock(mutex_);
            if (stopping_) return; // nobody is waiting for the outcome
        }
        if (command.onEnd) onMessageThread([end = std::move(command.onEnd), error] { end(error); });
        // Idle only once the outcome is on its way, so a test that waits for
        // idle and then runs the message loop sees it.
        const std::lock_guard lock(mutex_);
        running_ = false;
    }
}

std::string CliLane::runStep(const std::vector<std::string>& step, const std::function<void(const std::string&)>& onLine)
{
    std::vector<std::string> args{"--db", toUtf8(dbPath_), "--errors-to-stdout"};
    args.insert(args.end(), step.begin(), step.end());
    std::filesystem::path cli;
    {
        const std::lock_guard lock(mutex_);
        cli = cli_;
    }
    std::optional<Subprocess> process;
    try {
        process = Subprocess::start(cli, args);
    } catch (const SubprocessError&) {
        return kHelperMissing;
    }
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) return "stopped";
        current_->process = &*process;
    }
    std::string error;
    while (const auto line = process->readLine()) {
        if (line->rfind("error: ", 0) == 0) error = reasonText(line->substr(7));
        else if (onLine) onMessageThread([onLine, text = *line] { onLine(text); });
    }
    const ExitStatus status = process->wait();
    {
        const std::lock_guard lock(mutex_);
        current_->process = nullptr;
    }
    if (status.signalled || status.code != 0)
        return error.empty() ? "the helper stopped with code " + std::to_string(status.code) : error;
    return {};
}

// --- LibraryWriter ---------------------------------------------------------

LibraryWriter::LibraryWriter(std::filesystem::path dbPath, std::filesystem::path cli)
    : dbPath_(std::move(dbPath)), retries_(dbPath_, std::move(cli))
{
}

void LibraryWriter::retry(std::vector<std::int64_t> fileIds, RetryUpdate update)
{
    if (fileIds.empty()) return;
    std::vector<std::string> step{"retry"};
    for (const auto id : fileIds) {
        step.push_back("--id");
        step.push_back(std::to_string(id));
    }
    CliLane::Command command;
    command.steps = {std::move(step)};
    command.onLine = [update](const std::string& line) {
        if (line == "waiting for the scan" && update) update({RetryEvent::Kind::Waiting, {}});
    };
    command.onEnd = [update](const std::string& error) {
        if (update) update({error.empty() ? RetryEvent::Kind::Finished : RetryEvent::Kind::Failed, error});
    };
    retries_.run(std::move(command));
}

void LibraryWriter::setCli(std::filesystem::path cli) { retries_.setCli(std::move(cli)); }

bool LibraryWriter::idle() const { return retries_.idle(); }

std::filesystem::path LibraryWriter::cliNextTo(const std::filesystem::path& binary)
{
#ifdef _WIN32
    return binary.parent_path() / "asma-cli.exe";
#else
    return binary.parent_path() / "asma-cli";
#endif
}

void DirectWriter::write(const Write& w, WriteDone done)
{
    std::string error;
    try {
        if (!db_) db_ = Db::open(dbPath_);
        apply(*db_, w);
    } catch (const std::exception& e) {
        db_.reset(); // a broken connection is not kept for the next write
        error = reasonText(e.what());
    }
    if (done) done(error);
}

CliWriter::CliWriter(std::filesystem::path dbPath, std::filesystem::path cli)
    : LibraryWriter(dbPath, cli), writes_(dbPath, cli)
{
}

void CliWriter::write(const Write& w, WriteDone done)
{
    CliLane::Command command;
    command.steps = cliCommands(w);
    command.onEnd = std::move(done);
    writes_.run(std::move(command));
}

void CliWriter::setCli(std::filesystem::path cli)
{
    LibraryWriter::setCli(cli);
    writes_.setCli(std::move(cli));
}

bool CliWriter::idle() const { return LibraryWriter::idle() && writes_.idle(); }

} // namespace asma::app
```

Create `plugin/src/LibraryWriter.h`:

```cpp
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
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[writer]"`

Expected: `All tests passed (64 assertions in 8 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/CMakeLists.txt plugin/src/AsmaProcessor.cpp plugin/src/AsmaProcessor.h plugin/src/LibraryWriter.cpp plugin/src/LibraryWriter.h tests/plugin/test_library_writer.cpp
git commit -m "app: one writer for what the user adds, direct in the standalone and through asma-cli in a plugin"
```

---

### Task 5: Favourite and rate from the table

Spec section 9 (Organising: shown at once, the table). Clicking a row's star
toggles its favourite; clicking a rating star rates, and the rating it has
clears it; F and 0 to 5 do the same to the selection unless a text field has
focus. `PendingEdits` shows each change until the library's next change after
its write; a failed write rolls back and the footer says why. The editor's test
rig moves to `EditorRig.h` so the organise tests share it.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Create: `plugin/src/PendingEdits.cpp`
- Create: `plugin/src/PendingEdits.h`
- Create: `tests/plugin/EditorRig.h` (test)
- Modify: `tests/plugin/PluginTestUtil.h` (test)
- Modify: `tests/plugin/test_editor.cpp` (test)
- Create: `tests/plugin/test_organise.cpp` (test)
- Create: `tests/plugin/test_pending_edits.cpp` (test)

**Interfaces:**

- Consumes: task 4's writer.
- Produces: `class PendingEdits` (`setRating`, `setFavourite`, `setTags`
  returning a ticket, `finished(ticket, ok)`, `libraryChanged()`,
  `apply(SearchRow)`); `AsmaEditor::toggleFavourite`,
  `rate(const SearchRow&, int)`, `shownRow(int)`, `static starAt(int x)`; tests'
  `EditorRig`, `test::mouseAt`, `test::clickCell`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/EditorRig.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// The window over a scanned library, headless, as a user would drive it.
#pragma once

#include "AsmaEditor.h"
#include "LibraryFixture.h"
#include "PluginTestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <memory>
#include <thread>

namespace asma::test {

struct EditorRig {
    LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<app::AsmaProcessor> p;
    std::unique_ptr<app::AsmaEditor> editor;
    explicit EditorRig(app::AsmaProcessor::Mode mode = app::AsmaProcessor::Mode::FromWrapper)
    {
        f.scan();
        p = std::make_unique<app::AsmaProcessor>(mode);
        p->prepareToPlay(48000.0, 512);
        editor.reset(dynamic_cast<app::AsmaEditor*>(p->createEditorAndMakeActive()));
        REQUIRE(editor);
        editor->poll(); // what its timer does
    }
    ~EditorRig()
    {
        p->editorBeingDeleted(editor.get());
        editor.reset();
    }
    // Types into the search box the way a user would: the change arrives
    // through the message loop.
    void type(const char* text)
    {
        editor->searchBox().setText(text, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    // Blocks until the preview for the current selection is playing.
    bool playing()
    {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            p->processBlock(buffer, midi);
            if (p->engine().status().playing) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
};

} // namespace asma::test
```

In `tests/plugin/PluginTestUtil.h`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/PluginTestUtil.h b/tests/plugin/PluginTestUtil.h
index 7d0760e..f0ee764 100644
--- a/tests/plugin/PluginTestUtil.h
+++ b/tests/plugin/PluginTestUtil.h
@@ -40,4 +40,23 @@ inline bool waitForPreview(app::AsmaProcessor& p, std::uint64_t generation, int
     return false;
 }

+// A mouse event at `at` in `c`, as a click there would give.
+inline juce::MouseEvent mouseAt(juce::Component& c, juce::Point<int> at, juce::ModifierKeys mods = {})
+{
+    auto source = juce::Desktop::getInstance().getMainMouseSource();
+    const auto p = at.toFloat();
+    const auto now = juce::Time::getCurrentTime();
+    return juce::MouseEvent(source, p, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
+                            now, p, now, 1, false);
+}
+
+// Clicks a table cell the way its row would report it, `x` from the cell's
+// left (a right click with mods = rightButtonModifier).
+inline void clickCell(juce::TableListBox& table, int row, int column, int x, juce::ModifierKeys mods = {})
+{
+    auto& header = table.getHeader();
+    const int left = header.getColumnPosition(header.getIndexOfColumnId(column, true)).getX();
+    table.getTableListBoxModel()->cellClicked(row, column, mouseAt(table, {left + x, 5}, mods));
+}
+
 } // namespace asma::test
```

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index 09afab7..3848712 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -1,7 +1,5 @@
 // SPDX-License-Identifier: GPL-3.0-only
-#include "AsmaEditor.h"
-#include "LibraryFixture.h"
-#include "PluginTestUtil.h"
+#include "EditorRig.h"
 #include "asma/core/Analyser.h"
 #include "asma/core/UserData.h"
 #include "asma/audio/Render.h"
@@ -14,51 +12,7 @@ using namespace asma;
 namespace fs = std::filesystem;
 using app::AsmaEditor;
 using app::AsmaProcessor;
-
-namespace {
-
-struct EditorRig {
-    test::LibraryFixture f;
-    const juce::ScopedJuceInitialiser_GUI gui;
-    std::unique_ptr<AsmaProcessor> p;
-    std::unique_ptr<AsmaEditor> editor;
-    explicit EditorRig(AsmaProcessor::Mode mode = AsmaProcessor::Mode::FromWrapper)
-    {
-        f.scan();
-        p = std::make_unique<AsmaProcessor>(mode);
-        p->prepareToPlay(48000.0, 512);
-        editor.reset(dynamic_cast<AsmaEditor*>(p->createEditorAndMakeActive()));
-        REQUIRE(editor);
-        editor->poll(); // what its timer does
-    }
-    ~EditorRig()
-    {
-        p->editorBeingDeleted(editor.get());
-        editor.reset();
-    }
-    // Types into the search box the way a user would: the change arrives
-    // through the message loop.
-    void type(const char* text)
-    {
-        editor->searchBox().setText(text, true);
-        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
-    }
-    // Blocks until the preview for the current selection is playing.
-    bool playing()
-    {
-        juce::AudioBuffer<float> buffer(2, 512);
-        juce::MidiBuffer midi;
-        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
-        while (std::chrono::steady_clock::now() < deadline) {
-            p->processBlock(buffer, midi);
-            if (p->engine().status().playing) return true;
-            std::this_thread::sleep_for(std::chrono::milliseconds(1));
-        }
-        return false;
-    }
-};
-
-} // namespace
+using test::EditorRig;

 TEST_CASE("typing in the search box filters the table", "[editor]")
 {
```

Create `tests/plugin/test_organise.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "asma/core/Fs.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
namespace fs = std::filesystem;
using app::AsmaEditor;
using app::AsmaProcessor;
using test::EditorRig;

namespace {

constexpr int kFavourite = 1, kRating = 7; // the table's column ids

// Runs the message loop until the processor's writer is done, then checks
// the library as the editor's timer would.
void settle(EditorRig& rig)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!rig.p->writer().idle() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    rig.editor->poll();
}

// The x in the rating cell that lands on a star.
int xOfStar(int star)
{
    for (int x = 0; x < 200; ++x)
        if (AsmaEditor::starAt(x) == star) return x + 1;
    return -1;
}

std::optional<int> storedRating(EditorRig& rig, const fs::path& file)
{
    Db db = Db::open(rig.f.dbPath);
    return UserData(db).rating(Library(db).fileByAbsolutePath(file)->id);
}

bool storedFavourite(EditorRig& rig, const fs::path& file)
{
    Db db = Db::open(rig.f.dbPath);
    return UserData(db).isFavourite(Library(db).fileByAbsolutePath(file)->id);
}

} // namespace

TEST_CASE("the rating column's stars are where it draws them", "[organise]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    CHECK(AsmaEditor::starAt(-1) == 0);
    CHECK(AsmaEditor::starAt(0) == 1);
    CHECK(xOfStar(1) < xOfStar(2));
    CHECK(xOfStar(4) < xOfStar(5));
    CHECK(AsmaEditor::starAt(200) == 0);
}

TEST_CASE("clicking a row's star favourites it at once, and the library confirms it", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kFavourite, 10);
    CHECK(rig.editor->shownRow(0)->favourite);
    settle(rig);
    CHECK(storedFavourite(rig, rig.f.kick));
    CHECK(rig.editor->shownRow(0)->favourite); // now from the library
    test::clickCell(rig.editor->table(), 0, kFavourite, 10);
    settle(rig);
    CHECK_FALSE(storedFavourite(rig, rig.f.kick));
}

TEST_CASE("clicking a star rates, and clicking the rating it has clears it", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(3));
    CHECK(rig.editor->shownRow(0)->rating == 3);
    settle(rig);
    CHECK(storedRating(rig, rig.f.kick) == 3);
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(3));
    CHECK_FALSE(rig.editor->shownRow(0)->rating);
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
    test::clickCell(rig.editor->table(), 0, kRating, 200); // past the stars: nothing
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
}

TEST_CASE("in a plugin the helper writes, and the row shows the change before it does", "[organise]")
{
    EditorRig rig;
    rig.p->writer().setCli(ASMA_CLI_PATH);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(5));
    CHECK(rig.editor->shownRow(0)->rating == 5); // before the helper has run
    settle(rig);
    CHECK(storedRating(rig, rig.f.kick) == 5);
    CHECK(rig.editor->shownRow(0)->rating == 5);
}

TEST_CASE("a write that fails rolls back and the footer says why", "[organise]")
{
    EditorRig rig;
    rig.p->writer().setCli(rig.f.dir.path() / "no-such-asma");
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kFavourite, 10);
    CHECK(rig.editor->shownRow(0)->favourite);
    settle(rig);
    CHECK_FALSE(rig.editor->shownRow(0)->favourite);
    CHECK(rig.editor->footer().rightText().contains(
        "Could not save the favourite: asma's command-line helper is missing"));
}

TEST_CASE("F and 0 to 5 organise the selection", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    CHECK_FALSE(rig.editor->keyPressed(juce::KeyPress('f', {}, 'f'))); // nothing selected
    rig.editor->table().selectRow(0);
    CHECK(rig.editor->keyPressed(juce::KeyPress('f', {}, 'f')));
    CHECK(rig.editor->keyPressed(juce::KeyPress('4', {}, '4')));
    CHECK_FALSE(rig.editor->keyPressed(juce::KeyPress('2', juce::ModifierKeys::commandModifier, '2')));
    settle(rig);
    CHECK(storedFavourite(rig, rig.f.kick));
    CHECK(storedRating(rig, rig.f.kick) == 4);
    CHECK(rig.editor->keyPressed(juce::KeyPress('0', {}, '0')));
    CHECK(rig.editor->keyPressed(juce::KeyPress('F', {}, 'F')));
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
    CHECK_FALSE(storedFavourite(rig, rig.f.kick));
}
```

Create `tests/plugin/test_pending_edits.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PendingEdits.h"

#include <catch2/catch_test_macros.hpp>

using asma::SearchRow;
using asma::app::PendingEdits;

namespace {

SearchRow row(std::int64_t id)
{
    SearchRow r;
    r.id = id;
    r.rating = 2;
    r.tags = {"bass"};
    return r;
}

} // namespace

TEST_CASE("a pending edit shows until the library has it", "[pending]")
{
    PendingEdits pending;
    const auto rated = pending.setRating(1, 4);
    const auto fav = pending.setFavourite(1, true);
    CHECK(pending.apply(row(1)).rating == 4);
    CHECK(pending.apply(row(1)).favourite);
    CHECK(pending.apply(row(2)).rating == 2); // other rows as they are

    pending.libraryChanged(); // not written yet: a change from elsewhere
    CHECK(pending.apply(row(1)).rating == 4);
    pending.finished(rated, true);
    pending.finished(fav, true);
    CHECK(pending.apply(row(1)).rating == 4); // written, the rows not yet refetched
    pending.libraryChanged();
    CHECK(pending.empty());
}

TEST_CASE("a failed write rolls its edit back at once", "[pending]")
{
    PendingEdits pending;
    const auto ticket = pending.setRating(1, 5);
    pending.finished(ticket, false);
    CHECK(pending.apply(row(1)).rating == 2);
    CHECK(pending.empty());
}

TEST_CASE("the last edit wins, and an earlier one failing leaves it", "[pending]")
{
    PendingEdits pending;
    const auto first = pending.setRating(1, 5);
    const auto second = pending.setRating(1, 0);
    CHECK_FALSE(pending.apply(row(1)).rating);
    pending.finished(first, false);
    CHECK_FALSE(pending.apply(row(1)).rating);
    pending.finished(second, true);
    pending.libraryChanged();
    CHECK(pending.empty());
}

TEST_CASE("pending tags replace the row's, sorted", "[pending]")
{
    PendingEdits pending;
    pending.setTags(1, {"warm", "bass"});
    CHECK(pending.apply(row(1)).tags == std::vector<std::string>{"bass", "warm"});
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[organise],[pending]"`

Expected: the build stops:

```
tests/plugin/test_pending_edits.cpp:2:10: fatal error: 'PendingEdits.h' file not found
tests/plugin/test_organise.cpp:33:13: error: no member named 'starAt' in 'asma::app::AsmaEditor'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 330db01..f74d490 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -34,6 +34,9 @@ std::optional<SortField> sortFor(int column)

 juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

+// The rating column's stars, lit and faint alike.
+juce::Font starFont() { return theme::font(theme::Face::Text, 11.0f).withExtraKerningFactor(0.15f); }
+
 } // namespace

 AsmaEditor::AsmaEditor(AsmaProcessor& owner)
@@ -238,9 +241,78 @@ bool AsmaEditor::keyPressed(const juce::KeyPress& key)
         else processor_.engine().play();
         return true;
     }
+    // F and 0 to 5 organise the selection. A text field keeps what is typed
+    // in it; this is for keys that reach the window from anywhere else.
+    const int c = key.getTextCharacter() != 0 ? static_cast<int>(key.getTextCharacter()) : key.getKeyCode();
+    const bool typing = dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
+    if (!typing && selected_ && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown()) {
+        const auto row = pending_.apply(*selected_);
+        if (c == 'f' || c == 'F') {
+            toggleFavourite(row);
+            return true;
+        }
+        if (c >= '0' && c <= '5') {
+            const int stars = static_cast<int>(c - '0');
+            if (stars != row.rating.value_or(0)) rate(row, stars);
+            return true;
+        }
+    }
     return false;
 }

+int AsmaEditor::starAt(int x)
+{
+    const float advance =
+        juce::GlyphArrangement::getStringWidth(starFont(), juce::String::fromUTF8("\u2605\u2605\u2605\u2605\u2605")) / 5.0f;
+    if (x < 0 || advance <= 0.0f) return 0;
+    const int star = static_cast<int>(static_cast<float>(x) / advance) + 1;
+    return star <= 5 ? star : 0;
+}
+
+std::optional<SearchRow> AsmaEditor::shownRow(int row)
+{
+    const SearchRow* found = browser_.row(row);
+    if (!found) return std::nullopt;
+    return pending_.apply(*found);
+}
+
+void AsmaEditor::toggleFavourite(const SearchRow& row)
+{
+    const bool on = !pending_.apply(row).favourite;
+    write(Write::favourite(row.id, on), pending_.setFavourite(row.id, on));
+}
+
+void AsmaEditor::rate(const SearchRow& row, int stars)
+{
+    const int rating = pending_.apply(row).rating == stars ? 0 : stars;
+    write(Write::rate(row.id, rating), pending_.setRating(row.id, rating));
+}
+
+void AsmaEditor::write(const Write& w, std::uint64_t ticket)
+{
+    table_.repaint();
+    processor_.writer().write(w, [safe = juce::Component::SafePointer<AsmaEditor>(this), w, ticket](const std::string& error) {
+        if (!safe) return;
+        safe->pending_.finished(ticket, error.empty());
+        if (!error.empty()) safe->scanMessage_ = utf8(failureText(w, error));
+        safe->table_.repaint();
+        safe->updateReadouts();
+    });
+}
+
+void AsmaEditor::cellClicked(int row, int column, const juce::MouseEvent& event)
+{
+    const auto shown = shownRow(row);
+    if (!shown || event.mods.isPopupMenu()) return;
+    if (column == kFavourite) toggleFavourite(*shown);
+    if (column == kRating) {
+        // The event is the row's: measure from the cell's left.
+        auto& header = table_.getHeader();
+        const int left = header.getColumnPosition(header.getIndexOfColumnId(kRating, true)).getX();
+        if (const int star = starAt(event.x - left)) rate(*shown, star);
+    }
+}
+
 void AsmaEditor::chooseFolder()
 {
     chooser_ = std::make_unique<juce::FileChooser>("Add a sample folder");
@@ -308,6 +380,7 @@ void AsmaEditor::poll()
             }
         }
     if (browser_.poll()) {
+        pending_.libraryChanged();
         table_.updateContent();
         similarFor_ = 0; // the library changed: analysis may have reached the selection
         showSelection();
@@ -493,9 +566,9 @@ void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int width, int heigh
 void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
 {
     if (row < 0 || row >= getNumRows()) return;
-    const SearchRow* found = browser_.row(row);
-    if (!found) return;
-    const SearchRow& r = *found;
+    const auto shown = shownRow(row);
+    if (!shown) return;
+    const SearchRow& r = *shown;
     juce::String text;
     juce::Font font = theme::font(theme::Face::Mono, 12.0f);
     juce::Colour colour = theme::text;
@@ -510,7 +583,7 @@ void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, in
     case kRating: {
         // Lit stars for the rating, faint ones for the rest.
         const int lit = r.rating.value_or(0);
-        g.setFont(theme::font(theme::Face::Text, 11.0f).withExtraKerningFactor(0.15f));
+        g.setFont(starFont());
         juce::String on, off;
         for (int i = 0; i < 5; ++i) (i < lit ? on : off) << juce::String::fromUTF8("\u2605");
         const int onWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), on)));
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index 6498444..c2e6ce8 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -3,6 +3,8 @@

 #include "Browser.h"
 #include "LibraryView.h"
+#include "LibraryWriter.h"
+#include "PendingEdits.h"
 #include "Sidebar.h"
 #include "ui/AsmaLookAndFeel.h"
 #include "ui/ChipRow.h"
@@ -68,6 +70,16 @@ public:
     // Deletes every kept render; the footer's button asks first.
     void clearRenders();

+    // Organising: the table shows each change at once and the library
+    // confirms it; a write that fails rolls back and the footer says why.
+    void toggleFavourite(const SearchRow& row);
+    void rate(const SearchRow& row, int stars); // the rating it has clears it
+    // The table's row as shown, pending edits included; null out of range.
+    std::optional<SearchRow> shownRow(int row);
+    // Which star of the rating column an x (from the cell's left) falls on,
+    // 1 to 5; 0 past the fifth.
+    static int starAt(int x);
+
 private:
     enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
     // TableListBoxModel
@@ -77,6 +89,7 @@ private:
     void selectedRowsChanged(int lastRowSelected) override;
     void sortOrderChanged(int newSortColumnId, bool isForwards) override;
     void returnKeyPressed(int lastRowSelected) override;
+    void cellClicked(int row, int column, const juce::MouseEvent& event) override;
     juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
     void timerCallback() override;
     void searchChanged();
@@ -94,6 +107,8 @@ private:
     void select(const SearchRow& row); // auditions it as the selection, as it is
     void updateReadouts();  // the preview, the chips, the footer, the empty state
     void updateRenderSize();
+    // Sends a write; `ticket` is its pending edit (0: none).
+    void write(const Write& write, std::uint64_t ticket = 0);

     AsmaProcessor& processor_;
     AsmaLookAndFeel lookAndFeel_; // first in, last out: every child uses it
@@ -110,7 +125,7 @@ private:
     PreviewPanel preview_;
     Footer footer_;
     std::unique_ptr<juce::FileChooser> chooser_;
-    juce::String scanMessage_; // the last scan's outcome, until the next selection
+    juce::String scanMessage_; // the last scan's outcome or failed write, until the next selection
     bool quietSelection_ = false;    // selection changes that must not play
     std::uint64_t loadedStates_ = 0; // the processor's stateLoads() the controls show
     int ticks_ = 0;
@@ -119,6 +134,7 @@ private:
     std::int64_t similarFor_ = 0;       // the sample the Similar list is about
     audio::SampleInfo selectedInfo_;
     std::string selectedFolder_;
+    PendingEdits pending_;

     JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
 };
```

Create `plugin/src/PendingEdits.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PendingEdits.h"

#include <algorithm>

namespace asma::app {

std::uint64_t PendingEdits::add(Edit edit)
{
    edit.ticket = next_++;
    edits_.push_back(std::move(edit));
    return edits_.back().ticket;
}

std::uint64_t PendingEdits::setRating(std::int64_t fileId, int rating)
{
    Edit e;
    e.fileId = fileId;
    e.field = Field::Rating;
    e.rating = rating;
    return add(std::move(e));
}

std::uint64_t PendingEdits::setFavourite(std::int64_t fileId, bool favourite)
{
    Edit e;
    e.fileId = fileId;
    e.field = Field::Favourite;
    e.favourite = favourite;
    return add(std::move(e));
}

std::uint64_t PendingEdits::setTags(std::int64_t fileId, std::vector<std::string> tags)
{
    Edit e;
    e.fileId = fileId;
    e.field = Field::Tags;
    std::sort(tags.begin(), tags.end());
    e.tags = std::move(tags);
    return add(std::move(e));
}

void PendingEdits::finished(std::uint64_t ticket, bool ok)
{
    const auto it = std::find_if(edits_.begin(), edits_.end(), [&](const Edit& e) { return e.ticket == ticket; });
    if (it == edits_.end()) return;
    if (ok) it->written = true;
    else edits_.erase(it);
}

void PendingEdits::libraryChanged()
{
    edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [](const Edit& e) { return e.written; }), edits_.end());
}

SearchRow PendingEdits::apply(SearchRow row) const
{
    for (const auto& e : edits_) {
        if (e.fileId != row.id) continue;
        switch (e.field) {
        case Field::Rating: row.rating = e.rating ? std::optional<int>(e.rating) : std::nullopt; break;
        case Field::Favourite: row.favourite = e.favourite; break;
        case Field::Tags: row.tags = e.tags; break;
        }
    }
    return row;
}

} // namespace asma::app
```

Create `plugin/src/PendingEdits.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Query.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace asma::app {

// What the table shows before the library has it: a rating, a favourite or a
// sample's tags, changed by the user and on its way to the library. Each edit
// has a ticket. When its write succeeds the edit waits for the library's
// next change notice, which brings the stored value; when the write fails it
// goes at once, so the row shows what the library holds. JUCE-free.
class PendingEdits {
public:
    std::uint64_t setRating(std::int64_t fileId, int rating);
    std::uint64_t setFavourite(std::int64_t fileId, bool favourite);
    std::uint64_t setTags(std::int64_t fileId, std::vector<std::string> tags);

    // The write behind a ticket ended.
    void finished(std::uint64_t ticket, bool ok);
    // The library changed: written edits are in the rows now.
    void libraryChanged();

    // The row as it will be once every pending edit is in.
    SearchRow apply(SearchRow row) const;
    bool empty() const { return edits_.empty(); }

private:
    enum class Field { Rating, Favourite, Tags };
    struct Edit {
        std::uint64_t ticket = 0;
        std::int64_t fileId = 0;
        Field field = Field::Rating;
        int rating = 0;
        bool favourite = false;
        std::vector<std::string> tags;
        bool written = false;
    };
    std::uint64_t add(Edit edit);

    std::vector<Edit> edits_; // oldest first
    std::uint64_t next_ = 1;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[organise],[pending]"`

Expected: `All tests passed (47 assertions in 10 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/PendingEdits.cpp plugin/src/PendingEdits.h tests/plugin/EditorRig.h tests/plugin/PluginTestUtil.h tests/plugin/test_editor.cpp tests/plugin/test_organise.cpp tests/plugin/test_pending_edits.cpp
git commit -m "app: favourite and rate from the table, shown at once and confirmed by the library"
```

---

### Task 6: Collections and saved searches from the sidebar; Save search

Spec section 9 (Organising: the sidebar, Save search, names). COLLECTIONS always
shows, with a "+" that opens a name field in place; right-clicking a collection
or saved search gives Rename… (the same field) and Delete. Names are checked as
the library checks them, and renaming to the name it has is no change. Deleting
asks first only for a collection with samples, and the lit entry falls back to
All samples. The chip row's Save search opens a `NamePopover`. The main
reference picture gains Save search.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Create: `plugin/src/Names.cpp`
- Create: `plugin/src/Names.h`
- Modify: `plugin/src/ui/ChipRow.cpp`
- Modify: `plugin/src/ui/ChipRow.h`
- Create: `plugin/src/ui/NamePopover.cpp`
- Create: `plugin/src/ui/NamePopover.h`
- Modify: `plugin/src/ui/SidebarView.cpp`
- Modify: `plugin/src/ui/SidebarView.h`
- Modify: `plugin/src/ui/Theme.h`
- Create: `tests/plugin/test_names.cpp` (test)
- Modify: `tests/plugin/test_organise.cpp` (test)
- Modify: `tests/plugin/test_sidebar_view.cpp` (test)
- Modify: `tests/ui/reference/main.html` (test)
- Modify: `tests/ui/reference/main.png` (test)

**Interfaces:**

- Consumes: task 4's writes; task 5's rig.
- Produces: `enum class NameCheck { Ok, Empty, Taken, Unchanged }`, `checkName`,
  `trimmedName`; `class NamePopover` (`field()`, `saveButton()`,
  `cancelButton()`, `refusalText()`); `SidebarView::nameRefusal`, `onNamed`,
  `onDelete`, `startNewCollection()`, `startRename(int)`, `isEditing()`,
  `nameField()`, `refusalText()`, `addButton()`, `entryMenu(int)`,
  `entryMenuChosen(int, int)`, `kRename`, `kDelete`; `ChipRow::onSaveSearch`,
  `saveSearchButton()`; `AsmaEditor::deleteEntry(int)`,
  `static asksBeforeDeleting(const SidebarEntry&)`, `saveSearchPopover()`;
  `theme::refused`, `theme::refusedText`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_names.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Names.h"

#include <catch2/catch_test_macros.hpp>

using asma::app::checkName;
using asma::app::NameCheck;

TEST_CASE("names are trimmed, needed, and unique ignoring case", "[names]")
{
    const std::vector<std::string> existing{"Live set", "Album 2"};
    CHECK(checkName("Basslines", existing) == NameCheck::Ok);
    CHECK(checkName("   ", existing) == NameCheck::Empty);
    CHECK(checkName(" live SET ", existing) == NameCheck::Taken);
    CHECK(asma::app::trimmedName("  Album 3 ") == "Album 3");
}

TEST_CASE("renaming to the name it has is no change, and its own name in other case is fine", "[names]")
{
    const std::vector<std::string> existing{"Live set", "Album 2"};
    CHECK(checkName("Live set", existing, "Live set") == NameCheck::Unchanged);
    CHECK(checkName(" Live set ", existing, "Live set") == NameCheck::Unchanged);
    CHECK(checkName("LIVE SET", existing, "Live set") == NameCheck::Ok);
    CHECK(checkName("album 2", existing, "Live set") == NameCheck::Taken);
}
```

In `tests/plugin/test_organise.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_organise.cpp b/tests/plugin/test_organise.cpp
index ca5881a..d1ad6c6 100644
--- a/tests/plugin/test_organise.cpp
+++ b/tests/plugin/test_organise.cpp
@@ -34,6 +34,37 @@ int xOfStar(int star)
     return -1;
 }

+// The sidebar entry with this name.
+int entryNamed(EditorRig& rig, const juce::String& name)
+{
+    for (int i = 0; i < rig.editor->sidebar().rowCount(); ++i)
+        if (rig.editor->sidebar().row(i).getButtonText() == name) return i;
+    return -1;
+}
+
+std::string collectionsText(EditorRig& rig)
+{
+    Db db = Db::open(rig.f.dbPath);
+    std::string out;
+    for (const auto& c : UserData(db).collections()) out += c.name + "=" + std::to_string(c.size) + ";";
+    return out;
+}
+
+std::string searchesText(EditorRig& rig)
+{
+    Db db = Db::open(rig.f.dbPath);
+    std::string out;
+    for (const auto& s : UserData(db).savedSearches()) out += s.name + "=" + searchModelToJson(s.model) + ";";
+    return out;
+}
+
+void name(app::SidebarView& view, const char* text)
+{
+    view.nameField().setText(text, true);
+    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the field reports Return later
+}
+
 std::optional<int> storedRating(EditorRig& rig, const fs::path& file)
 {
     Db db = Db::open(rig.f.dbPath);
@@ -132,3 +163,107 @@ TEST_CASE("F and 0 to 5 organise the selection", "[organise]")
     CHECK_FALSE(storedRating(rig, rig.f.kick));
     CHECK_FALSE(storedFavourite(rig, rig.f.kick));
 }
+
+TEST_CASE("+ in the sidebar makes a collection; a taken name is refused", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    auto& sidebar = rig.editor->sidebar();
+    sidebar.addButton().triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // a click arrives later
+    name(sidebar, "  Live set ");
+    settle(rig);
+    CHECK(collectionsText(rig) == "Live set=0;");
+    REQUIRE(entryNamed(rig, "Live set") >= 0);
+
+    sidebar.startNewCollection();
+    name(sidebar, "LIVE SET");
+    CHECK(sidebar.isEditing());
+    CHECK(sidebar.refusalText() == "A collection with that name exists.");
+    sidebar.nameField().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    settle(rig);
+    CHECK(collectionsText(rig) == "Live set=0;");
+}
+
+TEST_CASE("renaming a collection or saved search, and to its own name changes nothing", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    {
+        Db db = Db::open(rig.f.dbPath);
+        UserData user(db);
+        user.createCollection("Live set");
+        user.createCollection("Album");
+        user.saveSearch("Kicks", {});
+    }
+    rig.editor->poll();
+    auto& sidebar = rig.editor->sidebar();
+    sidebar.startRename(entryNamed(rig, "Live set"));
+    name(sidebar, "Live set"); // its own name: no write, no refusal
+    CHECK_FALSE(sidebar.isEditing());
+    sidebar.startRename(entryNamed(rig, "Live set"));
+    name(sidebar, "album");
+    CHECK(sidebar.refusalText() == "A collection with that name exists.");
+    name(sidebar, "Set 2");
+    sidebar.startRename(entryNamed(rig, "Kicks")); // the entries have not refreshed yet
+    name(sidebar, "Short kicks");
+    settle(rig);
+    CHECK(collectionsText(rig) == "Album=0;Set 2=0;");
+    CHECK(searchesText(rig) == "Short kicks={\"v\":1};");
+}
+
+TEST_CASE("deleting asks first only for a collection with samples, and the lit entry falls back to All", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    {
+        Db db = Db::open(rig.f.dbPath);
+        UserData user(db);
+        const auto set = user.createCollection("Live set");
+        user.addToCollection(set, Library(db).fileByAbsolutePath(rig.f.kick)->id);
+        user.createCollection("Empty");
+    }
+    rig.editor->poll();
+    using app::SidebarEntry;
+    const auto entries = app::sidebarEntries(*std::make_unique<app::LibraryView>(rig.f.dbPath));
+    for (const auto& e : entries) {
+        if (e.name == "Live set") CHECK(AsmaEditor::asksBeforeDeleting(e));
+        if (e.name == "Empty") CHECK_FALSE(AsmaEditor::asksBeforeDeleting(e));
+        if (e.kind == app::EntryKind::Folder) CHECK_FALSE(AsmaEditor::asksBeforeDeleting(e));
+    }
+
+    rig.editor->sidebar().row(entryNamed(rig, "Empty")).triggerClick(); // lit: the table shows it
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    REQUIRE(rig.p->pluginState().search.collectionId);
+    rig.editor->deleteEntry(entryNamed(rig, "Empty"));
+    CHECK_FALSE(rig.p->pluginState().search.collectionId); // All samples
+    settle(rig);
+    CHECK(collectionsText(rig) == "Live set=1;");
+    CHECK(rig.editor->sidebar().row(0).getToggleState());
+}
+
+TEST_CASE("Save search names the search in force and saves it", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    {
+        Db db = Db::open(rig.f.dbPath);
+        UserData(db).saveSearch("Kicks", {});
+    }
+    rig.editor->poll();
+    rig.type("snare");
+    auto popover = rig.editor->saveSearchPopover();
+    CHECK_FALSE(popover->saveButton().isEnabled()); // no name yet
+    const auto typeName = [&](const char* text) {
+        popover->field().setText(text, true);
+        juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the field reports changes later
+    };
+    typeName(" kicks");
+    CHECK(popover->refusalText() == "A saved search with that name exists.");
+    CHECK_FALSE(popover->saveButton().isEnabled());
+    typeName("Snares");
+    CHECK(popover->refusalText().isEmpty());
+    popover->saveButton().triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    settle(rig);
+    CHECK(searchesText(rig) == "Kicks={\"v\":1};Snares={\"v\":1,\"text\":\"snare\"};");
+    // The search in force is the saved one now: it is lit.
+    CHECK(rig.editor->sidebar().row(entryNamed(rig, "Snares")).getToggleState());
+}
```

In `tests/plugin/test_sidebar_view.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_sidebar_view.cpp b/tests/plugin/test_sidebar_view.cpp
index 9126ffb..e35ce58 100644
--- a/tests/plugin/test_sidebar_view.cpp
+++ b/tests/plugin/test_sidebar_view.cpp
@@ -38,7 +38,9 @@ TEST_CASE("the sidebar shows its entries under their sections", "[sidebar]")
     CHECK(view.countText(5).isEmpty()); // a saved search has no count
     CHECK(view.sectionTitles() == juce::StringArray{"FOLDERS", "COLLECTIONS", "SAVED SEARCHES"});
     view.setEntries({entries()[0], entries()[1]});
-    CHECK(view.sectionTitles().isEmpty()); // no folders, no heading for them
+    // No folders, no heading for them; COLLECTIONS stays, for its "+".
+    CHECK(view.sectionTitles() == juce::StringArray{"COLLECTIONS"});
+    CHECK(view.addButton().isVisible());
 }

 TEST_CASE("clicking an entry picks it, and the picked one is lit", "[sidebar]")
@@ -87,3 +89,83 @@ TEST_CASE("a sidebar with many folders scrolls, and Problems stays in view", "[s
     CHECK(view.getLocalBounds().contains(view.problemsButton().getBounds()));
     CHECK(view.row(40).getBottom() > view.getHeight()); // past the view: scrolled to, not squeezed
 }
+
+TEST_CASE("+ opens a name field: Return keeps the name, Escape drops it", "[sidebar][organise]")
+{
+    const juce::ScopedJuceInitialiser_GUI gui;
+    SidebarView view;
+    view.setBounds(0, 0, 220, 482);
+    view.setEntries(entries());
+    std::vector<std::pair<int, juce::String>> named;
+    view.onNamed = [&](int index, const juce::String& name) { named.emplace_back(index, name); };
+
+    view.addButton().triggerClick();
+    settle();
+    REQUIRE(view.isEditing());
+    CHECK(view.nameField().isVisible());
+    // The field ends the collections, above the saved searches.
+    CHECK(view.nameField().getY() > view.row(4).getY());
+    CHECK(view.nameField().getY() < view.row(5).getY());
+    view.nameField().setText("Basslines", true);
+    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
+    settle(); // the field reports Return and Escape through the message loop
+    CHECK_FALSE(view.isEditing());
+    CHECK(named == std::vector<std::pair<int, juce::String>>{{-1, "Basslines"}});
+
+    view.startNewCollection();
+    view.nameField().setText("Dropped", true);
+    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
+    settle(); // the field reports Return and Escape through the message loop
+    CHECK_FALSE(view.isEditing());
+    CHECK(named.size() == 1);
+}
+
+TEST_CASE("a refused name keeps the field open and says why", "[sidebar][organise]")
+{
+    const juce::ScopedJuceInitialiser_GUI gui;
+    SidebarView view;
+    view.setBounds(0, 0, 220, 482);
+    view.setEntries(entries());
+    int calls = 0;
+    view.nameRefusal = [](int, const juce::String& name) -> std::optional<juce::String> {
+        if (name.trim().isEmpty()) return juce::String();
+        if (name.trim().equalsIgnoreCase("low end")) return juce::String("A collection with that name exists.");
+        return std::nullopt;
+    };
+    view.onNamed = [&](int, const juce::String&) { ++calls; };
+    view.startNewCollection();
+    view.nameField().setText("LOW END", true);
+    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
+    settle(); // the field reports Return and Escape through the message loop
+    CHECK(view.isEditing());
+    CHECK(view.refusalText() == "A collection with that name exists.");
+    view.nameField().setText("  ", true);
+    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
+    settle(); // the field reports Return and Escape through the message loop
+    CHECK(view.isEditing());
+    CHECK(calls == 0);
+}
+
+TEST_CASE("collections and saved searches have Rename and Delete; renaming edits in place", "[sidebar][organise]")
+{
+    const juce::ScopedJuceInitialiser_GUI gui;
+    SidebarView view;
+    view.setBounds(0, 0, 220, 482);
+    view.setEntries(entries());
+    CHECK(view.entryMenu(2).getNumItems() == 0); // a folder
+    CHECK(view.entryMenu(4).getNumItems() == 2); // a collection
+    CHECK(view.entryMenu(5).getNumItems() == 2); // a saved search
+    int deleted = -1;
+    view.onDelete = [&](int index) { deleted = index; };
+    view.entryMenuChosen(5, SidebarView::kDelete);
+    CHECK(deleted == 5);
+
+    view.entryMenuChosen(4, SidebarView::kRename);
+    REQUIRE(view.isEditing());
+    CHECK(view.nameField().getText() == "Low end");
+    CHECK(view.nameField().getY() == view.row(4).getY() + 2);
+    CHECK_FALSE(view.row(4).isVisible());
+    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
+    settle(); // the field reports Return and Escape through the message loop
+    CHECK(view.row(4).isVisible());
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[organise],[sidebar],[chips],[names]"`

Expected: the build stops:

```
tests/plugin/test_names.cpp:2:10: fatal error: 'Names.h' file not found
tests/plugin/test_sidebar_view.cpp:43:16: error: no member named 'addButton' in 'asma::app::SidebarView'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index f74d490..0ee07a0 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -9,6 +9,7 @@
 #include "ui/Theme.h"

 #include <cmath>
+#include <utility>

 namespace asma::app {

@@ -53,6 +54,9 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
         if (index >= 0 && index < static_cast<int>(entries_.size()))
             applySearch(withEntry(entries_[static_cast<std::size_t>(index)], browser_.searchModel()));
     };
+    sidebar_.nameRefusal = [this](int index, const juce::String& name) { return nameRefusal(index, name); };
+    sidebar_.onNamed = [this](int index, const juce::String& name) { named(index, name); };
+    sidebar_.onDelete = [this](int index) { deleteEntry(index); };
     addAndMakeVisible(sidebar_);
     chips_.onChange = [this](const SearchModel& model) { applySearch(model); };
     similar_.onPick = [this](const SearchRow& row) { pickSimilar(row); };
@@ -63,6 +67,9 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
         // Inside the editor, so a plugin's popover stays in its own window.
         juce::CallOutBox::launchAsynchronously(std::move(popover), getLocalArea(&anchor, anchor.getLocalBounds()), this);
     };
+    chips_.onSaveSearch = [this](juce::Component& anchor) {
+        juce::CallOutBox::launchAsynchronously(saveSearchPopover(), getLocalArea(&anchor, anchor.getLocalBounds()), this);
+    };
     addAndMakeVisible(chips_);
     if (processor_.isStandalone()) {
         top_.tempoBox().onValueChange = [this] {
@@ -300,6 +307,96 @@ void AsmaEditor::write(const Write& w, std::uint64_t ticket)
     });
 }

+std::vector<std::string> AsmaEditor::namesOf(EntryKind kind) const
+{
+    std::vector<std::string> names;
+    for (const auto& e : entries_)
+        if (e.kind == kind) names.push_back(e.name);
+    return names;
+}
+
+std::optional<juce::String> AsmaEditor::nameRefusal(int index, const juce::String& name)
+{
+    const bool existing = index >= 0 && index < static_cast<int>(entries_.size());
+    const EntryKind kind = existing ? entries_[static_cast<std::size_t>(index)].kind : EntryKind::Collection;
+    const std::string current = existing ? entries_[static_cast<std::size_t>(index)].name : std::string();
+    switch (checkName(name.toStdString(), namesOf(kind), current)) {
+    case NameCheck::Empty: return juce::String();
+    case NameCheck::Taken:
+        return kind == EntryKind::SavedSearch ? "A saved search with that name exists."
+                                              : "A collection with that name exists.";
+    case NameCheck::Ok:
+    case NameCheck::Unchanged: break;
+    }
+    return std::nullopt;
+}
+
+void AsmaEditor::named(int index, const juce::String& name)
+{
+    const std::string wanted = trimmedName(name.toStdString());
+    if (index < 0) {
+        write(Write::createCollection(wanted, std::exchange(newCollectionFile_, 0)));
+        return;
+    }
+    if (index >= static_cast<int>(entries_.size())) return;
+    const SidebarEntry& entry = entries_[static_cast<std::size_t>(index)];
+    if (checkName(wanted, {}, entry.name) == NameCheck::Unchanged) return;
+    if (entry.kind == EntryKind::Collection) write(Write::renameCollection(entry.name, wanted));
+    if (entry.kind == EntryKind::SavedSearch) write(Write::renameSearch(entry.name, wanted));
+}
+
+bool AsmaEditor::asksBeforeDeleting(const SidebarEntry& entry)
+{
+    return entry.kind == EntryKind::Collection && entry.count > 0;
+}
+
+void AsmaEditor::deleteEntry(int index)
+{
+    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
+    const SidebarEntry& entry = entries_[static_cast<std::size_t>(index)];
+    if (!asksBeforeDeleting(entry)) return deleteEntryNow(index);
+    const juce::String name = juce::String::fromUTF8(entry.name.c_str());
+    juce::NativeMessageBox::showOkCancelBox(
+        juce::MessageBoxIconType::QuestionIcon, "Delete collection",
+        "Delete the collection \"" + name + "\"? Its " + juce::String(entry.count)
+            + (entry.count == 1 ? " sample stays" : " samples stay") + " in the library.",
+        this, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<AsmaEditor>(this), index, name](int ok) {
+            // Only if the entry is still the one asked about.
+            if (ok != 0 && safe && index < static_cast<int>(safe->entries_.size())
+                && juce::String::fromUTF8(safe->entries_[static_cast<std::size_t>(index)].name.c_str()) == name)
+                safe->deleteEntryNow(index);
+        }));
+}
+
+void AsmaEditor::deleteEntryNow(int index)
+{
+    const SidebarEntry entry = entries_[static_cast<std::size_t>(index)];
+    const bool lit = entryFor(entries_, browser_.searchModel()) == index;
+    if (entry.kind == EntryKind::Collection) write(Write::deleteCollection(entry.name));
+    else if (entry.kind == EntryKind::SavedSearch) write(Write::deleteSearch(entry.name));
+    else return;
+    // What was showing has gone: back to All samples.
+    if (lit && !entries_.empty()) applySearch(withEntry(entries_.front(), browser_.searchModel()));
+}
+
+std::unique_ptr<NamePopover> AsmaEditor::saveSearchPopover()
+{
+    return std::make_unique<NamePopover>(
+        "Save search", juce::String(),
+        [this](const juce::String& name) -> std::optional<juce::String> {
+            switch (checkName(name.toStdString(), namesOf(EntryKind::SavedSearch))) {
+            case NameCheck::Empty: return juce::String();
+            case NameCheck::Taken: return juce::String("A saved search with that name exists.");
+            case NameCheck::Ok:
+            case NameCheck::Unchanged: break;
+            }
+            return std::nullopt;
+        },
+        [this](const juce::String& name) {
+            write(Write::saveSearch(trimmedName(name.toStdString()), browser_.searchModel()));
+        });
+}
+
 void AsmaEditor::cellClicked(int row, int column, const juce::MouseEvent& event)
 {
     const auto shown = shownRow(row);
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index c2e6ce8..ef74f1c 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -4,12 +4,14 @@
 #include "Browser.h"
 #include "LibraryView.h"
 #include "LibraryWriter.h"
+#include "Names.h"
 #include "PendingEdits.h"
 #include "Sidebar.h"
 #include "ui/AsmaLookAndFeel.h"
 #include "ui/ChipRow.h"
 #include "ui/FilterPopovers.h"
 #include "ui/Footer.h"
+#include "ui/NamePopover.h"
 #include "ui/PreviewPanel.h"
 #include "ui/SidebarView.h"
 #include "ui/SimilarView.h"
@@ -79,6 +81,12 @@ public:
     // Which star of the rating column an x (from the cell's left) falls on,
     // 1 to 5; 0 past the fifth.
     static int starAt(int x);
+    // Collections and saved searches from the sidebar and the chip row.
+    // Deleting asks first when the collection holds samples.
+    void deleteEntry(int index);
+    static bool asksBeforeDeleting(const SidebarEntry& entry);
+    // The chip row's Save search: names the search in force and saves it.
+    std::unique_ptr<NamePopover> saveSearchPopover();

 private:
     enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
@@ -109,6 +117,12 @@ private:
     void updateRenderSize();
     // Sends a write; `ticket` is its pending edit (0: none).
     void write(const Write& write, std::uint64_t ticket = 0);
+    // Why a collection (index -1: a new one) or saved search may not take a
+    // name; nothing when it may.
+    std::optional<juce::String> nameRefusal(int index, const juce::String& name);
+    void named(int index, const juce::String& name);
+    void deleteEntryNow(int index);
+    std::vector<std::string> namesOf(EntryKind kind) const;

     AsmaProcessor& processor_;
     AsmaLookAndFeel lookAndFeel_; // first in, last out: every child uses it
@@ -135,6 +149,7 @@ private:
     audio::SampleInfo selectedInfo_;
     std::string selectedFolder_;
     PendingEdits pending_;
+    std::int64_t newCollectionFile_ = 0; // the sample a new collection starts with (0: none)

     JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
 };
```

Create `plugin/src/Names.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Names.h"

#include <algorithm>
#include <cctype>

namespace asma::app {

namespace {

// As SQLite's NOCASE compares: ASCII letters only.
bool sameIgnoringCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::string trimmedName(std::string_view name)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!name.empty() && isSpace(name.front())) name.remove_prefix(1);
    while (!name.empty() && isSpace(name.back())) name.remove_suffix(1);
    return std::string(name);
}

NameCheck checkName(std::string_view name, const std::vector<std::string>& existing, std::string_view current)
{
    const std::string wanted = trimmedName(name);
    if (wanted.empty()) return NameCheck::Empty;
    if (!current.empty() && wanted == trimmedName(current)) return NameCheck::Unchanged;
    for (const auto& other : existing) {
        if (!current.empty() && sameIgnoringCase(other, trimmedName(current))) continue; // itself, in other case
        if (sameIgnoringCase(other, wanted)) return NameCheck::Taken;
    }
    return NameCheck::Ok;
}

} // namespace asma::app
```

Create `plugin/src/Names.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace asma::app {

// Collection and saved-search names, checked as the library checks them:
// trimmed, not empty, unique within their kind ignoring case. JUCE-free.
enum class NameCheck {
    Ok,
    Empty,
    Taken,     // another collection or saved search has it
    Unchanged, // a rename to the name it already has: no change, not a refusal
};

std::string trimmedName(std::string_view name);

// `current`: the name being renamed, empty for a new one.
NameCheck checkName(std::string_view name, const std::vector<std::string>& existing, std::string_view current = {});

} // namespace asma::app
```

In `plugin/src/ui/ChipRow.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/ChipRow.cpp b/plugin/src/ui/ChipRow.cpp
index 85e6d69..1de71c3 100644
--- a/plugin/src/ui/ChipRow.cpp
+++ b/plugin/src/ui/ChipRow.cpp
@@ -140,6 +140,13 @@ ChipRow::ChipRow()
         if (onChange) onChange(clearedAll(model_));
     };
     addChildComponent(clearAll_);
+    saveSearch_.getProperties().set("asma.quiet", true);
+    saveSearch_.getProperties().set("asma.size", 12.0f);
+    saveSearch_.getProperties().set("asma.segment", "middle");
+    saveSearch_.onClick = [this] {
+        if (onSaveSearch) onSaveSearch(saveSearch_);
+    };
+    addAndMakeVisible(saveSearch_);
     setModel({});
 }

@@ -166,6 +173,8 @@ void ChipRow::paint(juce::Graphics& g)
 void ChipRow::resized()
 {
     auto area = getLocalBounds().reduced(kMargin, 0);
+    saveSearch_.setBounds(area.removeFromRight(84).withSizeKeepingCentre(84, kChipHeight));
+    area.removeFromRight(kGap);
     clearAll_.setBounds(area.removeFromRight(72).withSizeKeepingCentre(72, kChipHeight));
     area.removeFromRight(kGap);
     // Ideal widths; when they do not fit, every chip gives up its share.
```

In `plugin/src/ui/ChipRow.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/ChipRow.h b/plugin/src/ui/ChipRow.h
index 53248c5..b0d7b13 100644
--- a/plugin/src/ui/ChipRow.h
+++ b/plugin/src/ui/ChipRow.h
@@ -34,8 +34,9 @@ private:
     std::unique_ptr<juce::Button> main_, clear_;
 };

-// The row over the table: a chip per filter and "Clear all". It shows the
-// search it is given and reports what the user changes.
+// The row over the table: a chip per filter, "Clear all" and, at its right
+// end, "Save search". It shows the search it is given and reports what the
+// user changes.
 class ChipRow : public juce::Component {
 public:
     ChipRow();
@@ -46,9 +47,12 @@ public:
     std::function<void(const SearchModel&)> onChange;
     // A chip was clicked: open its popover against `anchor`.
     std::function<void(Facet, juce::Component& anchor)> onOpen;
+    // Save search was clicked: ask for a name against `anchor`.
+    std::function<void(juce::Component& anchor)> onSaveSearch;

     FilterChip& chip(Facet facet) { return *chips_[static_cast<int>(facet)]; }
     juce::Button& clearAllButton() { return clearAll_; }
+    juce::Button& saveSearchButton() { return saveSearch_; }

     void paint(juce::Graphics& g) override;
     void resized() override;
@@ -57,6 +61,7 @@ private:
     SearchModel model_;
     juce::OwnedArray<FilterChip> chips_;
     juce::TextButton clearAll_{"Clear all"};
+    juce::TextButton saveSearch_{"Save search"};
 };

 } // namespace asma::app
```

Create `plugin/src/ui/NamePopover.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/NamePopover.h"

#include "ui/Theme.h"

namespace asma::app {

namespace {

constexpr int kPad = 14;
constexpr int kTitle = 22;

} // namespace

NamePopover::NamePopover(const juce::String& title, const juce::String& initial, Refusal refusal,
                         std::function<void(const juce::String&)> onSave)
    : title_(title), refusal_(std::move(refusal)), onSave_(std::move(onSave))
{
    setTitle(title);
    field_.setTitle("Name");
    field_.setFont(theme::font(theme::Face::Text, 13.0f));
    field_.setIndents(8, 6);
    field_.setText(initial, false);
    field_.onTextChange = [this] { check(); };
    field_.onReturnKey = [this] { save(); };
    field_.onEscapeKey = [this] { close(); };
    addAndMakeVisible(field_);
    cancel_.onClick = [this] { close(); };
    addAndMakeVisible(cancel_);
    save_.getProperties().set("asma.accent", true);
    save_.onClick = [this] { save(); };
    addAndMakeVisible(save_);
    setSize(300, 150);
    check();
}

void NamePopover::check()
{
    const auto refused = refusal_ ? refusal_(field_.getText()) : std::nullopt;
    refusalText_ = refused.value_or(juce::String());
    save_.setEnabled(!refused);
    field_.setColour(juce::TextEditor::outlineColourId, refusalText_.isNotEmpty() ? theme::refused : theme::border);
    field_.setColour(juce::TextEditor::focusedOutlineColourId, refusalText_.isNotEmpty() ? theme::refused : theme::amber);
    repaint();
}

void NamePopover::save()
{
    check();
    if (!save_.isEnabled()) return;
    if (onSave_) onSave_(field_.getText());
    close();
}

void NamePopover::close()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
}

void NamePopover::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(kPad);
    g.setFont(theme::font(theme::Face::Heading, 13.0f));
    g.setColour(theme::text);
    g.drawText(title_, area.removeFromTop(kTitle), juce::Justification::centredLeft, false);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText("Name", area.removeFromTop(16), juce::Justification::bottomLeft, false);
    if (refusalText_.isEmpty()) return;
    g.setFont(theme::font(theme::Face::Text, 12.0f));
    g.setColour(theme::refusedText);
    g.drawText(refusalText_, field_.getBounds().translated(0, field_.getHeight() + 4).withHeight(16),
               juce::Justification::centredLeft, true);
}

void NamePopover::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    area.removeFromTop(kTitle + 18);
    field_.setBounds(area.removeFromTop(28));
    auto buttons = area.removeFromBottom(28);
    save_.setBounds(buttons.removeFromRight(64));
    buttons.removeFromRight(8);
    cancel_.setBounds(buttons.removeFromRight(72));
}

} // namespace asma::app
```

Create `plugin/src/ui/NamePopover.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

namespace asma::app {

// A name to give: a title, the field, why a name is refused, Cancel and Save.
// Save, or Return in the field, gives the name only while nothing refuses it;
// a refusal shows under the field in red. Closes its callout box when done.
class NamePopover : public juce::Component {
public:
    // Nothing: the name is fine. Empty text: refused without a word (an
    // empty field). Otherwise what to tell the user.
    using Refusal = std::function<std::optional<juce::String>(const juce::String& name)>;
    NamePopover(const juce::String& title, const juce::String& initial, Refusal refusal,
                std::function<void(const juce::String& name)> onSave);

    juce::TextEditor& field() { return field_; }
    juce::Button& saveButton() { return save_; }
    juce::Button& cancelButton() { return cancel_; }
    juce::String refusalText() const { return refusalText_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void check();
    void save();
    void close();

    juce::String title_;
    Refusal refusal_;
    std::function<void(const juce::String&)> onSave_;
    juce::TextEditor field_;
    juce::TextButton cancel_{"Cancel"}, save_{"Save"};
    juce::String refusalText_;
};

} // namespace asma::app
```

Replace the whole of `plugin/src/ui/SidebarView.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SidebarView.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kRowHeight = 30;
constexpr int kHeadingHeight = 22;
constexpr int kSectionGap = 18;
constexpr int kTop = 14;

const char* headingFor(EntryKind kind)
{
    switch (kind) {
    case EntryKind::Folder: return "FOLDERS";
    case EntryKind::Collection: return "COLLECTIONS";
    case EntryKind::SavedSearch: return "SAVED SEARCHES";
    case EntryKind::All:
    case EntryKind::Favourites: break;
    }
    return nullptr;
}

// One entry: its name, its count on the right, lit when picked.
class Row final : public juce::Button {
public:
    Row(const juce::String& name, const juce::String& count) : juce::Button(name), count_(count)
    {
        setTitle(name);
        if (count.isNotEmpty()) setDescription(count + " samples");
        setClickingTogglesState(false);
    }
    const juce::String& count() const { return count_; }
    std::function<void()> onMenu; // a right click, for entries that have a menu
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu) return onMenu();
        juce::Button::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu) return;
        juce::Button::mouseUp(e);
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool lit = getToggleState();
        if (lit || highlighted || down) {
            g.setColour(lit ? theme::raised : theme::raised.withAlpha(0.5f));
            g.fillRect(getLocalBounds());
        }
        if (lit) {
            g.setColour(theme::amber);
            g.fillRect(0, 0, 2, getHeight());
        }
        auto area = getLocalBounds().reduced(18, 0);
        const auto countFont = theme::font(theme::Face::Mono, 11.0f);
        const int countWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(countFont, count_)));
        g.setFont(countFont);
        g.setColour(theme::muted);
        g.drawText(count_, area.removeFromRight(countWidth), juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(8), juce::Justification::centredLeft, true);
    }

private:
    juce::String count_;
};

// At the foot: Problems with an amber count.
class ProblemsButton final : public juce::Button {
public:
    ProblemsButton() : juce::Button("Problems") { setTitle("Problems"); }
    juce::String count;
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        if (highlighted || down) {
            g.setColour(theme::raised);
            g.fillRoundedRectangle(r, theme::kRadius);
        }
        g.setColour(theme::border);
        g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
        auto area = getLocalBounds().reduced(10, 0);
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::text);
        g.drawText("Problems", area, juce::Justification::centredLeft, false);
        const auto font = theme::font(theme::Face::Mono, 11.0f);
        const float w = juce::GlyphArrangement::getStringWidth(font, count) + 14.0f;
        const auto badge = area.toFloat().removeFromRight(w).withSizeKeepingCentre(w, 16.0f);
        g.setColour(theme::amber);
        g.fillRoundedRectangle(badge, 8.0f);
        g.setFont(font);
        g.setColour(theme::ground);
        g.drawText(count, badge, juce::Justification::centred, false);
    }
};

// The "+" beside COLLECTIONS.
class AddButton final : public juce::Button {
public:
    AddButton() : juce::Button("New collection") { setTitle("New collection"); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down) {
            g.setColour(theme::raised);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::kRadius);
        }
        g.setFont(theme::font(theme::Face::Text, 15.0f));
        g.setColour(highlighted ? theme::text : theme::muted);
        g.drawText("+", getLocalBounds(), juce::Justification::centred, false);
    }
};

} // namespace

// What scrolls: the rows and the headings between them.
class SidebarView::Content final : public juce::Component {
public:
    struct Heading {
        juce::String title;
        int y;
    };
    std::vector<Heading> headings;
    void paint(juce::Graphics& g) override
    {
        g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.08f));
        g.setColour(theme::muted);
        for (const auto& h : headings) g.drawText(h.title, 18, h.y, getWidth() - 36, kHeadingHeight, juce::Justification::centredLeft, false);
    }
};

SidebarView::SidebarView()
    : content_(std::make_unique<Content>()), problems_(std::make_unique<ProblemsButton>()),
      add_(std::make_unique<AddButton>())
{
    add_->onClick = [this] { startNewCollection(); };
    content_->addChildComponent(*add_);
    nameField_.setTitle("Name");
    nameField_.setFont(theme::font(theme::Face::Text, 13.0f));
    nameField_.setIndents(6, 5);
    nameField_.setColour(juce::TextEditor::focusedOutlineColourId, theme::amber);
    nameField_.onReturnKey = [this] { finishEditing(true); };
    nameField_.onEscapeKey = [this] { finishEditing(false); };
    nameField_.onTextChange = [this] {
        if (refusal_.isEmpty()) return;
        refusal_.clear();
        nameField_.setColour(juce::TextEditor::outlineColourId, theme::amber);
        nameField_.setTooltip({});
    };
    content_->addChildComponent(nameField_);
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    problems_->onClick = [this] {
        if (onProblems) onProblems();
    };
    addChildComponent(*problems_);
}

SidebarView::~SidebarView() = default;

void SidebarView::setEntries(std::vector<SidebarEntry> entries)
{
    entries_ = std::move(entries);
    // The entries moved under the field: what it was naming may be gone.
    if (editing_ && *editing_ >= 0) finishEditing(false);
    rows_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        auto* row = rows_.add(new Row(juce::String::fromUTF8(e.name.c_str()), e.count >= 0 ? juce::String(e.count) : juce::String()));
        row->onClick = [this, i] {
            if (onPick) onPick(static_cast<int>(i));
        };
        if (e.kind == EntryKind::Collection || e.kind == EntryKind::SavedSearch)
            static_cast<Row*>(row)->onMenu = [this, i, row] {
                entryMenu(static_cast<int>(i)).showMenuAsync(
                    juce::PopupMenu::Options().withTargetComponent(row).withParentComponent(getTopLevelComponent()),
                    [safe = juce::Component::SafePointer<SidebarView>(this), i](int result) {
                        if (safe) safe->entryMenuChosen(static_cast<int>(i), result);
                    });
            };
        content_->addAndMakeVisible(row);
    }
    resized();
}

void SidebarView::setSelected(int index)
{
    for (int i = 0; i < rows_.size(); ++i) rows_[i]->setToggleState(i == index, juce::dontSendNotification);
}

void SidebarView::setProblems(std::int64_t count)
{
    auto* p = static_cast<ProblemsButton*>(problems_.get());
    const juce::String text(count);
    if (p->isVisible() == (count > 0) && p->count == text) return;
    p->count = text;
    p->setVisible(count > 0);
    resized();
    repaint();
}

juce::String SidebarView::countText(int index) const
{
    return index >= 0 && index < rows_.size() ? static_cast<const Row*>(rows_[index])->count() : juce::String();
}

juce::StringArray SidebarView::sectionTitles() const
{
    juce::StringArray out;
    for (const auto& h : content_->headings) out.add(h.title);
    return out;
}

void SidebarView::startNewCollection()
{
    editing_ = -1;
    nameField_.setText({}, false);
    refusal_.clear();
    nameField_.setColour(juce::TextEditor::outlineColourId, theme::amber);
    nameField_.setVisible(true);
    resized();
    nameField_.grabKeyboardFocus();
}

void SidebarView::startRename(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    editing_ = index;
    nameField_.setText(juce::String::fromUTF8(entries_[static_cast<std::size_t>(index)].name.c_str()), false);
    nameField_.selectAll();
    refusal_.clear();
    nameField_.setColour(juce::TextEditor::outlineColourId, theme::amber);
    nameField_.setVisible(true);
    resized();
    nameField_.grabKeyboardFocus();
}

void SidebarView::finishEditing(bool keep)
{
    if (!editing_) return;
    const int index = *editing_;
    const juce::String name = nameField_.getText();
    if (keep) {
        const auto refused = nameRefusal ? nameRefusal(index, name) : std::nullopt;
        if (refused) {
            refusal_ = refused->isNotEmpty() ? *refused : juce::String("A name is needed.");
            nameField_.setColour(juce::TextEditor::outlineColourId, theme::refused);
            nameField_.setTooltip(refusal_);
            return; // the field stays for another try
        }
    }
    editing_.reset();
    refusal_.clear();
    nameField_.setVisible(false);
    resized();
    if (keep && onNamed) onNamed(index, name);
}

juce::PopupMenu SidebarView::entryMenu(int index) const
{
    juce::PopupMenu menu;
    if (index < 0 || index >= static_cast<int>(entries_.size())) return menu;
    const auto kind = entries_[static_cast<std::size_t>(index)].kind;
    if (kind != EntryKind::Collection && kind != EntryKind::SavedSearch) return menu;
    menu.addItem(kRename, juce::String::fromUTF8("Rename…"));
    menu.addItem(kDelete, "Delete");
    return menu;
}

void SidebarView::entryMenuChosen(int index, int result)
{
    if (result == kRename) startRename(index);
    if (result == kDelete && onDelete) onDelete(index);
}

juce::String SidebarView::problemsText() const { return static_cast<const ProblemsButton*>(problems_.get())->count; }

void SidebarView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void SidebarView::resized()
{
    auto area = getLocalBounds().withTrimmedRight(1);
    if (problems_->isVisible()) {
        problems_->setBounds(area.removeFromBottom(14 + 34).reduced(12, 0).withTrimmedBottom(14));
        area.removeFromBottom(8);
    }
    viewport_.setBounds(area);
    content_->headings.clear();
    const int width = area.getWidth();
    int y = kTop;
    EntryKind section = EntryKind::All;
    const auto heading = [&](const char* title) {
        y += kSectionGap;
        content_->headings.push_back({title, y});
        if (juce::String(title) == "COLLECTIONS") add_->setBounds(width - 10 - 22, y, 22, kHeadingHeight - 2);
        y += kHeadingHeight;
    };
    // COLLECTIONS always shows, and a new collection's field ends it.
    bool collectionsDone = false;
    const auto endCollections = [&] {
        if (collectionsDone) return;
        collectionsDone = true;
        if (section != EntryKind::Collection) heading("COLLECTIONS");
        if (editing_ == -1) {
            nameField_.setBounds(14, y + 2, width - 24, kRowHeight - 4);
            y += kRowHeight;
        }
    };
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto kind = entries_[i].kind;
        if (kind == EntryKind::SavedSearch) endCollections();
        if (const char* title = headingFor(kind); title && kind != section) heading(title);
        if (kind == EntryKind::Folder || kind == EntryKind::Collection || kind == EntryKind::SavedSearch) section = kind;
        auto* row = rows_[static_cast<int>(i)];
        row->setBounds(0, y, width, kRowHeight);
        const bool renaming = editing_ == static_cast<int>(i);
        row->setVisible(!renaming);
        if (renaming) nameField_.setBounds(14, y + 2, width - 24, kRowHeight - 4);
        y += kRowHeight;
    }
    if (!entries_.empty()) endCollections();
    add_->setVisible(!entries_.empty());
    content_->setSize(width, y + kTop);
    content_->repaint();
}

} // namespace asma::app
```

Replace the whole of `plugin/src/ui/SidebarView.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Sidebar.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
#include <vector>

namespace asma::app {

// The left column: All samples and Favourites, then the folders, the
// collections and the saved searches under their headings, each with its
// count, scrolling when they do not fit; Problems at the foot when anything
// failed. It shows what it is told and reports what is picked.
// COLLECTIONS is always there, with a "+" that makes one. A new collection,
// or a collection or saved search being renamed, is a name field in place:
// Return keeps the name unless it is refused, Escape drops it.
class SidebarView : public juce::Component {
public:
    SidebarView();
    ~SidebarView() override;

    void setEntries(std::vector<SidebarEntry> entries);
    // The lit entry; -1 for none.
    void setSelected(int index);
    void setProblems(std::int64_t count);

    std::function<void(int)> onPick;
    std::function<void()> onProblems;
    // Naming: index -1 is a new collection. A refusal keeps the field open.
    std::function<std::optional<juce::String>(int index, const juce::String& name)> nameRefusal;
    std::function<void(int index, const juce::String& name)> onNamed;
    std::function<void(int index)> onDelete;

    // The name field, for a new collection or in place of an entry.
    void startNewCollection();
    void startRename(int index);
    bool isEditing() const { return editing_.has_value(); }
    juce::TextEditor& nameField() { return nameField_; }
    juce::String refusalText() const { return refusal_; }
    juce::Button& addButton() { return *add_; }
    // A collection's or saved search's right-click menu (Rename…, Delete);
    // empty for the others.
    juce::PopupMenu entryMenu(int index) const;
    void entryMenuChosen(int index, int result);
    enum MenuItem { kRename = 1, kDelete };

    int rowCount() const { return rows_.size(); }
    juce::Button& row(int index) { return *rows_[index]; }
    juce::String countText(int index) const;
    juce::StringArray sectionTitles() const;
    juce::Button& problemsButton() { return *problems_; }
    juce::String problemsText() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void finishEditing(bool keep);

    class Content;
    std::vector<SidebarEntry> entries_;
    juce::OwnedArray<juce::Button> rows_;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    std::unique_ptr<juce::Button> problems_;
    std::unique_ptr<juce::Button> add_;
    juce::TextEditor nameField_;
    std::optional<int> editing_; // -1: a new collection
    juce::String refusal_;
};

} // namespace asma::app
```

In `plugin/src/ui/Theme.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/Theme.h b/plugin/src/ui/Theme.h
index fad9aa3..fbedafb 100644
--- a/plugin/src/ui/Theme.h
+++ b/plugin/src/ui/Theme.h
@@ -19,7 +19,7 @@ inline const juce::Colour faint{0xff3a3e45};   // empty stars, disabled text
 inline const juce::Colour text{0xffe8e9eb};
 inline const juce::Colour muted{0xff8a8f98};
 // Accents: amber only for what is active or selected, cyan for the waveform,
-// green only for a synced tempo.
+// green only for a synced tempo, red only for a refused name.
 inline const juce::Colour amber{0xffe8a33d};
 inline const juce::Colour amberLight{0xfff2bd6b};
 inline const juce::Colour cyan{0xff4fd1e6};
@@ -27,6 +27,9 @@ inline const juce::Colour cyanDim{0xff3aa3b5};  // the waveform after the playhe
 inline const juce::Colour cyanFaint{0xff2b5c66}; // the waveform outside the trim
 inline const juce::Colour zeroLine{0xff23262b};  // the waveform's centre line
 inline const juce::Colour green{0xff7de38e};
+// Red only for a name that is refused.
+inline const juce::Colour refused{0xffd9634c};
+inline const juce::Colour refusedText{0xffef8a75};

 constexpr float kRadius = 4.0f;
 constexpr float kCardRadius = 8.0f;
```

In `tests/ui/reference/main.html`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/ui/reference/main.html b/tests/ui/reference/main.html
index 14124d4..2f4dd00 100644
--- a/tests/ui/reference/main.html
+++ b/tests/ui/reference/main.html
@@ -96,6 +96,7 @@ nav { padding: 14px 0; display: flex; flex-direction: column; gap: 18px; }
 .fchip.on { padding: 0 5px 0 10px; border-color: #e8a33d; background: rgba(232,163,61,0.12); color: #f2bd6b; gap: 4px; }
 .fchip .x { width: 16px; height: 16px; display: inline-flex; align-items: center; justify-content: center; color: #e8a33d; font-size: 12px; }
 .clearall { margin-left: auto; width: 72px; text-align: center; color: #8a8f98; font-size: 12px; }
+.savesearch { width: 84px; text-align: center; color: #8a8f98; font-size: 12px; }
 .fav.on { color: #e8a33d; }
 .stars .lit { color: #e8a33d; }
 aside { padding-top: 12px; }
@@ -136,6 +137,7 @@ aside { padding-top: 12px; }
         <span class="fchip on">118&#8211;132 BPM<span class="x">&#215;</span></span>
         <span class="fchip">Key</span><span class="fchip">Instrument</span><span class="fchip">Length</span><span class="fchip">Rating</span>
         <span class="clearall">Clear all</span>
+        <span class="savesearch">Save search</span>
       </div>
       <div class="head"><span></span><span class="lit">NAME &#9652;</span><span>TYPE</span><span>BPM</span><span>KEY</span><span>LENGTH</span><span>RATING</span><span>TAGS</span></div>
       <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_132_rolling.wav</span><span class="muted">loop</span><span class="mono">132</span><span class="mono"></span><span class="mono muted">7.27 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
```

Render the reference pictures from their pages:

```sh
cd tests/ui/reference
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --hide-scrollbars --force-device-scale-factor=1 --window-size=1280,800 \
  --screenshot="$PWD/main.png" "file://$PWD/main.html"
cd ../../..
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[organise],[sidebar],[chips],[names]"`

Expected: `All tests passed (163 assertions in 31 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/Names.cpp plugin/src/Names.h plugin/src/ui/ChipRow.cpp plugin/src/ui/ChipRow.h plugin/src/ui/NamePopover.cpp plugin/src/ui/NamePopover.h plugin/src/ui/SidebarView.cpp plugin/src/ui/SidebarView.h plugin/src/ui/Theme.h tests/plugin/test_names.cpp tests/plugin/test_organise.cpp tests/plugin/test_sidebar_view.cpp tests/ui/reference/main.html tests/ui/reference/main.png
git commit -m "app: make, rename and delete collections and saved searches from the sidebar; Save search"
```

---

### Task 7: A row's menu: collections, Tags… and Show in Finder

Spec section 9 (Organising: right-clicking a row). "Add to collection" ticks the
collections the sample is in and picking one adds it or takes it out; "New
collection…" opens the sidebar's name field and adds the sample once named.
"Tags…" opens a `TagsPopover`: the user's tags removable, the analyser's greyed,
the library's tags suggested as the field is typed in, each change shown in the
table at once. A right click never rates.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Create: `plugin/src/ui/TagsPopover.cpp`
- Create: `plugin/src/ui/TagsPopover.h`
- Modify: `tests/plugin/test_organise.cpp` (test)
- Create: `tests/plugin/test_tags_popover.cpp` (test)

**Interfaces:**

- Consumes: task 6's sidebar field; task 5's `PendingEdits::setTags`.
- Produces: `LibraryView::collectionsOf(std::int64_t)`, `tagsOf(std::int64_t)`;
  `class TagsPopover` (`struct Tag { std::string name; bool user; }`,
  `chipCount()`, `chipText(int)`, `isRemovable(int)`, `removeButton(int)`,
  `field()`, `suggestions()`, `suggestion(int)`);
  `AsmaEditor::rowMenu(const SearchRow&)`,
  `rowMenuChosen(const SearchRow&, int)`, `kNewCollection`, `kEditTags`,
  `kReveal`, `kFirstCollection`, `static revealText()`,
  `tagsPopover(const SearchRow&)`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_organise.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_organise.cpp b/tests/plugin/test_organise.cpp
index d1ad6c6..1dc74f8 100644
--- a/tests/plugin/test_organise.cpp
+++ b/tests/plugin/test_organise.cpp
@@ -267,3 +267,118 @@ TEST_CASE("Save search names the search in force and saves it", "[organise]")
     // The search in force is the saved one now: it is lit.
     CHECK(rig.editor->sidebar().row(entryNamed(rig, "Snares")).getToggleState());
 }
+
+namespace {
+
+// The submenu's items, as text with a tick where ticked.
+std::vector<std::string> collectionItems(const juce::PopupMenu& menu)
+{
+    std::vector<std::string> out;
+    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
+        const auto& item = it.getItem();
+        if (!item.subMenu) continue;
+        for (juce::PopupMenu::MenuItemIterator sub(*item.subMenu); sub.next();)
+            if (!sub.getItem().isSeparator)
+                out.push_back((sub.getItem().isTicked ? "+" : "") + sub.getItem().text.toStdString());
+    }
+    return out;
+}
+
+std::vector<std::string> topItems(const juce::PopupMenu& menu)
+{
+    std::vector<std::string> out;
+    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
+        if (!it.getItem().isSeparator) out.push_back(it.getItem().text.toStdString());
+    return out;
+}
+
+int itemId(const juce::PopupMenu& menu, const std::string& text)
+{
+    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
+        if (it.getItem().text.toStdString() == text) return it.getItem().itemID;
+    return 0;
+}
+
+} // namespace
+
+TEST_CASE("a row's menu ticks the collections it is in, and picking one adds or takes it out", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    {
+        Db db = Db::open(rig.f.dbPath);
+        UserData user(db);
+        user.addToCollection(user.createCollection("Live set"), Library(db).fileByAbsolutePath(rig.f.kick)->id);
+        user.createCollection("Album");
+    }
+    rig.editor->poll();
+    rig.type("kick");
+    const SearchRow kick = *rig.editor->shownRow(0);
+    const auto menu = rig.editor->rowMenu(kick);
+    CHECK(topItems(menu) == std::vector<std::string>{"Add to collection", "Tags\u2026",
+                                                      AsmaEditor::revealText().toStdString()});
+    CHECK(collectionItems(menu) == std::vector<std::string>{"Album", "+Live set", "New collection\u2026"});
+
+    rig.editor->rowMenuChosen(kick, itemId(menu, "Album"));
+    rig.editor->rowMenuChosen(kick, itemId(menu, "Live set"));
+    settle(rig);
+    CHECK(collectionsText(rig) == "Album=1;Live set=0;");
+    CHECK(collectionItems(rig.editor->rowMenu(kick)) == std::vector<std::string>{"+Album", "Live set", "New collection\u2026"});
+}
+
+TEST_CASE("New collection from a row's menu makes one with the sample in it", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    rig.type("kick");
+    const SearchRow kick = *rig.editor->shownRow(0);
+    rig.editor->rowMenuChosen(kick, AsmaEditor::kNewCollection);
+    REQUIRE(rig.editor->sidebar().isEditing());
+    name(rig.editor->sidebar(), "Kicks");
+    settle(rig);
+    CHECK(collectionsText(rig) == "Kicks=1;");
+    // The next new collection starts empty.
+    rig.editor->sidebar().startNewCollection();
+    name(rig.editor->sidebar(), "Empty");
+    settle(rig);
+    CHECK(collectionsText(rig) == "Empty=0;Kicks=1;");
+}
+
+TEST_CASE("Tags changes a sample's tags, shown in the table at once", "[organise]")
+{
+    EditorRig rig;
+    rig.p->writer().setCli(ASMA_CLI_PATH);
+    rig.type("kick");
+    const SearchRow kick = *rig.editor->shownRow(0);
+    const auto analysed = kick.tags; // the scanner's, from the file name
+    auto popover = rig.editor->tagsPopover(kick);
+    for (int i = 0; i < popover->chipCount(); ++i) CHECK_FALSE(popover->isRemovable(i));
+    popover->field().setText("Punchy", true);
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    popover->field().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    const auto shown = rig.editor->shownRow(0)->tags;
+    CHECK(std::find(shown.begin(), shown.end(), "punchy") != shown.end()); // before the helper has run
+    settle(rig);
+    {
+        Db db = Db::open(rig.f.dbPath);
+        const auto tags = Library(db).tags(kick.id);
+        CHECK(std::find(tags.begin(), tags.end(), std::pair<std::string, TagSource>{"punchy", TagSource::User}) != tags.end());
+    }
+    auto again = rig.editor->tagsPopover(*rig.editor->shownRow(0));
+    REQUIRE(again->chipCount() == static_cast<int>(analysed.size()) + 1);
+    CHECK(again->chipText(0) == "punchy");
+    CHECK(again->isRemovable(0));
+    again->removeButton(0)->triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    settle(rig);
+    CHECK(rig.editor->shownRow(0)->tags == analysed);
+}
+
+TEST_CASE("a right click on a row opens its menu, not a rating", "[organise]")
+{
+    EditorRig rig(AsmaProcessor::Mode::Standalone);
+    rig.type("kick");
+    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(4), juce::ModifierKeys::rightButtonModifier);
+    juce::PopupMenu::dismissAllActiveMenus();
+    settle(rig);
+    CHECK_FALSE(storedRating(rig, rig.f.kick));
+}
```

Create `tests/plugin/test_tags_popover.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/TagsPopover.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::TagsPopover;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

struct Rig {
    const juce::ScopedJuceInitialiser_GUI gui;
    std::vector<std::pair<std::string, bool>> changes;
    TagsPopover popover{"Bass_Loop_Am_120.wav",
                        {{"dark", true}, {"bass", false}, {"synth", false}},
                        {{"bass", 214}, {"gritty", 31}, {"groove", 9}, {"dark", 4}, {"grime", 2}},
                        [this](const std::string& tag, bool added) { changes.emplace_back(tag, added); }};
    void type(const char* text)
    {
        popover.field().setText(text, true);
        settle(); // the field reports changes later
    }
    void pressReturn()
    {
        popover.field().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        settle();
    }
};

} // namespace

TEST_CASE("the user's tags can be removed, the analyser's cannot", "[tags]")
{
    Rig rig;
    REQUIRE(rig.popover.chipCount() == 3);
    CHECK(rig.popover.chipText(0) == "dark");
    CHECK(rig.popover.isRemovable(0));
    CHECK_FALSE(rig.popover.isRemovable(1));
    CHECK(rig.popover.removeButton(1) == nullptr);
    rig.popover.removeButton(0)->triggerClick();
    settle();
    CHECK(rig.changes == std::vector<std::pair<std::string, bool>>{{"dark", false}});
    CHECK(rig.popover.chipCount() == 2);
}

TEST_CASE("typing offers the library's tags that start with it, not ones the sample has", "[tags]")
{
    Rig rig;
    rig.type("Gr");
    CHECK(rig.popover.suggestions() == juce::StringArray{"gritty", "groove", "grime"});
    rig.type("d");
    CHECK(rig.popover.suggestions().isEmpty()); // "dark" is on it already
    rig.type("gro");
    rig.popover.suggestion(0).triggerClick();
    settle();
    CHECK(rig.changes == std::vector<std::pair<std::string, bool>>{{"groove", true}});
    CHECK(rig.popover.field().isEmpty());
}

TEST_CASE("Return adds the tag, trimmed and lower case, and never one it has", "[tags]")
{
    Rig rig;
    rig.type("  Warm ");
    rig.pressReturn();
    rig.type(" Bass");
    rig.pressReturn();
    rig.type("   ");
    rig.pressReturn();
    CHECK(rig.changes == std::vector<std::pair<std::string, bool>>{{"warm", true}});
    CHECK(rig.popover.chipCount() == 4);
    CHECK(rig.popover.chipText(1) == "warm"); // the user's together, first
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[organise],[tags]"`

Expected: the build stops:

```
tests/plugin/test_tags_popover.cpp:3:10: fatal error: 'ui/TagsPopover.h' file not found
tests/plugin/test_organise.cpp:316:35: error: no member named 'rowMenu' in 'asma::app::AsmaEditor'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 0ee07a0..75a4c0b 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -8,6 +8,7 @@
 #include "asma/core/Fs.h"
 #include "ui/Theme.h"

+#include <algorithm>
 #include <cmath>
 #include <utility>

@@ -397,10 +398,100 @@ std::unique_ptr<NamePopover> AsmaEditor::saveSearchPopover()
         });
 }

+juce::String AsmaEditor::revealText()
+{
+#if JUCE_MAC
+    return "Show in Finder";
+#elif JUCE_WINDOWS
+    return "Show in Explorer";
+#else
+    return "Show in the file manager";
+#endif
+}
+
+juce::PopupMenu AsmaEditor::rowMenu(const SearchRow& row)
+{
+    const auto in = library_.collectionsOf(row.id);
+    juce::PopupMenu collections;
+    for (std::size_t i = 0; i < entries_.size(); ++i) {
+        const auto& e = entries_[i];
+        if (e.kind != EntryKind::Collection) continue;
+        const bool ticked = std::find(in.begin(), in.end(), e.id) != in.end();
+        collections.addItem(kFirstCollection + static_cast<int>(i), utf8(e.name), true, ticked);
+    }
+    if (collections.getNumItems() > 0) collections.addSeparator();
+    collections.addItem(kNewCollection, juce::String::fromUTF8("New collection…"));
+    juce::PopupMenu menu;
+    menu.addSubMenu("Add to collection", collections);
+    menu.addItem(kEditTags, juce::String::fromUTF8("Tags…"));
+    menu.addSeparator();
+    menu.addItem(kReveal, revealText());
+    return menu;
+}
+
+void AsmaEditor::rowMenuChosen(const SearchRow& row, int result)
+{
+    if (result == kNewCollection) {
+        newCollectionFile_ = row.id; // the sidebar's name field makes it, with this sample in it
+        sidebar_.startNewCollection();
+    } else if (result == kEditTags) {
+        const int at = browser_.rowOf(LibraryView::pathOf(row));
+        const auto area = at >= 0 ? getLocalArea(&table_, table_.getRowPosition(at, true)) : table_.getBounds();
+        juce::CallOutBox::launchAsynchronously(tagsPopover(row), area, this);
+    } else if (result == kReveal) {
+        juce::File(utf8(toUtf8(LibraryView::pathOf(row)))).revealToUser();
+    } else if (result >= kFirstCollection) {
+        const auto index = static_cast<std::size_t>(result - kFirstCollection);
+        if (index >= entries_.size() || entries_[index].kind != EntryKind::Collection) return;
+        const auto in = library_.collectionsOf(row.id);
+        const auto& e = entries_[index];
+        if (std::find(in.begin(), in.end(), e.id) != in.end()) write(Write::removeFromCollection(e.name, row.id));
+        else write(Write::addToCollection(e.name, row.id));
+    }
+}
+
+std::unique_ptr<TagsPopover> AsmaEditor::tagsPopover(const SearchRow& row)
+{
+    std::vector<TagsPopover::Tag> tags;
+    for (const auto& [name, source] : library_.tagsOf(row.id)) tags.push_back({name, source == TagSource::User});
+    // The user's first, then the rest, each by name.
+    std::stable_sort(tags.begin(), tags.end(), [](const auto& a, const auto& b) {
+        return a.user != b.user ? a.user : a.name < b.name;
+    });
+    return std::make_unique<TagsPopover>(utf8(row.name), std::move(tags), library_.tagCounts(),
+                                         [this, id = row.id](const std::string& tag, bool added) {
+                                             changeTag(id, tag, added);
+                                         });
+}
+
+void AsmaEditor::changeTag(std::int64_t fileId, const std::string& tag, bool added)
+{
+    // The table's tags column shows the change at once.
+    std::vector<std::string> shown;
+    for (const auto& [name, source] : library_.tagsOf(fileId)) shown.push_back(name);
+    SearchRow probe;
+    probe.id = fileId;
+    probe.tags = shown;
+    shown = pending_.apply(probe).tags;
+    shown.erase(std::remove(shown.begin(), shown.end(), tag), shown.end());
+    if (added) shown.push_back(tag);
+    const auto ticket = pending_.setTags(fileId, shown);
+    write(added ? Write::addTag(fileId, tag) : Write::removeTag(fileId, tag), ticket);
+}
+
 void AsmaEditor::cellClicked(int row, int column, const juce::MouseEvent& event)
 {
     const auto shown = shownRow(row);
-    if (!shown || event.mods.isPopupMenu()) return;
+    if (!shown) return;
+    if (event.mods.isPopupMenu()) {
+        const auto area = getLocalArea(&table_, table_.getRowPosition(row, true));
+        rowMenu(*shown).showMenuAsync(
+            juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(area)).withParentComponent(this),
+            [safe = juce::Component::SafePointer<AsmaEditor>(this), r = *shown](int result) {
+                if (safe && result != 0) safe->rowMenuChosen(r, result);
+            });
+        return;
+    }
     if (column == kFavourite) toggleFavourite(*shown);
     if (column == kRating) {
         // The event is the row's: measure from the cell's left.
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index ef74f1c..adb3517 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -15,6 +15,7 @@
 #include "ui/PreviewPanel.h"
 #include "ui/SidebarView.h"
 #include "ui/SimilarView.h"
+#include "ui/TagsPopover.h"
 #include "ui/TopBar.h"

 #include <juce_audio_processors/juce_audio_processors.h>
@@ -87,6 +88,15 @@ public:
     static bool asksBeforeDeleting(const SidebarEntry& entry);
     // The chip row's Save search: names the search in force and saves it.
     std::unique_ptr<NamePopover> saveSearchPopover();
+    // A row's right-click menu: Add to collection (ticking those it is in,
+    // and New collection…), Tags… and Show in Finder (Explorer, the file
+    // manager); and what picking an item does.
+    juce::PopupMenu rowMenu(const SearchRow& row);
+    void rowMenuChosen(const SearchRow& row, int result);
+    enum RowMenuItem { kNewCollection = 1, kEditTags, kReveal, kFirstCollection = 100 };
+    static juce::String revealText();
+    // A sample's tags to change, as Tags… opens it.
+    std::unique_ptr<TagsPopover> tagsPopover(const SearchRow& row);

 private:
     enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
@@ -122,6 +132,7 @@ private:
     std::optional<juce::String> nameRefusal(int index, const juce::String& name);
     void named(int index, const juce::String& name);
     void deleteEntryNow(int index);
+    void changeTag(std::int64_t fileId, const std::string& tag, bool added);
     std::vector<std::string> namesOf(EntryKind kind) const;

     AsmaProcessor& processor_;
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index f68e4d1..b8343bc 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -113,6 +113,24 @@ std::vector<Collection> LibraryView::collections()
     return guarded([&] { return UserData(*db_).collections(); }, std::vector<Collection>{});
 }

+std::vector<std::int64_t> LibraryView::collectionsOf(std::int64_t fileId)
+{
+    return guarded(
+        [&] {
+            auto q = db_->prepare("SELECT collection_id FROM collection_items WHERE file_id = ? ORDER BY collection_id");
+            q.bind(1, fileId);
+            std::vector<std::int64_t> ids;
+            while (q.step()) ids.push_back(q.getInt(0));
+            return ids;
+        },
+        std::vector<std::int64_t>{});
+}
+
+std::vector<std::pair<std::string, TagSource>> LibraryView::tagsOf(std::int64_t fileId)
+{
+    return guarded([&] { return Library(*db_).tags(fileId); }, std::vector<std::pair<std::string, TagSource>>{});
+}
+
 std::vector<SavedSearch> LibraryView::savedSearches()
 {
     return guarded([&] { return UserData(*db_).savedSearches(); }, std::vector<SavedSearch>{});
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index dc9a9ea..68decdc 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -66,6 +66,10 @@ public:
     std::vector<Root> roots();
     std::vector<Collection> collections();
     std::vector<SavedSearch> savedSearches();
+    // The collections a file is in, by id; empty unless open.
+    std::vector<std::int64_t> collectionsOf(std::int64_t fileId);
+    // A file's tags and where each came from; empty unless open.
+    std::vector<std::pair<std::string, TagSource>> tagsOf(std::int64_t fileId);
     // What sounds like the file, from analysed sound profiles.
     SimilarResult similar(std::int64_t fileId, int limit = 10);
     // The tags searches can find, most used first; empty unless open.
```

Create `plugin/src/ui/TagsPopover.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/TagsPopover.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace asma::app {

namespace {

constexpr int kWidth = 360;
constexpr int kPad = 14;
constexpr int kTitle = 22;
constexpr int kChipHeight = 24;
constexpr int kChipGap = 6;
constexpr int kCross = 16;
constexpr int kFieldHeight = 28;
constexpr int kSuggestionHeight = 24;
constexpr int kMaxSuggestions = 5;

// As the library stores a tag: trimmed and lower case.
std::string normal(const juce::String& text)
{
    std::string out = text.trim().toStdString();
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

juce::Font chipFont() { return theme::font(theme::Face::Text, 11.0f); }

class Cross final : public juce::Button {
public:
    explicit Cross(const juce::String& tag) : juce::Button("Remove " + tag) { setTitle(getName()); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        g.setFont(theme::font(theme::Face::Text, 11.0f));
        g.setColour(highlighted ? theme::amberLight : theme::amber);
        g.drawText(juce::String::fromUTF8("×"), getLocalBounds(), juce::Justification::centred, false);
    }
};

class Suggestion final : public juce::Button {
public:
    Suggestion(const juce::String& name, std::int64_t count) : juce::Button(name), count_(count) { setTitle(name); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down) {
            g.setColour(theme::border);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
        }
        auto area = getLocalBounds().reduced(8, 0);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(theme::muted);
        g.drawText(juce::String(count_), area, juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(40), juce::Justification::centredLeft, true);
    }

private:
    std::int64_t count_;
};

} // namespace

TagsPopover::TagsPopover(const juce::String& sampleName, std::vector<Tag> tags, std::vector<TagCount> library,
                         Changed onChange)
    : sampleName_(sampleName), tags_(std::move(tags)), library_(std::move(library)), onChange_(std::move(onChange))
{
    setTitle("Tags");
    field_.setTitle("Add a tag");
    field_.setFont(theme::font(theme::Face::Text, 13.0f));
    field_.setIndents(8, 6);
    field_.setColour(juce::TextEditor::focusedOutlineColourId, theme::amber);
    field_.onTextChange = [this] { rebuild(); };
    field_.onReturnKey = [this] {
        const std::string tag = normal(field_.getText());
        field_.clear();
        add(tag);
    };
    field_.onEscapeKey = [this] {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    addAndMakeVisible(field_);
    rebuild();
}

juce::String TagsPopover::chipText(int index) const { return utf8(tags_[static_cast<std::size_t>(index)].name); }

bool TagsPopover::isRemovable(int index) const { return tags_[static_cast<std::size_t>(index)].user; }

juce::Button* TagsPopover::removeButton(int index)
{
    int user = 0;
    for (int i = 0; i < index; ++i) user += tags_[static_cast<std::size_t>(i)].user ? 1 : 0;
    return isRemovable(index) ? removeButtons_[user] : nullptr;
}

juce::StringArray TagsPopover::suggestions() const
{
    juce::StringArray out;
    for (auto* b : suggestionButtons_) out.add(b->getButtonText());
    return out;
}

void TagsPopover::add(std::string tag)
{
    if (tag.empty()) return;
    if (std::any_of(tags_.begin(), tags_.end(), [&](const Tag& t) { return t.name == tag; })) return;
    // The user's go first, as the library lists them.
    tags_.push_back({tag, true});
    std::stable_sort(tags_.begin(), tags_.end(), [](const Tag& a, const Tag& b) {
        return a.user != b.user ? a.user : a.name < b.name;
    });
    rebuild();
    if (onChange_) onChange_(tag, true);
}

void TagsPopover::remove(std::string tag)
{
    tags_.erase(std::remove_if(tags_.begin(), tags_.end(), [&](const Tag& t) { return t.user && t.name == tag; }),
                tags_.end());
    rebuild();
    if (onChange_) onChange_(tag, false);
}

void TagsPopover::rebuild()
{
    removeButtons_.clear();
    for (const auto& t : tags_) {
        if (!t.user) continue;
        auto* cross = removeButtons_.add(new Cross(utf8(t.name)));
        cross->onClick = [this, name = t.name] { remove(name); };
        addAndMakeVisible(cross);
    }
    suggestionButtons_.clear();
    const std::string typed = normal(field_.getText());
    if (!typed.empty()) {
        for (const auto& t : library_) {
            if (static_cast<int>(suggestionButtons_.size()) == kMaxSuggestions) break;
            if (t.name.rfind(typed, 0) != 0) continue;
            if (std::any_of(tags_.begin(), tags_.end(), [&](const Tag& have) { return have.name == t.name; })) continue;
            auto* s = suggestionButtons_.add(new Suggestion(utf8(t.name), t.count));
            s->onClick = [this, name = t.name] {
                field_.clear();
                add(name);
            };
            addAndMakeVisible(s);
        }
    }

    // The chips flow in rows; the height follows what there is.
    chipBounds_.clear();
    const int inner = kWidth - 2 * kPad;
    int x = 0, y = 0;
    for (const auto& t : tags_) {
        const int text = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(chipFont(), utf8(t.name))));
        const int w = std::min(inner, 9 + text + (t.user ? 4 + kCross + 4 : 9));
        if (x > 0 && x + w > inner) {
            x = 0;
            y += kChipHeight + kChipGap;
        }
        chipBounds_.emplace_back(kPad + x, kPad + kTitle + 6 + y, w, kChipHeight);
        x += w + kChipGap;
    }
    const int chipsHeight = tags_.empty() ? 0 : y + kChipHeight + 12;
    const int height = kPad + kTitle + 6 + chipsHeight + 16 + kFieldHeight
                     + (suggestionButtons_.isEmpty() ? 0 : 4 + suggestionButtons_.size() * kSuggestionHeight + 6) + 36;
    setSize(kWidth, height);
    resized();
    repaint();
}

void TagsPopover::paint(juce::Graphics& g)
{
    auto title = getLocalBounds().reduced(kPad).removeFromTop(kTitle);
    const auto titleFont = theme::font(theme::Face::Heading, 13.0f);
    g.setFont(titleFont);
    g.setColour(theme::text);
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(titleFont, "Tags")));
    g.drawText("Tags", title.removeFromLeft(w), juce::Justification::centredLeft, false);
    title.removeFromLeft(6);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText(sampleName_, title, juce::Justification::centredLeft, true);

    for (std::size_t i = 0; i < tags_.size(); ++i) {
        const auto r = chipBounds_[i].toFloat().reduced(0.5f);
        const bool user = tags_[i].user;
        if (user) {
            g.setColour(theme::amber.withAlpha(0.12f));
            g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
        }
        g.setColour(user ? theme::amber : theme::border);
        g.drawRoundedRectangle(r, r.getHeight() / 2.0f, 1.0f);
        g.setFont(chipFont());
        g.setColour(user ? theme::amberLight : theme::muted);
        g.drawText(utf8(tags_[i].name), chipBounds_[i].withTrimmedLeft(9).withTrimmedRight(user ? kCross + 8 : 9),
                   juce::Justification::centredLeft, true);
    }

    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText("Add a tag", field_.getBounds().translated(0, -16).withHeight(14), juce::Justification::bottomLeft, false);
    if (!suggestionButtons_.isEmpty()) {
        const auto list = suggestionButtons_.getFirst()->getBounds().getUnion(suggestionButtons_.getLast()->getBounds()).expanded(3);
        g.setColour(theme::raised);
        g.fillRoundedRectangle(list.toFloat(), theme::kRadius);
        g.setColour(theme::border);
        g.drawRoundedRectangle(list.toFloat().reduced(0.5f), theme::kRadius, 1.0f);
    }
    g.setFont(theme::font(theme::Face::Text, 12.0f));
    g.setColour(theme::muted);
    g.drawText("Return adds the tag. Changes apply at once.", getLocalBounds().reduced(kPad).removeFromBottom(18),
               juce::Justification::centredLeft, true);
}

void TagsPopover::resized()
{
    int user = 0;
    for (std::size_t i = 0; i < tags_.size() && i < chipBounds_.size(); ++i) {
        if (!tags_[i].user) continue;
        const auto r = chipBounds_[i];
        removeButtons_[user++]->setBounds(r.getRight() - 4 - kCross, r.getCentreY() - kCross / 2, kCross, kCross);
    }
    const int chipsBottom = chipBounds_.empty() ? kPad + kTitle + 6 : chipBounds_.back().getBottom() + 12;
    field_.setBounds(kPad, chipsBottom + 16, kWidth - 2 * kPad, kFieldHeight);
    int y = field_.getBottom() + 4 + 3;
    for (auto* s : suggestionButtons_) {
        s->setBounds(kPad + 3, y, kWidth - 2 * kPad - 6, kSuggestionHeight);
        y += kSuggestionHeight;
    }
}

} // namespace asma::app
```

Create `plugin/src/ui/TagsPopover.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Query.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>
#include <vector>

namespace asma::app {

// A sample's tags: the user's as chips with an x that removes them, the
// analyser's and the file's own greyed (they cannot be removed), and a field
// that adds one. As the field is typed in, the library's tags that start
// with it are offered with their counts; clicking one adds it. Return adds
// what the field holds. Tags are trimmed and lower
// case, so " Bass" adds nothing to a sample tagged "bass". Every change is
// reported at once.
class TagsPopover : public juce::Component {
public:
    struct Tag {
        std::string name;
        bool user = false; // the user's: removable
    };
    using Changed = std::function<void(const std::string& tag, bool added)>;
    TagsPopover(const juce::String& sampleName, std::vector<Tag> tags, std::vector<TagCount> library, Changed onChange);

    int chipCount() const { return static_cast<int>(tags_.size()); }
    juce::String chipText(int index) const;
    bool isRemovable(int index) const;
    juce::Button* removeButton(int index); // null for a tag that cannot be removed
    juce::TextEditor& field() { return field_; }
    // What the field's text offers, most used first.
    juce::StringArray suggestions() const;
    juce::Button& suggestion(int index) { return *suggestionButtons_[index]; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // By value: the button that calls them goes in rebuild(), with its copy.
    void add(std::string tag);
    void remove(std::string tag);
    void rebuild(); // the chips and the suggestions from tags_ and the field

    juce::String sampleName_;
    std::vector<Tag> tags_;
    std::vector<TagCount> library_;
    Changed onChange_;
    juce::OwnedArray<juce::Button> removeButtons_; // one per user tag, in tags_ order
    juce::TextEditor field_;
    juce::OwnedArray<juce::Button> suggestionButtons_;
    std::vector<juce::Rectangle<int>> chipBounds_;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[organise],[tags]"`

Expected: `All tests passed (113 assertions in 20 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/ui/TagsPopover.cpp plugin/src/ui/TagsPopover.h tests/plugin/test_organise.cpp tests/plugin/test_tags_popover.cpp
git commit -m "app: a row's menu adds it to collections, changes its tags and shows it in Finder"
```

---

### Task 8: The Problems panel, with Retry and Retry all

Spec section 9 (Organising: Problems). Clicking Problems puts the panel in place
of the chip row and the table until a sidebar entry is picked; each file shows
its folder and what went wrong, with Retry, and Retry all sits in the header. A
retry says it is running ("Retrying after the scan" while it waits); a file that
works leaves the list, one that fails again keeps its new reason, and an empty
list says so while Problems leaves the sidebar. Primary buttons (amber, dimmed
when they cannot act) come to the LookAndFeel.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Create: `plugin/src/ui/ProblemsView.cpp`
- Create: `plugin/src/ui/ProblemsView.h`
- Modify: `plugin/src/ui/SidebarView.cpp`
- Modify: `plugin/src/ui/SidebarView.h`
- Modify: `tests/plugin/test_organise.cpp` (test)
- Create: `tests/plugin/test_problems_view.cpp` (test)

**Interfaces:**

- Consumes: task 1's `Problem`; task 4's `LibraryWriter::retry`.
- Produces: `LibraryView::problems()`; `class ProblemsView` (`setProblems`,
  `setRetrying(std::set<std::int64_t>, bool waiting)`, `onRetry`,
  `static reasonText(const Problem&)`, `rowCount()`, `nameText`, `folderText`,
  `shownReason`, `retryButton(int)`, `retryAllButton()`, `countText()`,
  `message()`); `SidebarView::setProblemsLit(bool)`; `AsmaEditor::problems()`,
  `showProblems(bool)`, `showingProblems()`; the button property `asma.primary`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_organise.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_organise.cpp b/tests/plugin/test_organise.cpp
index 1dc74f8..fab5a40 100644
--- a/tests/plugin/test_organise.cpp
+++ b/tests/plugin/test_organise.cpp
@@ -1,6 +1,7 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #include "EditorRig.h"
 #include "asma/core/Fs.h"
+#include "asma/core/Scanner.h"
 #include "asma/core/UserData.h"

 #include <catch2/catch_test_macros.hpp>
@@ -382,3 +383,100 @@ TEST_CASE("a right click on a row opens its menu, not a rating", "[organise]")
     settle(rig);
     CHECK_FALSE(storedRating(rig, rig.f.kick));
 }
+
+namespace {
+
+// A library whose kick could not be read; the helper retries it.
+struct ProblemRig : EditorRig {
+    fs::path broken = f.lib / "Drums" / "Kick_01.wav";
+    ProblemRig() : EditorRig(AsmaProcessor::Mode::Standalone)
+    {
+        test::writeBytes(broken, "not audio");
+        touchLater();
+        {
+            Db db = Db::open(f.dbPath);
+            scanRoot(db, Library(db).roots().front().id);
+        }
+        p->writer().setCli(ASMA_CLI_PATH);
+        editor->poll();
+    }
+    void touchLater()
+    {
+        fs::last_write_time(broken, fs::last_write_time(broken) + std::chrono::seconds(5));
+    }
+    void waitForRetries()
+    {
+        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
+        while (!p->writer().idle() && std::chrono::steady_clock::now() < deadline)
+            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
+        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+        editor->poll();
+    }
+};
+
+} // namespace
+
+TEST_CASE("Problems takes the table's place until another sidebar entry is picked", "[organise][problems]")
+{
+    ProblemRig rig;
+    auto& problems = rig.editor->sidebar().problemsButton();
+    REQUIRE(problems.isVisible());
+    problems.triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->showingProblems());
+    CHECK(rig.editor->problems().isVisible());
+    CHECK_FALSE(rig.editor->table().isVisible());
+    CHECK_FALSE(rig.editor->chipRow().isVisible());
+    CHECK(problems.getToggleState());
+    REQUIRE(rig.editor->problems().rowCount() == 1);
+    CHECK(rig.editor->problems().nameText(0) == "Kick_01.wav");
+
+    rig.editor->sidebar().row(0).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK_FALSE(rig.editor->showingProblems());
+    CHECK(rig.editor->table().isVisible());
+}
+
+TEST_CASE("a retried file that still fails stays, with the new reason", "[organise][problems]")
+{
+    ProblemRig rig;
+    rig.editor->showProblems(true);
+    {
+        Db db = Db::open(rig.f.dbPath);
+        db.exec("UPDATE files SET failure_reason = 'an old reason' WHERE status = 'failed'");
+    }
+    rig.editor->poll();
+    REQUIRE(rig.editor->problems().shownReason(0) == "Could not read the file: an old reason");
+    rig.editor->problems().retryButton(0).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    rig.waitForRetries();
+    REQUIRE(rig.editor->problems().rowCount() == 1);
+    CHECK(rig.editor->problems().shownReason(0) != "Could not read the file: an old reason");
+    CHECK(rig.editor->problems().retryButton(0).isEnabled());
+}
+
+TEST_CASE("a fixed file leaves the list, and Problems leaves the sidebar", "[organise][problems]")
+{
+    ProblemRig rig;
+    rig.editor->showProblems(true);
+    test::writeWavFloat(rig.broken, 48000, {test::kickHit(48000)}); // fixed in place
+    rig.editor->problems().retryAllButton().triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    rig.waitForRetries();
+    CHECK(rig.editor->problems().rowCount() == 0);
+    CHECK(rig.editor->problems().message() == "Nothing has failed.");
+    CHECK(rig.editor->showingProblems()); // until another entry is picked
+    CHECK_FALSE(rig.editor->sidebar().problemsButton().isVisible());
+}
+
+TEST_CASE("a retry of a file deleted meanwhile says it has gone", "[organise][problems]")
+{
+    ProblemRig rig;
+    rig.editor->showProblems(true);
+    fs::remove(rig.broken);
+    rig.editor->problems().retryButton(0).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    rig.waitForRetries();
+    REQUIRE(rig.editor->problems().rowCount() == 1);
+    CHECK(rig.editor->problems().shownReason(0) == "The file is gone");
+}
```

Create `tests/plugin/test_problems_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/ProblemsView.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::ProblemsView;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

std::vector<Problem> problems()
{
    return {{1, "/Users/me/Samples", "Drums/Kicks/kick_broken_header.wav", Problem::Kind::Read, "not a valid WAV header"},
            {2, "/Users/me/Samples", "pad_long_take.flac", Problem::Kind::Read, "The file is gone"},
            {3, "/Users/me/Samples", "Vocals/vox_chop_07.mp3", Problem::Kind::Analysis, "the file is silent"}};
}

} // namespace

TEST_CASE("the Problems panel lists each file with its folder and what went wrong", "[problems]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ProblemsView view;
    view.setBounds(0, 0, 840, 400);
    view.setProblems(problems());
    REQUIRE(view.rowCount() == 3);
    CHECK(view.countText() == "3 files");
    CHECK(view.nameText(0) == "kick_broken_header.wav");
    CHECK(view.folderText(0) == "Samples/Drums/Kicks");
    CHECK(view.folderText(1) == "Samples");
    CHECK(view.shownReason(0) == "Could not read the file: not a valid WAV header");
    CHECK(view.shownReason(1) == "The file is gone");
    CHECK(view.shownReason(2) == "Analysis failed: the file is silent");
    CHECK(view.message().isEmpty());
    view.setProblems({});
    CHECK(view.message() == "Nothing has failed.");
    CHECK_FALSE(view.retryAllButton().isVisible());
}

TEST_CASE("Retry and Retry all ask for the files not already retrying", "[problems]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ProblemsView view;
    view.setBounds(0, 0, 840, 400);
    view.setProblems(problems());
    std::vector<std::vector<std::int64_t>> asked;
    view.onRetry = [&](std::vector<std::int64_t> ids) { asked.push_back(ids); };
    view.retryButton(1).triggerClick();
    settle();
    view.setRetrying({2}, false);
    CHECK_FALSE(view.retryButton(1).isEnabled());
    CHECK(view.shownReason(1) == juce::String::fromUTF8("Retrying…"));
    view.setRetrying({2}, true);
    CHECK(view.shownReason(1) == juce::String::fromUTF8("Retrying after the scan…"));
    view.retryAllButton().triggerClick();
    settle();
    CHECK(asked == std::vector<std::vector<std::int64_t>>{{2}, {1, 3}});
    view.setRetrying({1, 2, 3}, false);
    CHECK_FALSE(view.retryAllButton().isEnabled());
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[problems]"`

Expected: the build stops:

```
tests/plugin/test_problems_view.cpp:3:10: fatal error: 'ui/ProblemsView.h' file not found
tests/plugin/test_organise.cpp:426:23: error: no member named 'showingProblems' in 'asma::app::AsmaEditor'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 75a4c0b..49c4372 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -52,9 +52,13 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
     top_.searchBox().onTextChange = [this] { searchChanged(); };
     addAndMakeVisible(top_);
     sidebar_.onPick = [this](int index) {
+        showProblems(false);
         if (index >= 0 && index < static_cast<int>(entries_.size()))
             applySearch(withEntry(entries_[static_cast<std::size_t>(index)], browser_.searchModel()));
     };
+    sidebar_.onProblems = [this] { showProblems(true); };
+    problems_.onRetry = [this](std::vector<std::int64_t> ids) { retry(std::move(ids)); };
+    addChildComponent(problems_);
     sidebar_.nameRefusal = [this](int index, const juce::String& name) { return nameRefusal(index, name); };
     sidebar_.onNamed = [this](int index, const juce::String& name) { named(index, name); };
     sidebar_.onDelete = [this](int index) { deleteEntry(index); };
@@ -232,6 +236,7 @@ void AsmaEditor::resized()
     similar_.setBounds(bottom.removeFromRight(theme::kSimilarWidth).withTrimmedLeft(1));
     preview_.setBounds(bottom);
     sidebar_.setBounds(area.removeFromLeft(theme::kSidebarWidth));
+    problems_.setBounds(area);
     chips_.setBounds(area.removeFromTop(theme::kChipRowHeight));
     table_.setBounds(area);
     empty_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 520), 60).translated(0, -20));
@@ -479,6 +484,39 @@ void AsmaEditor::changeTag(std::int64_t fileId, const std::string& tag, bool add
     write(added ? Write::addTag(fileId, tag) : Write::removeTag(fileId, tag), ticket);
 }

+void AsmaEditor::showProblems(bool show)
+{
+    showingProblems_ = show;
+    if (show) problems_.setProblems(library_.problems());
+    problems_.setVisible(show);
+    chips_.setVisible(!show);
+    table_.setVisible(!show);
+    sidebar_.setProblemsLit(show);
+    updateReadouts();
+}
+
+void AsmaEditor::retry(std::vector<std::int64_t> ids)
+{
+    retrying_.insert(ids.begin(), ids.end());
+    problems_.setRetrying(retrying_, retryWaiting_);
+    processor_.writer().retry(ids, [safe = juce::Component::SafePointer<AsmaEditor>(this), ids](const RetryEvent& e) {
+        if (!safe) return;
+        if (e.kind == RetryEvent::Kind::Waiting) {
+            safe->retryWaiting_ = true;
+            safe->scanMessage_ = "Retrying after the scan";
+        } else {
+            for (const auto id : ids) safe->retrying_.erase(id);
+            if (safe->retrying_.empty()) safe->retryWaiting_ = false;
+            safe->scanMessage_ = e.kind == RetryEvent::Kind::Failed ? "Could not retry: " + utf8(e.error) : juce::String();
+            // Whatever the library now says about those files.
+            safe->problems_.setProblems(safe->library_.problems());
+            safe->refreshSidebar();
+        }
+        safe->problems_.setRetrying(safe->retrying_, safe->retryWaiting_);
+        safe->updateReadouts();
+    });
+}
+
 void AsmaEditor::cellClicked(int row, int column, const juce::MouseEvent& event)
 {
     const auto shown = shownRow(row);
@@ -573,6 +611,7 @@ void AsmaEditor::poll()
         similarFor_ = 0; // the library changed: analysis may have reached the selection
         showSelection();
         refreshSidebar();
+        if (showingProblems_) problems_.setProblems(library_.problems());
     }
     updateReadouts();
 }
@@ -695,12 +734,13 @@ void AsmaEditor::updateReadouts()
         // text, chips or scope) matches nothing.
         empty = browser_.total() == 0 ? (standalone ? "The library is empty. Add a folder of samples." : "The library is empty.")
                                       : "No samples match.";
+    if (showingProblems_) empty.clear(); // the panel says its own
     if (empty != empty_.getText()) empty_.setText(empty, juce::dontSendNotification);
     empty_.setVisible(empty.isNotEmpty());
     // "Add folder" where there is nothing yet, not where a search found nothing.
     const bool nothing = library_.state() == LibraryState::Missing
                       || (library_.state() == LibraryState::Open && browser_.total() == 0);
-    emptyAddFolder_.setVisible(standalone && nothing);
+    emptyAddFolder_.setVisible(standalone && nothing && !showingProblems_);

     // The preview.
     if (selected_) {
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index adb3517..1f172a2 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -13,6 +13,7 @@
 #include "ui/Footer.h"
 #include "ui/NamePopover.h"
 #include "ui/PreviewPanel.h"
+#include "ui/ProblemsView.h"
 #include "ui/SidebarView.h"
 #include "ui/SimilarView.h"
 #include "ui/TagsPopover.h"
@@ -22,6 +23,7 @@
 #include <cstdint>
 #include <filesystem>
 #include <memory>
+#include <set>

 namespace asma::app {

@@ -59,6 +61,10 @@ public:
     SidebarView& sidebar() { return sidebar_; }
     ChipRow& chipRow() { return chips_; }
     SimilarView& similar() { return similar_; }
+    ProblemsView& problems() { return problems_; }
+    // The Problems panel in the table's place, until a sidebar entry is picked.
+    void showProblems(bool show);
+    bool showingProblems() const { return showingProblems_; }
     // What a chip's popover needs: the library's tags and the tempo in force.
     PopoverContext popoverContext();
     // The standalone's tempo source and folders; hidden in a plugin.
@@ -133,6 +139,7 @@ private:
     void named(int index, const juce::String& name);
     void deleteEntryNow(int index);
     void changeTag(std::int64_t fileId, const std::string& tag, bool added);
+    void retry(std::vector<std::int64_t> ids);
     std::vector<std::string> namesOf(EntryKind kind) const;

     AsmaProcessor& processor_;
@@ -161,6 +168,10 @@ private:
     std::string selectedFolder_;
     PendingEdits pending_;
     std::int64_t newCollectionFile_ = 0; // the sample a new collection starts with (0: none)
+    ProblemsView problems_;
+    bool showingProblems_ = false;
+    std::set<std::int64_t> retrying_; // files a retry runs for
+    bool retryWaiting_ = false;       // a retry waits for a scan

     JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
 };
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index b8343bc..d3dd9f9 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -176,6 +176,11 @@ std::int64_t LibraryView::problemCount()
         std::int64_t{0});
 }

+std::vector<Problem> LibraryView::problems()
+{
+    return guarded([&] { return Library(*db_).problems(); }, std::vector<Problem>{});
+}
+
 std::int64_t LibraryView::sampleCount()
 {
     return guarded(
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index 68decdc..e815890 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -77,6 +77,8 @@ public:
     // Files in enabled folders that failed to decode or analyse: the
     // Problems entry's count. 0 unless open.
     std::int64_t problemCount();
+    // Those files, read failures first, each with why; empty unless open.
+    std::vector<Problem> problems();
     // Every sample a search could find: readable files in enabled folders. 0 unless open.
     std::int64_t sampleCount();
     // What the model matches, past the page search() returns. 0 unless open.
```

Create `plugin/src/ui/ProblemsView.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ProblemsView.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kHeaderHeight = 44;
constexpr int kColumnsHeight = 30;
constexpr int kRowHeight = 46;
constexpr int kButtonWidth = 70;
constexpr int kMargin = 14;

juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

std::string baseName(const std::string& relPath)
{
    const auto slash = relPath.rfind('/');
    return slash == std::string::npos ? relPath : relPath.substr(slash + 1);
}

std::string folderOf(const Problem& p)
{
    const auto slash = p.relPath.rfind('/');
    const std::string root = baseName(p.rootPath);
    if (slash == std::string::npos) return root;
    return root + "/" + p.relPath.substr(0, slash);
}

// The two columns' widths, from the area left of the button.
std::pair<int, int> columns(int width)
{
    const int room = width - 2 * kMargin - kButtonWidth - 20;
    const int file = room * 10 / 23;
    return {file, room - file};
}

} // namespace

class ProblemsView::Rows final : public juce::Component {
public:
    explicit Rows(ProblemsView& owner) : owner_(owner) {}
    void paint(juce::Graphics& g) override
    {
        const auto [file, reason] = columns(getWidth());
        for (int i = 0; i < owner_.rowCount(); ++i) {
            const int y = i * kRowHeight;
            g.setColour(theme::raised);
            g.fillRect(0, y + kRowHeight - 1, getWidth(), 1);
            g.setFont(theme::font(theme::Face::Text, 13.0f));
            g.setColour(theme::text);
            g.drawText(owner_.nameText(i), kMargin, y + 7, file, 18, juce::Justification::centredLeft, true);
            g.setFont(theme::font(theme::Face::Mono, 11.0f));
            g.setColour(theme::muted);
            g.drawText(owner_.folderText(i), kMargin, y + 25, file, 14, juce::Justification::centredLeft, true);
            const bool retrying = owner_.retrying_.count(owner_.problems_[static_cast<std::size_t>(i)].id) > 0;
            g.setFont(theme::font(theme::Face::Text, 12.0f));
            g.setColour(retrying ? theme::muted : theme::text);
            g.drawFittedText(owner_.shownReason(i), kMargin + file + 10, y + 4, reason, kRowHeight - 8,
                             juce::Justification::centredLeft, 2);
        }
    }

private:
    ProblemsView& owner_;
};

ProblemsView::ProblemsView() : rows_(std::make_unique<Rows>(*this))
{
    retryAll_.getProperties().set("asma.accent", true);
    retryAll_.onClick = [this] {
        std::vector<std::int64_t> ids;
        for (const auto& p : problems_)
            if (!retrying_.count(p.id)) ids.push_back(p.id);
        if (!ids.empty() && onRetry) onRetry(ids);
    };
    addAndMakeVisible(retryAll_);
    viewport_.setViewedComponent(rows_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
}

ProblemsView::~ProblemsView() = default;

juce::String ProblemsView::reasonText(const Problem& p)
{
    if (p.kind == Problem::Kind::Analysis) return "Analysis failed: " + utf8(p.reason);
    if (p.reason == "The file is gone") return utf8(p.reason);
    return "Could not read the file: " + utf8(p.reason);
}

juce::String ProblemsView::nameText(int index) const { return utf8(baseName(problems_[static_cast<std::size_t>(index)].relPath)); }

juce::String ProblemsView::folderText(int index) const { return utf8(folderOf(problems_[static_cast<std::size_t>(index)])); }

juce::String ProblemsView::shownReason(int index) const
{
    const auto& p = problems_[static_cast<std::size_t>(index)];
    if (retrying_.count(p.id)) return juce::String::fromUTF8(waiting_ ? "Retrying after the scan…" : "Retrying…");
    return reasonText(p);
}

juce::String ProblemsView::countText() const
{
    return juce::String(problems_.size()) + (problems_.size() == 1 ? " file" : " files");
}

void ProblemsView::setProblems(std::vector<Problem> problems)
{
    problems_ = std::move(problems);
    retryButtons_.clear();
    for (const auto& p : problems_) {
        auto* b = retryButtons_.add(new juce::TextButton("Retry"));
        b->setTitle("Retry " + utf8(baseName(p.relPath)));
        b->getProperties().set("asma.size", 12.0f);
        b->onClick = [this, id = p.id] {
            if (onRetry) onRetry({id});
        };
        rows_->addAndMakeVisible(b);
    }
    setRetrying(retrying_, waiting_);
    resized();
}

void ProblemsView::setRetrying(std::set<std::int64_t> ids, bool waiting)
{
    retrying_ = std::move(ids);
    waiting_ = waiting;
    for (std::size_t i = 0; i < problems_.size(); ++i)
        retryButtons_[static_cast<int>(i)]->setEnabled(!retrying_.count(problems_[i].id));
    bool any = false;
    for (const auto& p : problems_) any |= !retrying_.count(p.id);
    retryAll_.setEnabled(any);
    retryAll_.setVisible(!problems_.empty());
    rows_->repaint();
    repaint();
}

void ProblemsView::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    auto header = getLocalBounds().removeFromTop(kHeaderHeight).reduced(kMargin, 0);
    const auto title = theme::font(theme::Face::Heading, 14.0f);
    g.setFont(title);
    g.setColour(theme::text);
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(title, "Problems")));
    g.drawText("Problems", header.removeFromLeft(w), juce::Justification::centredLeft, false);
    header.removeFromLeft(10);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(theme::muted);
    g.drawText(countText(), header, juce::Justification::centredLeft, false);
    g.setColour(theme::border);
    g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);
    if (problems_.empty()) {
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::muted);
        g.drawText(message(), getLocalBounds().withTrimmedTop(kHeaderHeight), juce::Justification::centred, false);
        return;
    }
    const auto [file, reason] = columns(getWidth());
    g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.06f));
    g.setColour(theme::muted);
    g.drawText("FILE", kMargin, kHeaderHeight, file, kColumnsHeight, juce::Justification::centredLeft, false);
    g.drawText("WHAT WENT WRONG", kMargin + file + 10, kHeaderHeight, reason, kColumnsHeight,
               juce::Justification::centredLeft, false);
    g.setColour(theme::border);
    g.fillRect(0, kHeaderHeight + kColumnsHeight - 1, getWidth(), 1);
}

void ProblemsView::resized()
{
    retryAll_.setBounds(getWidth() - kMargin - 84, (kHeaderHeight - 28) / 2, 84, 28);
    viewport_.setBounds(getLocalBounds().withTrimmedTop(kHeaderHeight + kColumnsHeight));
    const int w = viewport_.getMaximumVisibleWidth();
    rows_->setSize(w, rowCount() * kRowHeight);
    for (int i = 0; i < retryButtons_.size(); ++i)
        retryButtons_[i]->setBounds(w - kMargin - kButtonWidth, i * kRowHeight + (kRowHeight - 26) / 2, kButtonWidth, 26);
}

} // namespace asma::app
```

Create `plugin/src/ui/ProblemsView.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Library.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

namespace asma::app {

// In the table's place: the files that could not be read or analysed, each
// with its folder, what went wrong and a Retry button, and Retry all in the
// header. A file being retried says so and cannot be retried again until it
// is done. With nothing left it says that nothing has failed.
class ProblemsView : public juce::Component {
public:
    ProblemsView();
    ~ProblemsView() override;

    void setProblems(std::vector<Problem> problems);
    // The files a retry is running for; `waiting` while it waits for a scan.
    void setRetrying(std::set<std::int64_t> ids, bool waiting);

    std::function<void(std::vector<std::int64_t> ids)> onRetry;

    // What a problem says: "Could not read the file: …", "Analysis failed: …".
    static juce::String reasonText(const Problem& problem);

    int rowCount() const { return static_cast<int>(problems_.size()); }
    juce::String nameText(int index) const;
    juce::String folderText(int index) const;
    juce::String shownReason(int index) const; // what the row says now, retrying or not
    juce::Button& retryButton(int index) { return *retryButtons_[index]; }
    juce::Button& retryAllButton() { return retryAll_; }
    juce::String countText() const;
    juce::String message() const { return problems_.empty() ? juce::String("Nothing has failed.") : juce::String(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Rows;
    std::vector<Problem> problems_;
    std::set<std::int64_t> retrying_;
    bool waiting_ = false;
    juce::OwnedArray<juce::TextButton> retryButtons_;
    juce::TextButton retryAll_{"Retry all"};
    std::unique_ptr<Rows> rows_;
    juce::Viewport viewport_;
};

} // namespace asma::app
```

In `plugin/src/ui/SidebarView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/SidebarView.cpp b/plugin/src/ui/SidebarView.cpp
index 612ccc0..eb6a10a 100644
--- a/plugin/src/ui/SidebarView.cpp
+++ b/plugin/src/ui/SidebarView.cpp
@@ -81,11 +81,11 @@ public:
     void paintButton(juce::Graphics& g, bool highlighted, bool down) override
     {
         const auto r = getLocalBounds().toFloat().reduced(0.5f);
-        if (highlighted || down) {
+        if (highlighted || down || getToggleState()) {
             g.setColour(theme::raised);
             g.fillRoundedRectangle(r, theme::kRadius);
         }
-        g.setColour(theme::border);
+        g.setColour(getToggleState() ? theme::amber : theme::border);
         g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
         auto area = getLocalBounds().reduced(10, 0);
         g.setFont(theme::font(theme::Face::Text, 13.0f));
@@ -207,6 +207,8 @@ void SidebarView::setProblems(std::int64_t count)
     repaint();
 }

+void SidebarView::setProblemsLit(bool lit) { problems_->setToggleState(lit, juce::dontSendNotification); }
+
 juce::String SidebarView::countText(int index) const
 {
     return index >= 0 && index < rows_.size() ? static_cast<const Row*>(rows_[index])->count() : juce::String();
```

In `plugin/src/ui/SidebarView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/SidebarView.h b/plugin/src/ui/SidebarView.h
index 0c52a7c..14a35ea 100644
--- a/plugin/src/ui/SidebarView.h
+++ b/plugin/src/ui/SidebarView.h
@@ -26,6 +26,8 @@ public:
     // The lit entry; -1 for none.
     void setSelected(int index);
     void setProblems(std::int64_t count);
+    // Problems is lit while its panel shows.
+    void setProblemsLit(bool lit);

     std::function<void(int)> onPick;
     std::function<void()> onProblems;
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[problems]"`

Expected: `All tests passed (40 assertions in 6 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/ui/ProblemsView.cpp plugin/src/ui/ProblemsView.h plugin/src/ui/SidebarView.cpp plugin/src/ui/SidebarView.h tests/plugin/test_organise.cpp tests/plugin/test_problems_view.cpp
git commit -m "app: the Problems panel, with Retry and Retry all"
```

---

### Task 9: Hold the Tags popover and the Problems panel to the design

Spec section 12 (UI fidelity). Each is rendered as it shows in the window and
compared, blurred, with a page rebuilt from the organise artboard at the
component's size. The limits sit between the figures with the content right and
missing (Tags 1.8% / 18.2%, Problems 2.7% / 10.3%).

**Files:**

- Modify: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Modify: `plugin/src/ui/NamePopover.cpp`
- Modify: `plugin/src/ui/ProblemsView.cpp`
- Modify: `tests/plugin/test_problems_view.cpp` (test)
- Modify: `tests/plugin/test_ui_fidelity.cpp` (test)
- Modify: `tests/ui/reference/README.md` (test)
- Create: `tests/ui/reference/problems.html` (test)
- Create: `tests/ui/reference/problems.png` (test)
- Create: `tests/ui/reference/tags-popover.html` (test)
- Create: `tests/ui/reference/tags-popover.png` (test)

**Interfaces:**

- Consumes: tasks 7 and 8.
- Produces: `tests/ui/reference/tags-popover.{html,png}`, `problems.{html,png}`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_problems_view.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_problems_view.cpp b/tests/plugin/test_problems_view.cpp
index e146252..dc7cc4e 100644
--- a/tests/plugin/test_problems_view.cpp
+++ b/tests/plugin/test_problems_view.cpp
@@ -29,7 +29,7 @@ TEST_CASE("the Problems panel lists each file with its folder and what went wron
     REQUIRE(view.rowCount() == 3);
     CHECK(view.countText() == "3 files");
     CHECK(view.nameText(0) == "kick_broken_header.wav");
-    CHECK(view.folderText(0) == "Samples/Drums/Kicks");
+    CHECK(view.folderText(0) == "Drums / Kicks");
     CHECK(view.folderText(1) == "Samples");
     CHECK(view.shownReason(0) == "Could not read the file: not a valid WAV header");
     CHECK(view.shownReason(1) == "The file is gone");
```

In `tests/plugin/test_ui_fidelity.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_ui_fidelity.cpp b/tests/plugin/test_ui_fidelity.cpp
index a55df6f..68d0959 100644
--- a/tests/plugin/test_ui_fidelity.cpp
+++ b/tests/plugin/test_ui_fidelity.cpp
@@ -1,12 +1,14 @@
 // SPDX-License-Identifier: GPL-3.0-only
 // The editor against the approved design: tests/ui/reference/main.png is the
-// design's main artboard filled with this file's demo library, the 3c2b
-// areas blanked (tests/ui/reference/main.html renders it; see README.md
-// there). Font rendering differs between systems, so this runs on macOS
+// design's main artboard filled with this file's demo library
+// (tests/ui/reference/main.html renders it; see README.md there), and the
+// popovers and the Problems panel against their own pictures. Font rendering differs between systems, so this runs on macOS
 // only; the behaviour tests run everywhere.
 #include "AsmaEditor.h"
 #include "PluginTestUtil.h"
 #include "ui/FilterPopovers.h"
+#include "ui/ProblemsView.h"
+#include "ui/TagsPopover.h"
 #include "ui/Theme.h"
 #include "Signals.h"
 #include "asma/core/Analyser.h"
@@ -288,3 +290,66 @@ TEST_CASE("the Key popover matches the approved design", "[fidelity]")
     CHECK(share <= 0.035); // 1.4% when right
     popover.setLookAndFeel(nullptr);
 }
+
+namespace {
+
+// A component as it shows in the window, over `ground`, against its
+// reference picture; the share of pixels that differ.
+double againstReference(juce::Component& component, juce::Colour ground, const char* name)
+{
+    const int w = component.getWidth(), h = component.getHeight();
+    juce::Image current(juce::Image::ARGB, w, h, true, juce::SoftwareImageType{});
+    {
+        juce::Graphics g(current);
+        g.fillAll(ground);
+        component.paintEntireComponent(g, false);
+    }
+    writePng(current, outDir() / (std::string(name) + ".png"));
+    const juce::Image reference = juce::ImageFileFormat::loadFrom(
+        juce::File(juce::String(ASMA_TEST_UI) + "/reference/" + name + ".png"));
+    REQUIRE(reference.getWidth() == w);
+    REQUIRE(reference.getHeight() == h);
+    return mismatch(blurred(current), blurred(reference), w, {0, 0, w, h}, {}, nullptr);
+}
+
+} // namespace
+
+TEST_CASE("the Tags popover matches the approved design", "[fidelity]")
+{
+#if !JUCE_MAC
+    SKIP("font rendering differs off macOS; the reference was made there");
+#endif
+    const juce::ScopedJuceInitialiser_GUI gui;
+    app::AsmaLookAndFeel lnf;
+    app::TagsPopover popover("Bass_Loop_Am_120.wav", {{"dark", true}, {"live set", true}, {"bass", false}, {"synth", false}},
+                             {{"bass", 214}, {"gritty", 31}, {"synth", 96}, {"groove", 9}}, {});
+    popover.setLookAndFeel(&lnf);
+    popover.field().setText("gr", true);
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the suggestions follow the field
+    REQUIRE(popover.suggestions() == juce::StringArray{"gritty", "groove"});
+    REQUIRE(popover.getWidth() == 360);
+    const double share = againstReference(popover, app::theme::panel, "tags-popover");
+    INFO("tags popover: " << share * 100.0 << "% of pixels differ");
+    CHECK(share <= 0.05); // 1.8% / 18.2% with no tags
+    popover.setLookAndFeel(nullptr);
+}
+
+TEST_CASE("the Problems panel matches the approved design", "[fidelity]")
+{
+#if !JUCE_MAC
+    SKIP("font rendering differs off macOS; the reference was made there");
+#endif
+    const juce::ScopedJuceInitialiser_GUI gui;
+    app::AsmaLookAndFeel lnf;
+    app::ProblemsView view;
+    view.setLookAndFeel(&lnf);
+    view.setBounds(0, 0, 840, 212);
+    view.setProblems({{1, "/Samples", "Drums/Kicks/kick_broken_header.wav", Problem::Kind::Read, "not a valid WAV header"},
+                      {2, "/Samples", "Pads/pad_long_take.flac", Problem::Kind::Read, "unexpected end of stream"},
+                      {3, "/Samples", "Vocals/Chops/vox_chop_07.mp3", Problem::Kind::Analysis, "the file is silent"}});
+    view.setRetrying({2}, false);
+    const double share = againstReference(view, app::theme::surface, "problems");
+    INFO("problems panel: " << share * 100.0 << "% of pixels differ");
+    CHECK(share <= 0.05); // 2.7% / 10.3% with no files
+    view.setLookAndFeel(nullptr);
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fidelity]"`

Expected: with the tests only (no reference pictures yet), the two new cases
fail:

```
tests/plugin/test_ui_fidelity.cpp:310: FAILED:
  REQUIRE( reference.getWidth() == w )
with expansion:
  0 == 840 (0x348)
tests/plugin/test_ui_fidelity.cpp:310: FAILED:
  REQUIRE( reference.getWidth() == w )
with expansion:
  0 == 360 (0x168)
```

- [ ] **Step 3: Implement**

In `plugin/src/ui/AsmaLookAndFeel.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.cpp b/plugin/src/ui/AsmaLookAndFeel.cpp
index 0f5205a..fbed6fc 100644
--- a/plugin/src/ui/AsmaLookAndFeel.cpp
+++ b/plugin/src/ui/AsmaLookAndFeel.cpp
@@ -69,6 +69,13 @@ void AsmaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& butt
     if (highlighted || down) fill = fill.isTransparent() ? theme::raised.withAlpha(0.6f) : fill.brighter(0.06f);

     auto r = button.getLocalBounds().toFloat();
+    if (props["asma.primary"]) { // the one action of a panel: amber, dimmed while it cannot act
+        const bool enabled = button.isEnabled();
+        g.setColour(enabled ? (highlighted || down ? theme::amber.brighter(0.06f) : theme::amber)
+                            : theme::amber.withAlpha(0.3f).overlaidWith(juce::Colours::transparentBlack));
+        g.fillRoundedRectangle(r, theme::kRadius);
+        return;
+    }
     if (segment.isEmpty()) {
         r = r.reduced(0.5f);
         g.setColour(fill);
@@ -96,12 +103,15 @@ void AsmaLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button
 {
     const auto& props = button.getProperties();
     const bool on = button.getToggleState();
-    const bool accent = on && props["asma.accent"];
+    const bool primary = props["asma.primary"];
+    const bool accent = (on && props["asma.accent"]) || primary;
     const bool quietText = !on && quiet(button);
     const float size = props.contains("asma.size") ? static_cast<float>(props["asma.size"]) : 13.0f;
     const bool mono = props["asma.mono"];
     g.setFont(theme::font(mono ? theme::Face::Mono : (accent ? theme::Face::SemiBold : theme::Face::Text), size));
-    g.setColour(accent ? theme::ground : (quietText ? theme::muted : theme::text));
+    juce::Colour colour = accent ? theme::ground : (quietText ? theme::muted : theme::text);
+    if (!button.isEnabled()) colour = primary ? theme::muted : theme::faint;
+    g.setColour(colour);
     g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
 }

```

In `plugin/src/ui/NamePopover.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/NamePopover.cpp b/plugin/src/ui/NamePopover.cpp
index 247e212..92f5db9 100644
--- a/plugin/src/ui/NamePopover.cpp
+++ b/plugin/src/ui/NamePopover.cpp
@@ -27,7 +27,7 @@ NamePopover::NamePopover(const juce::String& title, const juce::String& initial,
     addAndMakeVisible(field_);
     cancel_.onClick = [this] { close(); };
     addAndMakeVisible(cancel_);
-    save_.getProperties().set("asma.accent", true);
+    save_.getProperties().set("asma.primary", true);
     save_.onClick = [this] { save(); };
     addAndMakeVisible(save_);
     setSize(300, 150);
```

In `plugin/src/ui/ProblemsView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/ProblemsView.cpp b/plugin/src/ui/ProblemsView.cpp
index 923fb43..0e3230c 100644
--- a/plugin/src/ui/ProblemsView.cpp
+++ b/plugin/src/ui/ProblemsView.cpp
@@ -23,12 +23,13 @@ std::string baseName(const std::string& relPath)
     return slash == std::string::npos ? relPath : relPath.substr(slash + 1);
 }

-std::string folderOf(const Problem& p)
+// The folder as the preview's file line says it ("Drums / Kicks"); the
+// sample folder's own name for a file directly in it.
+juce::String folderOf(const Problem& p)
 {
     const auto slash = p.relPath.rfind('/');
-    const std::string root = baseName(p.rootPath);
-    if (slash == std::string::npos) return root;
-    return root + "/" + p.relPath.substr(0, slash);
+    if (slash == std::string::npos) return utf8(baseName(p.rootPath));
+    return utf8(p.relPath.substr(0, slash)).replace("/", " / ");
 }

 // The two columns' widths, from the area left of the button.
@@ -71,7 +72,7 @@ private:

 ProblemsView::ProblemsView() : rows_(std::make_unique<Rows>(*this))
 {
-    retryAll_.getProperties().set("asma.accent", true);
+    retryAll_.getProperties().set("asma.primary", true);
     retryAll_.onClick = [this] {
         std::vector<std::int64_t> ids;
         for (const auto& p : problems_)
@@ -95,7 +96,7 @@ juce::String ProblemsView::reasonText(const Problem& p)

 juce::String ProblemsView::nameText(int index) const { return utf8(baseName(problems_[static_cast<std::size_t>(index)].relPath)); }

-juce::String ProblemsView::folderText(int index) const { return utf8(folderOf(problems_[static_cast<std::size_t>(index)])); }
+juce::String ProblemsView::folderText(int index) const { return folderOf(problems_[static_cast<std::size_t>(index)]); }

 juce::String ProblemsView::shownReason(int index) const
 {
```

In `tests/ui/reference/README.md`, apply (`git apply` takes it as is):

````diff
diff --git a/tests/ui/reference/README.md b/tests/ui/reference/README.md
index 195272e..8b778c8 100644
--- a/tests/ui/reference/README.md
+++ b/tests/ui/reference/README.md
@@ -20,7 +20,15 @@ UI") rebuilt as `main.html`, with these changes and nothing else:
 artboard, rebuilt as `key-popover.html` at the size the app gives it (360×180),
 with C and Am picked.

-Both pictures are blurred before they are compared, so the test checks where
+`tags-popover.png` is the design's Tags popover from the canvas's organise
+artboard, rebuilt as `tags-popover.html` at the size the app gives it (360×216),
+"gr" typed. The field is drawn unfocused (the test cannot give it focus) and the
+hint is the app's one line.
+
+`problems.png` is the design's Problems panel from the same artboard, rebuilt as
+`problems.html` at 840×212 with three files, the second retrying.
+
+All the pictures are blurred before they are compared, so the test checks where
 text and shapes are rather than how each renderer draws their edges. Each area
 has its own limit, recorded in the test with the figures it measured. The test
 leaves out the waveform's own shape (the demo audio is not the design's) and the
@@ -38,6 +46,12 @@ Chrome at exactly 1280×800, scale 1:
 "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
   --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
   --window-size=360,180 --screenshot="$PWD/key-popover.png" "file://$PWD/key-popover.html"
+"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
+  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
+  --window-size=360,216 --screenshot="$PWD/tags-popover.png" "file://$PWD/tags-popover.html"
+"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
+  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
+  --window-size=840,212 --screenshot="$PWD/problems.png" "file://$PWD/problems.html"
 ```

 `main.html` loads the fonts from `plugin/fonts`, the same files the app embeds.
````

Create `tests/ui/reference/problems.html`:

<!-- prettier-ignore -->
````html
<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- The approved design's Problems panel (the canvas's organise artboard), at
     840x212 with three files, the second retrying. See README.md beside it. -->
<html lang="en">
<head>
<meta charset="utf-8">
<title>asma problems panel reference</title>
<style>
@font-face { font-family: "Inter"; font-weight: 400; src: url("../../../plugin/fonts/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; font-weight: 600; src: url("../../../plugin/fonts/Inter-SemiBold.ttf"); }
@font-face { font-family: "JetBrains Mono"; font-weight: 400; src: url("../../../plugin/fonts/JetBrainsMono-Regular.ttf"); }
@font-face { font-family: "Space Grotesk"; font-weight: 600; src: url("../../../plugin/fonts/SpaceGrotesk-SemiBold.ttf"); }
* { box-sizing: border-box; }
body { margin: 0; background: #15171a; }
.panel { width: 840px; height: 212px; background: #15171a; color: #e8e9eb; font-family: Inter; }
.header { height: 44px; padding: 0 14px; display: flex; align-items: center; gap: 10px; border-bottom: 1px solid #2a2d33; }
.header b { font-family: "Space Grotesk"; font-weight: 600; font-size: 14px; }
.header .n { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }
.header .all { margin-left: auto; width: 84px; height: 28px; border-radius: 4px; background: #e8a33d; color: #0b0c0e;
  font-weight: 600; font-size: 13px; display: flex; align-items: center; justify-content: center; }
.cols, .row { display: grid; grid-template-columns: 313px 409px; column-gap: 10px; padding: 0 14px; align-items: center; }
.cols { height: 30px; border-bottom: 1px solid #2a2d33; font-family: "Space Grotesk"; font-weight: 600; font-size: 11px;
  letter-spacing: 0.06em; color: #8a8f98; }
.row { position: relative; height: 46px; border-bottom: 1px solid #1c1f23; }
.file { display: flex; flex-direction: column; gap: 0; }
.file b { font-weight: 400; font-size: 13px; height: 18px; line-height: 18px; }
.file span { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; height: 14px; line-height: 14px; }
.why { font-size: 12px; }
.why.dim { color: #8a8f98; }
.retry { position: absolute; right: 14px; top: 10px; width: 70px; height: 26px; border: 1px solid #2a2d33; border-radius: 4px;
  background: #1c1f23; font-size: 12px; display: flex; align-items: center; justify-content: center; }
.retry.off { color: #3a3e45; }
</style>
</head>
<body>
<div class="panel">
  <div class="header"><b>Problems</b><span class="n">3 files</span><span class="all">Retry all</span></div>
  <div class="cols"><span>FILE</span><span>WHAT WENT WRONG</span></div>
  <div class="row"><div class="file"><b>kick_broken_header.wav</b><span>Drums / Kicks</span></div>
    <span class="why">Could not read the file: not a valid WAV header</span><span class="retry">Retry</span></div>
  <div class="row"><div class="file"><b>pad_long_take.flac</b><span>Pads</span></div>
    <span class="why dim">Retrying&#8230;</span><span class="retry off">Retry</span></div>
  <div class="row"><div class="file"><b>vox_chop_07.mp3</b><span>Vocals / Chops</span></div>
    <span class="why">Analysis failed: the file is silent</span><span class="retry">Retry</span></div>
</div>
</body>
</html>
````

Create `tests/ui/reference/tags-popover.html`:

<!-- prettier-ignore -->
````html
<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- The approved design's Tags popover (the canvas's organise artboard), at
     the size the app gives it, "gr" typed. See README.md beside it. -->
<html lang="en">
<head>
<meta charset="utf-8">
<title>asma tags popover reference</title>
<style>
@font-face { font-family: "Inter"; font-weight: 400; src: url("../../../plugin/fonts/Inter-Regular.ttf"); }
@font-face { font-family: "JetBrains Mono"; font-weight: 400; src: url("../../../plugin/fonts/JetBrainsMono-Regular.ttf"); }
@font-face { font-family: "Space Grotesk"; font-weight: 600; src: url("../../../plugin/fonts/SpaceGrotesk-SemiBold.ttf"); }
* { box-sizing: border-box; }
body { margin: 0; background: #111316; }
.pop { position: relative; width: 360px; height: 216px; background: #111316; color: #e8e9eb; font-family: Inter; }
.title { position: absolute; left: 14px; top: 14px; height: 22px; display: flex; align-items: center; gap: 6px; }
.title b { font-family: "Space Grotesk"; font-weight: 600; font-size: 13px; }
.title span { font-size: 11px; color: #8a8f98; }
.chips { position: absolute; left: 14px; top: 42px; display: flex; gap: 6px; }
.chip { height: 24px; padding: 0 9px; border: 1px solid #2a2d33; border-radius: 12px; color: #8a8f98; font-size: 11px;
  display: inline-flex; align-items: center; }
.chip.user { padding: 0 4px 0 9px; gap: 4px; border-color: #e8a33d; background: rgba(232,163,61,0.12); color: #f2bd6b; }
.chip .x { width: 16px; height: 16px; display: inline-flex; align-items: center; justify-content: center; color: #e8a33d; }
.label { position: absolute; left: 14px; top: 78px; height: 14px; font-size: 11px; color: #8a8f98; display: flex; align-items: flex-end; }
.field { position: absolute; left: 14px; top: 94px; width: 332px; height: 28px; padding: 0 8px; border: 1px solid #2a2d33;
  border-radius: 4px; background: #0b0c0e; font-size: 13px; display: flex; align-items: center; }
.list { position: absolute; left: 14px; top: 126px; width: 332px; padding: 3px; border: 1px solid #2a2d33; border-radius: 4px;
  background: #1c1f23; }
.opt { height: 24px; padding: 0 8px; display: flex; justify-content: space-between; align-items: center; font-size: 12px; }
.opt span { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }
.hint { position: absolute; left: 14px; top: 184px; height: 18px; font-size: 12px; color: #8a8f98; display: flex; align-items: center; }
</style>
</head>
<body>
<div class="pop">
  <div class="title"><b>Tags</b><span>Bass_Loop_Am_120.wav</span></div>
  <div class="chips">
    <span class="chip user">dark<span class="x">&#215;</span></span>
    <span class="chip user">live set<span class="x">&#215;</span></span>
    <span class="chip">bass</span>
    <span class="chip">synth</span>
  </div>
  <div class="label">Add a tag</div>
  <div class="field">gr</div>
  <div class="list">
    <div class="opt">gritty<span>31</span></div>
    <div class="opt">groove<span>9</span></div>
  </div>
  <div class="hint">Return adds the tag. Changes apply at once.</div>
</div>
</body>
</html>
````

Render the reference pictures from their pages:

```sh
cd tests/ui/reference
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --hide-scrollbars --force-device-scale-factor=1 --window-size=840,212 \
  --screenshot="$PWD/problems.png" "file://$PWD/problems.html"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --hide-scrollbars --force-device-scale-factor=1 --window-size=360,216 \
  --screenshot="$PWD/tags-popover.png" "file://$PWD/tags-popover.html"
cd ../../..
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fidelity]"`

Expected: tags popover 1.8%, problems panel 2.7%, the editor and the Key popover
as before, and `All tests passed (28 assertions in 4 test cases)`.

- [ ] **Step 5: Commit**

```sh
git add plugin/src/ui/AsmaLookAndFeel.cpp plugin/src/ui/NamePopover.cpp plugin/src/ui/ProblemsView.cpp tests/plugin/test_problems_view.cpp tests/plugin/test_ui_fidelity.cpp tests/ui/reference/README.md tests/ui/reference/problems.html tests/ui/reference/problems.png tests/ui/reference/tags-popover.html tests/ui/reference/tags-popover.png
git commit -m "app: hold the Tags popover and the Problems panel to the design"
```

---

### Task 10: Docs

The README gains organising and `asma retry`. The spec said the screenshot test
covers the row menu, which a native popup menu makes impossible, and did not
name `asma-cli`.

**Files:**

- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`

**Interfaces:**

- Consumes: tasks 1 to 9.
- Produces: nothing code depends on.

- [ ] **Step 1: Write the docs**

In `README.md`, apply (`git apply` takes it as is):

```diff
diff --git a/README.md b/README.md
index d89dab4..c966fcb 100644
--- a/README.md
+++ b/README.md
@@ -34,6 +34,7 @@ Ratings, favourites, tags, collections and saved searches:
     asma query --collection "Live set" --min-rating 4 --favourites
     asma search save "Fast loops" --type loop --bpm 140-180
     asma query --saved "Fast loops" --sort rating --desc
+    asma retry ~/Samples/Drums/broken.wav       # read a failed file again

 The library lives in the platform data directory (on macOS
 `~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
@@ -90,8 +91,17 @@ collection, or a saved search, which brings its whole search with it. The chips
 over the table narrow it further by type, BPM, key, instrument, length and
 rating; each opens a small panel, and its x clears it. The table holds every
 match, however many, sorts by clicking a column, and the Similar list beside the
-preview offers the samples that sound most like the selection. Rating, tagging
-and collecting come in a later release.
+preview offers the samples that sound most like the selection.
+
+Organising happens in place. Click a row's star to favourite it, or its rating
+stars to rate it (the rating it has clears it); with the table focused, F and 0
+to 5 do the same to the selection. Right-click a row to add it to a collection,
+change its tags or show it in Finder. The "+" beside COLLECTIONS makes one, a
+right-click renames or deletes a collection or a saved search, and "Save search"
+keeps the search in force. When files cannot be read or analysed, Problems
+appears in the sidebar: its panel lists each with the reason, and Retry reads it
+again. Changes show at once; in a plugin, which never writes the library inside
+the host, the `asma-cli` helper shipped beside it makes them.

 The look is checked against the approved design by `[fidelity]` in
 `asma_plugin_tests`, on macOS only; `tests/ui/reference/README.md` says how the
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, apply (`git apply` takes
it as is):

```diff
diff --git a/docs/superpowers/specs/2026-09-25-asma-design.md b/docs/superpowers/specs/2026-09-25-asma-design.md
index 045777e..16952fb 100644
--- a/docs/superpowers/specs/2026-09-25-asma-design.md
+++ b/docs/superpowers/specs/2026-09-25-asma-design.md
@@ -198,8 +198,10 @@ waits for a scan to end; SQLite's busy timeout covers the moment a scan batch is
 committing. The plugin never writes inside the host: it runs the `asma` CLI
 (`asma rate`, `asma fav`, `asma tag`, `asma collection`, `asma search`,
 `asma retry`) as a short-lived helper process, and opens the library read-only
-itself. `asma retry` is the one of these that takes the writer lock: it re-reads
-and re-analyses the files it is given, as a scan would.
+itself. Every format ships the CLI beside its own binary as `asma-cli` (the
+app's and the bundles' binaries are themselves called `asma`). `asma retry` is
+the one of these that takes the writer lock: it re-reads and re-analyses the
+files it is given, as a scan would.

 ### File operations

@@ -560,9 +562,10 @@ core, the audio engine, the CLI and the scanner build without JUCE.
   the direct one.
 - **UI fidelity:** a headless test renders the editor at 1280×800 with fixed
   demo data and compares it with `tests/ui/reference/main.png` (the approved
-  design), failing above a set mismatch; the same test covers the filter
-  popover, the row menu, the Tags popover and the Problems panel against their
-  own reference pictures. It runs on macOS only, since font rendering differs
+  design), failing above a set mismatch; the same test covers the Key popover,
+  the Tags popover and the Problems panel against their own reference pictures.
+  The row menu is a native popup menu, which cannot be drawn headless: the
+  behaviour tests cover it. It runs on macOS only, since font rendering differs
   between systems; the behaviour tests run everywhere.
 - **Plugin validation:** pluginval at strictness 10 (VST3 everywhere, AU on
   macOS) and clap-validator (CLAP) in CI on all three platforms.
```

- [ ] **Step 2: Check them**

Run:
`prettier --check README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: both files pass; no em-dashes.

- [ ] **Step 3: Commit**

```sh
git add README.md docs/superpowers/specs/2026-09-25-asma-design.md
git commit -m "docs: organising in the README; the spec on the row menu and asma-cli"
```

---

### Task 11: Verify and merge

- [ ] **Step 1: Everything, locally**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests
TOOLS=build/validators ci/validate-plugins.sh build
file build/plugin/asma_plugin_artefacts/Release/VST3/asma.vst3/Contents/MacOS/asma
```

Expected: no warnings from asma's code; `100% tests passed out of 513`; the
plugin tests also pass in one process (`All tests passed`, 178 test cases);
pluginval `SUCCESS` and clap-validator `0 failed`; `file` says
`Mach-O 64-bit bundle` (the plugin, not the helper copied over it), and
`asma-cli` sits beside it.

- [ ] **Step 2: Try the app by hand (macOS)**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
build/plugin/asma_plugin_artefacts/Release/Standalone/asma.app/Contents/MacOS/asma
```

Add a folder holding a file that is not audio (`echo junk > broken.wav`).
Expected: clicking a row's star and its rating stars changes them at once and
they stay; F and 0 to 5 do the same to the selection; right-click a row: "Add to
collection" ticks what it is in, "New collection…" opens the sidebar's name
field and the new collection holds the sample, "Tags…" adds and removes your
tags (the scanner's greyed), "Show in Finder" shows it; "+" beside COLLECTIONS,
right-click Rename… and Delete work on collections and saved searches (a taken
name turns the field red; deleting a collection with samples asks first); "Save
search" saves and lights the search; Problems lists `broken.wav`, and after
replacing it with a real WAV, Retry clears it and Problems leaves the sidebar.

- [ ] **Step 3: Try a plugin by hand**

Load the VST3 or AU in a DAW (or `pluginval --validate` it with a window) with
the same `ASMA_DATA_DIR`. Expected: rating, favourites and collections work as
in the app; the footer names no error; `asma-cli` (not the plugin) is the
process that writes, so the host never holds the library open for writing.

- [ ] **Step 4: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 5: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-3c2b2
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green, including "Validate plugins". Watch
the writer and Problems tests on Windows (the helper is `asma-cli.exe`; paths go
to it as UTF-8 arguments) and the `[fidelity]` tests on the macOS runner.

- [ ] **Step 6: Merge**

```sh
git -C product/asma merge --ff-only plan-3c2b2
git -C product/asma worktree remove .worktrees/plan-3c2b2
git -C product/asma branch -d plan-3c2b2
```

Push `main` once the user agrees, and delete the remote branch.
