# asma Plan 3c2b1: Browse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** asma browses its whole library: a sidebar that picks where to look
(all samples, favourites, a folder, a collection, a saved search) with counts,
filter chips with a popover each, a table that holds every match and sorts by
its headers with the favourite, rating and tags columns, and a Similar list
beside the preview; plus two failure fixes (a stream that throws, a project
whose sample has gone). It only reads the library; writing it is plan 3c2b2.

**Architecture:** The core gains tags per row, tag counts and a file's position
in a search (one window-function query over the search's own filter and order).
`Browser` pages: a count and 500-row pages fetched as rows are asked for.
JUCE-free pieces carry the logic (`Sidebar`: entries, picking, which entry is
lit; `Filters`: chip words and clearing); `plugin/src/ui/` gains `SidebarView`,
`ChipRow`, the filter popovers and `SimilarView`. Every change of search goes
through one `AsmaEditor::applySearch`, and the editor holds the selection as a
row so a Similar pick outside the search can be selected.

**Tech Stack:** C++20, JUCE 9.0.3, SQLite window functions, Catch2; headless
Chrome for the reference pictures.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (section 9
"Browsing (3c2b1)", sections 10 and 12, as amended in `64bc823`). Task 16
records that Problems shows while something has failed.

**How this plan was checked:** every task was built on a branch from `main`
(`64bc823`), then replayed commit by commit on a macOS Release build: with only
the task's test changes applied, each red step failed as stated (for task 4, a
test fix, the red is the sabotage step it gives); with the whole task, each
green step built without warnings and passed. The `Expected:` lines are the
recorded outputs. The code below is taken from those commits: new files in full,
changed files as diffs against the task before (or in full where most of the
file changed), so applying the tasks in order to `main` gives the prototype file
for file. The reference pictures come from the pages the plan gives, rendered by
Chrome as stated. The prototype did not run on CI: Windows and Linux are first
built in Task 17.

## Where this sits

Plan 3c2b was split in two (the user's call, 2026-10-02): **3c2b1 (this plan)**
browses and only reads the library; **3c2b2** adds what writes it (rating,
favourites, tags, collections and saved searches, through the `asma` CLI in a
plugin and directly in the standalone) and the Problems panel with retry. Plan 4
(file manager) and plan 5 (packaging) follow.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (HTML: `<!-- SPDX-... -->`).
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11), no
  floating-point `std::to_chars`/`std::from_chars` (macOS 12). The plugin build
  uses JUCE's warning set, which includes `-Wfloat-equal`: compare floats with
  `<`/`>`, never `==`/`!=`.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE.
- The plugin never writes the library inside the host; nothing in this plan
  writes it at all.
- Nothing on the audio thread allocates, locks or blocks.
- Numbers the UI shows are formatted with integer arithmetic, never through the
  locale; typed numbers are read the same way (a field holding anything but
  digits and one point is no limit, never 0).
- UI text in the source is UTF-8; `asma_ui` compiles with `/utf-8` on MSVC.
- Colours come from `theme`, never as hex in a component.
- No em-dashes in code, comments, docs or commit messages; no attribution
  trailers; no mention of the tools used to write the code.

## Decisions made while prototyping

- **Sidebar counts are of the whole library**, one `countSearch` per entry with
  only its scope set, so they agree with what the table then shows.
- **A saved search is lit only while the search equals it**, compared through
  `searchModelToJson`, the form it is stored in (paging is not part of it).
- **The selection is a row, not a table index**, because a Similar pick the
  search does not show must play, fill the preview and footer, and survive a
  library check.
- **Pages are 500 rows and eight are kept**; `rowOf` asks the library for the
  position (SQLite `ROW_NUMBER()`), so a restored selection costs one query
  however deep it sits.
- **"The library is empty" means the library**, not an empty search box: a saved
  search or a chip can match nothing in a full library, which now says "No
  samples match.", and "Add folder…" shows only when there is nothing.
- **Popover titles carry a hint** as the design does ("any of", "all of", "at
  least"); key buttons use the mono face and read plainly when unpicked.
- **A range typed backwards means the same range** (130 to 120 is 120 to 130).
- **The screenshot test compares blurred pictures.** Sharp, Chrome's and JUCE's
  glyphs disagree on almost every pixel, so missing text scored like present
  text (2.6% for a blank sidebar). Blurred by 3 px, right and missing separate:
  sidebar 2.8% against 7.6%, chip row 2.4% against 15.0%, Similar 9.5% against
  22.3%. Each area has its own limit between the two.
- **Similar stays on the UI thread**: 25 ms over 50,000 analysed files on the
  prototype, under the spec's 50 ms.
- **3c2a's font test was flaky** in a one-process run (pointer reuse); it now
  holds the first face.

## Review Focus

The five inputs most likely to bite a person that the tasks' main tests do not
reach; each has its own test, in the task named:

- A BPM or length range typed backwards: it means the same range, not one that
  matches nothing (Task 11).
- Re-sorting the table: the selection stays on its sample, on its new row (Task
  8).
- A saved search naming a collection since deleted: the table says nothing
  matches and the sidebar lights nothing, without an error (Task 9).
- A library with many tags: the Instrument popover scrolls instead of growing
  past the window (Task 11).
- A sidebar with many folders: it scrolls, and Problems stays in view (Task 9).

## File Structure

```
core/include/asma/core/Query.h, core/src/Query.cpp      tags per row, tagCounts, searchPosition
audio/src/StreamSource.cpp (+ .h comment)               fill() catches everything
plugin/src/AsmaProcessor.cpp                            a gone sample drops the selection
plugin/src/Browser.h, .cpp                              paging
plugin/src/LibraryView.h, .cpp                          position, fileId, roots, collections,
                                                        saved searches, problemCount, tagCounts, similar
plugin/src/Sidebar.h, .cpp                              sidebar entries and picking (JUCE-free)
plugin/src/Filters.h, .cpp                              chip words and clearing (JUCE-free)
plugin/src/AsmaEditor.h, .cpp                           columns, sorting, applySearch, wiring
plugin/src/ui/SidebarView.h, .cpp                       the sidebar
plugin/src/ui/ChipRow.h, .cpp                           the chip row
plugin/src/ui/FilterPopovers.h, .cpp                    the six popovers
plugin/src/ui/SimilarView.h, .cpp                       the Similar list
plugin/src/ui/AsmaLookAndFeel.h, .cpp                   sort arrow, callout boxes, asma.mono, asma.quiet
tests/test_query.cpp, tests/test_stream_source.cpp, tests/FakeReader.h
tests/plugin/test_{browser,sidebar,sidebar_view,filters,chip_row,filter_popovers,
  similar,editor,plugin_state,theme,ui_fidelity}.cpp
tests/ui/reference/{main,key-popover}.{html,png}, README.md
```

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-3c2b1-app -b plan-3c2b1-app
```

All paths below are relative to `product/asma/.worktrees/plan-3c2b1-app`.
Configure once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
`cmake --build build`.

---

### Task 1: End streaming quietly whatever reading ahead throws

Spec section 10 (Streaming failure). `StreamSource::fill` runs on the loader
thread and caught only `ProbeError`: an allocation failure while reading ahead
would end the thread and the host. It now ends streaming for that file only. The
test reader gains a switch that makes reads throw.

**Files:**

- Modify: `audio/include/asma/audio/StreamSource.h`
- Modify: `audio/src/StreamSource.cpp`
- Modify: `tests/FakeReader.h` (test)
- Modify: `tests/test_stream_source.cpp` (test)

**Interfaces:**

- Consumes: plan 3b's `StreamSource`, the tests' `FakeReader`.
- Produces: `FakeReader::throwing` (`std::shared_ptr<std::atomic<bool>>`, reads
  throw `std::bad_alloc` once set).

- [ ] **Step 1: Write the failing test**

In `tests/FakeReader.h`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/FakeReader.h b/tests/FakeReader.h
index 27780ee..23f19d6 100644
--- a/tests/FakeReader.h
+++ b/tests/FakeReader.h
@@ -8,6 +8,7 @@
 #include <atomic>
 #include <cstdint>
 #include <memory>
+#include <new>

 namespace asma::test {

@@ -29,10 +30,13 @@ public:

     // Seeks from now on fail, as when a drive goes away mid-stream.
     std::shared_ptr<std::atomic<bool>> broken = std::make_shared<std::atomic<bool>>(false);
+    // Reads from now on throw, as an allocation failure would.
+    std::shared_ptr<std::atomic<bool>> throwing = std::make_shared<std::atomic<bool>>(false);

 protected:
     std::uint64_t readInterleaved(float* out, std::uint64_t n) override
     {
+        if (throwing->load()) throw std::bad_alloc();
         std::uint64_t i = 0;
         for (; i < n && pos_ < frames_; ++i, ++pos_)
             for (int c = 0; c < sourceChannels_; ++c)
```

In `tests/test_stream_source.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_stream_source.cpp b/tests/test_stream_source.cpp
index 8d03804..d0e3597 100644
--- a/tests/test_stream_source.cpp
+++ b/tests/test_stream_source.cpp
@@ -101,6 +101,19 @@ TEST_CASE("StreamSource stops streaming when the file goes away", "[stream]")
     CHECK(readAndCheck(s, 0, 64)); // the head is still there
 }

+TEST_CASE("StreamSource stops streaming whatever reading throws", "[stream]")
+{
+    auto reader = std::make_unique<FakeReader>(static_cast<std::uint64_t>(20 * B));
+    const auto throwing = reader->throwing;
+    StreamSource s(std::move(reader));
+    throwing->store(true);
+    s.hint(5 * B, 1);
+    CHECK_NOTHROW(s.fill()); // on the loader thread, a throw would take the host down
+    CHECK(s.failed());
+    CHECK_FALSE(readAndCheck(s, 5 * B, 64)); // silence where it could not read
+    CHECK(readAndCheck(s, 0, 64));
+}
+
 TEST_CASE("StreamSource never hands out a block while fill overwrites it", "[stream]")
 {
     auto s = stream(200);
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[stream]"`

Expected: it fails:

```
tests/test_stream_source.cpp:111: FAILED:
  CHECK_NOTHROW( s.fill() )
due to unexpected exception with message:
  std::bad_alloc
```

- [ ] **Step 3: Implement**

In `audio/include/asma/audio/StreamSource.h`, apply (`git apply` takes it as
is):

```diff
diff --git a/audio/include/asma/audio/StreamSource.h b/audio/include/asma/audio/StreamSource.h
index e319491..b927fae 100644
--- a/audio/include/asma/audio/StreamSource.h
+++ b/audio/include/asma/audio/StreamSource.h
@@ -43,8 +43,8 @@ public:
     bool ready(std::int64_t frame) const override;

     // Loader thread: loads up to maxBlocks missing blocks around the playhead,
-    // nearest first. Returns how many it loaded. A read error ends streaming:
-    // failed() turns true and the missing blocks stay silent.
+    // nearest first. Returns how many it loaded. Anything reading throws ends
+    // streaming: failed() turns true and the missing blocks stay silent.
     int fill(int maxBlocks = 4);
     bool failed() const { return failed_.load(); }
     // Blocks read() found missing, for tests and diagnostics.
```

In `audio/src/StreamSource.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/src/StreamSource.cpp b/audio/src/StreamSource.cpp
index 9d2ed27..e8917b9 100644
--- a/audio/src/StreamSource.cpp
+++ b/audio/src/StreamSource.cpp
@@ -162,9 +162,11 @@ int StreamSource::fill(int maxBlocks)
         if (slot == slots_.end()) break;
         slot->block.store(-1);
         while (slot->readers.load() != 0) std::this_thread::yield();
+        // Whatever reading throws ends streaming for this file only: fill()
+        // runs on the loader thread, where an escape would end the host.
         try {
             loadBlock(b, slot->data.data(), kBlockFrames);
-        } catch (const ProbeError&) {
+        } catch (...) {
             failed_.store(true);
             return loaded;
         }
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[stream]"`

Expected: `All tests passed (246823 assertions in 8 test cases)`

- [ ] **Step 5: Commit**

```sh
git add audio/include/asma/audio/StreamSource.h audio/src/StreamSource.cpp tests/FakeReader.h tests/test_stream_source.cpp
git commit -m "audio: end streaming quietly whatever reading ahead throws"
```

---

### Task 2: A project whose sample has gone drops the old selection

Spec section 10 (A project's sample has gone). Restoring such a project kept the
previous sample loaded, so Space and MIDI played a sample the window did not
show. Now any project that names no playable file (gone, empty, unreadable)
selects nothing playable; the saved path stays.

**Files:**

- Modify: `plugin/src/AsmaProcessor.cpp`
- Modify: `tests/plugin/test_plugin_state.cpp` (test)

**Interfaces:**

- Consumes: `AsmaProcessor::setPluginState`, `AuditionEngine::select`.
- Produces: nothing new; `EngineStatus::failed` is true after such a restore.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_plugin_state.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_plugin_state.cpp b/tests/plugin/test_plugin_state.cpp
index b07d53f..9c7a5c3 100644
--- a/tests/plugin/test_plugin_state.cpp
+++ b/tests/plugin/test_plugin_state.cpp
@@ -136,3 +136,34 @@ TEST_CASE("restoring a project over a broken library does not take the host down
     CHECK_NOTHROW(p.setStateInformation(json.data(), static_cast<int>(json.size())));
     CHECK(p.pluginState().selected == s.selected);
 }
+
+TEST_CASE("a project whose sample has gone leaves nothing to play", "[state]")
+{
+    TempDir dir;
+    const auto file = dir.path() / "a.wav";
+    test::writeWavFloat(file, 48000, {std::vector<float>(48000, 0.25f)});
+    app::AsmaProcessor p;
+    p.prepareToPlay(48000.0, 512);
+    PluginState s;
+    s.selected = toUtf8(file);
+    p.setPluginState(s);
+    REQUIRE(asma::test::waitForPreview(p, p.engine().selected()));
+
+    PluginState gone;
+    gone.selected = toUtf8(dir.path() / "gone.wav");
+    p.setPluginState(gone);
+    REQUIRE(asma::test::waitForPreview(p, p.engine().selected()));
+    CHECK(p.engine().status().failed);
+    CHECK(p.pluginState().selected == gone.selected); // kept: the drive may come back
+    p.engine().play();
+    juce::AudioBuffer<float> buffer(2, 512);
+    juce::MidiBuffer midi;
+    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
+    float loudest = 0.0f;
+    for (int i = 0; i < 20; ++i) {
+        p.processBlock(buffer, midi);
+        midi.clear();
+        loudest = std::max(loudest, buffer.getMagnitude(0, 512));
+    }
+    CHECK(loudest == 0.0f); // neither play nor a MIDI note sounds the old sample
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[state]"`

Expected: it fails:

```
tests/plugin/test_plugin_state.cpp:156: FAILED:
  CHECK( p.engine().status().failed )
tests/plugin/test_plugin_state.cpp:168: FAILED:
  CHECK( loudest == 0.0f )
with expansion:
  1.781999946f == 0.0f
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaProcessor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.cpp b/plugin/src/AsmaProcessor.cpp
index 3163b59..bf76da4 100644
--- a/plugin/src/AsmaProcessor.cpp
+++ b/plugin/src/AsmaProcessor.cpp
@@ -110,20 +110,22 @@ void AsmaProcessor::setPluginState(const PluginState& given)
     engine_.setGainMatch(state.gainMatch);
     engine_.setQuantise(state.quantise);
     engine_.setEdits(state.edits);
-    if (!state.selected.empty()) {
-        std::filesystem::path path;
-        try {
-            path = fromUtf8(state.selected); // throws on Windows for bytes that are not UTF-8
-        } catch (const std::exception&) {
-            return;
-        }
-        std::error_code ec;
+    std::filesystem::path path;
+    try {
+        path = fromUtf8(state.selected); // throws on Windows for bytes that are not UTF-8
+    } catch (const std::exception&) {
+        path.clear();
+    }
+    std::error_code ec;
+    if (!path.empty() && std::filesystem::exists(path, ec)) {
         // Its tempo and key come from the library, so a restored loop syncs.
-        if (std::filesystem::exists(path, ec)) {
-            LibraryView library(libraryPath_);
-            library.refresh();
-            engine_.select(path, library.infoFor(path), false);
-        }
+        LibraryView library(libraryPath_);
+        library.refresh();
+        engine_.select(path, library.infoFor(path), false);
+    } else {
+        // Nothing to play: drop the old selection rather than keep sounding
+        // a sample the project does not name. The saved path stays.
+        engine_.select(path, {}, false);
     }
 }

```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[state]"`

Expected: `All tests passed (45 assertions in 5 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaProcessor.cpp tests/plugin/test_plugin_state.cpp
git commit -m "app: a project whose sample has gone drops the old selection"
```

---

### Task 3: Rows carry their tags; tag counts; a file's place in a search

The table's tags column, the Instrument popover's list and paging all need the
core. The search SQL is split into its filter and its order, so the count and
the position use exactly the conditions and order the rows do. The position is
one window-function query, so a restored selection finds its row without walking
pages.

**Files:**

- Modify: `core/include/asma/core/Query.h`
- Modify: `core/src/Query.cpp`
- Modify: `tests/test_query.cpp` (test)

**Interfaces:**

- Consumes: `buildSearchSql`, `countSearch`.
- Produces: `std::vector<std::string> SearchRow::tags` (sorted);
  `struct TagCount { std::string name; std::int64_t count; }`;
  `std::vector<TagCount> tagCounts(Db&)` (searchable files only, most used
  first, then by name);
  `std::optional<std::int64_t> searchPosition(Db&, const SearchModel&, std::int64_t fileId)`
  (from 0, ignoring limit and offset).

- [ ] **Step 1: Write the failing test**

In `tests/test_query.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_query.cpp b/tests/test_query.cpp
index 29ca764..53e46eb 100644
--- a/tests/test_query.cpp
+++ b/tests/test_query.cpp
@@ -91,6 +91,43 @@ TEST_CASE("countSearch counts every match, past the page the search returns", "[
     CHECK(countSearch(s.db, page) == 2);
 }

+TEST_CASE("each row carries its tags, sorted", "[query]")
+{
+    Seeded s;
+    for (const auto& r : search(s.db, {}))
+        if (r.id == s.padLoop) CHECK(r.tags == std::vector<std::string>{"pad", "synth"});
+        else if (r.id == s.kick) CHECK(r.tags == std::vector<std::string>{"kick"});
+}
+
+TEST_CASE("tagCounts lists the tags searches can find, most used first", "[query]")
+{
+    Seeded s;
+    const auto tags = tagCounts(s.db);
+    REQUIRE(tags.size() == 5);
+    CHECK(tags[0].name == "kick"); // two files
+    CHECK(tags[0].count == 2);
+    CHECK(tags[1].name == "bass"); // then by name
+    CHECK(tags[1].count == 1);
+    s.lib.setStatus(s.kick, FileStatus::Missing);
+    CHECK(tagCounts(s.db)[0].count == 1); // only files a search shows
+}
+
+TEST_CASE("searchPosition finds a file's row in the sorted results", "[query]")
+{
+    Seeded s;
+    SearchModel m; // by name: Bass_Loop, Kick_01, Kick_Deep, Pad_Loop, snare
+    CHECK(searchPosition(s.db, m, s.bassLoop) == 0);
+    CHECK(searchPosition(s.db, m, s.padLoop) == 3);
+    m.sort = SortField::Duration;
+    m.descending = true; // Pad 10.6, Bass 7.5, Kick_Deep 0.8, Kick 0.4, snare 0.3
+    CHECK(searchPosition(s.db, m, s.kick) == 3);
+    m.text = "kick";
+    CHECK(searchPosition(s.db, m, s.padLoop) == std::nullopt); // not a match
+    m.limit = 1;
+    m.offset = 1;
+    CHECK(searchPosition(s.db, m, s.flacHit) == 0); // the page does not matter
+}
+
 TEST_CASE("text search prefix-matches names, folders and tags", "[query]")
 {
     Seeded s;
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[query]"`

Expected: the build stops:

```
tests/test_query.cpp:105:23: error: use of undeclared identifier 'tagCounts'
tests/test_query.cpp:119:11: error: use of undeclared identifier 'searchPosition'
```

- [ ] **Step 3: Implement**

In `core/include/asma/core/Query.h`, apply (`git apply` takes it as is):

```diff
diff --git a/core/include/asma/core/Query.h b/core/include/asma/core/Query.h
index f3f0608..15be4ce 100644
--- a/core/include/asma/core/Query.h
+++ b/core/include/asma/core/Query.h
@@ -47,6 +47,12 @@ struct SearchRow {
     std::optional<bool> isLoop;
     std::optional<int> rating;
     bool favourite = false;
+    std::vector<std::string> tags; // sorted
+};
+
+struct TagCount {
+    std::string name;
+    std::int64_t count = 0;
 };

 using SqlParam = std::variant<std::int64_t, double, std::string>;
@@ -64,6 +70,11 @@ SqlQuery buildSearchSql(const SearchModel& model);
 std::vector<SearchRow> search(Db& db, const SearchModel& model);
 // Every file the model matches, whatever its limit and offset.
 std::int64_t countSearch(Db& db, const SearchModel& model);
+// Where the file falls in the model's order, from 0, whatever its limit and
+// offset; nothing when the model does not match it.
+std::optional<std::int64_t> searchPosition(Db& db, const SearchModel& model, std::int64_t fileId);
+// The tags of files a search can show, most used first, then by name.
+std::vector<TagCount> tagCounts(Db& db);

 // The model as one JSON object, for saved searches and plugin state. Paging
 // (limit, offset) is view state and is left out; so are default values.
```

In `core/src/Query.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/core/src/Query.cpp b/core/src/Query.cpp
index eb93462..c912834 100644
--- a/core/src/Query.cpp
+++ b/core/src/Query.cpp
@@ -27,14 +27,23 @@ std::string lower(std::string_view s)
     return out;
 }

-constexpr std::string_view kRowSelect =
-    "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop, rt.rating, "
-    "EXISTS (SELECT 1 FROM favourites fv WHERE fv.file_id = f.id) "
-    "FROM files f JOIN roots r ON r.id = f.root_id "
+// The files a search can show, with what its filters and orders use.
+constexpr std::string_view kRowFrom =
+    " FROM files f JOIN roots r ON r.id = f.root_id "
     "LEFT JOIN features ft ON ft.file_id = f.id "
     "LEFT JOIN ratings rt ON rt.file_id = f.id "
     "WHERE f.status = 'ok' AND r.enabled = 1";

+// A row's columns, as readRow reads them. Tags come as one string, sorted and
+// joined with the unit separator, which no tag contains.
+constexpr std::string_view kRowColumns =
+    "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop, rt.rating, "
+    "EXISTS (SELECT 1 FROM favourites fv WHERE fv.file_id = f.id), "
+    "(SELECT group_concat(name, char(31)) FROM (SELECT t.name AS name FROM file_tags x JOIN tags t ON t.id = x.tag_id "
+    "WHERE x.file_id = f.id ORDER BY t.name))";
+
+std::string rowSelect() { return std::string(kRowColumns) + std::string(kRowFrom); }
+
 SearchRow readRow(const Statement& s)
 {
     SearchRow r;
@@ -49,6 +58,13 @@ SearchRow readRow(const Statement& s)
     if (!s.isNull(8)) r.isLoop = s.getInt(8) != 0;
     if (!s.isNull(9)) r.rating = static_cast<int>(s.getInt(9));
     r.favourite = s.getInt(10) != 0;
+    if (!s.isNull(11)) {
+        const std::string joined = s.getText(11);
+        std::size_t start = 0;
+        for (std::size_t sep; (sep = joined.find('\x1f', start)) != std::string::npos; start = sep + 1)
+            r.tags.push_back(joined.substr(start, sep - start));
+        r.tags.push_back(joined.substr(start));
+    }
     return r;
 }

@@ -79,10 +95,12 @@ std::string ftsMatchExpression(std::string_view text)
     return out;
 }

-SqlQuery buildSearchSql(const SearchModel& m)
+namespace {
+
+// The search's conditions, appended to kRowFrom's WHERE.
+SqlQuery buildSearchFilter(const SearchModel& m)
 {
     SqlQuery q;
-    q.sql = std::string(kRowSelect);

     if (const std::string match = ftsMatchExpression(m.text); !match.empty()) {
         q.sql += " AND f.id IN (SELECT rowid FROM fts_files WHERE fts_files MATCH ?)";
@@ -131,7 +149,11 @@ SqlQuery buildSearchSql(const SearchModel& m)
         q.sql += " AND f.id IN (SELECT file_id FROM collection_items WHERE collection_id = ?)";
         q.params.emplace_back(*m.collectionId);
     }
+    return q;
+}

+std::string searchOrder(const SearchModel& m)
+{
     const std::string direction = m.descending ? " DESC" : " ASC";
     std::string order;
     switch (m.sort) {
@@ -141,7 +163,15 @@ SqlQuery buildSearchSql(const SearchModel& m)
     case SortField::Key: order = "ft.key IS NULL, ft.key" + direction; break;
     case SortField::Rating: order = "rt.rating IS NULL, rt.rating" + direction; break;
     }
-    q.sql += " ORDER BY " + order + ", f.id LIMIT ? OFFSET ?";
+    return order + ", f.id";
+}
+
+} // namespace
+
+SqlQuery buildSearchSql(const SearchModel& m)
+{
+    SqlQuery q = buildSearchFilter(m);
+    q.sql = rowSelect() + q.sql + " ORDER BY " + searchOrder(m) + " LIMIT ? OFFSET ?";
     q.params.emplace_back(static_cast<std::int64_t>(m.limit));
     q.params.emplace_back(static_cast<std::int64_t>(m.offset));
     return q;
@@ -178,7 +208,7 @@ std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids)
 {
     std::vector<SearchRow> rows;
     if (ids.empty()) return rows;
-    Statement s = db.prepare(std::string(kRowSelect) + " AND f.id IN (" + placeholders(ids.size()) + ")");
+    Statement s = db.prepare(rowSelect() + " AND f.id IN (" + placeholders(ids.size()) + ")");
     for (std::size_t i = 0; i < ids.size(); ++i) s.bind(static_cast<int>(i) + 1, ids[i]);
     std::vector<SearchRow> found;
     while (s.step()) found.push_back(readRow(s));
@@ -282,14 +312,31 @@ std::optional<SearchModel> searchModelFromJson(std::string_view json)

 std::int64_t countSearch(Db& db, const SearchModel& model)
 {
-    // The search's own filter, without its order and page: buildSearchSql
-    // ends with " ORDER BY ... LIMIT ? OFFSET ?" and those two parameters.
-    SqlQuery q = buildSearchSql(model);
-    q.sql.erase(q.sql.rfind(" ORDER BY "));
-    q.params.resize(q.params.size() - 2);
-    Statement s = db.prepare("SELECT COUNT(*) FROM (" + q.sql + ")");
-    bindAll(s, q.params);
+    const SqlQuery f = buildSearchFilter(model);
+    Statement s = db.prepare("SELECT COUNT(*)" + std::string(kRowFrom) + f.sql);
+    bindAll(s, f.params);
     return s.step() ? s.getInt(0) : 0;
 }

+std::optional<std::int64_t> searchPosition(Db& db, const SearchModel& model, std::int64_t fileId)
+{
+    const SqlQuery f = buildSearchFilter(model);
+    Statement s = db.prepare("SELECT pos FROM (SELECT f.id AS id, ROW_NUMBER() OVER (ORDER BY " + searchOrder(model)
+                             + ") - 1 AS pos" + std::string(kRowFrom) + f.sql + ") WHERE id = ?");
+    bindAll(s, f.params);
+    s.bind(static_cast<int>(f.params.size()) + 1, fileId);
+    if (!s.step()) return std::nullopt;
+    return s.getInt(0);
+}
+
+std::vector<TagCount> tagCounts(Db& db)
+{
+    Statement s = db.prepare("SELECT t.name, COUNT(*) FROM file_tags x JOIN tags t ON t.id = x.tag_id "
+                             "JOIN files f ON f.id = x.file_id JOIN roots r ON r.id = f.root_id "
+                             "WHERE f.status = 'ok' AND r.enabled = 1 GROUP BY t.id ORDER BY COUNT(*) DESC, t.name");
+    std::vector<TagCount> out;
+    while (s.step()) out.push_back({s.getText(0), s.getInt(1)});
+    return out;
+}
+
 } // namespace asma
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[query]"`

Expected: `All tests passed (68 assertions in 14 test cases)`

- [ ] **Step 5: Commit**

```sh
git add core/include/asma/core/Query.h core/src/Query.cpp tests/test_query.cpp
git commit -m "core: rows carry their tags; count a search's tags; find a file's place in a search"
```

---

### Task 4: Hold the first font while checking JUCE makes a new one

3c2a's "fonts go when JUCE shuts down" test compared raw pointers across two
JUCE start-ups. In one process the allocator may hand the new face the old
address, so it failed now and then when the whole suite ran in one process
(ctest runs each case alone, which hid it). Holding the first face keeps its
address taken.

**Files:**

- Modify: `tests/plugin/test_theme.cpp` (test)

**Interfaces:**

- Consumes: `theme::typeface`.
- Produces: nothing.

- [ ] **Step 1: Fix the test**

In `tests/plugin/test_theme.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_theme.cpp b/tests/plugin/test_theme.cpp
index 38a0d51..6c48923 100644
--- a/tests/plugin/test_theme.cpp
+++ b/tests/plugin/test_theme.cpp
@@ -53,12 +53,12 @@ TEST_CASE("the fonts go when JUCE shuts down, not after it", "[theme]")
 {
     // A plugin's statics outlive JUCE in the host; fonts held past shutdown
     // crash some hosts as they unload the plugin.
-    const juce::Typeface* first = nullptr;
+    juce::Typeface::Ptr first; // held, so a new face cannot reuse its address
     {
         const juce::ScopedJuceInitialiser_GUI gui;
-        first = theme::typeface(theme::Face::Text).get();
+        first = theme::typeface(theme::Face::Text);
     }
     const juce::ScopedJuceInitialiser_GUI gui;
-    CHECK(theme::typeface(theme::Face::Text).get() != first); // made afresh
+    CHECK(theme::typeface(theme::Face::Text) != first); // made afresh
     CHECK(theme::typeface(theme::Face::Text)->getName() == "Inter");
 }
```

- [ ] **Step 2: See it still catches fonts that outlive JUCE**

In `plugin/src/ui/Theme.cpp`, change `typeface` for a moment to keep a static
copy:

```cpp
juce::Typeface::Ptr typeface(Face face) { static const auto kept = Faces::getInstance()->faces; return kept[static_cast<std::size_t>(face)]; }
```

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[theme]"`

Expected: `tests/plugin/test_theme.cpp:62: FAILED:`. Then put `typeface` back as
it was:

```cpp
juce::Typeface::Ptr typeface(Face face) { return Faces::getInstance()->faces[static_cast<std::size_t>(face)]; }
```

- [ ] **Step 3: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[theme]"`

Expected: `All tests passed (12 assertions in 4 test cases)`, every time, also
when the whole suite runs in one process
(`./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests`).

- [ ] **Step 4: Commit**

```sh
git add tests/plugin/test_theme.cpp
git commit -m "test: hold the first font while checking JUCE makes a new one"
```

---

### Task 5: The table holds every match, fetched a page at a time

Spec section 9 (Browsing: the table). `Browser` gives the count of matches and
fetches 500-row pages as rows are asked for, keeping the eight most recent;
`rowOf` asks the library for the position instead of paging. The editor moves
from `rows()` to `count()` and `row(i)`.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/Browser.cpp`
- Modify: `plugin/src/Browser.h`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Modify: `tests/plugin/test_browser.cpp` (test)

**Interfaces:**

- Consumes: task 3's `searchPosition`; `countSearch`.
- Produces: `Browser(LibraryView&, int pageSize = Browser::kPage)`,
  `int count() const`, `const SearchRow* row(int)` (null out of range),
  `path/info/contentHash(int)`, `int rowOf(const std::filesystem::path&)`;
  `kPage` 500, `kPagesKept` 8;
  `LibraryView::position(const SearchModel&, std::int64_t)`,
  `LibraryView::fileId(const std::filesystem::path&)`. Removed:
  `Browser::rows()`, `Browser::matches()`.

- [ ] **Step 1: Write the failing test**

Replace the whole of `tests/plugin/test_browser.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Browser.h"
#include "LibraryFixture.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
namespace fs = std::filesystem;
using app::Browser;
using app::LibraryView;

TEST_CASE("Browser searches as the model changes", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library);
    browser.poll();
    CHECK(browser.count() == 3);
    CHECK(browser.total() == 3);
    SearchModel m;
    m.text = "kick";
    browser.setSearch(m);
    REQUIRE(browser.count() == 1);
    CHECK(browser.total() == 3); // "1 of 3"
    CHECK(fs::equivalent(browser.path(0), f.kick));
    CHECK(browser.searchModel().text == "kick");
    CHECK(browser.contentHash(0).size() == 16);
    CHECK(browser.path(5).empty()); // out of range
    CHECK(browser.row(5) == nullptr);
}

TEST_CASE("Browser picks up a library that appears and changes later", "[browser]")
{
    test::LibraryFixture f;
    LibraryView library(f.dbPath);
    Browser browser(library);
    CHECK_FALSE(browser.poll());
    CHECK(browser.count() == 0);
    f.scan();
    CHECK(browser.poll()); // the first scan finished
    CHECK(browser.count() == 3);
    CHECK_FALSE(browser.poll());

    SearchModel rated;
    rated.minRating = 4;
    browser.setSearch(rated);
    CHECK(browser.count() == 0);
    {
        Db writer = Db::open(f.dbPath);
        const auto id = Library(writer).fileByAbsolutePath(f.kick)->id;
        UserData(writer).setRating(id, 5);
    }
    CHECK(browser.poll());
    CHECK(browser.count() == 1);
    REQUIRE(browser.row(0));
    CHECK(browser.row(0)->rating == 5); // a page fetched before the change is not reused
}

TEST_CASE("Browser finds the row of a path again after a refresh", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library);
    browser.poll();
    const int snare = browser.rowOf(f.snare);
    REQUIRE(snare >= 0);
    CHECK(fs::equivalent(browser.path(snare), f.snare));
    CHECK(browser.rowOf(f.dir.path() / "nope.wav") == -1);
    CHECK(browser.info(browser.rowOf(f.loop)).bpm == 120.0);
}

TEST_CASE("Browser fetches rows a page at a time, as they are asked for", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library, 2); // pages of two: three samples span two pages
    browser.poll();
    REQUIRE(browser.count() == 3);
    // By name: Bass_Loop_Am_120, Kick_01, Snare_02.
    REQUIRE(browser.row(2));
    CHECK(browser.row(2)->name == "Snare_02.wav"); // from the second page
    REQUIRE(browser.row(0));
    CHECK(browser.row(0)->name == "Bass_Loop_Am_120.wav");
    CHECK(browser.row(3) == nullptr);
    CHECK(browser.rowOf(f.snare) == 2); // found past the first page, without walking pages
}

TEST_CASE("Browser sorts as the model says, across pages", "[browser]")
{
    test::LibraryFixture f;
    f.scan();
    LibraryView library(f.dbPath);
    Browser browser(library, 2);
    browser.poll();
    SearchModel longest;
    longest.sort = SortField::Duration;
    longest.descending = true; // loop 2 s, kick 0.5 s, snare 0.25 s
    browser.setSearch(longest);
    REQUIRE(browser.row(2));
    CHECK(browser.row(2)->name == "Snare_02.wav");
    CHECK(browser.rowOf(f.loop) == 0);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[browser],[editor]"`

Expected: the build stops:

```
plugin/test_browser.cpp:20:19: error: no member named 'count' in 'asma::app::Browser'
plugin/test_browser.cpp:31:19: error: no member named 'row' in 'asma::app::Browser'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 9d3344a..52fdaba 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -300,10 +300,9 @@ void AsmaEditor::selectionChanged()
     selectedRow_ = table_.getSelectedRow();
     selectedInfo_ = selectedRow_ >= 0 ? browser_.info(selectedRow_) : audio::SampleInfo{};
     selectedFolder_.clear();
-    if (selectedRow_ >= 0) {
-        const auto& rel = browser_.rows()[static_cast<std::size_t>(selectedRow_)].relPath;
-        const auto slash = rel.find_last_of('/');
-        if (slash != std::string::npos) selectedFolder_ = rel.substr(0, slash);
+    if (const SearchRow* r = browser_.row(selectedRow_)) {
+        const auto slash = r->relPath.find_last_of('/');
+        if (slash != std::string::npos) selectedFolder_ = r->relPath.substr(0, slash);
     }
 }

@@ -314,7 +313,7 @@ void AsmaEditor::updateReadouts()
     const bool current = status.generation == processor_.engine().selected(); // the status is the selection's

     // The top bar.
-    top_.setCount(static_cast<int>(browser_.matches()), static_cast<int>(browser_.total()));
+    top_.setCount(browser_.count(), static_cast<int>(browser_.total()));
     if (!processor_.isStandalone()) top_.setHostBpm(processor_.hostBpm());

     // The table, or what it says instead.
@@ -325,7 +324,7 @@ void AsmaEditor::updateReadouts()
                            : "No library yet. Open the asma app and add a folder of samples.";
     else if (library_.state() != LibraryState::Open)
         empty = juce::String(library_.message());
-    else if (browser_.rows().empty())
+    else if (browser_.count() == 0)
         empty = browser_.searchModel().text.empty() ? (standalone ? "The library is empty. Add a folder of samples."
                                                                   : "The library is empty.")
                                                     : "No samples match.";
@@ -335,8 +334,8 @@ void AsmaEditor::updateReadouts()
                                && library_.state() != LibraryState::Unreadable && browser_.searchModel().text.empty());

     // The preview.
-    if (selectedRow_ >= 0 && selectedRow_ < static_cast<int>(browser_.rows().size())) {
-        const SearchRow& r = browser_.rows()[static_cast<std::size_t>(selectedRow_)];
+    if (const SearchRow* row = browser_.row(selectedRow_)) {
+        const SearchRow& r = *row;
         const auto overview = processor_.engine().overview();
         preview_.waveform().setOverview(overview);
         preview_.setFile(utf8(r.name), overview ? PreviewPanel::fileLine(selectedFolder_, overview->sampleRate,
@@ -370,7 +369,7 @@ void AsmaEditor::updateReadouts()
     footer_.setStatus(scans && scans->busy() ? juce::String(scans->progress()) : scanMessage_);
 }

-int AsmaEditor::getNumRows() { return static_cast<int>(browser_.rows().size()); }
+int AsmaEditor::getNumRows() { return browser_.count(); }

 void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int width, int height, bool selected)
 {
@@ -386,7 +385,9 @@ void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int width, int heigh
 void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
 {
     if (row < 0 || row >= getNumRows()) return;
-    const SearchRow& r = browser_.rows()[static_cast<std::size_t>(row)];
+    const SearchRow* found = browser_.row(row);
+    if (!found) return;
+    const SearchRow& r = *found;
     juce::String text;
     juce::Font font = theme::font(theme::Face::Mono, 12.0f);
     juce::Colour colour = theme::text;
```

Replace the whole of `plugin/src/Browser.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Browser.h"

#include <climits>

namespace asma::app {

void Browser::setSearch(SearchModel model)
{
    model_ = std::move(model);
    refetch();
}

bool Browser::poll()
{
    library_.refresh();
    if (!library_.changed()) return false;
    refetch();
    return true;
}

void Browser::refetch()
{
    pages_.clear();
    total_ = library_.sampleCount();
    count_ = static_cast<int>(std::min<std::int64_t>(library_.matchCount(model_), INT_MAX));
}

const SearchRow* Browser::row(int index)
{
    if (index < 0 || index >= count_) return nullptr;
    const int page = index / pageSize_;
    auto it = pages_.begin();
    while (it != pages_.end() && it->first != page) ++it;
    if (it == pages_.end()) {
        SearchModel m = model_;
        m.limit = pageSize_;
        m.offset = page * pageSize_;
        pages_.emplace_front(page, library_.search(m));
        if (static_cast<int>(pages_.size()) > kPagesKept) pages_.pop_back();
        it = pages_.begin();
    } else if (it != pages_.begin()) {
        pages_.splice(pages_.begin(), pages_, it); // most recently used first
        it = pages_.begin();
    }
    const auto at = static_cast<std::size_t>(index - page * pageSize_);
    return at < it->second.size() ? &it->second[at] : nullptr; // the library shrank since the count
}

std::filesystem::path Browser::path(int index)
{
    const SearchRow* r = row(index);
    return r ? LibraryView::pathOf(*r) : std::filesystem::path();
}

audio::SampleInfo Browser::info(int index)
{
    const SearchRow* r = row(index);
    return r ? library_.info(r->id) : audio::SampleInfo{};
}

std::string Browser::contentHash(int index)
{
    const SearchRow* r = row(index);
    return r ? library_.contentHash(r->id) : std::string();
}

int Browser::rowOf(const std::filesystem::path& file)
{
    const auto id = library_.fileId(file);
    if (!id) return -1;
    const auto position = library_.position(model_, *id);
    return position && *position < count_ ? static_cast<int>(*position) : -1;
}

} // namespace asma::app
```

Replace the whole of `plugin/src/Browser.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <cstdint>
#include <filesystem>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace asma::app {

// The search the UI shows and every row it matches, fetched a page at a time
// as rows are asked for, re-run when the library changes. Message thread only.
class Browser {
public:
    static constexpr int kPage = 500;  // rows fetched at once
    static constexpr int kPagesKept = 8; // pages held; older ones are fetched again

    explicit Browser(LibraryView& library, int pageSize = kPage) : library_(library), pageSize_(pageSize) {}

    // The model's limit and offset are ignored: paging is the browser's.
    void setSearch(SearchModel model);
    const SearchModel& searchModel() const { return model_; }
    // Samples the search matches: the table's rows.
    int count() const { return count_; }
    // Samples in the library, whatever the search: "48 of 585".
    std::int64_t total() const { return total_; }

    // Opens the library if it has appeared and re-runs the search when it
    // changed. Returns whether the rows may have changed.
    bool poll();

    // The row, fetching its page if need be; null out of range. Valid until
    // the next call that fetches.
    const SearchRow* row(int index);
    // Row accessors; out of range gives empty values.
    std::filesystem::path path(int index);
    audio::SampleInfo info(int index);
    std::string contentHash(int index);
    // The row showing this file, or -1, found by one query, not by paging.
    int rowOf(const std::filesystem::path& file);

    LibraryView& library() { return library_; }

private:
    void refetch(); // after the search or the library changed

    LibraryView& library_;
    const int pageSize_;
    SearchModel model_;
    int count_ = 0;
    std::int64_t total_ = 0;
    std::list<std::pair<int, std::vector<SearchRow>>> pages_; // most recent first
};

} // namespace asma::app
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index b66266b..1b8f765 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -87,6 +87,21 @@ std::int64_t LibraryView::matchCount(const SearchModel& model)
     return guarded([&] { return asma::countSearch(*db_, model); }, std::int64_t{0});
 }

+std::optional<std::int64_t> LibraryView::position(const SearchModel& model, std::int64_t fileId)
+{
+    return guarded([&] { return asma::searchPosition(*db_, model, fileId); }, std::optional<std::int64_t>{});
+}
+
+std::optional<std::int64_t> LibraryView::fileId(const std::filesystem::path& file)
+{
+    return guarded(
+        [&]() -> std::optional<std::int64_t> {
+            const auto record = Library(*db_).fileByAbsolutePath(file);
+            return record ? std::optional<std::int64_t>(record->id) : std::nullopt;
+        },
+        std::optional<std::int64_t>{});
+}
+
 std::int64_t LibraryView::sampleCount()
 {
     return guarded(
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index 5f45dd2..cba9c09 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -49,6 +49,11 @@ public:
     std::int64_t sampleCount();
     // What the model matches, past the page search() returns. 0 unless open.
     std::int64_t matchCount(const SearchModel& model);
+    // Where the file falls in the model's order, from 0; nothing when it does
+    // not match, or the library is not open.
+    std::optional<std::int64_t> position(const SearchModel& model, std::int64_t fileId);
+    // The library's id for a file, by its path; nothing when it is not in it.
+    std::optional<std::int64_t> fileId(const std::filesystem::path& file);
     audio::SampleInfo info(std::int64_t fileId);
     // The file's content hash, or empty when unknown.
     std::string contentHash(std::int64_t fileId);
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[browser],[editor]"`

Expected: `All tests passed (125 assertions in 22 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/Browser.cpp plugin/src/Browser.h plugin/src/LibraryView.cpp plugin/src/LibraryView.h tests/plugin/test_browser.cpp
git commit -m "app: the table holds every match, fetched a page at a time as it scrolls"
```

---

### Task 6: What the sidebar lists, and how picking sets the search

Spec section 9 (Browsing: the sidebar sets the scope). JUCE-free: the entries
with whole-library counts, the search after picking one, and which entry the
search lights. A saved search is lit only while the search equals it, compared
through the JSON it is stored as.

**Files:**

- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Create: `plugin/src/Sidebar.cpp`
- Create: `plugin/src/Sidebar.h`
- Create: `tests/plugin/test_sidebar.cpp` (test)

**Interfaces:**

- Consumes: `LibraryView::matchCount`; `searchModelToJson`.
- Produces:
  `enum class EntryKind { All, Favourites, Folder, Collection, SavedSearch }`;
  `struct SidebarEntry { EntryKind kind; std::int64_t id; std::string name; std::int64_t count /*-1: none*/; SearchModel saved; }`;
  `std::vector<SidebarEntry> sidebarEntries(LibraryView&)`;
  `SearchModel withEntry(const SidebarEntry&, const SearchModel&)`;
  `int entryFor(const std::vector<SidebarEntry>&, const SearchModel&)` (-1 for
  none); `LibraryView::roots()`, `collections()`, `savedSearches()`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_sidebar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFixture.h"
#include "Sidebar.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::EntryKind;
using app::LibraryView;
using app::SidebarEntry;

namespace {

// The fixture's library, with a favourite, a collection and a saved search.
struct Rig {
    test::LibraryFixture f;
    std::int64_t loop = 0, kick = 0, collection = 0;
    Rig()
    {
        f.scan();
        Db writer = Db::open(f.dbPath);
        Library lib(writer);
        UserData data(writer);
        loop = lib.fileByAbsolutePath(f.loop)->id;
        kick = lib.fileByAbsolutePath(f.kick)->id;
        data.setFavourite(kick, true);
        collection = data.createCollection("Low end");
        data.addToCollection(collection, loop);
        SearchModel loops;
        loops.type = SampleType::Loop;
        data.saveSearch("Loops only", loops);
    }
};

const SidebarEntry* find(const std::vector<SidebarEntry>& entries, EntryKind kind)
{
    for (const auto& e : entries)
        if (e.kind == kind) return &e;
    return nullptr;
}

} // namespace

TEST_CASE("the sidebar lists the library's scopes with what each holds", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    REQUIRE(entries.size() == 5); // All, Favourites, one folder, one collection, one saved search
    CHECK(entries[0].kind == EntryKind::All);
    CHECK(entries[0].count == 3);
    CHECK(entries[1].kind == EntryKind::Favourites);
    CHECK(entries[1].count == 1);
    CHECK(entries[2].kind == EntryKind::Folder);
    CHECK(entries[2].name == "Samples"); // the folder's own name, not its path
    CHECK(entries[2].count == 3);
    CHECK(entries[3].kind == EntryKind::Collection);
    CHECK(entries[3].name == "Low end");
    CHECK(entries[3].count == 1);
    CHECK(entries[4].kind == EntryKind::SavedSearch);
    CHECK(entries[4].name == "Loops only");
    CHECK(entries[4].count < 0); // a saved search shows no count
}

TEST_CASE("picking a scope sets it and clears the others, keeping the search", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    SearchModel m;
    m.text = "bass";
    m.bpmMin = 100.0;
    m = app::withEntry(*find(entries, EntryKind::Collection), m);
    CHECK(m.collectionId == rig.collection);
    CHECK(m.text == "bass"); // the text and the chips narrow within it
    CHECK(m.bpmMin == 100.0);
    m = app::withEntry(*find(entries, EntryKind::Favourites), m);
    CHECK(m.favouritesOnly);
    CHECK_FALSE(m.collectionId); // one scope at a time
    m = app::withEntry(*find(entries, EntryKind::Folder), m);
    CHECK(m.rootId);
    CHECK_FALSE(m.favouritesOnly);
    m = app::withEntry(entries[0], m);
    CHECK_FALSE(m.rootId);
    CHECK(m.text == "bass");
}

TEST_CASE("a saved search loads its whole search and stays lit until it changes", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    SearchModel m;
    m.text = "something else";
    m = app::withEntry(*find(entries, EntryKind::SavedSearch), m);
    CHECK(m.type == SampleType::Loop);
    CHECK(m.text.empty()); // replaced, not merged
    CHECK(entries[static_cast<std::size_t>(app::entryFor(entries, m))].kind == EntryKind::SavedSearch);
    m.text = "bass"; // changed: no longer the saved search
    CHECK(entries[static_cast<std::size_t>(app::entryFor(entries, m))].kind == EntryKind::All);
}

TEST_CASE("the sidebar lights the entry the search's scope is", "[sidebar]")
{
    Rig rig;
    LibraryView library(rig.f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    SearchModel m;
    CHECK(app::entryFor(entries, m) == 0);
    m.collectionId = rig.collection;
    CHECK(entries[static_cast<std::size_t>(app::entryFor(entries, m))].kind == EntryKind::Collection);
    m.collectionId = 9999; // a collection since deleted
    CHECK(app::entryFor(entries, m) == -1); // nothing lit rather than the wrong one
}

TEST_CASE("an unopened library lists the fixed entries without counts", "[sidebar]")
{
    test::LibraryFixture f; // no scan
    LibraryView library(f.dbPath);
    library.refresh();
    const auto entries = app::sidebarEntries(library);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].count == 0);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[sidebar]"`

Expected: the build stops:

```
plugin/test_sidebar.cpp:3:10: fatal error: 'Sidebar.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index 1b8f765..a5af99d 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -102,6 +102,21 @@ std::optional<std::int64_t> LibraryView::fileId(const std::filesystem::path& fil
         std::optional<std::int64_t>{});
 }

+std::vector<Root> LibraryView::roots()
+{
+    return guarded([&] { return Library(*db_).roots(); }, std::vector<Root>{});
+}
+
+std::vector<Collection> LibraryView::collections()
+{
+    return guarded([&] { return UserData(*db_).collections(); }, std::vector<Collection>{});
+}
+
+std::vector<SavedSearch> LibraryView::savedSearches()
+{
+    return guarded([&] { return UserData(*db_).savedSearches(); }, std::vector<SavedSearch>{});
+}
+
 std::int64_t LibraryView::sampleCount()
 {
     return guarded(
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index cba9c09..6169414 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -4,7 +4,9 @@
 #include "asma/audio/SampleInfo.h"
 #include "asma/core/ChangeWatcher.h"
 #include "asma/core/Db.h"
+#include "asma/core/Library.h"
 #include "asma/core/Query.h"
+#include "asma/core/UserData.h"

 #include <filesystem>
 #include <memory>
@@ -45,6 +47,10 @@ public:

     // Empty unless open.
     std::vector<SearchRow> search(const SearchModel& model);
+    // The library's folders, collections and saved searches; empty unless open.
+    std::vector<Root> roots();
+    std::vector<Collection> collections();
+    std::vector<SavedSearch> savedSearches();
     // Every sample a search could find: readable files in enabled folders. 0 unless open.
     std::int64_t sampleCount();
     // What the model matches, past the page search() returns. 0 unless open.
```

Create `plugin/src/Sidebar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Sidebar.h"

#include "asma/core/Query.h"

namespace asma::app {

namespace {

SearchModel scopeOnly(const SearchModel& from)
{
    SearchModel m = from;
    m.rootId.reset();
    m.collectionId.reset();
    m.favouritesOnly = false;
    return m;
}

std::string folderName(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return name.empty() ? path : name;
}

} // namespace

std::vector<SidebarEntry> sidebarEntries(LibraryView& library)
{
    std::vector<SidebarEntry> out;
    const SearchModel all;
    out.push_back({EntryKind::All, 0, "All samples", library.matchCount(all), {}});
    SearchModel favourites;
    favourites.favouritesOnly = true;
    out.push_back({EntryKind::Favourites, 0, "Favourites", library.matchCount(favourites), {}});
    for (const auto& root : library.roots()) {
        if (!root.enabled) continue;
        SearchModel in;
        in.rootId = root.id;
        out.push_back({EntryKind::Folder, root.id, folderName(root.path), library.matchCount(in), {}});
    }
    for (const auto& c : library.collections()) {
        SearchModel in;
        in.collectionId = c.id;
        out.push_back({EntryKind::Collection, c.id, c.name, library.matchCount(in), {}});
    }
    for (const auto& s : library.savedSearches()) out.push_back({EntryKind::SavedSearch, s.id, s.name, -1, s.model});
    return out;
}

SearchModel withEntry(const SidebarEntry& entry, const SearchModel& current)
{
    SearchModel m = scopeOnly(current);
    switch (entry.kind) {
    case EntryKind::All: break;
    case EntryKind::Favourites: m.favouritesOnly = true; break;
    case EntryKind::Folder: m.rootId = entry.id; break;
    case EntryKind::Collection: m.collectionId = entry.id; break;
    case EntryKind::SavedSearch: m = entry.saved; break;
    }
    return m;
}

int entryFor(const std::vector<SidebarEntry>& entries, const SearchModel& model)
{
    // The saved search's own form, as it is stored: paging is not part of it.
    const std::string json = searchModelToJson(model);
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].kind == EntryKind::SavedSearch && searchModelToJson(entries[i].saved) == json)
            return static_cast<int>(i);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const bool lit = (model.favouritesOnly && e.kind == EntryKind::Favourites)
                      || (!model.favouritesOnly && model.rootId && e.kind == EntryKind::Folder && e.id == *model.rootId)
                      || (!model.favouritesOnly && model.collectionId && e.kind == EntryKind::Collection
                          && e.id == *model.collectionId)
                      || (!model.favouritesOnly && !model.rootId && !model.collectionId && e.kind == EntryKind::All);
        if (lit) return static_cast<int>(i);
    }
    return -1;
}

} // namespace asma::app
```

Create `plugin/src/Sidebar.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace asma::app {

enum class EntryKind { All, Favourites, Folder, Collection, SavedSearch };

// One line of the sidebar.
struct SidebarEntry {
    EntryKind kind = EntryKind::All;
    std::int64_t id = 0;     // the folder's, collection's or saved search's
    std::string name;        // UTF-8
    std::int64_t count = -1; // samples it holds in the whole library; -1: none shown
    SearchModel saved;       // a saved search's search
};

// What the sidebar lists: All samples, Favourites, the folders, the
// collections and the saved searches, each kind by name. Counts are of the
// whole library, not of what is being searched.
std::vector<SidebarEntry> sidebarEntries(LibraryView& library);

// The search after picking the entry. A scope (All, Favourites, a folder, a
// collection) replaces the search's scope and keeps its text and chips; a
// saved search replaces the whole search.
SearchModel withEntry(const SidebarEntry& entry, const SearchModel& current);

// The entry the search shows: a saved search it equals, else its scope; -1
// when its scope is not listed (a collection since deleted).
int entryFor(const std::vector<SidebarEntry>& entries, const SearchModel& model);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[sidebar]"`

Expected: `All tests passed (32 assertions in 5 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/Sidebar.cpp plugin/src/Sidebar.h tests/plugin/test_sidebar.cpp
git commit -m "app: what the sidebar lists, and how picking an entry sets the search"
```

---

### Task 7: The filter chips' words, and clearing them

Spec section 9 (Browsing: filter chips). JUCE-free: each chip's label from the
search, clearing one filter or all six, the length presets and the "near" BPM
range (within 3%, whole BPM).

**Files:**

- Create: `plugin/src/Filters.cpp`
- Create: `plugin/src/Filters.h`
- Create: `tests/plugin/test_filters.cpp` (test)

**Interfaces:**

- Consumes: `bpmText`.
- Produces: `enum class Facet { Type, Bpm, Key, Instrument, Length, Rating }`,
  `kFacets`; `std::string chipLabel(Facet, const SearchModel&)`;
  `bool isSet(Facet, const SearchModel&)`;
  `SearchModel cleared(Facet, SearchModel)`,
  `SearchModel clearedAll(SearchModel)`;
  `struct LengthPreset { const char* label; std::optional<double> min, max; }`,
  `kLengthPresets` (4); `std::pair<double, double> bpmNear(double)`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_filters.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Filters.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::Facet;
using app::chipLabel;

TEST_CASE("an unset chip says what it filters", "[filters]")
{
    const SearchModel m;
    CHECK(chipLabel(Facet::Type, m) == "Type");
    CHECK(chipLabel(Facet::Bpm, m) == "BPM");
    CHECK(chipLabel(Facet::Key, m) == "Key");
    CHECK(chipLabel(Facet::Instrument, m) == "Instrument");
    CHECK(chipLabel(Facet::Length, m) == "Length");
    CHECK(chipLabel(Facet::Rating, m) == "Rating");
    for (const Facet f : app::kFacets) CHECK_FALSE(app::isSet(f, m));
}

TEST_CASE("a set chip says its value", "[filters]")
{
    SearchModel m;
    m.type = SampleType::Loop;
    CHECK(chipLabel(Facet::Type, m) == "Loops");
    m.type = SampleType::OneShot;
    CHECK(chipLabel(Facet::Type, m) == "One-shots");

    m.bpmMin = 118.0;
    m.bpmMax = 132.0;
    CHECK(chipLabel(Facet::Bpm, m) == "118–132 BPM");
    m.bpmMax.reset();
    CHECK(chipLabel(Facet::Bpm, m) == "from 118 BPM");
    m.bpmMin.reset();
    m.bpmMax = 97.5;
    CHECK(chipLabel(Facet::Bpm, m) == "up to 97.5 BPM");

    m.keys = {"Am", "C"};
    CHECK(chipLabel(Facet::Key, m) == "Am, C");
    m.keys = {"Am", "C", "Dm", "F", "G"};
    CHECK(chipLabel(Facet::Key, m) == "Am, C, Dm +2"); // a long pick stays short

    m.tags = {"bass", "synth"};
    CHECK(chipLabel(Facet::Instrument, m) == "bass, synth");

    m.durationMin = 1.0;
    m.durationMax = 10.0;
    CHECK(chipLabel(Facet::Length, m) == "1–10 s");
    m.durationMin.reset();
    CHECK(chipLabel(Facet::Length, m) == "under 10 s");
    m.durationMax.reset();
    m.durationMin = 60.0;
    CHECK(chipLabel(Facet::Length, m) == "over 60 s");

    m.minRating = 3;
    CHECK(chipLabel(Facet::Rating, m) == "★★★ and up");
    m.minRating = 5;
    CHECK(chipLabel(Facet::Rating, m) == "★★★★★");
    for (const Facet f : app::kFacets) CHECK(app::isSet(f, m));
}

TEST_CASE("clearing a chip clears its filter, and Clear all keeps the scope and text", "[filters]")
{
    SearchModel m;
    m.text = "bass";
    m.rootId = 2;
    m.sort = SortField::Bpm;
    m.type = SampleType::Loop;
    m.bpmMin = 100.0;
    m.keys = {"Am"};
    m.tags = {"bass"};
    m.durationMax = 4.0;
    m.minRating = 2;
    const SearchModel noKey = app::cleared(Facet::Key, m);
    CHECK(noKey.keys.empty());
    CHECK(noKey.bpmMin == 100.0);
    const SearchModel none = app::clearedAll(m);
    for (const Facet f : app::kFacets) CHECK_FALSE(app::isSet(f, none));
    CHECK(none.text == "bass");
    CHECK(none.rootId == 2);
    CHECK(none.sort == SortField::Bpm);
}

TEST_CASE("length presets and near-tempo ranges", "[filters]")
{
    REQUIRE(app::kLengthPresets.size() == 4);
    CHECK(app::kLengthPresets[0].label == std::string("Under 1 s"));
    CHECK_FALSE(app::kLengthPresets[0].min);
    CHECK(app::kLengthPresets[0].max == 1.0);
    CHECK(app::kLengthPresets[3].min == 60.0);
    CHECK_FALSE(app::kLengthPresets[3].max);
    const auto near = app::bpmNear(120.0); // within 3%, whole BPM
    CHECK(near.first == 116.0);
    CHECK(near.second == 124.0);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[filters]"`

Expected: the build stops:

```
plugin/test_filters.cpp:2:10: fatal error: 'Filters.h' file not found
```

- [ ] **Step 3: Implement**

Create `plugin/src/Filters.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Filters.h"

#include "TempoChip.h"

#include <cmath>

namespace asma::app {

namespace {

// The first three, then how many more: a chip stays short.
std::string listed(const std::vector<std::string>& items)
{
    std::string text;
    for (std::size_t i = 0; i < items.size() && i < 3; ++i) text += (i ? ", " : "") + items[i];
    if (items.size() > 3) text += " +" + std::to_string(items.size() - 3);
    return text;
}

// "118\u2013132 BPM", or with one end open "from 118 BPM" / "up to 132 BPM".
std::string range(const std::optional<double>& lo, const std::optional<double>& hi, const char* unit, const char* above,
                  const char* below)
{
    if (lo && hi) return bpmText(*lo) + "\u2013" + bpmText(*hi) + " " + unit;
    if (lo) return std::string(above) + " " + bpmText(*lo) + " " + unit;
    return std::string(below) + " " + bpmText(*hi) + " " + unit;
}

} // namespace

bool isSet(Facet facet, const SearchModel& m)
{
    switch (facet) {
    case Facet::Type: return m.type != SampleType::Any;
    case Facet::Bpm: return m.bpmMin.has_value() || m.bpmMax.has_value();
    case Facet::Key: return !m.keys.empty();
    case Facet::Instrument: return !m.tags.empty();
    case Facet::Length: return m.durationMin.has_value() || m.durationMax.has_value();
    case Facet::Rating: return m.minRating.has_value();
    }
    return false;
}

std::string chipLabel(Facet facet, const SearchModel& m)
{
    if (!isSet(facet, m)) {
        switch (facet) {
        case Facet::Type: return "Type";
        case Facet::Bpm: return "BPM";
        case Facet::Key: return "Key";
        case Facet::Instrument: return "Instrument";
        case Facet::Length: return "Length";
        case Facet::Rating: return "Rating";
        }
    }
    switch (facet) {
    case Facet::Type: return m.type == SampleType::Loop ? "Loops" : "One-shots";
    case Facet::Bpm: return range(m.bpmMin, m.bpmMax, "BPM", "from", "up to");
    case Facet::Key: return listed(m.keys);
    case Facet::Instrument: return listed(m.tags);
    case Facet::Length: return range(m.durationMin, m.durationMax, "s", "over", "under");
    case Facet::Rating: {
        std::string stars;
        for (int i = 0; i < *m.minRating; ++i) stars += "★";
        return *m.minRating >= 5 ? stars : stars + " and up";
    }
    }
    return {};
}

SearchModel cleared(Facet facet, SearchModel m)
{
    switch (facet) {
    case Facet::Type: m.type = SampleType::Any; break;
    case Facet::Bpm: m.bpmMin.reset(); m.bpmMax.reset(); break;
    case Facet::Key: m.keys.clear(); break;
    case Facet::Instrument: m.tags.clear(); break;
    case Facet::Length: m.durationMin.reset(); m.durationMax.reset(); break;
    case Facet::Rating: m.minRating.reset(); break;
    }
    return m;
}

SearchModel clearedAll(SearchModel m)
{
    for (const Facet f : kFacets) m = cleared(f, std::move(m));
    return m;
}

std::pair<double, double> bpmNear(double bpm)
{
    return {std::round(bpm * 0.97), std::round(bpm * 1.03)};
}

} // namespace asma::app
```

Create `plugin/src/Filters.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Query.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace asma::app {

// The filters the chip row offers, in its order.
enum class Facet { Type, Bpm, Key, Instrument, Length, Rating };
inline constexpr std::array<Facet, 6> kFacets{Facet::Type, Facet::Bpm, Facet::Key,
                                              Facet::Instrument, Facet::Length, Facet::Rating};

// The chip's text: its value when the search sets it ("118–132 BPM",
// "Am, C", "★★★ and up"), else what it filters ("BPM"). UTF-8.
std::string chipLabel(Facet facet, const SearchModel& model);
bool isSet(Facet facet, const SearchModel& model);
// The search without that filter; clearedAll drops all six, keeping the
// scope, the text and the sort.
SearchModel cleared(Facet facet, SearchModel model);
SearchModel clearedAll(SearchModel model);

struct LengthPreset {
    const char* label;
    std::optional<double> min, max; // seconds
};
inline const std::array<LengthPreset, 4> kLengthPresets{{{"Under 1 s", std::nullopt, 1.0},
                                                         {"1–10 s", 1.0, 10.0},
                                                         {"10–60 s", 10.0, 60.0},
                                                         {"Over a minute", 60.0, std::nullopt}}};

// A BPM range "near" a tempo: within 3%, to whole BPM.
std::pair<double, double> bpmNear(double bpm);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[filters]"`

Expected: `All tests passed (50 assertions in 4 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/Filters.cpp plugin/src/Filters.h tests/plugin/test_filters.cpp
git commit -m "app: the filter chips' words, and clearing one or all of them"
```

---

### Task 8: The table's favourite, rating and tags columns, and header sorting

Spec section 9 (Browsing: the table). The columns follow the design; name, BPM,
key, length and rating sort, saved with the project, and the sorted header is
lit with an arrow. Re-sorting keeps the selection on its sample. The screenshot
reference gains the three columns, so it stays green.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Modify: `tests/plugin/test_editor.cpp` (test)
- Modify: `tests/ui/reference/README.md` (test)
- Modify: `tests/ui/reference/main.html` (test)
- Modify: `tests/ui/reference/main.png` (test)

**Interfaces:**

- Consumes: task 5's `Browser`.
- Produces: editor columns `kFavourite`, `kName`, `kType`, `kBpm`, `kKey`,
  `kLength`, `kRating`, `kTags`; `AsmaEditor::sortOrderChanged`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index bf29852..c3a11f6 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -363,3 +363,64 @@ TEST_CASE("the footer and the drag agree on the tempo before any audio has run",
     CHECK(editor->preview().tempoChip().detail() == juce::String::fromUTF8("120 \u2192 180 \u00b7 x1.50"));
     p.editorBeingDeleted(editor.get());
 }
+
+TEST_CASE("the table shows the design's columns, and only some of them sort", "[editor]")
+{
+    EditorRig rig;
+    auto& header = rig.editor->table().getHeader();
+    REQUIRE(header.getNumColumns(true) == 8);
+    juce::StringArray names;
+    for (int i = 0; i < header.getNumColumns(true); ++i) names.add(header.getColumnName(header.getColumnIdOfIndex(i, true)));
+    CHECK(names.joinIntoString(",") == ",Name,Type,BPM,Key,Length,Rating,Tags"); // the favourite star has no title
+    // A column that does not sort leaves the search's sort alone.
+    for (int i = 0; i < header.getNumColumns(true); ++i) {
+        const int id = header.getColumnIdOfIndex(i, true);
+        if (header.getColumnName(id) != "Type" && header.getColumnName(id) != "Tags") continue;
+        header.setSortColumnId(id, false);
+        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+        CHECK(rig.p->pluginState().search.sort == SortField::Name);
+        CHECK_FALSE(rig.p->pluginState().search.descending);
+    }
+}
+
+TEST_CASE("clicking a header sorts the table and the project keeps it", "[editor]")
+{
+    EditorRig rig;
+    auto& header = rig.editor->table().getHeader();
+    int length = 0;
+    for (int i = 0; i < header.getNumColumns(true); ++i)
+        if (header.getColumnName(header.getColumnIdOfIndex(i, true)) == "Length") length = header.getColumnIdOfIndex(i, true);
+    REQUIRE(length > 0);
+    header.setSortColumnId(length, false); // longest first, as a second click would
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.p->pluginState().search.sort == SortField::Duration);
+    CHECK(rig.p->pluginState().search.descending);
+    rig.editor->table().selectRow(0);
+    CHECK(fs::equivalent(fromUtf8(rig.p->pluginState().selected), rig.f.loop)); // 2 s, the longest
+
+    app::PluginState s = rig.p->pluginState(); // a project saved sorted by BPM opens sorted by BPM
+    s.search.sort = SortField::Bpm;
+    s.search.descending = false;
+    rig.p->setPluginState(s);
+    rig.editor->poll();
+    CHECK(header.getSortColumnId() != length);
+    CHECK(header.isSortedForwards());
+}
+
+TEST_CASE("re-sorting keeps the selection on its sample, on its new row", "[editor]")
+{
+    EditorRig rig;
+    rig.editor->table().selectRow(rig.editor->table().getNumRows() - 1); // by name: the snare, last
+    const auto selected = fromUtf8(rig.p->pluginState().selected);
+    auto& header = rig.editor->table().getHeader();
+    for (int i = 0; i < header.getNumColumns(true); ++i)
+        if (header.getColumnName(header.getColumnIdOfIndex(i, true)) == "Length")
+            header.setSortColumnId(header.getColumnIdOfIndex(i, true), false); // longest first: the snare is still last
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    for (int i = 0; i < header.getNumColumns(true); ++i)
+        if (header.getColumnName(header.getColumnIdOfIndex(i, true)) == "Length")
+            header.setSortColumnId(header.getColumnIdOfIndex(i, true), true); // shortest first: now first
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->table().getSelectedRow() == 0);
+    CHECK(fromUtf8(rig.p->pluginState().selected) == selected); // the same sample, not row 2's
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[editor]"`

Expected: it fails:

```
tests/plugin/test_editor.cpp:371: FAILED:
  REQUIRE( header.getNumColumns(true) == 8 )
with expansion:
  5 == 8
tests/plugin/test_editor.cpp:396: FAILED:
  CHECK( rig.p->pluginState().search.sort == SortField::Duration )
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 52fdaba..6dd499d 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -17,6 +17,20 @@ namespace {
 constexpr int kTimerHz = 30;      // the playhead moves smoothly
 constexpr int kLibraryEvery = 6;  // ticks between library checks: 5 a second
 constexpr int kRendersEvery = 60; // ticks between measuring the renders: every 2 s
+constexpr int kStarColumn = 48;   // the favourite star, after the table's margin
+
+// The sort a column gives; nothing for those that do not sort.
+std::optional<SortField> sortFor(int column)
+{
+    switch (column) {
+    case 2: return SortField::Name;
+    case 4: return SortField::Bpm;
+    case 5: return SortField::Key;
+    case 6: return SortField::Duration;
+    case 7: return SortField::Rating;
+    default: return std::nullopt;
+    }
+}

 juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

@@ -44,11 +58,18 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
     }

     auto& header = table_.getHeader();
-    header.addColumn("Name", kName, 400, 120, -1, juce::TableHeaderComponent::notSortable);
-    header.addColumn("Type", kType, 76, 60, 120, juce::TableHeaderComponent::notSortable);
-    header.addColumn("BPM", kBpm, 70, 50, 120, juce::TableHeaderComponent::notSortable);
-    header.addColumn("Key", kKey, 56, 40, 100, juce::TableHeaderComponent::notSortable);
-    header.addColumn("Length", kLength, 72, 50, 120, juce::TableHeaderComponent::notSortable);
+    using Header = juce::TableHeaderComponent;
+    const int fixed = Header::visible | Header::notSortable; // neither sorts nor resizes
+    const int sorts = Header::visible | Header::resizable | Header::sortable;
+    const int plain = Header::visible | Header::resizable | Header::notSortable;
+    header.addColumn({}, kFavourite, kStarColumn, kStarColumn, kStarColumn, fixed);
+    header.addColumn("Name", kName, 400, 120, -1, sorts);
+    header.addColumn("Type", kType, 76, 60, 120, plain);
+    header.addColumn("BPM", kBpm, 70, 50, 120, sorts);
+    header.addColumn("Key", kKey, 56, 40, 100, sorts);
+    header.addColumn("Length", kLength, 72, 50, 120, sorts);
+    header.addColumn("Rating", kRating, 88, 70, 120, sorts);
+    header.addColumn("Tags", kTags, 140, 60, 400, plain);
     header.setStretchToFitActive(true);
     table_.setHeaderHeight(theme::kHeaderRowHeight);
     table_.setRowHeight(theme::kRowHeight);
@@ -133,10 +154,30 @@ void AsmaEditor::loadState()
         top_.linkChip().setToggleState(state.link, juce::dontSendNotification);
     }
     browser_.setSearch(state.search);
+    {
+        // The header shows the saved sort; that is not the user sorting.
+        const juce::ScopedValueSetter quiet(quietSort_, true);
+        for (int column = kFavourite; column <= kTags; ++column)
+            if (sortFor(column) == state.search.sort) table_.getHeader().setSortColumnId(column, !state.search.descending);
+    }
     table_.updateContent();
     showSelection();
 }

+void AsmaEditor::sortOrderChanged(int newSortColumnId, bool isForwards)
+{
+    const auto field = sortFor(newSortColumnId);
+    if (quietSort_ || !field) return;
+    SearchModel model = browser_.searchModel();
+    model.sort = *field;
+    model.descending = !isForwards;
+    browser_.setSearch(model);
+    processor_.updateState([&](PluginState& s) { s.search = model; });
+    table_.updateContent();
+    showSelection(); // the selection keeps its sample, on its new row
+    updateReadouts();
+}
+
 void AsmaEditor::paint(juce::Graphics& g)
 {
     g.fillAll(theme::surface);
@@ -393,10 +434,36 @@ void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, in
     juce::Colour colour = theme::text;
     int x = 0;
     switch (column) {
+    case kFavourite:
+        g.setFont(theme::font(theme::Face::Text, 13.0f));
+        g.setColour(r.favourite ? theme::amber : theme::faint);
+        g.drawText(juce::String::fromUTF8(r.favourite ? "\u2605" : "\u2606"), AsmaLookAndFeel::kTableMargin, 0, 20,
+                   height - 1, juce::Justification::centredLeft, false);
+        return;
+    case kRating: {
+        // Lit stars for the rating, faint ones for the rest.
+        const int lit = r.rating.value_or(0);
+        g.setFont(theme::font(theme::Face::Text, 11.0f).withExtraKerningFactor(0.15f));
+        juce::String on, off;
+        for (int i = 0; i < 5; ++i) (i < lit ? on : off) << juce::String::fromUTF8("\u2605");
+        const int onWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), on)));
+        g.setColour(theme::amber);
+        g.drawText(on, 0, 0, onWidth, height - 1, juce::Justification::centredLeft, false);
+        g.setColour(theme::faint);
+        g.drawText(off, onWidth, 0, width - onWidth, height - 1, juce::Justification::centredLeft, false);
+        return;
+    }
+    case kTags: {
+        juce::StringArray tags;
+        for (const auto& t : r.tags) tags.add(utf8(t));
+        text = tags.joinIntoString(", ");
+        font = theme::font(theme::Face::Text, 12.0f);
+        colour = theme::muted;
+        break;
+    }
     case kName:
         text = utf8(r.name);
         font = theme::font(theme::Face::Text, 13.0f);
-        x = AsmaLookAndFeel::kTableMargin;
         break;
     case kType:
         text = !r.isLoop ? "" : (*r.isLoop ? "loop" : "one-shot");
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index cbce409..53b11ec 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -59,12 +59,13 @@ public:
     void clearRenders();

 private:
-    enum Column { kName = 1, kType, kBpm, kKey, kLength };
+    enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
     // TableListBoxModel
     int getNumRows() override;
     void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override;
     void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
     void selectedRowsChanged(int lastRowSelected) override;
+    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
     void returnKeyPressed(int lastRowSelected) override;
     juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
     void timerCallback() override;
@@ -90,6 +91,7 @@ private:
     std::unique_ptr<juce::FileChooser> chooser_;
     juce::String scanMessage_; // the last scan's outcome, until the next selection
     bool quietSelection_ = false;    // selection changes that must not play
+    bool quietSort_ = false;         // a header showing the saved sort is not a new sort
     std::uint64_t loadedStates_ = 0; // the processor's stateLoads() the controls show
     int ticks_ = 0;
     // What the readouts know about the selection.
```

In `plugin/src/ui/AsmaLookAndFeel.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.cpp b/plugin/src/ui/AsmaLookAndFeel.cpp
index a4b9618..ec73fb7 100644
--- a/plugin/src/ui/AsmaLookAndFeel.cpp
+++ b/plugin/src/ui/AsmaLookAndFeel.cpp
@@ -101,13 +101,19 @@ void AsmaLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHe
 }

 void AsmaLookAndFeel::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& name,
-                                            int columnId, int width, int height, bool, bool, int)
+                                            int columnId, int width, int height, bool, bool, int columnFlags)
 {
-    // The first column starts at the table's margin, as its cells do.
+    // The first column starts at the table's margin, as its cells do; the
+    // sorted one is lit, with an arrow for its direction.
+    using Header = juce::TableHeaderComponent;
     const int x = header.getIndexOfColumnId(columnId, true) == 0 ? kTableMargin : 0;
+    const bool up = (columnFlags & Header::sortedForwards) != 0, down = (columnFlags & Header::sortedBackwards) != 0;
+    juce::String text = name.toUpperCase();
+    if (up) text << juce::String::fromUTF8(" \u25b4");
+    if (down) text << juce::String::fromUTF8(" \u25be");
     g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.06f));
-    g.setColour(theme::muted);
-    g.drawText(name.toUpperCase(), x, 0, width - x, height - 1, juce::Justification::centredLeft, true);
+    g.setColour(up || down ? theme::text : theme::muted);
+    g.drawText(text, x, 0, width - x, height - 1, juce::Justification::centredLeft, true);
 }

 void AsmaLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
```

In `tests/ui/reference/README.md`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/ui/reference/README.md b/tests/ui/reference/README.md
index d7ba4aa..2ac8629 100644
--- a/tests/ui/reference/README.md
+++ b/tests/ui/reference/README.md
@@ -6,8 +6,9 @@ UI") rebuilt as `main.html`, with these changes and nothing else:

 - The rows, the search, the count, the file line and the tempo are the test's
   demo library and state, not the design's sample data.
-- The areas plan 3c2b fills are blank: the sidebar, the chip row, the Similar
-  list, and the favourite, rating and tags columns.
+- The areas plan 3c2b fills are blank: the sidebar, the chip row and the Similar
+  list. The demo library has no favourites or ratings, so those columns show
+  faint stars.
 - The sample is stopped (a play button, no playhead), and the Key and Start
   chips say what they say when off ("off", "now"), so the picture is
   deterministic.
```

In `tests/ui/reference/main.html`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/ui/reference/main.html b/tests/ui/reference/main.html
index c50650c..e256034 100644
--- a/tests/ui/reference/main.html
+++ b/tests/ui/reference/main.html
@@ -44,12 +44,16 @@ header { height: 56px; flex-shrink: 0; display: flex; align-items: center; gap:
 nav { width: 220px; flex-shrink: 0; border-right: 1px solid #2a2d33; background: #111316; }
 main { flex-grow: 1; display: flex; flex-direction: column; min-width: 0; }
 .chips { height: 44px; flex-shrink: 0; border-bottom: 1px solid #2a2d33; }
-.head, .row { height: 30px; display: grid; grid-template-columns: minmax(0, 1fr) 76px 70px 56px 72px; align-items: center; }
+.head, .row { height: 30px; display: grid; grid-template-columns: 48px minmax(0, 1fr) 76px 70px 56px 72px 88px 140px; align-items: center; }
 .head { border-bottom: 1px solid #2a2d33; font-family: "Space Grotesk"; font-size: 11px; font-weight: 600;
   letter-spacing: 0.06em; color: #8a8f98; }
 .row { border-bottom: 1px solid #1c1f23; }
 .row .mono { font-size: 12px; }
-.head span:first-child, .row .name { padding-left: 14px; }
+.head span:first-child, .row .fav { padding-left: 14px; }
+.fav { color: #3a3e45; }
+.stars { color: #3a3e45; font-size: 11px; letter-spacing: 1.5px; }
+.tags { color: #8a8f98; font-size: 12px; }
+.head .lit { color: #e8e9eb; }
 .row.sel { background: rgba(232,163,61,0.10); box-shadow: inset 2px 0 0 #e8a33d; }
 .preview { height: 236px; flex-shrink: 0; border-top: 1px solid #2a2d33; background: #111316; display: flex; }
 .pleft { flex-grow: 1; min-width: 0; padding: 12px 16px; display: flex; flex-direction: column; gap: 10px; }
@@ -105,20 +109,20 @@ footer { height: 26px; flex-shrink: 0; display: flex; align-items: center; justi
     <nav></nav>
     <main>
       <div class="chips"></div>
-      <div class="head"><span>NAME</span><span>TYPE</span><span>BPM</span><span>KEY</span><span>LENGTH</span></div>
-      <div class="row"><span class="name">Bass_Loop_132_rolling.wav</span><span class="muted">loop</span><span class="mono">132</span><span class="mono"></span><span class="mono muted">7.27 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_A#m_128_wobble.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">A#m</span><span class="mono muted">7.50 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_A#m_130.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">A#m</span><span class="mono muted">7.38 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Am_118.wav</span><span class="muted">loop</span><span class="mono">118</span><span class="mono">Am</span><span class="mono muted">8.14 s</span></div>
-      <div class="row sel"><span class="name">Bass_Loop_Am_120.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Am</span><span class="mono muted">8.00 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Am_130_acid.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">Am</span><span class="mono muted">7.38 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_C_126_fingered.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono"></span><span class="mono muted">7.62 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Cm_126_wet.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono">Cm</span><span class="mono muted">7.62 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Dm_120_dusty.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Dm</span><span class="mono muted">4.00 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Em_124.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono">Em</span><span class="mono muted">3.87 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_F_124_sub.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono"></span><span class="mono muted">7.74 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Fm_128.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">Fm</span><span class="mono muted">3.75 s</span></div>
-      <div class="row"><span class="name">Bass_Loop_Gm_122.wav</span><span class="muted">loop</span><span class="mono">122</span><span class="mono">Gm</span><span class="mono muted">7.87 s</span></div>
+      <div class="head"><span></span><span class="lit">NAME &#9652;</span><span>TYPE</span><span>BPM</span><span>KEY</span><span>LENGTH</span><span>RATING</span><span>TAGS</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_132_rolling.wav</span><span class="muted">loop</span><span class="mono">132</span><span class="mono"></span><span class="mono muted">7.27 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_A#m_128_wobble.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">A#m</span><span class="mono muted">7.50 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_A#m_130.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">A#m</span><span class="mono muted">7.38 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Am_118.wav</span><span class="muted">loop</span><span class="mono">118</span><span class="mono">Am</span><span class="mono muted">8.14 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row sel"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Am_120.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Am</span><span class="mono muted">8.00 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Am_130_acid.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">Am</span><span class="mono muted">7.38 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_C_126_fingered.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono"></span><span class="mono muted">7.62 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Cm_126_wet.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono">Cm</span><span class="mono muted">7.62 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Dm_120_dusty.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Dm</span><span class="mono muted">4.00 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Em_124.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono">Em</span><span class="mono muted">3.87 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_F_124_sub.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono"></span><span class="mono muted">7.74 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Fm_128.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">Fm</span><span class="mono muted">3.75 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
+      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Gm_122.wav</span><span class="muted">loop</span><span class="mono">122</span><span class="mono">Gm</span><span class="mono muted">7.87 s</span><span class="stars">&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
     </main>
   </div>
   <section class="preview">
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
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[editor],[fidelity]"`

Expected: `All tests passed (122 assertions in 21 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/ui/AsmaLookAndFeel.cpp tests/plugin/test_editor.cpp tests/ui/reference/README.md tests/ui/reference/main.html tests/ui/reference/main.png
git commit -m "app: the table's favourite, rating and tags columns, and sorting by a header"
```

---

### Task 9: The sidebar

Spec section 9 (Browsing). The sidebar component (rows as buttons in a scrolling
list, headings only for kinds present, Problems at the foot while anything
failed) and its wiring: every change of search now goes through `applySearch`,
so the table, the project, the search box and the lit entry follow. The table
area says "No samples match." whenever the library holds anything, and offers
"Add folder…" only when it holds nothing.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Create: `plugin/src/ui/SidebarView.cpp`
- Create: `plugin/src/ui/SidebarView.h`
- Modify: `tests/plugin/test_editor.cpp` (test)
- Modify: `tests/plugin/test_sidebar.cpp` (test)
- Create: `tests/plugin/test_sidebar_view.cpp` (test)

**Interfaces:**

- Consumes: task 6's sidebar logic.
- Produces: `class SidebarView` (`setEntries`, `setSelected(int)`,
  `setProblems(std::int64_t)`, `onPick(int)`, `onProblems()`, `rowCount()`,
  `row(int)`, `countText(int)`, `sectionTitles()`, `problemsButton()`,
  `problemsText()`); `LibraryView::problemCount()`; `AsmaEditor::sidebar()`,
  `applySearch(const SearchModel&)`, `refreshSidebar()`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index c3a11f6..3761164 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -2,6 +2,7 @@
 #include "AsmaEditor.h"
 #include "LibraryFixture.h"
 #include "PluginTestUtil.h"
+#include "asma/core/UserData.h"
 #include "asma/audio/Render.h"
 #include "asma/core/Fs.h"

@@ -424,3 +425,70 @@ TEST_CASE("re-sorting keeps the selection on its sample, on its new row", "[edit
     CHECK(rig.editor->table().getSelectedRow() == 0);
     CHECK(fromUtf8(rig.p->pluginState().selected) == selected); // the same sample, not row 2's
 }
+
+TEST_CASE("picking in the sidebar sets the table's scope, and a saved search its whole search", "[editor]")
+{
+    EditorRig rig;
+    {
+        Db writer = Db::open(rig.f.dbPath);
+        Library lib(writer);
+        UserData data(writer);
+        data.setFavourite(lib.fileByAbsolutePath(rig.f.kick)->id, true);
+        SearchModel snares;
+        snares.text = "snare";
+        data.saveSearch("Snares", snares);
+    }
+    rig.editor->poll();
+    auto& sidebar = rig.editor->sidebar();
+    REQUIRE(sidebar.rowCount() == 4); // All, Favourites, the folder, the saved search
+    sidebar.row(1).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->table().getNumRows() == 1);
+    CHECK(rig.p->pluginState().search.favouritesOnly); // saved with the project
+    CHECK(sidebar.row(1).getToggleState());
+
+    sidebar.row(3).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->searchBox().getText() == "snare");
+    CHECK(rig.editor->table().getNumRows() == 1);
+    CHECK_FALSE(rig.p->pluginState().search.favouritesOnly);
+    CHECK(sidebar.row(3).getToggleState()); // lit while unchanged
+    rig.type("snare 02");
+    CHECK_FALSE(sidebar.row(3).getToggleState());
+    CHECK(sidebar.row(0).getToggleState()); // back to the scope, All
+}
+
+TEST_CASE("the sidebar shows how many files have problems", "[editor]")
+{
+    EditorRig rig;
+    CHECK_FALSE(rig.editor->sidebar().problemsButton().isVisible());
+    {
+        Db writer = Db::open(rig.f.dbPath);
+        Library lib(writer);
+        lib.setStatus(lib.fileByAbsolutePath(rig.f.snare)->id, FileStatus::Failed, "not audio");
+    }
+    rig.editor->poll();
+    CHECK(rig.editor->sidebar().problemsButton().isVisible());
+    CHECK(rig.editor->sidebar().problemsText() == "1");
+}
+
+TEST_CASE("a saved search naming a collection since deleted shows nothing, quietly", "[editor]")
+{
+    EditorRig rig;
+    {
+        Db writer = Db::open(rig.f.dbPath);
+        UserData data(writer);
+        const auto gone = data.createCollection("Gone");
+        SearchModel inGone;
+        inGone.collectionId = gone;
+        data.saveSearch("In gone", inGone);
+        data.deleteCollection(gone);
+    }
+    rig.editor->poll();
+    auto& sidebar = rig.editor->sidebar();
+    REQUIRE(sidebar.rowCount() == 4); // All, Favourites, the folder, the saved search
+    sidebar.row(3).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->table().getNumRows() == 0);
+    CHECK(rig.editor->emptyText() == "No samples match.");
+}
```

In `tests/plugin/test_sidebar.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_sidebar.cpp b/tests/plugin/test_sidebar.cpp
index 5e233fd..5971f42 100644
--- a/tests/plugin/test_sidebar.cpp
+++ b/tests/plugin/test_sidebar.cpp
@@ -127,3 +127,19 @@ TEST_CASE("an unopened library lists the fixed entries without counts", "[sideba
     REQUIRE(entries.size() == 2);
     CHECK(entries[0].count == 0);
 }
+
+TEST_CASE("the library counts the files that failed to decode or analyse", "[sidebar]")
+{
+    Rig rig;
+    LibraryView library(rig.f.dbPath);
+    library.refresh();
+    CHECK(library.problemCount() == 0);
+    {
+        Db writer = Db::open(rig.f.dbPath);
+        Library lib(writer);
+        lib.setStatus(rig.kick, FileStatus::Failed, "not audio");
+        lib.setAnalysisError(rig.loop, "too short");
+    }
+    library.changed();
+    CHECK(library.problemCount() == 2);
+}
```

Create `tests/plugin/test_sidebar_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/SidebarView.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::EntryKind;
using app::SidebarEntry;
using app::SidebarView;

namespace {

std::vector<SidebarEntry> entries()
{
    return {{EntryKind::All, 0, "All samples", 585, {}},
            {EntryKind::Favourites, 0, "Favourites", 12, {}},
            {EntryKind::Folder, 1, "Samples", 412, {}},
            {EntryKind::Folder, 2, "Splice", 151, {}},
            {EntryKind::Collection, 7, "Low end", 18, {}},
            {EntryKind::SavedSearch, 3, "Short kicks", -1, {}}};
}

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

} // namespace

TEST_CASE("the sidebar shows its entries under their sections", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    REQUIRE(view.rowCount() == 6);
    CHECK(view.row(2).getButtonText() == "Samples");
    CHECK(view.countText(0) == "585");
    CHECK(view.countText(5).isEmpty()); // a saved search has no count
    CHECK(view.sectionTitles() == juce::StringArray{"FOLDERS", "COLLECTIONS", "SAVED SEARCHES"});
    view.setEntries({entries()[0], entries()[1]});
    CHECK(view.sectionTitles().isEmpty()); // no folders, no heading for them
}

TEST_CASE("clicking an entry picks it, and the picked one is lit", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaLookAndFeel lnf;
    SidebarView view;
    view.setLookAndFeel(&lnf);
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    int picked = -1;
    view.onPick = [&](int i) { picked = i; };
    view.row(4).triggerClick();
    settle();
    CHECK(picked == 4);
    view.setSelected(4);
    CHECK(view.row(4).getToggleState());
    CHECK_FALSE(view.row(0).getToggleState());
    view.setSelected(-1); // a scope not listed: nothing lit
    CHECK_FALSE(view.row(4).getToggleState());
    view.setLookAndFeel(nullptr);
}

TEST_CASE("the Problems entry shows only when something failed", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    view.setProblems(0);
    CHECK_FALSE(view.problemsButton().isVisible());
    view.setProblems(3);
    CHECK(view.problemsButton().isVisible());
    CHECK(view.problemsText() == "3");
}

TEST_CASE("a sidebar with many folders scrolls, and Problems stays in view", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    std::vector<SidebarEntry> lots{{EntryKind::All, 0, "All samples", 5000, {}}};
    for (int i = 0; i < 40; ++i) lots.push_back({EntryKind::Folder, i + 1, "Folder " + std::to_string(i), 100, {}});
    view.setEntries(lots);
    view.setProblems(2);
    CHECK(view.getLocalBounds().contains(view.problemsButton().getBounds()));
    CHECK(view.row(40).getBottom() > view.getHeight()); // past the view: scrolled to, not squeezed
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[sidebar],[editor]"`

Expected: the build stops:

```
plugin/test_editor.cpp:442:33: error: no member named 'sidebar' in 'asma::app::AsmaEditor'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 6dd499d..3384c02 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -46,6 +46,11 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)

     top_.searchBox().onTextChange = [this] { searchChanged(); };
     addAndMakeVisible(top_);
+    sidebar_.onPick = [this](int index) {
+        if (index >= 0 && index < static_cast<int>(entries_.size()))
+            applySearch(withEntry(entries_[static_cast<std::size_t>(index)], browser_.searchModel()));
+    };
+    addAndMakeVisible(sidebar_);
     if (processor_.isStandalone()) {
         top_.tempoBox().onValueChange = [this] {
             const double bpm = top_.tempoBox().getValue();
@@ -154,6 +159,7 @@ void AsmaEditor::loadState()
         top_.linkChip().setToggleState(state.link, juce::dontSendNotification);
     }
     browser_.setSearch(state.search);
+    refreshSidebar();
     {
         // The header shows the saved sort; that is not the user sorting.
         const juce::ScopedValueSetter quiet(quietSort_, true);
@@ -191,12 +197,7 @@ void AsmaEditor::paint(juce::Graphics& g)
     g.setColour(theme::border);
     g.fillRect(bottom.getX(), bottom.getY(), bottom.getWidth(), 1);
     g.fillRect(bottom.getRight() - theme::kSimilarWidth, bottom.getY(), 1, bottom.getHeight());
-    // The sidebar's place (plan 3c2b).
-    const auto sidebar = area.removeFromLeft(theme::kSidebarWidth);
-    g.setColour(theme::panel);
-    g.fillRect(sidebar);
-    g.setColour(theme::border);
-    g.fillRect(sidebar.getRight() - 1, sidebar.getY(), 1, sidebar.getHeight());
+    area.removeFromLeft(theme::kSidebarWidth);
     // The chip row's place (plan 3c2b).
     g.fillRect(area.getX(), area.getY() + theme::kChipRowHeight - 1, area.getWidth(), 1);
 }
@@ -210,7 +211,7 @@ void AsmaEditor::resized()
     bottom.removeFromTop(1);
     bottom.removeFromRight(theme::kSimilarWidth);
     preview_.setBounds(bottom);
-    area.removeFromLeft(theme::kSidebarWidth);
+    sidebar_.setBounds(area.removeFromLeft(theme::kSidebarWidth));
     area.removeFromTop(theme::kChipRowHeight);
     table_.setBounds(area);
     empty_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 520), 60).translated(0, -20));
@@ -300,6 +301,7 @@ void AsmaEditor::poll()
     if (browser_.poll()) {
         table_.updateContent();
         showSelection();
+        refreshSidebar();
     }
     updateReadouts();
 }
@@ -308,13 +310,26 @@ void AsmaEditor::searchChanged()
 {
     SearchModel model = browser_.searchModel();
     model.text = top_.searchBox().getText().toStdString();
+    applySearch(model);
+}
+
+void AsmaEditor::applySearch(const SearchModel& model)
+{
     browser_.setSearch(model);
     processor_.updateState([&](PluginState& s) { s.search = model; });
+    if (top_.searchBox().getText().toStdString() != model.text) top_.searchBox().setText(model.text, false);
     table_.updateContent();
     showSelection();
     updateReadouts();
 }

+void AsmaEditor::refreshSidebar()
+{
+    entries_ = sidebarEntries(library_);
+    sidebar_.setEntries(entries_);
+    sidebar_.setProblems(library_.problemCount());
+}
+
 void AsmaEditor::syncChanged(const audio::SyncSettings& sync)
 {
     processor_.updateState([&](PluginState& s) { s.sync = sync; });
@@ -353,7 +368,8 @@ void AsmaEditor::updateReadouts()
     const audio::EngineStatus status = processor_.engine().status();
     const bool current = status.generation == processor_.engine().selected(); // the status is the selection's

-    // The top bar.
+    // The top bar and the sidebar's lit entry.
+    sidebar_.setSelected(entryFor(entries_, browser_.searchModel()));
     top_.setCount(browser_.count(), static_cast<int>(browser_.total()));
     if (!processor_.isStandalone()) top_.setHostBpm(processor_.hostBpm());

@@ -366,13 +382,16 @@ void AsmaEditor::updateReadouts()
     else if (library_.state() != LibraryState::Open)
         empty = juce::String(library_.message());
     else if (browser_.count() == 0)
-        empty = browser_.searchModel().text.empty() ? (standalone ? "The library is empty. Add a folder of samples."
-                                                                  : "The library is empty.")
-                                                    : "No samples match.";
+        // Empty because there is nothing at all, or because the search (its
+        // text, chips or scope) matches nothing.
+        empty = browser_.total() == 0 ? (standalone ? "The library is empty. Add a folder of samples." : "The library is empty.")
+                                      : "No samples match.";
     if (empty != empty_.getText()) empty_.setText(empty, juce::dontSendNotification);
     empty_.setVisible(empty.isNotEmpty());
-    emptyAddFolder_.setVisible(standalone && empty.isNotEmpty() && library_.state() != LibraryState::Outdated
-                               && library_.state() != LibraryState::Unreadable && browser_.searchModel().text.empty());
+    // "Add folder" where there is nothing yet, not where a search found nothing.
+    const bool nothing = library_.state() == LibraryState::Missing
+                      || (library_.state() == LibraryState::Open && browser_.total() == 0);
+    emptyAddFolder_.setVisible(standalone && nothing);

     // The preview.
     if (const SearchRow* row = browser_.row(selectedRow_)) {
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index 53b11ec..a170ef2 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -3,9 +3,11 @@

 #include "Browser.h"
 #include "LibraryView.h"
+#include "Sidebar.h"
 #include "ui/AsmaLookAndFeel.h"
 #include "ui/Footer.h"
 #include "ui/PreviewPanel.h"
+#include "ui/SidebarView.h"
 #include "ui/TopBar.h"

 #include <juce_audio_processors/juce_audio_processors.h>
@@ -46,6 +48,7 @@ public:
     PreviewPanel& preview() { return preview_; }
     Footer& footer() { return footer_; }
     TopBar& topBar() { return top_; }
+    SidebarView& sidebar() { return sidebar_; }
     // The standalone's tempo source and folders; hidden in a plugin.
     juce::Button& linkToggle() { return top_.linkChip(); }
     TempoBox& bpmBox() { return top_.tempoBox(); }
@@ -70,6 +73,10 @@ private:
     juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
     void timerCallback() override;
     void searchChanged();
+    // The one way the search changes: the table, the project, the search box
+    // and the sidebar's lit entry all follow.
+    void applySearch(const SearchModel& model);
+    void refreshSidebar(); // after the library changed
     void syncChanged(const audio::SyncSettings& sync);
     void chooseFolder();
     void showSelection();   // selects the saved file's row without playing it
@@ -83,6 +90,8 @@ private:
     LibraryView library_;
     Browser browser_{library_};
     TopBar top_;
+    SidebarView sidebar_;
+    std::vector<SidebarEntry> entries_; // what the sidebar lists
     juce::TableListBox table_{"Samples", this};
     juce::Label empty_;
     juce::TextButton emptyAddFolder_{juce::String::fromUTF8("Add folder…")};
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index a5af99d..beeca85 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -117,6 +117,17 @@ std::vector<SavedSearch> LibraryView::savedSearches()
     return guarded([&] { return UserData(*db_).savedSearches(); }, std::vector<SavedSearch>{});
 }

+std::int64_t LibraryView::problemCount()
+{
+    return guarded(
+        [&] {
+            auto s = db_->prepare("SELECT COUNT(*) FROM files f JOIN roots r ON r.id = f.root_id WHERE r.enabled = 1 "
+                                  "AND (f.status = 'failed' OR (f.status = 'ok' AND f.analysis_error IS NOT NULL))");
+            return s.step() ? s.getInt(0) : std::int64_t{0};
+        },
+        std::int64_t{0});
+}
+
 std::int64_t LibraryView::sampleCount()
 {
     return guarded(
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index 6169414..5ad1fa2 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -51,6 +51,9 @@ public:
     std::vector<Root> roots();
     std::vector<Collection> collections();
     std::vector<SavedSearch> savedSearches();
+    // Files in enabled folders that failed to decode or analyse: the
+    // Problems entry's count. 0 unless open.
+    std::int64_t problemCount();
     // Every sample a search could find: readable files in enabled folders. 0 unless open.
     std::int64_t sampleCount();
     // What the model matches, past the page search() returns. 0 unless open.
```

Create `plugin/src/ui/SidebarView.cpp`:

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

SidebarView::SidebarView() : content_(std::make_unique<Content>()), problems_(std::make_unique<ProblemsButton>())
{
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
    rows_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        auto* row = rows_.add(new Row(juce::String::fromUTF8(e.name.c_str()), e.count >= 0 ? juce::String(e.count) : juce::String()));
        row->onClick = [this, i] {
            if (onPick) onPick(static_cast<int>(i));
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
    int y = kTop;
    EntryKind section = EntryKind::All;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto kind = entries_[i].kind;
        if (const char* heading = headingFor(kind); heading && kind != section) {
            y += kSectionGap;
            content_->headings.push_back({heading, y});
            y += kHeadingHeight;
        }
        if (kind == EntryKind::Folder || kind == EntryKind::Collection || kind == EntryKind::SavedSearch) section = kind;
        rows_[static_cast<int>(i)]->setBounds(0, y, area.getWidth(), kRowHeight);
        y += kRowHeight;
    }
    content_->setSize(area.getWidth(), y + kTop);
    content_->repaint();
}

} // namespace asma::app
```

Create `plugin/src/ui/SidebarView.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Sidebar.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace asma::app {

// The left column: All samples and Favourites, then the folders, the
// collections and the saved searches under their headings, each with its
// count, scrolling when they do not fit; Problems at the foot when anything
// failed. It shows what it is told and reports what is picked.
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

    int rowCount() const { return rows_.size(); }
    juce::Button& row(int index) { return *rows_[index]; }
    juce::String countText(int index) const;
    juce::StringArray sectionTitles() const;
    juce::Button& problemsButton() { return *problems_; }
    juce::String problemsText() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Content;
    std::vector<SidebarEntry> entries_;
    juce::OwnedArray<juce::Button> rows_;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    std::unique_ptr<juce::Button> problems_;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[sidebar],[editor]"`

Expected: `All tests passed (177 assertions in 33 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/ui/SidebarView.cpp plugin/src/ui/SidebarView.h tests/plugin/test_editor.cpp tests/plugin/test_sidebar.cpp tests/plugin/test_sidebar_view.cpp
git commit -m "app: the sidebar: scopes with their counts, saved searches, and the Problems count"
```

---

### Task 10: The filter chips

Spec section 9 (Browsing: filter chips). Each chip is a pill with two buttons,
itself and its x, so both take the keyboard; when the row is too narrow the
chips give up width in proportion and their labels shorten. A chip's x and Clear
all go through `applySearch`.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Create: `plugin/src/ui/ChipRow.cpp`
- Create: `plugin/src/ui/ChipRow.h`
- Create: `tests/plugin/test_chip_row.cpp` (test)
- Modify: `tests/plugin/test_editor.cpp` (test)

**Interfaces:**

- Consumes: task 7's filter words.
- Produces: `class FilterChip` (`setLabel`, `label()`, `isActive()`,
  `idealWidth()`, `textArea()`, `labelWidth()`, `mainButton()`,
  `clearButton()`); `class ChipRow` (`setModel`, `onChange(const SearchModel&)`,
  `onOpen(Facet, juce::Component&)`, `chip(Facet)`, `clearAllButton()`);
  `AsmaEditor::chipRow()`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_chip_row.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ChipRow.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::ChipRow;
using app::Facet;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

SearchModel loopsAt120()
{
    SearchModel m;
    m.text = "bass";
    m.type = SampleType::Loop;
    m.bpmMin = 118.0;
    m.bpmMax = 132.0;
    return m;
}

} // namespace

TEST_CASE("the chip row labels each chip from the search", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44);
    row.setModel(SearchModel{});
    CHECK(row.chip(Facet::Bpm).label() == "BPM");
    CHECK_FALSE(row.chip(Facet::Bpm).isActive());
    CHECK_FALSE(row.clearAllButton().isVisible());
    row.setModel(loopsAt120());
    CHECK(row.chip(Facet::Type).label() == "Loops");
    CHECK(row.chip(Facet::Bpm).label() == juce::String::fromUTF8("118–132 BPM"));
    CHECK(row.chip(Facet::Bpm).isActive());
    CHECK(row.chip(Facet::Bpm).clearButton().isVisible());
    CHECK_FALSE(row.chip(Facet::Key).clearButton().isVisible());
    CHECK(row.clearAllButton().isVisible());
}

TEST_CASE("a chip's x clears its filter; Clear all clears them all", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44);
    row.setModel(loopsAt120());
    SearchModel last;
    int changes = 0;
    row.onChange = [&](const SearchModel& m) {
        last = m;
        ++changes;
    };
    row.chip(Facet::Bpm).clearButton().triggerClick();
    settle();
    CHECK(changes == 1);
    CHECK_FALSE(last.bpmMin);
    CHECK(last.type == SampleType::Loop); // only that one
    CHECK(last.text == "bass");
    row.clearAllButton().triggerClick();
    settle();
    CHECK(last.type == SampleType::Any);
    CHECK(last.text == "bass"); // the text is not a chip
}

TEST_CASE("clicking a chip asks for its popover", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44);
    row.setModel(SearchModel{});
    std::optional<Facet> opened;
    row.onOpen = [&](Facet f, juce::Component&) { opened = f; };
    row.chip(Facet::Key).mainButton().triggerClick();
    settle();
    CHECK(opened == Facet::Key);
}

TEST_CASE("the chips fit the row at the narrowest window", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 680, 44); // a 900 px window less the 220 px sidebar
    SearchModel busy = loopsAt120();
    busy.keys = {"Am", "C", "Dm", "F"};
    busy.tags = {"bass", "synth"};
    busy.durationMin = 1.0;
    busy.durationMax = 10.0;
    busy.minRating = 3;
    row.setModel(busy);
    for (const Facet f : app::kFacets) CHECK(row.getLocalBounds().contains(row.chip(f).getBounds()));
}

TEST_CASE("a set chip is wide enough for its whole label beside its x", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44); // room to spare
    row.setModel(loopsAt120());
    auto& chip = row.chip(Facet::Bpm);
    CHECK(chip.getWidth() == chip.idealWidth());
    CHECK(chip.textArea().getRight() <= chip.clearButton().getX()); // the label stops before the x
    CHECK(chip.textArea().getWidth() >= chip.labelWidth());         // and is not cut short
}
```

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index 3761164..3bd632e 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -492,3 +492,20 @@ TEST_CASE("a saved search naming a collection since deleted shows nothing, quiet
     CHECK(rig.editor->table().getNumRows() == 0);
     CHECK(rig.editor->emptyText() == "No samples match.");
 }
+
+TEST_CASE("the chip row shows the project's filters, and clearing one widens the table", "[editor]")
+{
+    EditorRig rig;
+    app::PluginState s = rig.p->pluginState();
+    s.search.type = SampleType::Loop;
+    rig.p->setPluginState(s);
+    rig.editor->poll();
+    auto& type = rig.editor->chipRow().chip(app::Facet::Type);
+    CHECK(type.label() == "Loops");
+    CHECK(rig.editor->table().getNumRows() == 1); // the bass loop
+    type.clearButton().triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->table().getNumRows() == 3);
+    CHECK(rig.p->pluginState().search.type == SampleType::Any); // the project keeps it
+    CHECK(type.label() == "Type");
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[chips],[editor]"`

Expected: the build stops:

```
plugin/test_chip_row.cpp:2:10: fatal error: 'ui/ChipRow.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 3384c02..439bcd2 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -51,6 +51,8 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
             applySearch(withEntry(entries_[static_cast<std::size_t>(index)], browser_.searchModel()));
     };
     addAndMakeVisible(sidebar_);
+    chips_.onChange = [this](const SearchModel& model) { applySearch(model); };
+    addAndMakeVisible(chips_);
     if (processor_.isStandalone()) {
         top_.tempoBox().onValueChange = [this] {
             const double bpm = top_.tempoBox().getValue();
@@ -159,6 +161,7 @@ void AsmaEditor::loadState()
         top_.linkChip().setToggleState(state.link, juce::dontSendNotification);
     }
     browser_.setSearch(state.search);
+    chips_.setModel(state.search);
     refreshSidebar();
     {
         // The header shows the saved sort; that is not the user sorting.
@@ -197,9 +200,7 @@ void AsmaEditor::paint(juce::Graphics& g)
     g.setColour(theme::border);
     g.fillRect(bottom.getX(), bottom.getY(), bottom.getWidth(), 1);
     g.fillRect(bottom.getRight() - theme::kSimilarWidth, bottom.getY(), 1, bottom.getHeight());
-    area.removeFromLeft(theme::kSidebarWidth);
-    // The chip row's place (plan 3c2b).
-    g.fillRect(area.getX(), area.getY() + theme::kChipRowHeight - 1, area.getWidth(), 1);
+
 }

 void AsmaEditor::resized()
@@ -212,7 +213,7 @@ void AsmaEditor::resized()
     bottom.removeFromRight(theme::kSimilarWidth);
     preview_.setBounds(bottom);
     sidebar_.setBounds(area.removeFromLeft(theme::kSidebarWidth));
-    area.removeFromTop(theme::kChipRowHeight);
+    chips_.setBounds(area.removeFromTop(theme::kChipRowHeight));
     table_.setBounds(area);
     empty_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 520), 60).translated(0, -20));
     emptyAddFolder_.setBounds(area.withSizeKeepingCentre(120, 30).translated(0, 30));
@@ -318,6 +319,7 @@ void AsmaEditor::applySearch(const SearchModel& model)
     browser_.setSearch(model);
     processor_.updateState([&](PluginState& s) { s.search = model; });
     if (top_.searchBox().getText().toStdString() != model.text) top_.searchBox().setText(model.text, false);
+    chips_.setModel(model);
     table_.updateContent();
     showSelection();
     updateReadouts();
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index a170ef2..6e1b955 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -5,6 +5,7 @@
 #include "LibraryView.h"
 #include "Sidebar.h"
 #include "ui/AsmaLookAndFeel.h"
+#include "ui/ChipRow.h"
 #include "ui/Footer.h"
 #include "ui/PreviewPanel.h"
 #include "ui/SidebarView.h"
@@ -49,6 +50,7 @@ public:
     Footer& footer() { return footer_; }
     TopBar& topBar() { return top_; }
     SidebarView& sidebar() { return sidebar_; }
+    ChipRow& chipRow() { return chips_; }
     // The standalone's tempo source and folders; hidden in a plugin.
     juce::Button& linkToggle() { return top_.linkChip(); }
     TempoBox& bpmBox() { return top_.tempoBox(); }
@@ -91,6 +93,7 @@ private:
     Browser browser_{library_};
     TopBar top_;
     SidebarView sidebar_;
+    ChipRow chips_;
     std::vector<SidebarEntry> entries_; // what the sidebar lists
     juce::TableListBox table_{"Samples", this};
     juce::Label empty_;
```

Create `plugin/src/ui/ChipRow.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ChipRow.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kChipHeight = 26;
constexpr int kClear = 16;   // the x's circle
constexpr int kClearInset = 5; // from the chip's right edge to the x
constexpr int kClearGap = 4;   // between the label and the x
constexpr int kPad = 10;
constexpr int kGap = 8;
constexpr int kMargin = 14;

const juce::Font& chipFont()
{
    static const juce::Font font = theme::font(theme::Face::Text, 12.0f);
    return font;
}

// The chip's own area: transparent, the pill draws everything.
class Invisible final : public juce::Button {
public:
    explicit Invisible(const juce::String& name) : juce::Button(name) { setTitle(name); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        if (!highlighted) return;
        g.setColour(theme::text.withAlpha(0.05f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), static_cast<float>(getHeight()) / 2.0f);
    }
};

// The x that clears a set filter.
class Cross final : public juce::Button {
public:
    Cross() : juce::Button("Clear") { setTitle("Clear"); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        if (highlighted) {
            g.setColour(theme::amber.withAlpha(0.25f));
            g.fillEllipse(r);
        }
        const auto c = r.getCentre();
        g.setColour(theme::amber);
        g.drawLine(c.x - 3.0f, c.y - 3.0f, c.x + 3.0f, c.y + 3.0f, 1.4f);
        g.drawLine(c.x + 3.0f, c.y - 3.0f, c.x - 3.0f, c.y + 3.0f, 1.4f);
    }
};

const char* nameOf(Facet f)
{
    switch (f) {
    case Facet::Type: return "Type";
    case Facet::Bpm: return "BPM";
    case Facet::Key: return "Key";
    case Facet::Instrument: return "Instrument";
    case Facet::Length: return "Length";
    case Facet::Rating: return "Rating";
    }
    return "";
}

} // namespace

FilterChip::FilterChip() : main_(std::make_unique<Invisible>("Filter")), clear_(std::make_unique<Cross>())
{
    addAndMakeVisible(*main_);
    addChildComponent(*clear_);
}

void FilterChip::setLabel(const juce::String& label, bool active)
{
    if (label == label_ && active == active_) return;
    label_ = label;
    active_ = active;
    main_->setDescription(label);
    clear_->setVisible(active);
    resized();
    repaint();
}

int FilterChip::labelWidth() const
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(chipFont(), label_)));
}

int FilterChip::idealWidth() const
{
    return kPad + labelWidth() + (active_ ? kClearGap + kClear + kClearInset : kPad);
}

juce::Rectangle<int> FilterChip::textArea() const
{
    return getLocalBounds().withTrimmedLeft(kPad).withTrimmedRight(active_ ? kClearGap + kClear + kClearInset : kPad);
}

void FilterChip::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float radius = r.getHeight() / 2.0f;
    if (active_) {
        g.setColour(theme::amber.withAlpha(0.12f));
        g.fillRoundedRectangle(r, radius);
    }
    g.setColour(active_ ? theme::amber : theme::border);
    g.drawRoundedRectangle(r, radius, 1.0f);
    g.setFont(chipFont());
    g.setColour(active_ ? theme::amberLight : theme::muted);
    g.drawFittedText(label_, textArea(), juce::Justification::centredLeft, 1, 1.0f); // shortens with an ellipsis
}

void FilterChip::resized()
{
    main_->setBounds(getLocalBounds());
    clear_->setBounds(getWidth() - kClear - kClearInset, (getHeight() - kClear) / 2, kClear, kClear);
}

ChipRow::ChipRow()
{
    for (const Facet f : kFacets) {
        auto* added = chips_.add(new FilterChip());
        added->mainButton().setTitle(nameOf(f));
        added->mainButton().onClick = [this, f] {
            if (onOpen) onOpen(f, chip(f));
        };
        added->clearButton().setTitle(juce::String("Clear ") + nameOf(f));
        added->clearButton().onClick = [this, f] {
            if (onChange) onChange(cleared(f, model_));
        };
        addAndMakeVisible(added);
    }
    clearAll_.getProperties().set("asma.quiet", true);
    clearAll_.getProperties().set("asma.size", 12.0f);
    clearAll_.getProperties().set("asma.segment", "middle"); // no frame: a link-like button
    clearAll_.onClick = [this] {
        if (onChange) onChange(clearedAll(model_));
    };
    addChildComponent(clearAll_);
    setModel({});
}

void ChipRow::setModel(const SearchModel& model)
{
    model_ = model;
    bool any = false;
    for (const Facet f : kFacets) {
        const bool set = isSet(f, model);
        any |= set;
        chip(f).setLabel(juce::String::fromUTF8(chipLabel(f, model).c_str()), set);
    }
    clearAll_.setVisible(any);
    resized();
}

void ChipRow::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    g.setColour(theme::border);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void ChipRow::resized()
{
    auto area = getLocalBounds().reduced(kMargin, 0);
    clearAll_.setBounds(area.removeFromRight(72).withSizeKeepingCentre(72, kChipHeight));
    area.removeFromRight(kGap);
    // Ideal widths; when they do not fit, every chip gives up its share.
    int wanted = 0;
    for (auto* c : chips_) wanted += c->idealWidth();
    const int room = area.getWidth() - kGap * (chips_.size() - 1);
    const double scale = wanted > room && wanted > 0 ? static_cast<double>(room) / wanted : 1.0;
    int x = area.getX();
    for (auto* c : chips_) {
        const int w = std::max(40, static_cast<int>(c->idealWidth() * scale));
        c->setBounds(x, (getHeight() - kChipHeight) / 2, std::min(w, area.getRight() - x), kChipHeight);
        x += w + kGap;
    }
}

} // namespace asma::app
```

Create `plugin/src/ui/ChipRow.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Filters.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// One filter as a pill: its label (amber with an x when set, grey when not).
// The pill and the x are separate buttons, so both take the keyboard.
class FilterChip : public juce::Component {
public:
    FilterChip();

    void setLabel(const juce::String& label, bool active);
    const juce::String& label() const { return label_; }
    bool isActive() const { return active_; }
    int idealWidth() const;
    // Where the label goes, left of the x when there is one.
    juce::Rectangle<int> textArea() const;
    int labelWidth() const;

    juce::Button& mainButton() { return *main_; }
    juce::Button& clearButton() { return *clear_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String label_;
    bool active_ = false;
    std::unique_ptr<juce::Button> main_, clear_;
};

// The row over the table: a chip per filter and "Clear all". It shows the
// search it is given and reports what the user changes.
class ChipRow : public juce::Component {
public:
    ChipRow();

    void setModel(const SearchModel& model);

    // A chip's x or Clear all: the search without those filters.
    std::function<void(const SearchModel&)> onChange;
    // A chip was clicked: open its popover against `anchor`.
    std::function<void(Facet, juce::Component& anchor)> onOpen;

    FilterChip& chip(Facet facet) { return *chips_[static_cast<int>(facet)]; }
    juce::Button& clearAllButton() { return clearAll_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    SearchModel model_;
    juce::OwnedArray<FilterChip> chips_;
    juce::TextButton clearAll_{"Clear all"};
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[chips],[editor]"`

Expected: `All tests passed (159 assertions in 29 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/ui/ChipRow.cpp plugin/src/ui/ChipRow.h tests/plugin/test_chip_row.cpp tests/plugin/test_editor.cpp
git commit -m "app: the filter chips over the table, each with an x that clears it"
```

---

### Task 11: A popover for each filter chip

Spec section 9 (Browsing: the chips' popovers) and the approved popovers
artboard. Each edits its own copy of the search and reports every change; a
range typed backwards means the same range. They open in a `CallOutBox` inside
the editor, so a plugin's popover stays in its window, styled by the
LookAndFeel.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Modify: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Modify: `plugin/src/ui/AsmaLookAndFeel.h`
- Create: `plugin/src/ui/FilterPopovers.cpp`
- Create: `plugin/src/ui/FilterPopovers.h`
- Modify: `tests/plugin/test_editor.cpp` (test)
- Create: `tests/plugin/test_filter_popovers.cpp` (test)

**Interfaces:**

- Consumes: tasks 7 and 10; `SegmentedControl`; `PreviewPanel::keys()`.
- Produces: `FilterPopover` (`hint()`), `TypePopover`, `BpmPopover`,
  `KeyPopover`, `InstrumentPopover`, `LengthPopover`, `RatingPopover`;
  `struct PopoverContext { std::vector<TagCount> tags; double tempo; }`;
  `makeFilterPopover(Facet, const SearchModel&, const PopoverContext&, SearchChanged)`;
  `LibraryView::tagCounts()`; `AsmaEditor::popoverContext()`; `AsmaLookAndFeel`
  draws callout boxes and reads `asma.mono`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index 3bd632e..283fd0e 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -509,3 +509,29 @@ TEST_CASE("the chip row shows the project's filters, and clearing one widens the
     CHECK(rig.p->pluginState().search.type == SampleType::Any); // the project keeps it
     CHECK(type.label() == "Type");
 }
+
+TEST_CASE("clicking a chip opens its popover in the window, and its changes reach the table", "[editor]")
+{
+    EditorRig rig;
+    rig.editor->chipRow().chip(app::Facet::Key).mainButton().triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
+    app::KeyPopover* keys = nullptr;
+    for (auto* child : rig.editor->getChildren())
+        if (auto* box = dynamic_cast<juce::CallOutBox*>(child))
+            for (auto* inner : box->getChildren())
+                if (auto* found = dynamic_cast<app::KeyPopover*>(inner)) keys = found;
+    REQUIRE(keys); // inside the editor: a plugin's popover stays in its window
+    keys->key(21).triggerClick(); // Am: the bass loop's key
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->table().getNumRows() == 1);
+    CHECK(rig.editor->chipRow().chip(app::Facet::Key).label() == "Am");
+    CHECK(rig.p->pluginState().search.keys == std::vector<std::string>{"Am"});
+}
+
+TEST_CASE("the Instrument popover lists the library's tags", "[editor]")
+{
+    EditorRig rig;
+    const auto tags = rig.editor->popoverContext().tags;
+    REQUIRE_FALSE(tags.empty());
+    CHECK(tags.front().count >= 1);
+}
```

Create `tests/plugin/test_filter_popovers.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/FilterPopovers.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::app;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

// Collects what a popover reports.
struct Sink {
    SearchModel last;
    int changes = 0;
    std::function<void(const SearchModel&)> fn()
    {
        return [this](const SearchModel& m) {
            last = m;
            ++changes;
        };
    }
};

void type(juce::TextEditor& field, const char* text)
{
    field.setText(text, true);
    settle();
}

} // namespace

TEST_CASE("the Type popover switches between any, loops and one-shots", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    SearchModel m;
    m.text = "bass";
    TypePopover p(m, sink.fn());
    p.choice().segment(1).triggerClick();
    settle();
    CHECK(sink.last.type == SampleType::Loop);
    CHECK(sink.last.text == "bass"); // only the filter changes
    p.choice().segment(2).triggerClick();
    settle();
    CHECK(sink.last.type == SampleType::OneShot);
}

TEST_CASE("the BPM popover takes a range, or one near a tempo", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    BpmPopover p({}, 124.0, sink.fn());
    type(p.from(), "118");
    type(p.to(), "132.5");
    CHECK(sink.last.bpmMin == 118.0);
    CHECK(sink.last.bpmMax == 132.5);
    type(p.from(), "");
    CHECK_FALSE(sink.last.bpmMin); // an empty end is open
    type(p.to(), "fast");
    CHECK_FALSE(sink.last.bpmMax); // not a number: no limit, not 0
    p.nearTempo().triggerClick();
    settle();
    CHECK(sink.last.bpmMin == 120.0); // within 3% of 124
    CHECK(sink.last.bpmMax == 128.0);
    CHECK(p.from().getText() == "120");
    BpmPopover none({}, 0.0, sink.fn());
    CHECK_FALSE(none.nearTempo().isEnabled()); // no tempo to be near
}

TEST_CASE("the Key popover picks any number of keys, in the order picked", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    KeyPopover p({}, sink.fn());
    REQUIRE(p.keyCount() == 24);
    p.key(21).triggerClick(); // Am
    settle();
    p.key(0).triggerClick();  // C
    settle();
    CHECK(sink.last.keys == std::vector<std::string>{"Am", "C"});
    p.key(21).triggerClick(); // Am again: off
    settle();
    CHECK(sink.last.keys == std::vector<std::string>{"C"});
    SearchModel saved;
    saved.keys = {"Dm"};
    KeyPopover shown(saved, sink.fn());
    CHECK(shown.key(14).getToggleState()); // a saved pick shows lit
}

TEST_CASE("the Instrument popover lists the library's tags and ticks what is picked", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    SearchModel m;
    m.tags = {"kick"};
    InstrumentPopover p(m, {{"bass", 214}, {"kick", 188}, {"synth", 96}}, sink.fn());
    REQUIRE(p.tagCount() == 3);
    CHECK(p.tag(1).getToggleState());
    CHECK(p.countText(0) == "214");
    p.tag(0).triggerClick();
    settle();
    CHECK(sink.last.tags == std::vector<std::string>{"kick", "bass"}); // all of them must match
    p.tag(1).triggerClick();
    settle();
    CHECK(sink.last.tags == std::vector<std::string>{"bass"});
    InstrumentPopover empty({}, {}, sink.fn());
    CHECK(empty.tagCount() == 0);
    CHECK(empty.emptyText().isNotEmpty()); // says there are none yet
}

TEST_CASE("the Length popover takes a preset or a range", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    LengthPopover p({}, sink.fn());
    p.preset(1).triggerClick(); // 1-10 s
    settle();
    CHECK(sink.last.durationMin == 1.0);
    CHECK(sink.last.durationMax == 10.0);
    CHECK(p.preset(1).getToggleState());
    type(p.to(), "4");
    CHECK(sink.last.durationMax == 4.0);
    CHECK_FALSE(p.preset(1).getToggleState()); // no longer the preset
    p.preset(0).triggerClick(); // under 1 s
    settle();
    CHECK_FALSE(sink.last.durationMin);
    CHECK(sink.last.durationMax == 1.0);
}

TEST_CASE("the Rating popover sets a minimum, and the same star again clears it", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    RatingPopover p({}, sink.fn());
    p.star(2).triggerClick(); // three stars
    settle();
    CHECK(sink.last.minRating == 3);
    CHECK(p.star(2).getToggleState());
    CHECK_FALSE(p.star(3).getToggleState());
    p.star(2).triggerClick();
    settle();
    CHECK_FALSE(sink.last.minRating);
}

TEST_CASE("makeFilterPopover makes the right panel for each chip", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    for (const Facet f : kFacets) {
        auto p = makeFilterPopover(f, {}, {}, [](const SearchModel&) {});
        REQUIRE(p);
        CHECK(p->getWidth() > 0);
        CHECK(p->getHeight() > 0);
    }
    CHECK(dynamic_cast<KeyPopover*>(makeFilterPopover(Facet::Key, {}, {}, [](const SearchModel&) {}).get()));
}

TEST_CASE("a popover's title says how its picks combine, as the design does", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto none = [](const SearchModel&) {};
    CHECK(KeyPopover({}, none).hint() == "any of");
    CHECK(InstrumentPopover({}, {}, none).hint() == "all of");
    CHECK(RatingPopover({}, none).hint() == "at least");
    CHECK(TypePopover({}, none).hint().isEmpty());
    KeyPopover keys({}, none);
    CHECK(keys.key(0).getProperties()["asma.mono"]); // keys read as names, in the mono face
}

TEST_CASE("a range typed backwards means the same range", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    BpmPopover bpm({}, 0.0, sink.fn());
    type(bpm.from(), "130");
    type(bpm.to(), "120");
    CHECK(sink.last.bpmMin == 120.0); // not an empty range
    CHECK(sink.last.bpmMax == 130.0);
    LengthPopover length({}, sink.fn());
    type(length.from(), "10");
    type(length.to(), "2");
    CHECK(sink.last.durationMin == 2.0);
    CHECK(sink.last.durationMax == 10.0);
}

TEST_CASE("a library with many tags scrolls in the Instrument popover", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    std::vector<TagCount> many;
    for (int i = 0; i < 40; ++i) many.push_back({"tag" + std::to_string(i), 40 - i});
    InstrumentPopover p({}, many, [](const SearchModel&) {});
    InstrumentPopover eight({}, std::vector<TagCount>(many.begin(), many.begin() + 8), [](const SearchModel&) {});
    CHECK(p.getHeight() == eight.getHeight()); // no taller than eight rows
    CHECK(p.tagCount() == 40);                 // every tag is there to scroll to
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[popovers],[editor]"`

Expected: the build stops:

```
plugin/test_filter_popovers.cpp:2:10: fatal error: 'ui/FilterPopovers.h' file not found
plugin/test_editor.cpp:518:10: error: no type named 'KeyPopover' in namespace 'asma::app'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 439bcd2..b274d54 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -52,6 +52,12 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
     };
     addAndMakeVisible(sidebar_);
     chips_.onChange = [this](const SearchModel& model) { applySearch(model); };
+    chips_.onOpen = [this](Facet facet, juce::Component& anchor) {
+        auto popover = makeFilterPopover(facet, browser_.searchModel(), popoverContext(),
+                                         [this](const SearchModel& model) { applySearch(model); });
+        // Inside the editor, so a plugin's popover stays in its own window.
+        juce::CallOutBox::launchAsynchronously(std::move(popover), getLocalArea(&anchor, anchor.getLocalBounds()), this);
+    };
     addAndMakeVisible(chips_);
     if (processor_.isStandalone()) {
         top_.tempoBox().onValueChange = [this] {
@@ -325,6 +331,8 @@ void AsmaEditor::applySearch(const SearchModel& model)
     updateReadouts();
 }

+PopoverContext AsmaEditor::popoverContext() { return {library_.tagCounts(), processor_.tempoInForce()}; }
+
 void AsmaEditor::refreshSidebar()
 {
     entries_ = sidebarEntries(library_);
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index 6e1b955..81fa2b1 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -6,6 +6,7 @@
 #include "Sidebar.h"
 #include "ui/AsmaLookAndFeel.h"
 #include "ui/ChipRow.h"
+#include "ui/FilterPopovers.h"
 #include "ui/Footer.h"
 #include "ui/PreviewPanel.h"
 #include "ui/SidebarView.h"
@@ -51,6 +52,8 @@ public:
     TopBar& topBar() { return top_; }
     SidebarView& sidebar() { return sidebar_; }
     ChipRow& chipRow() { return chips_; }
+    // What a chip's popover needs: the library's tags and the tempo in force.
+    PopoverContext popoverContext();
     // The standalone's tempo source and folders; hidden in a plugin.
     juce::Button& linkToggle() { return top_.linkChip(); }
     TempoBox& bpmBox() { return top_.tempoBox(); }
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index beeca85..b23258e 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -117,6 +117,11 @@ std::vector<SavedSearch> LibraryView::savedSearches()
     return guarded([&] { return UserData(*db_).savedSearches(); }, std::vector<SavedSearch>{});
 }

+std::vector<TagCount> LibraryView::tagCounts()
+{
+    return guarded([&] { return asma::tagCounts(*db_); }, std::vector<TagCount>{});
+}
+
 std::int64_t LibraryView::problemCount()
 {
     return guarded(
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index 5ad1fa2..215f904 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -51,6 +51,8 @@ public:
     std::vector<Root> roots();
     std::vector<Collection> collections();
     std::vector<SavedSearch> savedSearches();
+    // The tags searches can find, most used first; empty unless open.
+    std::vector<TagCount> tagCounts();
     // Files in enabled folders that failed to decode or analyse: the
     // Problems entry's count. 0 unless open.
     std::int64_t problemCount();
```

In `plugin/src/ui/AsmaLookAndFeel.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.cpp b/plugin/src/ui/AsmaLookAndFeel.cpp
index ec73fb7..b5a0fa8 100644
--- a/plugin/src/ui/AsmaLookAndFeel.cpp
+++ b/plugin/src/ui/AsmaLookAndFeel.cpp
@@ -88,7 +88,8 @@ void AsmaLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button
     const bool accent = on && props["asma.accent"];
     const bool quiet = !on && (props["asma.quiet"] || button.getClickingTogglesState());
     const float size = props.contains("asma.size") ? static_cast<float>(props["asma.size"]) : 13.0f;
-    g.setFont(theme::font(accent ? theme::Face::SemiBold : theme::Face::Text, size));
+    const bool mono = props["asma.mono"];
+    g.setFont(theme::font(mono ? theme::Face::Mono : (accent ? theme::Face::SemiBold : theme::Face::Text), size));
     g.setColour(accent ? theme::ground : (quiet ? theme::muted : theme::text));
     g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
 }
@@ -125,6 +126,15 @@ void AsmaLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x,
     g.fillRoundedRectangle(thumb.toFloat().reduced(3.0f), 2.0f);
 }

+void AsmaLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
+{
+    // A popover: a raised panel with a hairline, no shadow.
+    g.setColour(theme::panel);
+    g.fillPath(path);
+    g.setColour(theme::border);
+    g.strokePath(path, juce::PathStrokeType(1.0f));
+}
+
 void AsmaLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
 {
     g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
```

In `plugin/src/ui/AsmaLookAndFeel.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.h b/plugin/src/ui/AsmaLookAndFeel.h
index e53b165..0e4ee34 100644
--- a/plugin/src/ui/AsmaLookAndFeel.h
+++ b/plugin/src/ui/AsmaLookAndFeel.h
@@ -8,7 +8,8 @@ namespace asma::app {
 // The Anode theme for JUCE's own widgets. Buttons read properties:
 // "asma.segment" ("first", "middle" or "last") draws one as part of a
 // segmented switch; "asma.accent" fills it amber when on; "asma.quiet" leaves
-// it transparent and muted when off; "asma.size" sets its text size (13).
+// it transparent and muted when off; "asma.size" sets its text size (13);
+// "asma.mono" draws its text in the mono face.
 // A toggling button is quiet when off.
 class AsmaLookAndFeel : public juce::LookAndFeel_V4 {
 public:
@@ -32,6 +33,10 @@ public:
     void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override;

+    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path,
+                                  juce::Image& cachedImage) override;
+    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 12; }
+
     void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
     void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
 };
```

Create `plugin/src/ui/FilterPopovers.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/FilterPopovers.h"

#include "TempoChip.h"
#include "ui/PreviewPanel.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace asma::app {

namespace {

constexpr int kPad = 14;
constexpr int kTitle = 22;
constexpr int kField = 28;

// Empty, or not a plain number, is no limit; never 0 by accident.
std::optional<double> number(const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty() || !t.containsOnly("0123456789.") || t.indexOfChar('.') != t.lastIndexOfChar('.')) return std::nullopt;
    return t.getDoubleValue();
}

juce::String shown(const std::optional<double>& value)
{
    return value ? juce::String::fromUTF8(bpmText(*value).c_str()) : juce::String();
}

void styleField(juce::TextEditor& field, const char* title)
{
    field.setTitle(title);
    field.setFont(theme::font(theme::Face::Mono, 13.0f));
    field.setInputRestrictions(7, "0123456789.");
    field.setIndents(8, 6);
}

void stylePill(juce::TextButton& button)
{
    button.getProperties().set("asma.accent", true);
    button.getProperties().set("asma.size", 11.0f);
}

// A tag to tick: a box, the name, how many samples carry it.
class TagRow final : public juce::Button {
public:
    TagRow(const juce::String& name, const juce::String& count) : juce::Button(name), count_(count)
    {
        setTitle(name);
        setClickingTogglesState(true);
    }
    const juce::String& count() const { return count_; }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const bool on = getToggleState();
        if (on || highlighted) {
            g.setColour(on ? theme::amber.withAlpha(0.08f) : theme::raised.withAlpha(0.6f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::kRadius);
        }
        auto area = getLocalBounds().reduced(6, 0);
        const auto box = area.removeFromLeft(14).withSizeKeepingCentre(14, 14).toFloat();
        if (on) {
            g.setColour(theme::amber);
            g.fillRoundedRectangle(box, 3.0f);
        } else {
            g.setColour(theme::muted);
            g.drawRoundedRectangle(box.reduced(0.75f), 3.0f, 1.5f);
        }
        area.removeFromLeft(10);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(theme::muted);
        g.drawText(count_, area, juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(40), juce::Justification::centredLeft, true);
    }

private:
    juce::String count_;
};

// One star of five: lit up to the minimum.
class Star final : public juce::Button {
public:
    explicit Star(int n) : juce::Button(juce::String(n) + (n == 1 ? " star" : " stars")) { setTitle(getName()); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        g.setFont(theme::font(theme::Face::Text, 22.0f));
        g.setColour(getToggleState() ? theme::amber : (highlighted ? theme::muted : theme::faint));
        g.drawText(juce::String::fromUTF8("★"), getLocalBounds(), juce::Justification::centred, false);
    }
};

} // namespace

FilterPopover::FilterPopover(const juce::String& title, const SearchModel& model, SearchChanged onChange,
                             const juce::String& hint)
    : model_(model), title_(title), hint_(hint), onChange_(std::move(onChange))
{
    setTitle(title);
}

void FilterPopover::changed()
{
    if (!onChange_) return;
    // A range typed backwards means the same range: 130 to 120 is 120 to 130.
    SearchModel reported = model_;
    if (reported.bpmMin && reported.bpmMax && *reported.bpmMin > *reported.bpmMax) std::swap(reported.bpmMin, reported.bpmMax);
    if (reported.durationMin && reported.durationMax && *reported.durationMin > *reported.durationMax)
        std::swap(reported.durationMin, reported.durationMax);
    onChange_(reported);
}

juce::Rectangle<int> FilterPopover::body() const { return getLocalBounds().reduced(kPad).withTrimmedTop(kTitle + 6); }

void FilterPopover::paint(juce::Graphics& g)
{
    auto line = getLocalBounds().reduced(kPad).removeFromTop(kTitle);
    const auto titleFont = theme::font(theme::Face::Heading, 13.0f);
    g.setFont(titleFont);
    g.setColour(theme::text);
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(titleFont, title_)));
    g.drawText(title_, line.removeFromLeft(w), juce::Justification::centredLeft, false);
    if (hint_.isEmpty()) return;
    line.removeFromLeft(6);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText(hint_, line, juce::Justification::centredLeft, false);
}

TypePopover::TypePopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Type", model, std::move(onChange)), choice_({"Any", "Loops", "One-shots"}, true)
{
    choice_.setSelected(static_cast<int>(model.type), juce::dontSendNotification);
    choice_.onChange = [this](int i) {
        model_.type = static_cast<SampleType>(i);
        changed();
    };
    addAndMakeVisible(choice_);
    setSize(300, 2 * kPad + kTitle + 6 + kField);
}

void TypePopover::resized() { choice_.setBounds(body().removeFromTop(kField)); }

BpmPopover::BpmPopover(const SearchModel& model, double tempo, SearchChanged onChange)
    : FilterPopover("BPM", model, std::move(onChange)), tempo_(tempo)
{
    styleField(from_, "From BPM");
    styleField(to_, "To BPM");
    from_.setText(shown(model.bpmMin), false);
    to_.setText(shown(model.bpmMax), false);
    from_.onTextChange = [this] {
        model_.bpmMin = number(from_.getText());
        changed();
    };
    to_.onTextChange = [this] {
        model_.bpmMax = number(to_.getText());
        changed();
    };
    for (auto* b : {&near120_, &nearTempo_}) {
        b->getProperties().set("asma.quiet", true);
        b->getProperties().set("asma.size", 11.0f);
    }
    near120_.onClick = [this] {
        const auto r = bpmNear(120.0);
        setRange(r.first, r.second);
    };
    nearTempo_.setEnabled(tempo > 0.0);
    nearTempo_.onClick = [this] {
        const auto r = bpmNear(tempo_);
        setRange(r.first, r.second);
    };
    for (juce::Component* c : {static_cast<juce::Component*>(&from_), static_cast<juce::Component*>(&to_),
                               static_cast<juce::Component*>(&near120_), static_cast<juce::Component*>(&nearTempo_)})
        addAndMakeVisible(c);
    setSize(300, 2 * kPad + kTitle + 6 + kField + 10 + 24);
}

void BpmPopover::setRange(double lo, double hi)
{
    model_.bpmMin = lo;
    model_.bpmMax = hi;
    from_.setText(shown(model_.bpmMin), false);
    to_.setText(shown(model_.bpmMax), false);
    changed();
}

void BpmPopover::resized()
{
    auto area = body();
    auto fields = area.removeFromTop(kField);
    from_.setBounds(fields.removeFromLeft(90));
    fields.removeFromLeft(24);
    to_.setBounds(fields.removeFromLeft(90));
    area.removeFromTop(10);
    auto pills = area.removeFromTop(24);
    near120_.setBounds(pills.removeFromLeft(84));
    pills.removeFromLeft(6);
    nearTempo_.setBounds(pills.removeFromLeft(118));
}

KeyPopover::KeyPopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Key", model, std::move(onChange), "any of")
{
    const auto& names = PreviewPanel::keys();
    for (int i = 0; i < names.size(); ++i) {
        auto* b = keys_.add(new juce::TextButton(names[i]));
        b->setClickingTogglesState(true);
        b->getProperties().set("asma.accent", true);
        b->getProperties().set("asma.size", 12.0f);
        b->getProperties().set("asma.mono", true);
        b->setToggleState(std::find(model.keys.begin(), model.keys.end(), names[i].toStdString()) != model.keys.end(),
                          juce::dontSendNotification);
        b->onClick = [this, i, b] {
            const std::string key = PreviewPanel::keys()[i].toStdString();
            auto& keys = model_.keys;
            keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
            if (b->getToggleState()) keys.push_back(key); // in the order picked
            changed();
        };
        addAndMakeVisible(b);
    }
    setSize(360, 2 * kPad + kTitle + 6 + 4 * kField + 3 * 4);
}

void KeyPopover::resized()
{
    const auto area = body();
    const int w = (area.getWidth() - 5 * 4) / 6;
    for (int i = 0; i < keys_.size(); ++i)
        keys_[i]->setBounds(area.getX() + (i % 6) * (w + 4), area.getY() + (i / 6) * (kField + 4), w, kField);
}

InstrumentPopover::InstrumentPopover(const SearchModel& model, std::vector<TagCount> tags, SearchChanged onChange)
    : FilterPopover("Instrument", model, std::move(onChange), "all of"), counts_(std::move(tags))
{
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        const auto& t = counts_[i];
        auto* row = tags_.add(new TagRow(juce::String::fromUTF8(t.name.c_str()), juce::String(t.count)));
        row->setToggleState(std::find(model.tags.begin(), model.tags.end(), t.name) != model.tags.end(),
                            juce::dontSendNotification);
        row->onClick = [this, i, row] {
            const std::string& name = counts_[i].name;
            auto& picked = model_.tags;
            picked.erase(std::remove(picked.begin(), picked.end(), name), picked.end());
            if (row->getToggleState()) picked.push_back(name);
            changed();
        };
        list_.addAndMakeVisible(row);
    }
    if (counts_.empty()) empty_ = "No instrument tags yet: they come from file names when a folder is scanned.";
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    const int rows = std::clamp(static_cast<int>(counts_.size()), 1, 8);
    setSize(300, 2 * kPad + kTitle + 6 + rows * 28);
}

juce::String InstrumentPopover::countText(int index) const
{
    return index >= 0 && index < tags_.size() ? static_cast<const TagRow*>(tags_[index])->count() : juce::String();
}

void InstrumentPopover::paint(juce::Graphics& g)
{
    FilterPopover::paint(g);
    if (empty_.isEmpty()) return;
    g.setFont(theme::font(theme::Face::Text, 12.0f));
    g.setColour(theme::muted);
    g.drawFittedText(empty_, body(), juce::Justification::topLeft, 2);
}

void InstrumentPopover::resized()
{
    viewport_.setBounds(body());
    const int w = viewport_.getWidth() - (tags_.size() > 8 ? viewport_.getScrollBarThickness() : 0);
    for (int i = 0; i < tags_.size(); ++i) tags_[i]->setBounds(0, i * 28, w, 26);
    list_.setSize(w, tags_.size() * 28);
}

LengthPopover::LengthPopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Length", model, std::move(onChange))
{
    for (std::size_t i = 0; i < kLengthPresets.size(); ++i) {
        auto* b = presets_.add(new juce::TextButton(juce::String::fromUTF8(kLengthPresets[i].label)));
        stylePill(*b);
        b->onClick = [this, i] {
            model_.durationMin = kLengthPresets[i].min;
            model_.durationMax = kLengthPresets[i].max;
            from_.setText(shown(model_.durationMin), false);
            to_.setText(shown(model_.durationMax), false);
            showPreset();
            changed();
        };
        addAndMakeVisible(b);
    }
    styleField(from_, "From seconds");
    styleField(to_, "To seconds");
    from_.setText(shown(model.durationMin), false);
    to_.setText(shown(model.durationMax), false);
    from_.onTextChange = [this] {
        model_.durationMin = number(from_.getText());
        showPreset();
        changed();
    };
    to_.onTextChange = [this] {
        model_.durationMax = number(to_.getText());
        showPreset();
        changed();
    };
    addAndMakeVisible(from_);
    addAndMakeVisible(to_);
    showPreset();
    setSize(360, 2 * kPad + kTitle + 6 + 24 + 10 + kField);
}

void LengthPopover::showPreset()
{
    for (std::size_t i = 0; i < kLengthPresets.size(); ++i)
        presets_[static_cast<int>(i)]->setToggleState(
            kLengthPresets[i].min == model_.durationMin && kLengthPresets[i].max == model_.durationMax,
            juce::dontSendNotification);
}

void LengthPopover::resized()
{
    auto area = body();
    auto pills = area.removeFromTop(24);
    for (auto* b : presets_) {
        const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(theme::font(theme::Face::SemiBold, 11.0f),
                                                                                         b->getButtonText())))
                    + 20;
        b->setBounds(pills.removeFromLeft(w));
        pills.removeFromLeft(6);
    }
    area.removeFromTop(10);
    auto fields = area.removeFromTop(kField);
    from_.setBounds(fields.removeFromLeft(90));
    fields.removeFromLeft(24);
    to_.setBounds(fields.removeFromLeft(90));
}

RatingPopover::RatingPopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Rating", model, std::move(onChange), "at least")
{
    for (int n = 1; n <= 5; ++n) {
        auto* s = stars_.add(new Star(n));
        s->onClick = [this, n] {
            if (model_.minRating == n) model_.minRating.reset(); // the same star again: no minimum
            else model_.minRating = n;
            show();
            changed();
        };
        addAndMakeVisible(s);
    }
    show();
    setSize(300, 2 * kPad + kTitle + 6 + 34);
}

void RatingPopover::show()
{
    for (int i = 0; i < stars_.size(); ++i)
        stars_[i]->setToggleState(model_.minRating && i < *model_.minRating, juce::dontSendNotification);
}

void RatingPopover::resized()
{
    auto area = body().removeFromTop(34);
    for (auto* s : stars_) {
        s->setBounds(area.removeFromLeft(34));
        area.removeFromLeft(4);
    }
}

std::unique_ptr<FilterPopover> makeFilterPopover(Facet facet, const SearchModel& model, const PopoverContext& context,
                                                 SearchChanged onChange)
{
    switch (facet) {
    case Facet::Type: return std::make_unique<TypePopover>(model, std::move(onChange));
    case Facet::Bpm: return std::make_unique<BpmPopover>(model, context.tempo, std::move(onChange));
    case Facet::Key: return std::make_unique<KeyPopover>(model, std::move(onChange));
    case Facet::Instrument: return std::make_unique<InstrumentPopover>(model, context.tags, std::move(onChange));
    case Facet::Length: return std::make_unique<LengthPopover>(model, std::move(onChange));
    case Facet::Rating: return std::make_unique<RatingPopover>(model, std::move(onChange));
    }
    return nullptr;
}

} // namespace asma::app
```

Create `plugin/src/ui/FilterPopovers.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Filters.h"
#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace asma::app {

using SearchChanged = std::function<void(const SearchModel&)>;

// A chip's panel: it edits its own copy of the search and reports every
// change at once; there is no OK button.
class FilterPopover : public juce::Component {
public:
    // hint: how the picks combine ("any of"), drawn after the title.
    FilterPopover(const juce::String& title, const SearchModel& model, SearchChanged onChange,
                  const juce::String& hint = {});
    const juce::String& hint() const { return hint_; }
    void paint(juce::Graphics& g) override;

protected:
    void changed(); // reports model_
    juce::Rectangle<int> body() const; // below the title
    SearchModel model_;

private:
    juce::String title_, hint_;
    SearchChanged onChange_;
};

class TypePopover : public FilterPopover {
public:
    TypePopover(const SearchModel& model, SearchChanged onChange);
    SegmentedControl& choice() { return choice_; }
    void resized() override;

private:
    SegmentedControl choice_;
};

class BpmPopover : public FilterPopover {
public:
    // tempo: the tempo in force, for "near the tempo"; 0 when there is none.
    BpmPopover(const SearchModel& model, double tempo, SearchChanged onChange);
    juce::TextEditor& from() { return from_; }
    juce::TextEditor& to() { return to_; }
    juce::Button& near120() { return near120_; }
    juce::Button& nearTempo() { return nearTempo_; }
    void resized() override;

private:
    void setRange(double lo, double hi);
    juce::TextEditor from_, to_;
    juce::TextButton near120_{"Near 120"}, nearTempo_{"Near the tempo"};
    double tempo_;
};

class KeyPopover : public FilterPopover {
public:
    KeyPopover(const SearchModel& model, SearchChanged onChange);
    int keyCount() const { return keys_.size(); }
    juce::Button& key(int index) { return *keys_[index]; }
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> keys_;
};

class InstrumentPopover : public FilterPopover {
public:
    InstrumentPopover(const SearchModel& model, std::vector<TagCount> tags, SearchChanged onChange);
    int tagCount() const { return tags_.size(); }
    juce::Button& tag(int index) { return *tags_[index]; }
    juce::String countText(int index) const;
    juce::String emptyText() const { return empty_; }
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    std::vector<TagCount> counts_;
    juce::OwnedArray<juce::Button> tags_;
    juce::Viewport viewport_;
    juce::Component list_;
    juce::String empty_;
};

class LengthPopover : public FilterPopover {
public:
    LengthPopover(const SearchModel& model, SearchChanged onChange);
    juce::Button& preset(int index) { return *presets_[index]; }
    juce::TextEditor& from() { return from_; }
    juce::TextEditor& to() { return to_; }
    void resized() override;

private:
    void showPreset();
    juce::OwnedArray<juce::TextButton> presets_;
    juce::TextEditor from_, to_;
};

class RatingPopover : public FilterPopover {
public:
    RatingPopover(const SearchModel& model, SearchChanged onChange);
    juce::Button& star(int index) { return *stars_[index]; }
    void resized() override;

private:
    void show();
    juce::OwnedArray<juce::Button> stars_;
};

// What a popover needs from outside the search.
struct PopoverContext {
    std::vector<TagCount> tags; // the library's, most used first
    double tempo = 0.0;         // in force; 0 when none
};

// The panel for a chip, sized to its content.
std::unique_ptr<FilterPopover> makeFilterPopover(Facet facet, const SearchModel& model, const PopoverContext& context,
                                                 SearchChanged onChange);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[popovers],[editor]"`

Expected: `All tests passed (205 assertions in 36 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/ui/AsmaLookAndFeel.cpp plugin/src/ui/AsmaLookAndFeel.h plugin/src/ui/FilterPopovers.cpp plugin/src/ui/FilterPopovers.h tests/plugin/test_editor.cpp tests/plugin/test_filter_popovers.cpp
git commit -m "app: a popover for each filter chip, applying as it changes"
```

---

### Task 12: What sounds like the selection

Spec section 9 (Browsing: Similar). The library read (nearest first, distance 1
minus the similarity, or why there are none) and the list component, which shows
only the rows that fit whole.

**Files:**

- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Create: `plugin/src/ui/SimilarView.cpp`
- Create: `plugin/src/ui/SimilarView.h`
- Create: `tests/plugin/test_similar.cpp` (test)

**Interfaces:**

- Consumes: `findSimilar`, `rowsForIds`.
- Produces:
  `struct SimilarResult { enum class State { Ok, NotAnalysed, Failed }; struct Match { SearchRow row; double distance; }; State state; std::vector<Match> matches; }`;
  `LibraryView::similar(std::int64_t, int limit = 10)`; `class SimilarView`
  (`setResult`, `clear()`, `onPick(const SearchRow&)`, `message()`,
  `rowCount()`, `row(int)`, `distanceText(int)`).

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_similar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFixture.h"
#include "LibraryView.h"
#include "asma/core/Analyser.h"
#include "ui/SimilarView.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::LibraryView;
using app::SimilarResult;
using app::SimilarView;
namespace fs = std::filesystem;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

std::int64_t idOf(const test::LibraryFixture& f, const fs::path& file)
{
    Db db = Db::open(f.dbPath);
    return Library(db).fileByAbsolutePath(file)->id;
}

} // namespace

TEST_CASE("the library lists what sounds like a sample, nearest first", "[similar]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        analysePending(db); // sound profiles
    }
    LibraryView library(f.dbPath);
    library.refresh();
    const SimilarResult r = library.similar(idOf(f, f.kick));
    CHECK(r.state == SimilarResult::State::Ok);
    REQUIRE(r.matches.size() == 2); // everything else in a library of three
    CHECK(r.matches[0].distance <= r.matches[1].distance);
    CHECK(r.matches[0].distance >= 0.0);
    CHECK(r.matches[0].row.name != "Kick_01.wav"); // never itself
}

TEST_CASE("a sample not analysed yet has no similar list, and says so", "[similar]")
{
    test::LibraryFixture f;
    f.scan(); // scanned, not analysed
    LibraryView library(f.dbPath);
    library.refresh();
    CHECK(library.similar(idOf(f, f.kick)).state == SimilarResult::State::NotAnalysed);
}

TEST_CASE("the Similar list shows its matches, or why it has none", "[similar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SimilarView view;
    view.setBounds(0, 0, 284, 235);
    CHECK(view.message() == "Select a sample");
    SimilarResult r;
    r.state = SimilarResult::State::NotAnalysed;
    view.setResult(r);
    CHECK(view.message() == "Not analysed yet");
    r.state = SimilarResult::State::Failed;
    view.setResult(r);
    CHECK(view.message() == "No similar samples");
    r.state = SimilarResult::State::Ok;
    SearchRow a;
    a.id = 7;
    a.name = "Bass_Loop_Am_118.wav";
    r.matches = {{a, 0.08}};
    view.setResult(r);
    CHECK(view.message().isEmpty());
    REQUIRE(view.rowCount() == 1);
    CHECK(view.row(0).getButtonText() == "Bass_Loop_Am_118.wav");
    CHECK(view.distanceText(0) == "0.08");
    std::int64_t picked = 0;
    view.onPick = [&](const SearchRow& row) { picked = row.id; };
    view.row(0).triggerClick();
    settle();
    CHECK(picked == 7);
    view.clear();
    CHECK(view.message() == "Select a sample");
}

TEST_CASE("the Similar list shows only the rows that fit whole", "[similar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SimilarView view;
    view.setBounds(0, 0, 284, 235);
    SimilarResult r;
    r.state = SimilarResult::State::Ok;
    for (int i = 0; i < 10; ++i) {
        SearchRow row;
        row.id = i + 1;
        row.name = "Sample_" + std::to_string(i) + ".wav";
        r.matches.push_back({row, 0.1 * i});
    }
    view.setResult(r);
    for (int i = 0; i < view.rowCount(); ++i)
        if (view.row(i).isVisible()) CHECK(view.getLocalBounds().contains(view.row(i).getBounds()));
    CHECK(view.row(0).isVisible());
    CHECK_FALSE(view.row(9).isVisible()); // ten do not fit in the panel
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[similar]"`

Expected: the build stops:

```
plugin/test_similar.cpp:5:10: fatal error: 'ui/SimilarView.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index b23258e..f68e4d1 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -5,6 +5,7 @@
 #include "asma/core/Fs.h"
 #include "asma/core/Library.h"
 #include "asma/core/Schema.h"
+#include "asma/core/Similar.h"

 namespace asma::app {

@@ -117,6 +118,30 @@ std::vector<SavedSearch> LibraryView::savedSearches()
     return guarded([&] { return UserData(*db_).savedSearches(); }, std::vector<SavedSearch>{});
 }

+SimilarResult LibraryView::similar(std::int64_t fileId, int limit)
+{
+    return guarded(
+        [&] {
+            SimilarResult out;
+            auto analysed = db_->prepare("SELECT feature_vector IS NOT NULL FROM features WHERE file_id = ?");
+            analysed.bind(1, fileId);
+            if (!analysed.step() || analysed.getInt(0) == 0) {
+                out.state = SimilarResult::State::NotAnalysed;
+                return out;
+            }
+            const auto found = findSimilar(*db_, fileId, limit);
+            std::vector<std::int64_t> ids;
+            for (const auto& m : found) ids.push_back(m.id);
+            const auto rows = rowsForIds(*db_, ids); // in the order asked
+            for (const auto& row : rows)
+                for (const auto& m : found)
+                    if (m.id == row.id) out.matches.push_back({row, 1.0 - m.similarity});
+            out.state = SimilarResult::State::Ok;
+            return out;
+        },
+        SimilarResult{});
+}
+
 std::vector<TagCount> LibraryView::tagCounts()
 {
     return guarded([&] { return asma::tagCounts(*db_); }, std::vector<TagCount>{});
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index 215f904..dc9a9ea 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -27,6 +27,21 @@ enum class LibraryState {
 // The library as the UI sees it: opened read-only (so it works inside a
 // host), never created, and watched for other processes' writes. Message
 // thread only.
+// The samples that sound most like one, nearest first.
+struct SimilarResult {
+    enum class State {
+        Ok,          // matches, possibly none
+        NotAnalysed, // the sample has no sound profile yet
+        Failed,      // the query failed, or the library is not open
+    };
+    struct Match {
+        SearchRow row;
+        double distance = 0.0; // 0 sounds the same; 1 minus the similarity
+    };
+    State state = State::Failed;
+    std::vector<Match> matches;
+};
+
 class LibraryView {
 public:
     // ReadOnly inside a host. The standalone may migrate an older library
@@ -51,6 +66,8 @@ public:
     std::vector<Root> roots();
     std::vector<Collection> collections();
     std::vector<SavedSearch> savedSearches();
+    // What sounds like the file, from analysed sound profiles.
+    SimilarResult similar(std::int64_t fileId, int limit = 10);
     // The tags searches can find, most used first; empty unless open.
     std::vector<TagCount> tagCounts();
     // Files in enabled folders that failed to decode or analyse: the
```

Create `plugin/src/ui/SimilarView.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SimilarView.h"

#include "TempoChip.h"
#include "ui/Theme.h"

namespace asma::app {

namespace {

constexpr int kRowHeight = 24;
constexpr int kTop = 12;
constexpr int kHeading = 20;

class MatchRow final : public juce::Button {
public:
    MatchRow(const juce::String& name, const juce::String& distance) : juce::Button(name), distance_(distance)
    {
        setTitle(name);
        setDescription("distance " + distance);
    }
    const juce::String& distance() const { return distance_; }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down) {
            g.setColour(theme::raised);
            g.fillRect(getLocalBounds());
        }
        auto area = getLocalBounds().reduced(14, 0);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(theme::muted);
        g.drawText(distance_, area.removeFromRight(36), juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(8), juce::Justification::centredLeft, true);
    }

private:
    juce::String distance_;
};

} // namespace

SimilarView::SimilarView() { clear(); }

void SimilarView::clear()
{
    matches_.clear();
    rows_.clear();
    message_ = "Select a sample";
    repaint();
}

void SimilarView::setResult(const SimilarResult& result)
{
    matches_ = result.matches;
    rows_.clear();
    switch (result.state) {
    case SimilarResult::State::NotAnalysed: message_ = "Not analysed yet"; break;
    case SimilarResult::State::Failed: message_ = "No similar samples"; break;
    case SimilarResult::State::Ok: message_ = matches_.empty() ? "No similar samples" : juce::String(); break;
    }
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        const auto& m = matches_[i];
        auto* row = rows_.add(new MatchRow(juce::String::fromUTF8(m.row.name.c_str()),
                                           juce::String::fromUTF8(ratioText(m.distance).c_str())));
        row->onClick = [this, i] {
            if (onPick && i < matches_.size()) onPick(matches_[i].row);
        };
        addAndMakeVisible(row);
    }
    resized();
    repaint();
}

juce::String SimilarView::distanceText(int index) const
{
    return index >= 0 && index < rows_.size() ? static_cast<const MatchRow*>(rows_[index])->distance() : juce::String();
}

void SimilarView::paint(juce::Graphics& g)
{
    g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.08f));
    g.setColour(theme::muted);
    g.drawText("SIMILAR", 14, kTop, getWidth() - 28, kHeading - 4, juce::Justification::centredLeft, false);
    if (message_.isNotEmpty()) {
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.drawText(message_, 14, kTop + kHeading, getWidth() - 28, kRowHeight, juce::Justification::centredLeft, true);
    }
}

void SimilarView::resized()
{
    // As many as fit whole; the rest wait for a taller window.
    int y = kTop + kHeading;
    for (auto* r : rows_) {
        r->setBounds(0, y, getWidth(), kRowHeight);
        r->setVisible(y + kRowHeight <= getHeight());
        y += kRowHeight;
    }
}

} // namespace asma::app
```

Create `plugin/src/ui/SimilarView.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// The bottom panel's right side: the samples nearest the selection by sound,
// with their distance, or why there are none.
class SimilarView : public juce::Component {
public:
    SimilarView();

    void setResult(const SimilarResult& result);
    // Nothing selected.
    void clear();

    std::function<void(const SearchRow&)> onPick;

    juce::String message() const { return message_; }
    int rowCount() const { return rows_.size(); }
    juce::Button& row(int index) { return *rows_[index]; }
    juce::String distanceText(int index) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    std::vector<SimilarResult::Match> matches_;
    juce::OwnedArray<juce::Button> rows_;
    juce::String message_;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[similar]"`

Expected: `All tests passed (25 assertions in 4 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/ui/SimilarView.cpp plugin/src/ui/SimilarView.h tests/plugin/test_similar.cpp
git commit -m "app: what sounds like the selection, nearest first, or why nothing does"
```

---

### Task 13: The Similar list beside the preview

Spec section 9 (Browsing: Similar). The editor holds the selection as a row, not
a table index, so a Similar pick the search does not show can be the selection,
playing and described by the preview and footer, and survives a library check.
The list refreshes when the selection changes.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `tests/plugin/test_editor.cpp` (test)

**Interfaces:**

- Consumes: task 12.
- Produces: `AsmaEditor::similar()`, `pickSimilar(const SearchRow&)`,
  `select(const SearchRow&)`; the editor's `selected_`
  (`std::optional<SearchRow>`) replaces `selectedRow_`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index 283fd0e..70a4c35 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -2,6 +2,7 @@
 #include "AsmaEditor.h"
 #include "LibraryFixture.h"
 #include "PluginTestUtil.h"
+#include "asma/core/Analyser.h"
 #include "asma/core/UserData.h"
 #include "asma/audio/Render.h"
 #include "asma/core/Fs.h"
@@ -535,3 +536,64 @@ TEST_CASE("the Instrument popover lists the library's tags", "[editor]")
     REQUIRE_FALSE(tags.empty());
     CHECK(tags.front().count >= 1);
 }
+
+namespace {
+
+// An editor over an analysed library, so Similar has sound profiles.
+struct AnalysedRig : EditorRig {
+    AnalysedRig()
+    {
+        {
+            Db db = Db::open(f.dbPath);
+            analysePending(db);
+        }
+        editor->poll();
+    }
+};
+
+} // namespace
+
+TEST_CASE("picking a similar sample auditions it, and the table follows when it shows it", "[editor]")
+{
+    AnalysedRig rig;
+    rig.type("");
+    rig.editor->table().selectRow(rig.editor->table().getNumRows() - 1); // whatever is last
+    auto& similar = rig.editor->similar();
+    REQUIRE(similar.rowCount() == 2);
+    const juce::String name = similar.row(0).getButtonText();
+    similar.row(0).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(fromUtf8(rig.p->pluginState().selected).filename().string() == name.toStdString());
+    const int row = rig.editor->table().getSelectedRow();
+    REQUIRE(row >= 0);
+    CHECK(rig.editor->table().getNumRows() == 3);
+    CHECK(rig.p->engine().selected() > 0);
+}
+
+TEST_CASE("a similar sample the search does not show still plays, without a table row", "[editor]")
+{
+    AnalysedRig rig;
+    rig.type("kick");
+    rig.editor->table().selectRow(0);
+    auto& similar = rig.editor->similar();
+    REQUIRE(similar.rowCount() == 2);
+    similar.row(0).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.editor->table().getSelectedRow() < 0); // not in the search
+    const auto picked = fromUtf8(rig.p->pluginState().selected);
+    CHECK_FALSE(fs::equivalent(picked, rig.f.kick));
+    REQUIRE(rig.playing());
+    rig.editor->poll(); // a library check does not drop it
+    CHECK(fromUtf8(rig.p->pluginState().selected) == picked);
+    CHECK(rig.editor->footer().dragText().isNotEmpty()); // the preview and footer describe it
+}
+
+TEST_CASE("the Similar list says when nothing is selected or analysed", "[editor]")
+{
+    EditorRig rig; // scanned, not analysed
+    CHECK(rig.editor->similar().message() == "Select a sample");
+    rig.type("kick");
+    rig.editor->table().selectRow(0);
+    rig.editor->poll();
+    CHECK(rig.editor->similar().message() == "Not analysed yet");
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[editor]"`

Expected: the build stops:

```
plugin/test_editor.cpp:561:33: error: no member named 'similar' in 'asma::app::AsmaEditor'
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index b274d54..742c6f6 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -52,6 +52,8 @@ AsmaEditor::AsmaEditor(AsmaProcessor& owner)
     };
     addAndMakeVisible(sidebar_);
     chips_.onChange = [this](const SearchModel& model) { applySearch(model); };
+    similar_.onPick = [this](const SearchRow& row) { pickSimilar(row); };
+    addAndMakeVisible(similar_);
     chips_.onOpen = [this](Facet facet, juce::Component& anchor) {
         auto popover = makeFilterPopover(facet, browser_.searchModel(), popoverContext(),
                                          [this](const SearchModel& model) { applySearch(model); });
@@ -216,7 +218,7 @@ void AsmaEditor::resized()
     footer_.setBounds(area.removeFromBottom(theme::kFooterHeight));
     auto bottom = area.removeFromBottom(theme::kPreviewHeight);
     bottom.removeFromTop(1);
-    bottom.removeFromRight(theme::kSimilarWidth);
+    similar_.setBounds(bottom.removeFromRight(theme::kSimilarWidth).withTrimmedLeft(1));
     preview_.setBounds(bottom);
     sidebar_.setBounds(area.removeFromLeft(theme::kSidebarWidth));
     chips_.setBounds(area.removeFromTop(theme::kChipRowHeight));
@@ -363,13 +365,47 @@ void AsmaEditor::showSelection()

 void AsmaEditor::selectionChanged()
 {
-    selectedRow_ = table_.getSelectedRow();
-    selectedInfo_ = selectedRow_ >= 0 ? browser_.info(selectedRow_) : audio::SampleInfo{};
-    selectedFolder_.clear();
-    if (const SearchRow* r = browser_.row(selectedRow_)) {
-        const auto slash = r->relPath.find_last_of('/');
-        if (slash != std::string::npos) selectedFolder_ = r->relPath.substr(0, slash);
+    if (const SearchRow* r = browser_.row(table_.getSelectedRow())) {
+        selected_ = *r;
+    } else {
+        // No row: keep a Similar pick the search does not show, as long as it
+        // is still the project's selection.
+        const bool kept = selected_ && toUtf8(LibraryView::pathOf(*selected_)) == processor_.pluginState().selected;
+        if (!kept) selected_.reset();
     }
+    selectedInfo_ = selected_ ? library_.info(selected_->id) : audio::SampleInfo{};
+    selectedFolder_.clear();
+    if (selected_) {
+        const auto slash = selected_->relPath.find_last_of('/');
+        if (slash != std::string::npos) selectedFolder_ = selected_->relPath.substr(0, slash);
+    }
+    const std::int64_t id = selected_ ? selected_->id : 0;
+    if (id != similarFor_) {
+        similarFor_ = id;
+        if (id) similar_.setResult(library_.similar(id));
+        else similar_.clear();
+    }
+}
+
+void AsmaEditor::select(const SearchRow& row)
+{
+    scanMessage_.clear();
+    processor_.select(LibraryView::pathOf(row), library_.info(row.id));
+    preview_.setEdits({}); // a new selection plays as it is
+}
+
+void AsmaEditor::pickSimilar(const SearchRow& row)
+{
+    select(row);
+    selected_ = row;
+    const int at = browser_.rowOf(LibraryView::pathOf(row));
+    {
+        const juce::ScopedValueSetter quiet(quietSelection_, true); // already playing
+        if (at >= 0) table_.selectRow(at);
+        else table_.deselectAllRows();
+    }
+    selectionChanged();
+    updateReadouts();
 }

 void AsmaEditor::updateReadouts()
@@ -404,8 +440,8 @@ void AsmaEditor::updateReadouts()
     emptyAddFolder_.setVisible(standalone && nothing);

     // The preview.
-    if (const SearchRow* row = browser_.row(selectedRow_)) {
-        const SearchRow& r = *row;
+    if (selected_) {
+        const SearchRow& r = *selected_;
         const auto overview = processor_.engine().overview();
         preview_.waveform().setOverview(overview);
         preview_.setFile(utf8(r.name), overview ? PreviewPanel::fileLine(selectedFolder_, overview->sampleRate,
@@ -419,7 +455,7 @@ void AsmaEditor::updateReadouts()
     preview_.setPlaying(status.playing);
     audio::SyncSettings sync = state.sync;
     sync.hostBpm = processor_.tempoInForce();
-    preview_.setTempo(state.sync.tempo, selectedRow_ >= 0 ? tempoChip(selectedInfo_, sync, current && status.failed)
+    preview_.setTempo(state.sync.tempo, selected_ ? tempoChip(selectedInfo_, sync, current && status.failed)
                                                           : ChipText{state.sync.tempo ? "" : "off", Tone::Muted});
     std::string keyStatus;
     if (current && status.keyUnsure) keyStatus = "?";
@@ -428,7 +464,7 @@ void AsmaEditor::updateReadouts()
     preview_.setKey(state.sync.key, std::string(state.sync.projectKey.view()), keyStatus);

     // The footer.
-    if (selectedRow_ >= 0) {
+    if (selected_) {
         const audio::RenderSettings drag =
             dragSettings(state.edits, audio::planSync(selectedInfo_, sync), static_cast<int>(processor_.sampleRate()));
         footer_.setDrag(utf8(dragSummary(drag, selectedInfo_.bpm)));
@@ -515,10 +551,8 @@ void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, in
 void AsmaEditor::selectedRowsChanged(int lastRowSelected)
 {
     selectionChanged();
-    if (quietSelection_ || lastRowSelected < 0) return;
-    scanMessage_.clear();
-    processor_.select(browser_.path(lastRowSelected), selectedInfo_);
-    preview_.setEdits({}); // a new selection plays as it is
+    if (quietSelection_ || lastRowSelected < 0 || !selected_) return;
+    select(*selected_);
     updateReadouts();
 }

@@ -532,18 +566,17 @@ juce::var AsmaEditor::getDragSourceDescription(const juce::SparseSet<int>& rows)
 bool AsmaEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails&,
                                                       juce::StringArray& files, bool& canMoveFiles)
 {
-    const int row = table_.getSelectedRow();
-    if (row < 0) return false;
+    if (!selected_) return false;
     canMoveFiles = false;
-    const auto path = browser_.path(row);
+    const auto path = LibraryView::pathOf(*selected_);
     const PluginState state = processor_.pluginState();
     audio::SyncSettings sync = state.sync;
     sync.hostBpm = processor_.tempoInForce();
     const audio::RenderSettings settings =
-        dragSettings(state.edits, audio::planSync(browser_.info(row), sync), static_cast<int>(processor_.sampleRate()));
+        dragSettings(state.edits, audio::planSync(selectedInfo_, sync), static_cast<int>(processor_.sampleRate()));
     std::filesystem::path file = path;
     try {
-        file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, browser_.contentHash(row));
+        file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, library_.contentHash(selected_->id));
     } catch (const std::exception&) {
         // A render that fails still leaves the original to drag.
     }
```

In `plugin/src/AsmaEditor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.h b/plugin/src/AsmaEditor.h
index 81fa2b1..e86e280 100644
--- a/plugin/src/AsmaEditor.h
+++ b/plugin/src/AsmaEditor.h
@@ -10,6 +10,7 @@
 #include "ui/Footer.h"
 #include "ui/PreviewPanel.h"
 #include "ui/SidebarView.h"
+#include "ui/SimilarView.h"
 #include "ui/TopBar.h"

 #include <juce_audio_processors/juce_audio_processors.h>
@@ -52,6 +53,7 @@ public:
     TopBar& topBar() { return top_; }
     SidebarView& sidebar() { return sidebar_; }
     ChipRow& chipRow() { return chips_; }
+    SimilarView& similar() { return similar_; }
     // What a chip's popover needs: the library's tags and the tempo in force.
     PopoverContext popoverContext();
     // The standalone's tempo source and folders; hidden in a plugin.
@@ -87,6 +89,8 @@ private:
     void showSelection();   // selects the saved file's row without playing it
     void loadState();       // every control from the processor's state
     void selectionChanged(); // re-reads what the readouts need about the selection
+    void pickSimilar(const SearchRow& row);
+    void select(const SearchRow& row); // auditions it as the selection, as it is
     void updateReadouts();  // the preview, the chips, the footer, the empty state
     void updateRenderSize();

@@ -97,6 +101,7 @@ private:
     TopBar top_;
     SidebarView sidebar_;
     ChipRow chips_;
+    SimilarView similar_;
     std::vector<SidebarEntry> entries_; // what the sidebar lists
     juce::TableListBox table_{"Samples", this};
     juce::Label empty_;
@@ -110,7 +115,8 @@ private:
     std::uint64_t loadedStates_ = 0; // the processor's stateLoads() the controls show
     int ticks_ = 0;
     // What the readouts know about the selection.
-    int selectedRow_ = -1;
+    std::optional<SearchRow> selected_; // the selection, in the table or not (a Similar pick)
+    std::int64_t similarFor_ = 0;       // the sample the Similar list is about
     audio::SampleInfo selectedInfo_;
     std::string selectedFolder_;

```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[editor]"`

Expected: `All tests passed (158 assertions in 29 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h tests/plugin/test_editor.cpp
git commit -m "app: the Similar list beside the preview; a pick plays even when the search does not show it"
```

---

### Task 14: Hold the sidebar, the chips, the Similar list and the Key popover to the design

Spec section 12 (UI fidelity). The demo library gains a second folder, a
collection, two saved searches, the design's ratings and favourites and analysed
profiles, and its search sets the design's two active chips. Both pictures are
now blurred before comparing: compared sharp, text drawn by Chrome and by JUCE
disagree on almost every glyph pixel, so missing text scored the same as present
text (the sidebar read 2.6% against a blank reference). Blurred, each area's
limit sits between its right and its missing figure. The Key popover gets its
own reference, and the key buttons read as the design draws them (mono, not
muted).

**Files:**

- Modify: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Modify: `plugin/src/ui/AsmaLookAndFeel.h`
- Modify: `plugin/src/ui/FilterPopovers.cpp`
- Modify: `tests/plugin/test_ui_fidelity.cpp` (test)
- Modify: `tests/ui/reference/README.md` (test)
- Create: `tests/ui/reference/key-popover.html` (test)
- Create: `tests/ui/reference/key-popover.png` (test)
- Modify: `tests/ui/reference/main.html` (test)
- Modify: `tests/ui/reference/main.png` (test)

**Interfaces:**

- Consumes: everything above.
- Produces: `tests/ui/reference/key-popover.html` and `.png`; the redone
  `main.html` and `.png`; `AsmaLookAndFeel` honours an explicit
  `asma.quiet = false`.

- [ ] **Step 1: Write the failing test**

Replace the whole of `tests/plugin/test_ui_fidelity.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// The editor against the approved design: tests/ui/reference/main.png is the
// design's main artboard filled with this file's demo library, the 3c2b
// areas blanked (tests/ui/reference/main.html renders it; see README.md
// there). Font rendering differs between systems, so this runs on macOS
// only; the behaviour tests run everywhere.
#include "AsmaEditor.h"
#include "PluginTestUtil.h"
#include "ui/FilterPopovers.h"
#include "ui/Theme.h"
#include "Signals.h"
#include "asma/core/Analyser.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

using namespace asma;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 1280, kHeight = 800;
// Text is drawn by Chrome in the reference and by JUCE here, and the two
// never agree pixel by pixel: compared sharp, text drawn slightly differently
// scores the same as no text at all. So both pictures are blurred first, which
// keeps where the text and the shapes are and drops how their edges were
// drawn; a pixel then differs when a channel is off by more than kTolerance.
// Areas are judged apart, at the design's own coordinates, so a regression in
// one is not lost in the dark ground of the rest. Each limit sits between what
// the area measured when right and when its content was missing (the figures
// beside it, right / missing).
constexpr int kBlur = 3; // px, each way
constexpr int kTolerance = 20;

struct Area {
    const char* name;
    juce::Rectangle<int> bounds;
    double limit; // share of pixels that may differ
};
const Area kAreas[] = {
    {"top bar", {0, 0, kWidth, 56}, 0.035},            // 2.0%
    {"sidebar", {0, 56, 220, 482}, 0.045},             // 2.8% / 7.6%
    {"chip row", {220, 56, kWidth - 220, 44}, 0.06},   // 2.3% / 15.0%
    {"table", {220, 100, kWidth - 220, 438}, 0.06},    // 3.4% / 10.8%
    {"preview", {0, 538, kWidth - 284, 236}, 0.045},   // 3.0%
    {"similar", {kWidth - 284, 538, 284, 236}, 0.14},  // 9.1% / 22.1%: almost all text
    {"footer", {0, 774, kWidth, 26}, 0.02},            // 0.6%
};

// The design's rows, as files whose names the scan reads: tempo, key, loop.
struct DemoFile {
    const char* name;
    double seconds;
};
const DemoFile kDemo[] = {
    {"Bass_Loop_Am_118.wav", 8.14},      {"Bass_Loop_Am_120.wav", 8.00},     {"Bass_Loop_Dm_120_dusty.wav", 4.00},
    {"Bass_Loop_Gm_122.wav", 7.87},      {"Bass_Loop_F_124_sub.wav", 7.74},  {"Bass_Loop_Em_124.wav", 3.87},
    {"Bass_Loop_C_126_fingered.wav", 7.62}, {"Bass_Loop_Cm_126_wet.wav", 7.62}, {"Bass_Loop_A#m_128_wobble.wav", 7.50},
    {"Bass_Loop_Fm_128.wav", 3.75},      {"Bass_Loop_Am_130_acid.wav", 7.38}, {"Bass_Loop_A#m_130.wav", 7.38},
    {"Bass_Loop_132_rolling.wav", 7.27},
};

// The design's favourites and ratings, by file.
struct Organised {
    const char* name;
    bool favourite;
    int rating; // 0: unrated
};
const Organised kOrganised[] = {
    {"Bass_Loop_Am_118.wav", false, 3},    {"Bass_Loop_Am_120.wav", true, 4},  {"Bass_Loop_Gm_122.wav", false, 2},
    {"Bass_Loop_F_124_sub.wav", true, 5},  {"Bass_Loop_C_126_fingered.wav", false, 3},
    {"Bass_Loop_A#m_128_wobble.wav", false, 1}, {"Bass_Loop_Am_130_acid.wav", false, 4},
};

struct Demo {
    test::TempDir dir;
    std::string dataDir = (dir.path() / "data").string();
    test::ScopedEnv env{"ASMA_DATA_DIR", dataDir.c_str()};
    fs::path lib = dir.path() / "Samples";
    fs::path splice = dir.path() / "Splice";
    Demo()
    {
        constexpr int rate = 8000; // small files; the waveform is not compared
        for (const auto& f : kDemo) test::writeWavFloat(lib / "Loops" / f.name, rate, {test::sine(55.0, f.seconds, 0.6, rate)});
        test::writeWavFloat(splice / "Kick_Deep.wav", rate, {test::kickHit(rate)});
        test::writeWavFloat(splice / "Snare_Tight.wav", rate, {test::hatHit(rate, 5)});
        Db db = Db::open(dir.path() / "data" / "library.db");
        Library library(db);
        UserData data(db);
        const auto root = library.addRoot(lib);
        scanRoot(db, root);
        scanRoot(db, library.addRoot(splice));
        analysePending(db); // sound profiles, for Similar
        const auto low = data.createCollection("Low end");
        for (const auto& o : kOrganised) {
            const auto id = library.fileByPath(root, std::string("Loops/") + o.name)->id;
            if (o.favourite) data.setFavourite(id, true);
            if (o.rating > 0) data.setRating(id, o.rating);
            if (o.rating >= 4) data.addToCollection(low, id);
        }
        SearchModel inAm;
        inAm.type = SampleType::Loop;
        inAm.bpmMin = 120.0;
        inAm.bpmMax = 130.0;
        inAm.keys = {"Am"};
        data.saveSearch("Loops 120\u2013130 in Am", inAm);
        SearchModel kicks;
        kicks.text = "kick";
        kicks.durationMax = 1.0;
        data.saveSearch("Short kicks", kicks);
    }
};

// A box blur, kBlur px each way, as RGB floats: where things are, not how
// their edges were drawn.
std::vector<float> blurred(const juce::Image& image)
{
    const int w = image.getWidth(), h = image.getHeight();
    std::vector<float> a(static_cast<std::size_t>(w * h * 3)), b(a.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto c = image.getPixelAt(x, y);
            const auto i = static_cast<std::size_t>((y * w + x) * 3);
            a[i] = c.getRed();
            a[i + 1] = c.getGreen();
            a[i + 2] = c.getBlue();
        }
    const auto pass = [&](const std::vector<float>& in, std::vector<float>& out, bool across) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int ch = 0; ch < 3; ++ch) {
                    float sum = 0.0f;
                    int n = 0;
                    for (int k = -kBlur; k <= kBlur; ++k) {
                        const int xx = across ? x + k : x, yy = across ? y : y + k;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        sum += in[static_cast<std::size_t>((yy * w + xx) * 3 + ch)];
                        ++n;
                    }
                    out[static_cast<std::size_t>((y * w + x) * 3 + ch)] = sum / static_cast<float>(n);
                }
    };
    pass(a, b, true);
    pass(b, a, false);
    return a;
}

// The share of the area's pixels (less `masked`) that differ, blurred, and
// marks them in `diff` when given.
double mismatch(const std::vector<float>& current, const std::vector<float>& reference, int width,
                juce::Rectangle<int> area, const std::vector<juce::Rectangle<int>>& masked, juce::Image* diff)
{
    std::int64_t compared = 0, mismatched = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x) {
            bool skip = false;
            for (const auto& m : masked) skip |= m.contains(x, y);
            if (skip) continue;
            ++compared;
            const auto i = static_cast<std::size_t>((y * width + x) * 3);
            float d = 0.0f;
            for (int ch = 0; ch < 3; ++ch) d = std::max(d, std::abs(current[i + static_cast<std::size_t>(ch)] - reference[i + static_cast<std::size_t>(ch)]));
            if (d > kTolerance) {
                ++mismatched;
                if (diff) diff->setPixelAt(x, y, juce::Colours::magenta);
            }
        }
    return compared ? static_cast<double>(mismatched) / static_cast<double>(compared) : 0.0;
}

juce::Image renderEditor(app::AsmaEditor& editor)
{
    juce::Image image(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType{});
    juce::Graphics g(image);
    editor.paintEntireComponent(g, false);
    return image;
}

fs::path outDir()
{
    const char* env = std::getenv("ASMA_UI_OUT");
    const fs::path dir = env ? fs::path(env) : fs::temp_directory_path() / "asma-ui";
    fs::create_directories(dir);
    return dir;
}

void writePng(const juce::Image& image, const fs::path& path)
{
    juce::File file(juce::String::fromUTF8(path.string().c_str()));
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
}

} // namespace

TEST_CASE("the editor matches the approved design", "[fidelity]")
{
#if !JUCE_MAC
    SKIP("font rendering differs off macOS; the reference was made there");
#endif
    Demo demo;
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaProcessor p(app::AsmaProcessor::Mode::Standalone);
    p.prepareToPlay(48000.0, 512);
    // The design's state, as a saved project: its sample selected (not
    // playing) and edited, the standalone at 180 BPM.
    app::PluginState state = p.pluginState();
    state.sync.hostBpm = 180.0;
    state.search.text = "bass loop";
    state.search.type = SampleType::Loop; // the design's two active chips
    state.search.bpmMin = 118.0;
    state.search.bpmMax = 132.0;
    state.selected = toUtf8(demo.lib / "Loops" / "Bass_Loop_Am_120.wav");
    state.edits.direction = audio::Direction::Reverse;
    state.edits.trimStart = 0.76;
    state.edits.trimEnd = 6.80;
    p.setPluginState(state);
    REQUIRE(test::waitForPreview(p, p.engine().selected()));
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi); // the tempo reaches the transport
    std::unique_ptr<app::AsmaEditor> editor(dynamic_cast<app::AsmaEditor*>(p.createEditorAndMakeActive()));
    REQUIRE(editor);
    editor->setSize(kWidth, kHeight);
    editor->poll();
    REQUIRE(editor->table().getNumRows() == static_cast<int>(std::size(kDemo)));
    REQUIRE(editor->table().getSelectedRow() >= 0);
    REQUIRE_FALSE(p.engine().status().playing);
    REQUIRE(editor->preview().waveform().overview());

    const juce::Image current = renderEditor(*editor);
    writePng(current, outDir() / "current.png");
    const juce::File referenceFile(juce::String(ASMA_TEST_UI) + "/reference/main.png");
    REQUIRE(referenceFile.existsAsFile());
    const juce::Image reference = juce::ImageFileFormat::loadFrom(referenceFile);
    REQUIRE(reference.getWidth() == kWidth);
    REQUIRE(reference.getHeight() == kHeight);

    // Left out: the waveform's own shape (the demo's audio is not the
    // design's) and the window's resize corner (the design has none).
    const auto wave = editor->preview().waveform().getBounds() + editor->preview().getPosition();
    const std::vector<juce::Rectangle<int>> masked{wave.reduced(2, 14), {kWidth - 18, kHeight - 18, 18, 18}};
    juce::Image diff(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType{});
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) diff.setPixelAt(x, y, reference.getPixelAt(x, y).withMultipliedAlpha(0.25f));
    const auto cur = blurred(current), ref = blurred(reference);
    for (const Area& area : kAreas) {
        const double share = mismatch(cur, ref, kWidth, area.bounds, masked, &diff);
        INFO(area.name << ": " << share * 100.0 << "% of pixels differ; see " << outDir().string() << "/diff.png");
        CHECK(share <= area.limit);
    }
    writePng(diff, outDir() / "diff.png");
    p.editorBeingDeleted(editor.get());
}

TEST_CASE("the Key popover matches the approved design", "[fidelity]")
{
#if !JUCE_MAC
    SKIP("font rendering differs off macOS; the reference was made there");
#endif
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaLookAndFeel lnf;
    SearchModel picked;
    picked.keys = {"C", "Am"};
    app::KeyPopover popover(picked, [](const SearchModel&) {});
    popover.setLookAndFeel(&lnf);
    REQUIRE(popover.getWidth() == 360);
    REQUIRE(popover.getHeight() == 180);
    // The callout box draws the panel behind it.
    juce::Image current(juce::Image::ARGB, 360, 180, true, juce::SoftwareImageType{});
    {
        juce::Graphics g(current);
        g.fillAll(app::theme::panel);
        popover.paintEntireComponent(g, false);
    }
    writePng(current, outDir() / "key-popover.png");
    const juce::Image reference =
        juce::ImageFileFormat::loadFrom(juce::File(juce::String(ASMA_TEST_UI) + "/reference/key-popover.png"));
    REQUIRE(reference.getWidth() == 360);
    const double share = mismatch(blurred(current), blurred(reference), 360, {0, 0, 360, 180}, {}, nullptr);
    INFO("key popover: " << share * 100.0 << "% of pixels differ");
    CHECK(share <= 0.035); // 1.4% when right
    popover.setLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fidelity]"`

Expected: with the new test against the old references (before rendering them),
the Key popover's reference is missing:

```
tests/plugin/test_ui_fidelity.cpp:239: FAILED:
  REQUIRE( reference.getWidth() == 360 )
```

and against the old blank `main.png` the new areas fail: sidebar 7.7%, chip row
15.1%, table 11.0%, similar 22.3%.

- [ ] **Step 3: Implement**

In `plugin/src/ui/AsmaLookAndFeel.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.cpp b/plugin/src/ui/AsmaLookAndFeel.cpp
index b5a0fa8..0f5205a 100644
--- a/plugin/src/ui/AsmaLookAndFeel.cpp
+++ b/plugin/src/ui/AsmaLookAndFeel.cpp
@@ -5,6 +5,17 @@

 namespace asma::app {

+namespace {
+
+// Quiet (transparent, muted) when off: as "asma.quiet" says, else when it toggles.
+bool quiet(const juce::Button& button)
+{
+    const auto& props = button.getProperties();
+    return props.contains("asma.quiet") ? static_cast<bool>(props["asma.quiet"]) : button.getClickingTogglesState();
+}
+
+} // namespace
+
 AsmaLookAndFeel::AsmaLookAndFeel()
 {
     using namespace theme;
@@ -54,7 +65,7 @@ void AsmaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& butt
     const bool on = button.getToggleState();
     juce::Colour fill = theme::raised;
     if (on && props["asma.accent"]) fill = theme::amber;
-    else if (!on && (props["asma.quiet"] || button.getClickingTogglesState())) fill = juce::Colours::transparentBlack;
+    else if (!on && quiet(button)) fill = juce::Colours::transparentBlack;
     if (highlighted || down) fill = fill.isTransparent() ? theme::raised.withAlpha(0.6f) : fill.brighter(0.06f);

     auto r = button.getLocalBounds().toFloat();
@@ -86,11 +97,11 @@ void AsmaLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button
     const auto& props = button.getProperties();
     const bool on = button.getToggleState();
     const bool accent = on && props["asma.accent"];
-    const bool quiet = !on && (props["asma.quiet"] || button.getClickingTogglesState());
+    const bool quietText = !on && quiet(button);
     const float size = props.contains("asma.size") ? static_cast<float>(props["asma.size"]) : 13.0f;
     const bool mono = props["asma.mono"];
     g.setFont(theme::font(mono ? theme::Face::Mono : (accent ? theme::Face::SemiBold : theme::Face::Text), size));
-    g.setColour(accent ? theme::ground : (quiet ? theme::muted : theme::text));
+    g.setColour(accent ? theme::ground : (quietText ? theme::muted : theme::text));
     g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
 }

```

In `plugin/src/ui/AsmaLookAndFeel.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.h b/plugin/src/ui/AsmaLookAndFeel.h
index 0e4ee34..b132184 100644
--- a/plugin/src/ui/AsmaLookAndFeel.h
+++ b/plugin/src/ui/AsmaLookAndFeel.h
@@ -10,7 +10,7 @@ namespace asma::app {
 // segmented switch; "asma.accent" fills it amber when on; "asma.quiet" leaves
 // it transparent and muted when off; "asma.size" sets its text size (13);
 // "asma.mono" draws its text in the mono face.
-// A toggling button is quiet when off.
+// A toggling button is quiet when off, unless "asma.quiet" is set false.
 class AsmaLookAndFeel : public juce::LookAndFeel_V4 {
 public:
     static constexpr int kTableMargin = 14; // px before a table's first column's text
```

In `plugin/src/ui/FilterPopovers.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/FilterPopovers.cpp b/plugin/src/ui/FilterPopovers.cpp
index 06cd10d..1de5976 100644
--- a/plugin/src/ui/FilterPopovers.cpp
+++ b/plugin/src/ui/FilterPopovers.cpp
@@ -211,6 +211,7 @@ KeyPopover::KeyPopover(const SearchModel& model, SearchChanged onChange)
         b->getProperties().set("asma.accent", true);
         b->getProperties().set("asma.size", 12.0f);
         b->getProperties().set("asma.mono", true);
+        b->getProperties().set("asma.quiet", false); // unpicked keys read plainly, as the design has them
         b->setToggleState(std::find(model.keys.begin(), model.keys.end(), names[i].toStdString()) != model.keys.end(),
                           juce::dontSendNotification);
         b->onClick = [this, i, b] {
```

Replace the whole of `tests/ui/reference/README.md` with:

````markdown
# UI reference

`main.png` is what `tests/plugin/test_ui_fidelity.cpp` compares the editor with.
It is the approved design's main artboard (a private design canvas, "asma 3c2a
UI") rebuilt as `main.html`, with these changes and nothing else:

- The rows, the search, the count, the file line and the tempo are the test's
  demo library and state, not the design's sample data.
- The sidebar, the chip row, the ratings and favourites and the Similar list
  show the demo library: two folders, a collection, two saved searches, the
  design's ratings and favourites, its two active filters, and the neighbours
  the demo's analysis finds.
- The sample is stopped (a play button, no playhead), and the Key and Start
  chips say what they say when off ("off", "now"), so the picture is
  deterministic.
- The footer's right side is empty: the test has no scan to report and no
  renders.

`key-popover.png` is the design's Key popover from the canvas's popovers
artboard, rebuilt as `key-popover.html` at the size the app gives it (360×180),
with C and Am picked.

Both pictures are blurred before they are compared, so the test checks where
text and shapes are rather than how each renderer draws their edges. Each area
has its own limit, recorded in the test with the figures it measured. The test
leaves out the waveform's own shape (the demo audio is not the design's) and the
window's resize corner.

## Making it again

After a deliberate change to the design, edit `main.html` and render it with
Chrome at exactly 1280×800, scale 1:

```sh
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=1280,800 --screenshot="$PWD/main.png" "file://$PWD/main.html"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=360,180 --screenshot="$PWD/key-popover.png" "file://$PWD/key-popover.html"
```

`main.html` loads the fonts from `plugin/fonts`, the same files the app embeds.
The test writes `current.png` and `diff.png` (mismatched pixels in magenta) to
`$ASMA_UI_OUT`, or `asma-ui` in the system's temporary directory.
````

Create `tests/ui/reference/key-popover.html`:

<!-- prettier-ignore -->
````html
<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- The approved design's Key popover (the canvas's popovers artboard), at the
     size the app gives it, C and Am picked. See README.md beside it. -->
<html lang="en">
<head>
<meta charset="utf-8">
<title>asma key popover reference</title>
<style>
@font-face { font-family: "Inter"; font-weight: 400; src: url("../../../plugin/fonts/Inter-Regular.ttf"); }
@font-face { font-family: "JetBrains Mono"; font-weight: 400; src: url("../../../plugin/fonts/JetBrainsMono-Regular.ttf"); }
@font-face { font-family: "Space Grotesk"; font-weight: 600; src: url("../../../plugin/fonts/SpaceGrotesk-SemiBold.ttf"); }
* { box-sizing: border-box; }
body { margin: 0; background: #111316; }
.pop { width: 360px; height: 180px; padding: 14px; background: #111316; color: #e8e9eb; }
.title { height: 22px; display: flex; align-items: center; gap: 6px; margin-bottom: 6px; }
.title b { font-family: "Space Grotesk"; font-weight: 600; font-size: 13px; }
.title span { font-family: Inter; font-size: 11px; color: #8a8f98; }
.grid { display: grid; grid-template-columns: repeat(6, 52px); gap: 4px; }
.k { height: 28px; border: 1px solid #2a2d33; border-radius: 4px; display: flex; align-items: center; justify-content: center;
  font-family: "JetBrains Mono"; font-size: 12px; color: #e8e9eb; }
.k.on { background: #e8a33d; border-color: #2a2d33; color: #0b0c0e; }
</style>
</head>
<body>
<div class="pop">
  <div class="title"><b>Key</b><span>any of</span></div>
  <div class="grid">
    <span class="k on">C</span>
    <span class="k">C#</span>
    <span class="k">D</span>
    <span class="k">D#</span>
    <span class="k">E</span>
    <span class="k">F</span>
    <span class="k">F#</span>
    <span class="k">G</span>
    <span class="k">G#</span>
    <span class="k">A</span>
    <span class="k">A#</span>
    <span class="k">B</span>
    <span class="k">Cm</span>
    <span class="k">C#m</span>
    <span class="k">Dm</span>
    <span class="k">D#m</span>
    <span class="k">Em</span>
    <span class="k">Fm</span>
    <span class="k">F#m</span>
    <span class="k">Gm</span>
    <span class="k">G#m</span>
    <span class="k on">Am</span>
    <span class="k">A#m</span>
    <span class="k">Bm</span>
  </div>
</div>
</body>
</html>
````

Replace the whole of `tests/ui/reference/main.html` with:

<!-- prettier-ignore -->
````html
<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- The approved design's main artboard (a private design canvas, "asma 3c2a UI"),
     filled with tests/plugin/test_ui_fidelity.cpp's demo library: the
     sidebar, the chip row, the table and the Similar list show its folders,
     filters, ratings and neighbours. See README.md beside it. -->
<html lang="en">
<head>
<meta charset="utf-8">
<title>asma reference</title>
<style>
@font-face { font-family: "Inter"; font-weight: 400; src: url("../../../plugin/fonts/Inter-Regular.ttf"); }
@font-face { font-family: "Inter"; font-weight: 500; src: url("../../../plugin/fonts/Inter-Medium.ttf"); }
@font-face { font-family: "Inter"; font-weight: 600; src: url("../../../plugin/fonts/Inter-SemiBold.ttf"); }
@font-face { font-family: "JetBrains Mono"; font-weight: 400; src: url("../../../plugin/fonts/JetBrainsMono-Regular.ttf"); }
@font-face { font-family: "Space Grotesk"; font-weight: 600; src: url("../../../plugin/fonts/SpaceGrotesk-SemiBold.ttf"); }
* { box-sizing: border-box; }
body { margin: 0; background: #15171a; }
button { font: inherit; color: inherit; }
.app { width: 1280px; height: 800px; display: flex; flex-direction: column; background: #15171a; color: #e8e9eb;
  font-family: Inter, sans-serif; font-size: 13px; overflow: hidden; }
header { height: 56px; flex-shrink: 0; display: flex; align-items: center; gap: 16px; padding: 0 16px;
  border-bottom: 1px solid #2a2d33; background: #111316; }
.brand { display: flex; align-items: center; gap: 10px; width: 188px; }
.word { font-family: "Space Grotesk"; font-weight: 600; font-size: 18px; letter-spacing: 0.06em; }
.search { width: 520px; height: 34px; display: flex; align-items: center; gap: 8px; padding: 0 12px;
  border: 1px solid #2a2d33; border-radius: 4px; background: #0b0c0e; }
.search .q { flex-grow: 1; }
.mono { font-family: "JetBrains Mono"; }
.muted { color: #8a8f98; }
.count { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }
.spacer { flex-grow: 1; }
.right { display: flex; align-items: center; gap: 8px; }
.chip { height: 30px; padding: 0 10px; border: 1px solid #2a2d33; border-radius: 4px; background: transparent;
  color: #8a8f98; display: flex; align-items: center; gap: 8px; font-size: 12px; }
.ring { width: 8px; height: 8px; border-radius: 4px; border: 1.5px solid #8a8f98; }
.dot { width: 8px; height: 8px; border-radius: 4px; }
.tempo { width: 140px; height: 30px; display: flex; align-items: center; border: 1px solid #2a2d33; border-radius: 4px; background: #0b0c0e; }
.tempo button { width: 26px; height: 28px; border: 0; background: transparent; color: #8a8f98; }
.tempo .v { flex-grow: 1; text-align: center; font-family: "JetBrains Mono"; font-size: 13px; }
.tempo .u { width: 34px; font-size: 11px; color: #8a8f98; }
.add { width: 104px; height: 30px; border: 1px solid #2a2d33; border-radius: 4px; background: #1c1f23; }
.middle { flex-grow: 1; display: flex; min-height: 0; }
nav { width: 220px; flex-shrink: 0; border-right: 1px solid #2a2d33; background: #111316; }
main { flex-grow: 1; display: flex; flex-direction: column; min-width: 0; }
.chips { height: 44px; flex-shrink: 0; border-bottom: 1px solid #2a2d33; }
.head, .row { height: 30px; display: grid; grid-template-columns: 48px minmax(0, 1fr) 76px 70px 56px 72px 88px 140px; align-items: center; }
.head { border-bottom: 1px solid #2a2d33; font-family: "Space Grotesk"; font-size: 11px; font-weight: 600;
  letter-spacing: 0.06em; color: #8a8f98; }
.row { border-bottom: 1px solid #1c1f23; }
.row .mono { font-size: 12px; }
.head span:first-child, .row .fav { padding-left: 14px; }
.fav { color: #3a3e45; }
.stars { color: #3a3e45; font-size: 11px; letter-spacing: 1.5px; }
.tags { color: #8a8f98; font-size: 12px; }
.head .lit { color: #e8e9eb; }
.row.sel { background: rgba(232,163,61,0.10); box-shadow: inset 2px 0 0 #e8a33d; }
.preview { height: 236px; flex-shrink: 0; border-top: 1px solid #2a2d33; background: #111316; display: flex; }
.pleft { flex-grow: 1; min-width: 0; padding: 12px 16px; display: flex; flex-direction: column; gap: 10px; }
.title { height: 20px; display: flex; align-items: baseline; gap: 12px; }
.fname { font-family: "Space Grotesk"; font-weight: 600; font-size: 15px; }
.fline { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }
.wave { position: relative; flex-grow: 1; border-radius: 4px; background: #0b0c0e; border: 1px solid #2a2d33; overflow: hidden; }
.bars { position: absolute; inset: 6px 0; display: flex; align-items: center; gap: 1px; padding: 0 2px; }
.bars span { flex-grow: 1; background: #3aa3b5; border-radius: 1px; }
.dim { position: absolute; top: 0; bottom: 0; background: rgba(11,12,14,0.55); }
.handle { position: absolute; top: 0; bottom: 0; width: 2px; background: #e8a33d; }
.tab { position: absolute; top: 0; width: 12px; height: 12px; background: #e8a33d; border-radius: 0 0 3px 3px; }
.tlabel { position: absolute; bottom: 4px; font-family: "JetBrains Mono"; font-size: 10px; color: #e8a33d; }
.controls { height: 28px; display: flex; align-items: center; gap: 10px; }
.play { width: 32px; height: 28px; border: 1px solid #2a2d33; border-radius: 4px; background: #111316;
  display: flex; align-items: center; justify-content: center; }
.seg { display: flex; border: 1px solid #2a2d33; border-radius: 4px; overflow: hidden; font-size: 12px; }
.seg button { height: 26px; padding: 0 10px; border: 0; background: transparent; color: #8a8f98; }
.seg button + button { border-left: 1px solid #2a2d33; }
.seg .on { background: #e8a33d; color: #0b0c0e; font-weight: 600; }
.seg .lift { background: #1c1f23; color: #e8e9eb; }
.quiet { height: 28px; padding: 0 9px; border: 1px solid #2a2d33; border-radius: 4px; background: transparent; color: #8a8f98; font-size: 12px; }
.sep { width: 1px; height: 22px; background: #2a2d33; }
.pchip { height: 28px; padding: 0 10px; border: 1px solid #2a2d33; border-radius: 4px; display: flex; align-items: center; gap: 8px; font-size: 12px; }
.pchip.on { background: #1c1f23; }
.pchip.off { color: #8a8f98; }
.pchip .d { font-family: "JetBrains Mono"; font-size: 11px; }
aside { width: 284px; flex-shrink: 0; border-left: 1px solid #2a2d33; }
footer { height: 26px; flex-shrink: 0; display: flex; align-items: center; justify-content: space-between; padding: 0 16px;
  border-top: 1px solid #2a2d33; background: #0b0c0e; font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }

nav { padding: 14px 0; display: flex; flex-direction: column; gap: 18px; }
.navgroup { display: flex; flex-direction: column; }
.navhead { height: 22px; line-height: 22px; padding: 0 18px; font-family: "Space Grotesk"; font-size: 11px; font-weight: 600; letter-spacing: 0.08em; color: #8a8f98; }
.navrow { height: 30px; display: flex; align-items: center; justify-content: space-between; padding: 0 18px; }
.navrow.lit { background: #1c1f23; box-shadow: inset 2px 0 0 #e8a33d; }
.navrow .n { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }
.chips { display: flex; align-items: center; gap: 8px; padding: 0 14px; }
.fchip { height: 26px; box-sizing: border-box; padding: 0 10px; border: 1px solid #2a2d33; border-radius: 13px; color: #8a8f98; font-size: 12px; display: flex; align-items: center; }
.fchip.on { padding: 0 5px 0 10px; border-color: #e8a33d; background: rgba(232,163,61,0.12); color: #f2bd6b; gap: 4px; }
.fchip .x { width: 16px; height: 16px; display: inline-flex; align-items: center; justify-content: center; color: #e8a33d; font-size: 12px; }
.clearall { margin-left: auto; width: 72px; text-align: center; color: #8a8f98; font-size: 12px; }
.fav.on { color: #e8a33d; }
.stars .lit { color: #e8a33d; }
aside { padding-top: 12px; }
.simhead { height: 16px; line-height: 16px; padding: 0 14px; margin-bottom: 4px; font-family: "Space Grotesk"; font-size: 11px; font-weight: 600; letter-spacing: 0.08em; color: #8a8f98; }
.simrow { height: 24px; display: flex; align-items: center; justify-content: space-between; padding: 0 14px; font-size: 12px; }
.simrow .d { font-family: "JetBrains Mono"; font-size: 11px; color: #8a8f98; }
</style>
</head>
<body>
<div class="app">
  <header>
    <div class="brand">
      <svg width="22" height="22" viewBox="0 0 22 22"><path d="M17.4 4.6 A9 9 0 1 0 19.8 9.2" fill="none" stroke="#e8a33d" stroke-width="2.2" stroke-linecap="round"/></svg>
      <span class="word">asma</span>
    </div>
    <div class="search">
      <svg width="14" height="14" viewBox="0 0 14 14"><circle cx="6" cy="6" r="4.5" fill="none" stroke="#8a8f98" stroke-width="1.5"/><path d="M9.5 9.5 L13 13" stroke="#8a8f98" stroke-width="1.5" stroke-linecap="round"/></svg>
      <span class="q">bass loop</span>
      <span class="count">13 of 15</span>
    </div>
    <div class="spacer"></div>
    <div class="right">
      <div class="chip"><span class="ring"></span>Link</div>
      <div class="tempo"><button>&#8722;</button><span class="v">180.0</span><span class="u">BPM</span><button>+</button></div>
      <button class="add">Add folder&#8230;</button>
    </div>
  </header>
  <div class="middle">
    <nav>
      <div class="navgroup"><div class="navrow lit">All samples<span class="n">15</span></div><div class="navrow">Favourites<span class="n">2</span></div></div>
      <div class="navgroup"><div class="navhead">FOLDERS</div><div class="navrow">Samples<span class="n">13</span></div><div class="navrow">Splice<span class="n">2</span></div></div>
      <div class="navgroup"><div class="navhead">COLLECTIONS</div><div class="navrow">Low end<span class="n">3</span></div></div>
      <div class="navgroup"><div class="navhead">SAVED SEARCHES</div><div class="navrow">Loops 120&#8211;130 in Am</div><div class="navrow">Short kicks</div></div>
    </nav>
    <main>
      <div class="chips">
        <span class="fchip on">Loops<span class="x">&#215;</span></span>
        <span class="fchip on">118&#8211;132 BPM<span class="x">&#215;</span></span>
        <span class="fchip">Key</span><span class="fchip">Instrument</span><span class="fchip">Length</span><span class="fchip">Rating</span>
        <span class="clearall">Clear all</span>
      </div>
      <div class="head"><span></span><span class="lit">NAME &#9652;</span><span>TYPE</span><span>BPM</span><span>KEY</span><span>LENGTH</span><span>RATING</span><span>TAGS</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_132_rolling.wav</span><span class="muted">loop</span><span class="mono">132</span><span class="mono"></span><span class="mono muted">7.27 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_A#m_128_wobble.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">A#m</span><span class="mono muted">7.50 s</span><span class="stars"><span class="lit">&#9733;</span>&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_A#m_130.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">A#m</span><span class="mono muted">7.38 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Am_118.wav</span><span class="muted">loop</span><span class="mono">118</span><span class="mono">Am</span><span class="mono muted">8.14 s</span><span class="stars"><span class="lit">&#9733;&#9733;&#9733;</span>&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row sel"><span class="fav on">&#9733;</span><span class="name">Bass_Loop_Am_120.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Am</span><span class="mono muted">8.00 s</span><span class="stars"><span class="lit">&#9733;&#9733;&#9733;&#9733;</span>&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Am_130_acid.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">Am</span><span class="mono muted">7.38 s</span><span class="stars"><span class="lit">&#9733;&#9733;&#9733;&#9733;</span>&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_C_126_fingered.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono"></span><span class="mono muted">7.62 s</span><span class="stars"><span class="lit">&#9733;&#9733;&#9733;</span>&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Cm_126_wet.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono">Cm</span><span class="mono muted">7.62 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Dm_120_dusty.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Dm</span><span class="mono muted">4.00 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Em_124.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono">Em</span><span class="mono muted">3.87 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav on">&#9733;</span><span class="name">Bass_Loop_F_124_sub.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono"></span><span class="mono muted">7.74 s</span><span class="stars"><span class="lit">&#9733;&#9733;&#9733;&#9733;&#9733;</span></span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Fm_128.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">Fm</span><span class="mono muted">3.75 s</span><span class="stars"><span class="lit"></span>&#9733;&#9733;&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
      <div class="row"><span class="fav">&#9734;</span><span class="name">Bass_Loop_Gm_122.wav</span><span class="muted">loop</span><span class="mono">122</span><span class="mono">Gm</span><span class="mono muted">7.87 s</span><span class="stars"><span class="lit">&#9733;&#9733;</span>&#9733;&#9733;&#9733;</span><span class="tags">bass</span></div>
    </main>
  </div>
  <section class="preview">
    <div class="pleft">
      <div class="title"><span class="fname">Bass_Loop_Am_120.wav</span><span class="fline">Loops &#183; 8 kHz &#183; mono &#183; 8.00 s</span></div>
      <div class="wave">
        <div class="bars"><span style="height:88px"></span><span style="height:77px"></span><span style="height:68px"></span><span style="height:58px"></span><span style="height:47px"></span><span style="height:37px"></span><span style="height:33px"></span><span style="height:34px"></span><span style="height:37px"></span><span style="height:37px"></span><span style="height:29px"></span><span style="height:19px"></span><span style="height:12px"></span><span style="height:13px"></span><span style="height:20px"></span><span style="height:26px"></span><span style="height:26px"></span><span style="height:20px"></span><span style="height:13px"></span><span style="height:10px"></span><span style="height:12px"></span><span style="height:16px"></span><span style="height:17px"></span><span style="height:83px"></span><span style="height:70px"></span><span style="height:61px"></span><span style="height:55px"></span><span style="height:49px"></span><span style="height:40px"></span><span style="height:31px"></span><span style="height:26px"></span><span style="height:26px"></span><span style="height:31px"></span><span style="height:33px"></span><span style="height:30px"></span><span style="height:20px"></span><span style="height:12px"></span><span style="height:9px"></span><span style="height:15px"></span><span style="height:22px"></span><span style="height:25px"></span><span style="height:22px"></span><span style="height:15px"></span><span style="height:10px"></span><span style="height:10px"></span><span style="height:87px"></span><span style="height:77px"></span><span style="height:67px"></span><span style="height:57px"></span><span style="height:49px"></span><span style="height:45px"></span><span style="height:41px"></span><span style="height:36px"></span><span style="height:28px"></span><span style="height:21px"></span><span style="height:20px"></span><span style="height:25px"></span><span style="height:30px"></span><span style="height:29px"></span><span style="height:22px"></span><span style="height:13px"></span><span style="height:8px"></span><span style="height:10px"></span><span style="height:18px"></span><span style="height:24px"></span><span style="height:23px"></span><span style="height:17px"></span><span style="height:11px"></span><span style="height:77px"></span><span style="height:67px"></span><span style="height:61px"></span><span style="height:54px"></span><span style="height:47px"></span><span style="height:40px"></span><span style="height:37px"></span><span style="height:35px"></span><span style="height:32px"></span><span style="height:26px"></span><span style="height:19px"></span><span style="height:16px"></span><span style="height:19px"></span><span style="height:25px"></span><span style="height:28px"></span><span style="height:24px"></span><span style="height:15px"></span><span style="height:8px"></span><span style="height:7px"></span><span style="height:14px"></span><span style="height:21px"></span><span style="height:24px"></span><span style="height:88px"></span><span style="height:74px"></span><span style="height:60px"></span><span style="height:52px"></span><span style="height:49px"></span><span style="height:45px"></span><span style="height:39px"></span><span style="height:34px"></span><span style="height:31px"></span><span style="height:30px"></span><span style="height:29px"></span><span style="height:25px"></span><span style="height:18px"></span><span style="height:13px"></span><span style="height:14px"></span><span style="height:21px"></span><span style="height:26px"></span><span style="height:26px"></span><span style="height:18px"></span><span style="height:10px"></span><span style="height:6px"></span><span style="height:10px"></span><span style="height:17px"></span><span style="height:88px"></span><span style="height:77px"></span><span style="height:62px"></span><span style="height:49px"></span><span style="height:41px"></span><span style="height:39px"></span><span style="height:37px"></span><span style="height:33px"></span><span style="height:29px"></span><span style="height:26px"></span><span style="height:26px"></span><span style="height:27px"></span><span style="height:24px"></span><span style="height:18px"></span><span style="height:12px"></span><span style="height:11px"></span><span style="height:16px"></span><span style="height:23px"></span><span style="height:26px"></span><span style="height:21px"></span><span style="height:12px"></span><span style="height:6px"></span><span style="height:81px"></span><span style="height:75px"></span><span style="height:71px"></span><span style="height:64px"></span><span style="height:53px"></span><span style="height:41px"></span><span style="height:34px"></span><span style="height:31px"></span><span style="height:30px"></span><span style="height:29px"></span><span style="height:25px"></span><span style="height:23px"></span><span style="height:23px"></span><span style="height:24px"></span><span style="height:23px"></span><span style="height:19px"></span><span style="height:13px"></span><span style="height:9px"></span><span style="height:12px"></span><span style="height:19px"></span><span style="height:25px"></span><span style="height:23px"></span><span style="height:16px"></span><span style="height:75px"></span><span style="height:61px"></span><span style="height:56px"></span><span style="height:56px"></span><span style="height:54px"></span><span style="height:46px"></span><span style="height:36px"></span><span style="height:28px"></span><span style="height:25px"></span><span style="height:25px"></span><span style="height:25px"></span><span style="height:23px"></span><span style="height:20px"></span><span style="height:20px"></span><span style="height:22px"></span><span style="height:23px"></span><span style="height:20px"></span><span style="height:14px"></span><span style="height:9px"></span><span style="height:9px"></span><span style="height:15px"></span><span style="height:23px"></span></div>
        <div class="dim" style="left: 0; width: 9.5%"></div>
        <div class="dim" style="right: 0; width: 15%"></div>
        <div class="handle" style="left: 9.5%"></div><div class="tab" style="left: calc(9.5% - 5px)"></div>
        <div class="handle" style="right: 15%"></div><div class="tab" style="right: calc(15% - 5px)"></div>
        <span class="tlabel" style="left: calc(9.5% + 6px)">0.76 s</span>
        <span class="tlabel" style="right: calc(15% + 6px)">6.80 s</span>
      </div>
      <div class="controls">
        <div class="play"><svg width="12" height="12" viewBox="0 0 12 12"><path d="M2 0 L2 12 L12 6 Z" fill="#e8e9eb"/></svg></div>
        <div class="seg"><button>&#8594;</button><button class="on">&#8592;</button><button>&#8596;</button></div>
        <div class="seg"><button class="lift">Loop auto</button><button>On</button><button>Off</button></div>
        <button class="quiet">Reset edits</button>
        <div class="sep"></div>
        <div class="pchip on"><span class="dot" style="background:#7de38e"></span>Tempo<span class="d" style="color:#7de38e">120 &#8594; 180 &#183; x1.50</span></div>
        <div class="pchip off"><span class="ring"></span>Key<span class="d">off</span></div>
        <div class="pchip on"><span class="dot" style="background:#e8a33d"></span>Match loudness</div>
        <div class="pchip off"><span class="ring"></span>Start<span class="d">now</span></div>
      </div>
    </div>
    <aside>
      <div class="simhead">SIMILAR</div>
      <div class="simrow">Bass_Loop_A#m_128_wobble.wav<span class="d">0.00</span></div>
      <div class="simrow">Bass_Loop_Gm_122.wav<span class="d">0.05</span></div>
      <div class="simrow">Bass_Loop_Dm_120_dusty.wav<span class="d">0.06</span></div>
      <div class="simrow">Bass_Loop_Am_118.wav<span class="d">0.07</span></div>
      <div class="simrow">Bass_Loop_F_124_sub.wav<span class="d">0.09</span></div>
      <div class="simrow">Bass_Loop_Am_130_acid.wav<span class="d">0.12</span></div>
      <div class="simrow">Bass_Loop_A#m_130.wav<span class="d">0.12</span></div>
      <div class="simrow">Bass_Loop_132_rolling.wav<span class="d">0.14</span></div>
    </aside>
  </section>
  <footer><span>Drag out renders: reversed, trimmed, stretched to 180 BPM</span><span></span></footer>
</div>
</body>
</html>
````

Render the reference pictures from their pages:

```sh
cd tests/ui/reference
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --hide-scrollbars --force-device-scale-factor=1 --window-size=360,180 \
  --screenshot="$PWD/key-popover.png" "file://$PWD/key-popover.html"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --hide-scrollbars --force-device-scale-factor=1 --window-size=1280,800 \
  --screenshot="$PWD/main.png" "file://$PWD/main.html"
cd ../../..
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fidelity]" -s`

Expected: every area within its limit (top bar 2.1%, sidebar 2.8%, chip row
2.4%, table 3.5%, preview 3.0%, similar 9.5%, footer 0.6%, key popover 1.4%) and
`All tests passed (20 assertions in 2 test cases)`.

- [ ] **Step 5: Commit**

```sh
git add plugin/src/ui/AsmaLookAndFeel.cpp plugin/src/ui/AsmaLookAndFeel.h plugin/src/ui/FilterPopovers.cpp tests/plugin/test_ui_fidelity.cpp tests/ui/reference/README.md tests/ui/reference/key-popover.html tests/ui/reference/key-popover.png tests/ui/reference/main.html tests/ui/reference/main.png
git commit -m "test: hold the sidebar, the chips, the Similar list and the Key popover to the design"
```

---

### Task 15: Time Similar over 50k analysed files

Spec section 12 (Performance): Similar runs on the UI thread while it stays
under 50 ms on a 50,000-file library. On the prototype's Release build it took
25 ms.

**Files:**

- Modify: `tests/test_query.cpp` (test)

**Interfaces:**

- Consumes: `findSimilar`.
- Produces: a hidden `[.perf]` test.

- [ ] **Step 1: Write the timing test**

In `tests/test_query.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_query.cpp b/tests/test_query.cpp
index 53e46eb..3d2c34b 100644
--- a/tests/test_query.cpp
+++ b/tests/test_query.cpp
@@ -1,7 +1,9 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #include "TestUtil.h"
+#include "asma/core/Analysis.h"
 #include "asma/core/Library.h"
 #include "asma/core/Query.h"
+#include "asma/core/Similar.h"
 #include "asma/core/UserData.h"

 #include <catch2/catch_test_macros.hpp>
@@ -316,3 +318,45 @@ TEST_CASE("rowsForIds keeps the given order and drops unusable ids", "[query]")
     CHECK(ids(rowsForIds(s.db, {s.padLoop, s.snare, 9999, s.kick})) == Ids{s.padLoop, s.kick});
     CHECK(rowsForIds(s.db, {}).empty());
 }
+
+// Hidden: run with ./build/tests/asma_tests "[.perf]" on a Release build.
+// Similar runs on the UI thread while it stays this quick.
+TEST_CASE("similar over 50k analysed files", "[.perf]")
+{
+    TempDir dir;
+    Db db = Db::openInMemory();
+    Library lib(db);
+    const auto root = lib.addRoot(dir.path());
+    std::int64_t first = 0;
+    {
+        Transaction tx(db);
+        auto vector = db.prepare("UPDATE features SET feature_vector = ? WHERE file_id = ?");
+        std::uint32_t seed = 1;
+        std::vector<float> v(kFeatureVectorSize);
+        for (int i = 0; i < 50000; ++i) {
+            FileRecord f;
+            f.rootId = root;
+            f.relPath = "Pack/" + std::to_string(i) + ".wav";
+            f.size = i;
+            f.mtime = i;
+            f.format = "wav";
+            f.duration = 1.0;
+            const auto id = lib.insertFile(f);
+            if (i == 0) first = id;
+            lib.setDerived(id, {});
+            for (auto& x : v) {
+                seed = seed * 1664525u + 1013904223u; // any spread will do
+                x = static_cast<float>(seed >> 8) / 16777216.0f;
+            }
+            vector.bindBlob(1, v.data(), v.size() * sizeof(float)).bind(2, id).run();
+            vector.reset();
+        }
+        tx.commit();
+    }
+    const auto start = std::chrono::steady_clock::now();
+    const auto matches = findSimilar(db, first, 10);
+    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
+    WARN("similar took " << ms << " ms over 50000 files");
+    CHECK(matches.size() == 10);
+    CHECK(ms < 50.0); // past this, Similar moves off the UI thread (spec section 12)
+}
```

- [ ] **Step 2: Run it**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "similar over 50k analysed files"`

Expected: `similar took` under 50 ms (25 ms on the prototype) and
`All tests passed (2 assertions in 1 test case)`. Over 50 ms, stop: the spec
says Similar then moves to a worker, which this plan does not do.

- [ ] **Step 3: Commit**

```sh
git add tests/test_query.cpp
git commit -m "test: time Similar over 50k analysed files"
```

---

### Task 16: Docs

The README gains browsing; the spec says Problems shows while something has
failed.

**Files:**

- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`

**Interfaces:**

- Consumes: tasks 1 to 15.
- Produces: nothing code depends on.

- [ ] **Step 1: Write the docs**

In `README.md`, apply (`git apply` takes it as is):

```diff
diff --git a/README.md b/README.md
index e40767d..d89dab4 100644
--- a/README.md
+++ b/README.md
@@ -85,6 +85,14 @@ Selecting another sample drops the edits, so moving through the table always
 plays each sample as it is. The footer says what a drag-out will carry and
 offers to clear the kept renders. Space plays and stops.

+The sidebar picks where to look: all samples, favourites, a folder, a
+collection, or a saved search, which brings its whole search with it. The chips
+over the table narrow it further by type, BPM, key, instrument, length and
+rating; each opens a small panel, and its x clears it. The table holds every
+match, however many, sorts by clicking a column, and the Similar list beside the
+preview offers the samples that sound most like the selection. Rating, tagging
+and collecting come in a later release.
+
 The look is checked against the approved design by `[fidelity]` in
 `asma_plugin_tests`, on macOS only; `tests/ui/reference/README.md` says how the
 reference picture is made.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, apply (`git apply` takes
it as is):

```diff
diff --git a/docs/superpowers/specs/2026-09-25-asma-design.md b/docs/superpowers/specs/2026-09-25-asma-design.md
index 885b6f3..112cba9 100644
--- a/docs/superpowers/specs/2026-09-25-asma-design.md
+++ b/docs/superpowers/specs/2026-09-25-asma-design.md
@@ -306,7 +306,7 @@ filter chips' popovers (approved for 3c2b1).

 - **Left sidebar (3c2b1):** All samples, Favourites, folders (roots),
   collections and saved searches, with counts; a Problems entry with a count at
-  the foot (its panel: 3c2b2).
+  the foot while any file has failed to decode or analyse (its panel: 3c2b2).
 - **Top bar:** the asma mark, the search field with a result count, and on the
   right the tempo source: in the standalone the Link switch, the BPM box and
   "Add folder…"; in a plugin the host's tempo, read-only ("host 124.0 BPM").
```

- [ ] **Step 2: Check them**

Run:
`prettier --check README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: both files pass; no em-dashes.

- [ ] **Step 3: Commit**

```sh
git add README.md docs/superpowers/specs/2026-09-25-asma-design.md
git commit -m "docs: browsing in the README; Problems shows while something has failed"
```

---

### Task 17: Verify and merge

- [ ] **Step 1: Everything, locally**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests
./build/tests/asma_tests "[.perf]"
TOOLS=build/validators ci/validate-plugins.sh build
```

Expected: no warnings from asma's code; `100% tests passed out of 451`; the
plugin tests also pass in one process (`All tests passed`); `search took` and
`similar took` under 50 ms; pluginval `SUCCESS` and clap-validator `0 failed`.

- [ ] **Step 2: Try the app by hand (macOS)**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
build/plugin/asma_plugin_artefacts/Release/Standalone/asma.app/Contents/MacOS/asma
```

Add two folders. Expected: the sidebar lists All samples, Favourites and both
folders with counts; picking a folder narrows the table; the chips open their
popovers in the window and narrow as you change them, each chip saying its value
with an x that clears it; clicking a header sorts and lights it; the table
scrolls through every match, the top bar saying "N of M"; selecting a sample
fills the Similar list ("Not analysed yet" until the scan's analysis reaches
it), and clicking one there plays it. With `asma` (the CLI) save a search and
make a collection; the sidebar shows them at the next library change.

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-3c2b1-app
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green, including "Validate plugins". Watch
the `[fidelity]` tests on the macOS runner, and the window-based repaint test
(skipped on Linux) for flakiness.

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-3c2b1-app
git -C product/asma worktree remove .worktrees/plan-3c2b1-app
git -C product/asma branch -d plan-3c2b1-app
```

Push `main` once the user agrees, and delete the remote branch.
