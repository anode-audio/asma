# asma Plan 3c2a: Looks and Audition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** asma's window looks like the approved design and auditions like it:
the Anode theme, the top bar, a styled table, a preview panel with the selected
sample's waveform, trim handles, direction and loop switches and chips that say
what sync does, and a footer that says what a drag-out carries and clears the
kept renders; the editor matched to the design by a screenshot test.

**Architecture:** The audio side gains three things: a loader that survives any
exception, a sync plan that says when it clamped, and a waveform overview the
loader builds (at once for decoded files, by a read-through after playback
starts for streamed ones). The processor gains `select` and `setEdits`, so edits
belong to the selection. On the JUCE side, JUCE-free functions carry the words
(`tempoChip`, `dragSummary`); `plugin/src/ui/` holds the `Theme`, the
`AsmaLookAndFeel` and the components (`WaveformView`, `ChipButton`,
`SegmentedControl`, `PreviewPanel`, `Footer`, `TopBar`), each tested alone;
`AsmaEditor` lays them out and joins them to the processor.

**Tech Stack:** C++20, JUCE 9.0.3, Catch2; Inter, JetBrains Mono and Space
Grotesk (SIL OFL) embedded with `juce_add_binary_data`; headless Chrome to
render the reference picture.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (sections 8, 9, 10
and 12, as amended for 3c2a in `9f33a49`). Task 13 completes the Tempo chip
table.

**How this plan was checked:** every task was built on a branch from `main`
(`9f33a49`), then replayed commit by commit on a macOS Release build: with only
the task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The code below is taken from those commits: new files
in full, changed files as diffs against the task before (or in full where most
of the file changed), so applying the tasks in order to `main` gives the
prototype file for file. Unlike plan 3c1, the prototype did not run on CI:
Windows and Linux are first built in Task 14.

## Where this sits

Plan 3c2 was split in two (the user's call, 2026-10-01):

- **3c2a (this plan):** the theme, the layout shell, the preview panel with the
  waveform and the edit controls, the Tempo chip, the footer, the loader guard
  and "Clear renders".
- **3c2b:** the sidebar (folders, collections, saved searches, favourites), the
  facet chips, ratings, tags and favourites through the CLI helper, the Similar
  list and the Problems panel, in the places 3c2a leaves empty.

Plan 4 (file manager) and plan 5 (packaging) are unchanged.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (CMake, shell and YAML files:
  `# SPDX-...`; HTML: `<!-- SPDX-... -->`). The fonts are SIL OFL 1.1, with
  their licences in `plugin/fonts`.
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11), no
  floating-point `std::to_chars`/`std::from_chars` (macOS 12). The plugin build
  uses JUCE's warning set, which includes `-Wfloat-equal`: compare floats with
  `<`/`>`, never `==`/`!=`.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE. `-DASMA_BUILD_PLUGIN=OFF` still
  builds the core, the CLI and their tests.
- The plugin never writes the library inside the host.
- Nothing on the audio thread allocates, locks or blocks. The overview is built
  on the loader thread and handed over under a mutex the audio thread never
  takes.
- Numbers the UI shows are formatted with integer arithmetic (`bpmText`,
  `ratioText`, `secondsText`, `Footer::sizeText`), never through the locale.
- UI text in the source is UTF-8 (`→`, `·`); `asma_ui` compiles with `/utf-8` on
  MSVC.
- Colours come from `theme`, never as hex in a component.
- No em-dashes in code, comments, docs or commit messages; no attribution
  trailers; no mention of the tools used to write the code.

## Decisions made while prototyping

- **The overview crosses threads under a mutex**, not an atomic `shared_ptr`:
  libc++ on macOS 12 lacks `std::atomic<std::shared_ptr>`, and only the loader
  thread and the UI touch it.
- **A drag on a trim handle reports once, on release.** Reporting every step
  restarted the sound at every mouse move.
- **Both trim handles stand inside the kept region**, as the design draws them
  (the screenshot test found the end handle 2 px outside).
- **`setEdits(edits, false)`** lets a new selection drop the old edits without
  restarting the old sample, which then fades out as it was.
- **The standalone's tempo is a `TempoBox`** (minus, value, plus), not a JUCE
  `Slider`, whose inc/dec layout cannot put the value between the buttons.
- **The Key chip opens a menu** ("Off" and the 24 keys); picking a key turns key
  sync on in it. **The Start chip cycles** at once, beat, bar.
- **The search count needs the library's size**: `LibraryView::sampleCount`
  counts what a search starts from (readable files in enabled folders).
- **"Clear renders" asks first**, naming what is lost, and says so when a file
  could not be deleted (another program holding it, or a folder asma may not
  write).
- **The table is not sortable yet**; the design's sort arrow and the favourite,
  rating and tags columns come with 3c2b.
- **Major keys spelled as a single letter** ("Bass_Loop_F_124") get no key from
  the file name: the parser's existing rule. The reference shows them blank.
- **The screenshot test judges four areas apart.** One figure over the whole
  window let a 20 px layout fault through (2.18% against glyph noise of 1.7%);
  per area the same fault reads 4.26% in the preview against 2.70%.

## Review Focus

The five inputs most likely to bite a person that the tasks' main tests do not
reach; each has its own test, in the task named:

- A project whose trim runs past the end of a file since replaced by a shorter
  one: the end handle stands at the file's end and a drag never goes beyond it
  (Task 8).
- A file shorter than the handles' 10 ms gap: the handles keep their order and a
  drag gives an untrimmed sample (Task 8).
- A long stem auditioned while its overview is read: playback gets its blocks
  first and the picture comes after (Task 3).
- A project whose selected sample has gone: the window opens with nothing
  selected and nothing to drag, without an error (Task 11).
- Renders that cannot be deleted: "Clear renders" says so and keeps offering
  (Task 11).

## File Structure

```
audio/include/asma/audio/Overview.h, audio/src/Overview.cpp   the waveform summary
audio/include/asma/audio/Loader.h, audio/src/Loader.cpp       guard, overview read-through
audio/include/asma/audio/AuditionEngine.h, .cpp               failed, selected(), overview(), setEdits(restart)
audio/include/asma/audio/Sync.h, audio/src/Sync.cpp           tempoClamped
plugin/CMakeLists.txt                                          ui/ sources, fonts, /utf-8
plugin/fonts/                                                  five TTFs and three OFL texts
plugin/src/TempoChip.h, .cpp                                   the chip's words, number formatting
plugin/src/DragOut.h, .cpp                                     drag settings and the footer's line
plugin/src/AsmaProcessor.h, .cpp                               select, setEdits
plugin/src/LibraryView.h, .cpp; Browser.h, .cpp                sampleCount, total
plugin/src/PluginState.h                                       1100x720 default
plugin/src/AsmaEditor.h, .cpp                                  layout and wiring
plugin/src/ui/Theme.h, .cpp                                    palette, sizes, fonts
plugin/src/ui/AsmaLookAndFeel.h, .cpp                          JUCE widgets in the theme
plugin/src/ui/WaveformView.h, .cpp                             waveform, trim, playhead
plugin/src/ui/Controls.h, .cpp                                 ChipButton, SegmentedControl
plugin/src/ui/PreviewPanel.h, .cpp; Footer.h, .cpp             the bottom panel and footer
plugin/src/ui/TopBar.h, .cpp                                   mark, search, tempo source
tests/test_overview.cpp                                        overview and read-through
tests/plugin/test_{tempo_chip,drag_out,theme,waveform_view,
  controls,preview_panel,ui_fidelity}.cpp                      the new UI tests
tests/ui/reference/{main.html,main.png,README.md}              the design, as a picture
```

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-3c2a -b plan-3c2a-app
```

All paths below are relative to `product/asma/.worktrees/plan-3c2a`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
`cmake --build build`.

---

### Task 1: Keep the loader thread alive, and say when a selection failed

Spec section 10 (Loader failure). 3b left `Loader::pump` catching only
`ProbeError`: anything else a load threw ended the loader thread and the host
with it. The editor also needs to know that the selection could not be opened,
for the Tempo chip.

**Files:**

- Modify: `audio/include/asma/audio/AuditionEngine.h`
- Modify: `audio/include/asma/audio/Loader.h`
- Modify: `audio/src/AuditionEngine.cpp`
- Modify: `audio/src/Loader.cpp`
- Modify: `tests/test_audition_engine.cpp` (test)
- Modify: `tests/test_loader.cpp` (test)

**Interfaces:**

- Consumes: plan 3b's `Loader`, `AuditionEngine`, `openSource`.
- Produces: `Loader::Opener`
  (`std::function<std::shared_ptr<SampleSource>(const std::filesystem::path&, PreviewCache&)>`)
  and `Loader(PreviewCache&, Opener = {})`; `EngineStatus::failed` (the
  selection's file could not be opened);
  `std::uint64_t AuditionEngine::selected() const` (the newest selection's
  generation).

- [ ] **Step 1: Write the failing test**

In `tests/test_audition_engine.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_audition_engine.cpp b/tests/test_audition_engine.cpp
index 9bd03bb..0f85a76 100644
--- a/tests/test_audition_engine.cpp
+++ b/tests/test_audition_engine.cpp
@@ -342,12 +342,17 @@ TEST_CASE("AuditionEngine silences the old sample when auto-play lands on a brok
     loop.isLoop = true;
     rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
     rig.run(1024);
+    CHECK_FALSE(rig.engine.status().failed);
     const auto bad = rig.dir.path() / "bad.wav";
     test::writeBytes(bad, "RIFF\x04\x00\x00\x00WAVEjunk");
-    rig.engine.select(bad, {}, true);
+    const std::uint64_t generation = rig.engine.select(bad, {}, true);
+    CHECK(rig.engine.selected() == generation);
     const auto out = rig.run(1024);
     CHECK(out[kFade + 10] == 0.0f);
-    CHECK_FALSE(rig.engine.status().playing);
+    const EngineStatus status = rig.engine.status();
+    CHECK_FALSE(status.playing);
+    CHECK(status.generation == generation);
+    CHECK(status.failed); // the UI says it cannot read the file
 }

 TEST_CASE("AuditionEngine waits for a trimmed start deep in a long file to load", "[engine]")
```

In `tests/test_loader.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_loader.cpp b/tests/test_loader.cpp
index 1a9b089..40254a7 100644
--- a/tests/test_loader.cpp
+++ b/tests/test_loader.cpp
@@ -206,3 +206,41 @@ TEST_CASE("Loader runs on its own thread", "[loader]")
     REQUIRE(p);
     CHECK(p->source->frames() == 700);
 }
+
+TEST_CASE("Loader survives a load that throws anything, and loads the next", "[loader]")
+{
+    Files f;
+    PreviewCache cache;
+    int calls = 0;
+    Loader loader(cache, [&](const fs::path& path, PreviewCache& c) -> std::shared_ptr<SampleSource> {
+        ++calls;
+        if (calls == 1) throw std::runtime_error("disk on fire");
+        if (calls == 2) throw 42;
+        return openSource(path, c);
+    });
+    loader.start(); // an exception escaping the loader thread would end the test run
+    const auto next = [&] {
+        Preview* p = nullptr;
+        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
+        while (!p && std::chrono::steady_clock::now() < deadline) {
+            p = loader.takeReady();
+            std::this_thread::sleep_for(std::chrono::milliseconds(1));
+        }
+        return p;
+    };
+    loader.select(f.a);
+    Preview* p = next();
+    REQUIRE(p);
+    CHECK_FALSE(p->source);
+    CHECK(p->error == "disk on fire");
+    loader.select(f.a);
+    p = next();
+    REQUIRE(p);
+    CHECK_FALSE(p->source);
+    CHECK(p->error == "unknown error");
+    loader.select(f.b);
+    p = next();
+    REQUIRE(p);
+    REQUIRE(p->source);
+    CHECK(p->source->frames() == 700);
+}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[loader],[engine]"`

Expected: the build stops:

```
tests/test_loader.cpp:215:12: error: no matching constructor for initialization of 'Loader'
tests/test_audition_engine.cpp:345:37: error: no member named 'failed' in 'asma::audio::EngineStatus'
tests/test_audition_engine.cpp:349:22: error: no member named 'selected' in 'asma::audio::AuditionEngine'
```

- [ ] **Step 3: Implement**

In `audio/include/asma/audio/AuditionEngine.h`, apply (`git apply` takes it as
is):

```diff
diff --git a/audio/include/asma/audio/AuditionEngine.h b/audio/include/asma/audio/AuditionEngine.h
index 1357bcb..15e43c2 100644
--- a/audio/include/asma/audio/AuditionEngine.h
+++ b/audio/include/asma/audio/AuditionEngine.h
@@ -33,6 +33,7 @@ struct EngineStatus {
     double semitones = 0.0;
     bool tempoSynced = false, keySynced = false;
     bool tempoUnsure = false, keyUnsure = false; // show "?"
+    bool failed = false; // the selection's file could not be opened
     int voices = 0;
 };

@@ -58,6 +59,9 @@ public:

     // Control thread.
     std::uint64_t select(const std::filesystem::path& path, SampleInfo info, bool autoplay);
+    // The newest selection's generation; status() reports on it once the
+    // audio thread holds it.
+    std::uint64_t selected() const { return selected_.load(); }
     void play();
     void stop();
     void setEdits(const Edits& edits);
```

In `audio/include/asma/audio/Loader.h`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/include/asma/audio/Loader.h b/audio/include/asma/audio/Loader.h
index 9c71767..adaf1f8 100644
--- a/audio/include/asma/audio/Loader.h
+++ b/audio/include/asma/audio/Loader.h
@@ -10,6 +10,7 @@
 #include <condition_variable>
 #include <cstdint>
 #include <filesystem>
+#include <functional>
 #include <limits>
 #include <memory>
 #include <mutex>
@@ -39,7 +40,10 @@ class Loader {
 public:
     static constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

-    explicit Loader(PreviewCache& cache) : cache_(cache) {}
+    // Opens a file for playing; openSource unless a test says otherwise.
+    using Opener = std::function<std::shared_ptr<SampleSource>(const std::filesystem::path&, PreviewCache&)>;
+
+    explicit Loader(PreviewCache& cache, Opener opener = {}) : cache_(cache), opener_(std::move(opener)) {}
     ~Loader();
     Loader(const Loader&) = delete;
     Loader& operator=(const Loader&) = delete;
@@ -59,7 +63,8 @@ public:

     // Loads the pending selection, hands finished previews to the audio
     // thread, feeds streams and frees what the audio thread let go. Returns
-    // whether it did anything.
+    // whether it did anything. A load that throws, whatever it throws, gives
+    // a preview with no source and the reason.
     bool pump();

     // Previews alive, for tests.
@@ -77,7 +82,11 @@ private:
         bool delivered = false;
     };

+    // Opens the file and measures what playing it needs. Throws.
+    void load(Preview& preview, const std::filesystem::path& path);
+
     PreviewCache& cache_;
+    Opener opener_;
     std::mutex requestMutex_;
     std::condition_variable wake_;
     std::optional<Request> request_;
```

In `audio/src/AuditionEngine.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/src/AuditionEngine.cpp b/audio/src/AuditionEngine.cpp
index 766f018..387b472 100644
--- a/audio/src/AuditionEngine.cpp
+++ b/audio/src/AuditionEngine.cpp
@@ -10,7 +10,7 @@ namespace asma::audio {

 namespace {

-enum Flags { kTempoSynced = 1, kKeySynced = 2, kTempoUnsure = 4, kKeyUnsure = 8 };
+enum Flags { kTempoSynced = 1, kKeySynced = 2, kTempoUnsure = 4, kKeyUnsure = 8, kFailed = 16 };

 } // namespace

@@ -304,7 +304,8 @@ void AuditionEngine::publish()
     statusRatio_.store(plan_.ratio, std::memory_order_relaxed);
     statusSemitones_.store(plan_.semitones, std::memory_order_relaxed);
     statusFlags_.store((plan_.tempoSynced ? kTempoSynced : 0) | (plan_.keySynced ? kKeySynced : 0)
-                           | (plan_.tempoUnsure ? kTempoUnsure : 0) | (plan_.keyUnsure ? kKeyUnsure : 0),
+                           | (plan_.tempoUnsure ? kTempoUnsure : 0) | (plan_.keyUnsure ? kKeyUnsure : 0)
+                           | (current_ && !current_->source ? kFailed : 0),
                        std::memory_order_relaxed);
     statusVoices_.store(voices_.active(), std::memory_order_relaxed);
 }
@@ -322,6 +323,7 @@ EngineStatus AuditionEngine::status() const
     s.keySynced = flags & kKeySynced;
     s.tempoUnsure = flags & kTempoUnsure;
     s.keyUnsure = flags & kKeyUnsure;
+    s.failed = flags & kFailed;
     s.voices = statusVoices_.load(std::memory_order_relaxed);
     return s;
 }
```

In `audio/src/Loader.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/src/Loader.cpp b/audio/src/Loader.cpp
index d05132c..0598e87 100644
--- a/audio/src/Loader.cpp
+++ b/audio/src/Loader.cpp
@@ -62,6 +62,21 @@ void Loader::release(std::uint64_t oldestInUse)
     inUseFrom_.store(std::min(oldestInUse, seen_.load() + 1));
 }

+void Loader::load(Preview& preview, const std::filesystem::path& path)
+{
+    preview.source = opener_ ? opener_(path, cache_) : openSource(path, cache_);
+    // Not analysed yet: a short file is cheap to measure for gain matching.
+    if (const auto* memory = dynamic_cast<const MemorySource*>(preview.source.get()); memory && !preview.info.lufs) {
+        const AudioBuffer& b = memory->buffer();
+        std::vector<float> mono(b.channels[0]);
+        if (b.channelCount() == 2)
+            for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + b.channels[1][i]);
+        const Loudness l = measureLoudness(mono, b.sampleRate);
+        preview.info.lufs = l.lufs;
+        preview.info.peak = l.peak;
+    }
+}
+
 bool Loader::pump()
 {
     bool did = false;
@@ -76,20 +91,16 @@ bool Loader::pump()
         preview->generation = request->generation;
         preview->info = std::move(request->info);
         preview->autoplay = request->autoplay;
+        // Nothing a load throws may escape: it would end the loader thread,
+        // and the whole host with it.
         try {
-            preview->source = openSource(request->path, cache_);
-        } catch (const ProbeError& e) {
+            load(*preview, request->path);
+        } catch (const std::exception& e) {
+            preview->source.reset();
             preview->error = e.what();
-        }
-        // Not analysed yet: a short file is cheap to measure for gain matching.
-        if (const auto* memory = dynamic_cast<const MemorySource*>(preview->source.get()); memory && !preview->info.lufs) {
-            const AudioBuffer& b = memory->buffer();
-            std::vector<float> mono(b.channels[0]);
-            if (b.channelCount() == 2)
-                for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + b.channels[1][i]);
-            const Loudness l = measureLoudness(mono, b.sampleRate);
-            preview->info.lufs = l.lufs;
-            preview->info.peak = l.peak;
+        } catch (...) {
+            preview->source.reset();
+            preview->error = "unknown error";
         }
         const std::lock_guard lock(liveMutex_);
         live_.push_back({std::move(preview), false});
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[loader],[engine]"`

Expected: `All tests passed (115 assertions in 25 test cases)`

- [ ] **Step 5: Commit**

```sh
git add audio/include/asma/audio/AuditionEngine.h audio/include/asma/audio/Loader.h audio/src/AuditionEngine.cpp audio/src/Loader.cpp tests/test_audition_engine.cpp tests/test_loader.cpp
git commit -m "audio: keep the loader thread alive whatever a load throws, and say when a selection failed"
```

---

### Task 2: Say when a tempo sync hit the stretcher's limit

Spec section 8 (Sync) and the Tempo chip's "max" case: `planSync` clamps to
0.25x..4x and now says when it did.

**Files:**

- Modify: `audio/include/asma/audio/Sync.h`
- Modify: `audio/src/Sync.cpp`
- Modify: `tests/test_sync.cpp` (test)

**Interfaces:**

- Consumes: `planSync`, `Stretcher::kMinRatio`, `Stretcher::kMaxRatio`.
- Produces: `bool SyncPlan::tempoClamped`, true when the wanted ratio was
  outside 0.25x..4x (exactly 0.25 or 4 is not clamped).

- [ ] **Step 1: Write the failing test**

In `tests/test_sync.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_sync.cpp b/tests/test_sync.cpp
index b5b766a..81e7387 100644
--- a/tests/test_sync.cpp
+++ b/tests/test_sync.cpp
@@ -38,11 +38,17 @@ TEST_CASE("planSync stretches loops to exactly the host tempo, within the stretc
     CHECK(planSync(loop(70), host(140)).ratio == Catch::Approx(2.0));        // no half time
     CHECK(planSync(loop(120), host(180)).ratio == Catch::Approx(1.5));
     CHECK(planSync(loop(120), host(60)).ratio == Catch::Approx(0.5));
-    CHECK(planSync(loop(120), host(20)).ratio == Catch::Approx(Stretcher::kMinRatio));  // what plays, not what was asked
-    CHECK(planSync(loop(60), host(300)).ratio == Catch::Approx(Stretcher::kMaxRatio));
+    const SyncPlan slow = planSync(loop(120), host(20));
+    CHECK(slow.ratio == Catch::Approx(Stretcher::kMinRatio)); // what plays, not what was asked
+    CHECK(slow.tempoClamped);
+    const SyncPlan fast = planSync(loop(60), host(300));
+    CHECK(fast.ratio == Catch::Approx(Stretcher::kMaxRatio));
+    CHECK(fast.tempoClamped);
+    CHECK_FALSE(planSync(loop(120), host(30)).tempoClamped); // exactly 0.25x is in range
     const SyncPlan p = planSync(loop(100), host(120));
     CHECK(p.tempoSynced);
     CHECK_FALSE(p.tempoUnsure);
+    CHECK_FALSE(p.tempoClamped);
 }

 TEST_CASE("planSync leaves one-shots, unknown tempos and a switched-off sync alone", "[sync]")
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[sync]"`

Expected: the build stops:

```
tests/test_sync.cpp:43:16: error: no member named 'tempoClamped' in 'asma::audio::SyncPlan'
```

- [ ] **Step 3: Implement**

In `audio/include/asma/audio/Sync.h`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/include/asma/audio/Sync.h b/audio/include/asma/audio/Sync.h
index d0ddca2..24190e3 100644
--- a/audio/include/asma/audio/Sync.h
+++ b/audio/include/asma/audio/Sync.h
@@ -52,6 +52,7 @@ struct SyncPlan {
     double ratio = 1.0;     // input frames per output frame, for Stretcher::setTiming
     double semitones = 0.0;
     bool tempoSynced = false;
+    bool tempoClamped = false; // the tempo wanted more than the stretcher's 0.25x..4x
     bool keySynced = false;
     // Sync was wanted but the sample's own value is too uncertain.
     bool tempoUnsure = false;
```

In `audio/src/Sync.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/src/Sync.cpp b/audio/src/Sync.cpp
index eb7ee92..b0e8fcc 100644
--- a/audio/src/Sync.cpp
+++ b/audio/src/Sync.cpp
@@ -55,7 +55,9 @@ SyncPlan planSync(const SampleInfo& info, const SyncSettings& settings)
         if (info.bpm && *info.bpm > 0.0 && info.bpmConfidence >= kMinTempoConfidence) {
             // Exactly the host tempo, however far that stretches the loop, up
             // to what the stretcher plays: the status shows what is heard.
-            plan.ratio = std::clamp(settings.hostBpm / *info.bpm, Stretcher::kMinRatio, Stretcher::kMaxRatio);
+            const double wanted = settings.hostBpm / *info.bpm;
+            plan.ratio = std::clamp(wanted, Stretcher::kMinRatio, Stretcher::kMaxRatio);
+            plan.tempoClamped = wanted < Stretcher::kMinRatio || wanted > Stretcher::kMaxRatio;
             plan.tempoSynced = true;
         } else {
             plan.tempoUnsure = true;
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[sync]"`

Expected: `All tests passed (55 assertions in 7 test cases)`

- [ ] **Step 5: Commit**

```sh
git add audio/include/asma/audio/Sync.h audio/src/Sync.cpp tests/test_sync.cpp
git commit -m "audio: say when a tempo sync hit the stretcher's limit"
```

---

### Task 3: Build each selection's waveform in the loader

Spec section 8 (Overview), the user's choice A of 2026-10-01: computed when the
preview loads, nothing stored in the library. A decoded file gets its overview
at once; a streamed one from a read-through on the loader thread, one
65536-frame chunk per `pump()`, after the stream has been fed, so playback never
waits for the picture.

**Files:**

- Modify: `audio/include/asma/audio/AuditionEngine.h`
- Modify: `audio/include/asma/audio/Loader.h`
- Create: `audio/include/asma/audio/Overview.h`
- Modify: `audio/src/Loader.cpp`
- Create: `audio/src/Overview.cpp`
- Modify: `tests/test_audition_engine.cpp` (test)
- Create: `tests/test_overview.cpp` (test)

**Interfaces:**

- Consumes: task 1's `Loader::load`; `AudioReader`; `MemorySource`,
  `StreamSource`.
- Produces:
  `struct Overview { static constexpr int kPoints = 2048; std::int64_t frames; int sampleRate; std::vector<std::vector<float>> min, max; int channels() const; double seconds() const; }`;
  `class OverviewBuilder(std::int64_t frames, int channels, int sampleRate)`
  with `add(const float* const*, std::int64_t)`, `done()`, `overview()`;
  `Overview makeOverview(const AudioBuffer&)`;
  `std::shared_ptr<const Overview> Loader::overview(std::uint64_t generation) const`;
  `std::shared_ptr<const Overview> AuditionEngine::overview() const` (the newest
  selection's, or null).

- [ ] **Step 1: Write the failing test**

In `tests/test_audition_engine.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_audition_engine.cpp b/tests/test_audition_engine.cpp
index 0f85a76..9f4c907 100644
--- a/tests/test_audition_engine.cpp
+++ b/tests/test_audition_engine.cpp
@@ -371,3 +371,17 @@ TEST_CASE("AuditionEngine waits for a trimmed start deep in a long file to load"
     // cutting in once the block turns up.
     CHECK(out[first] < counting(800001)[800000] / 2.0f);
 }
+
+TEST_CASE("AuditionEngine hands the UI the selection's waveform", "[engine]")
+{
+    Rig rig;
+    rig.engine.select(rig.file("a.wav", counting(4800)), {}, false);
+    CHECK_FALSE(rig.engine.overview()); // not loaded yet
+    rig.run(kBlock);
+    const auto o = rig.engine.overview();
+    REQUIRE(o);
+    CHECK(o->frames == 4800);
+    CHECK(o->max[0][Overview::kPoints - 1] == counting(4800)[4799]);
+    rig.engine.select(rig.file("b.wav", counting(2400)), {}, false);
+    CHECK_FALSE(rig.engine.overview()); // the old one is not the selection's
+}
```

Create `tests/test_overview.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/Loader.h"
#include "asma/audio/Overview.h"
#include "asma/audio/StreamSource.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kPoints = Overview::kPoints;

// A bumpy signal, so a min and max computed over the wrong frames show.
std::vector<float> bumpy(std::size_t n, float phase = 0.0f)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = 0.5f * std::sin(0.013f * static_cast<float>(i) + phase) + 0.3f * std::sin(0.31f * static_cast<float>(i));
    return x;
}

bool same(const Overview& a, const Overview& b)
{
    return a.frames == b.frames && a.sampleRate == b.sampleRate && a.min == b.min && a.max == b.max;
}

// Pumps until the loader has an overview for `generation`, at most `limit` times.
std::shared_ptr<const Overview> pumpForOverview(Loader& loader, std::uint64_t generation, int limit = 1000)
{
    for (int i = 0; i < limit; ++i) {
        if (auto o = loader.overview(generation)) return o;
        loader.pump();
    }
    return loader.overview(generation);
}

} // namespace

TEST_CASE("makeOverview keeps each stretch's lowest and highest sample", "[overview]")
{
    AudioBuffer b;
    b.sampleRate = 1000;
    std::vector<float> up(2 * kPoints), down(2 * kPoints);
    for (std::size_t i = 0; i < up.size(); ++i) {
        up[i] = static_cast<float>(i) / 10000.0f;
        down[i] = -up[i];
    }
    b.channels = {up, down};
    const Overview o = makeOverview(b);
    CHECK(o.channels() == 2);
    CHECK(o.frames == 2 * kPoints);
    CHECK(o.seconds() == 2 * kPoints / 1000.0);
    // Two frames per point: point i holds frames 2i and 2i + 1.
    CHECK(o.min[0][0] == up[0]);
    CHECK(o.max[0][0] == up[1]);
    CHECK(o.min[0][kPoints - 1] == up[2 * kPoints - 2]);
    CHECK(o.max[0][kPoints - 1] == up[2 * kPoints - 1]);
    CHECK(o.min[1][5] == down[11]);
    CHECK(o.max[1][5] == down[10]);
}

TEST_CASE("a file shorter than the overview leaves empty points at zero", "[overview]")
{
    AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(1000, 0.25f)};
    const Overview o = makeOverview(b);
    CHECK(o.max[0][0] == 0.25f);  // frame 0
    CHECK(o.max[0][1] == 0.0f);   // between frames 0 and 1: empty
    CHECK(o.max[0][2] == 0.25f);  // frame 1 lands on point 2
    CHECK(o.min[0][kPoints - 1] == 0.0f);
}

TEST_CASE("OverviewBuilder gives the same in chunks as in one go", "[overview]")
{
    AudioBuffer b;
    b.sampleRate = 44100;
    b.channels = {bumpy(100003), bumpy(100003, 1.0f)};
    OverviewBuilder chunks(b.frames(), 2, b.sampleRate);
    for (std::int64_t at = 0; at < b.frames(); at += 777) {
        const float* in[] = {b.channels[0].data() + at, b.channels[1].data() + at};
        chunks.add(in, std::min<std::int64_t>(777, b.frames() - at));
    }
    CHECK(chunks.done());
    CHECK(same(chunks.overview(), makeOverview(b)));
}

TEST_CASE("Loader has a short file's overview as soon as it loads", "[overview]")
{
    TempDir dir;
    const auto file = dir.path() / "short.wav";
    test::writeWavFloat(file, 1000, {bumpy(5000)});
    PreviewCache cache;
    Loader loader(cache);
    const auto generation = loader.select(file);
    loader.pump();
    const auto o = loader.overview(generation);
    REQUIRE(o);
    CHECK(same(*o, makeOverview(loadAudio(file))));
}

TEST_CASE("Loader reads a streamed file through for its overview after it starts playing", "[overview]")
{
    TempDir dir;
    const auto file = dir.path() / "long.wav";
    test::writeWavFloat(file, 1000, {bumpy(300000), bumpy(300000, 2.0f)}); // 300 s: streams
    PreviewCache cache;
    Loader loader(cache);
    const auto generation = loader.select(file);
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    CHECK(dynamic_cast<StreamSource*>(p->source.get()));
    CHECK_FALSE(loader.overview(generation)); // playback did not wait for it
    loader.release(generation);
    const auto o = pumpForOverview(loader, generation);
    REQUIRE(o);
    CHECK(same(*o, makeOverview(loadAudio(file))));
}

TEST_CASE("Loader drops an overview the selection moved away from", "[overview]")
{
    TempDir dir;
    const auto longFile = dir.path() / "long.wav";
    const auto shortFile = dir.path() / "short.wav";
    test::writeWavFloat(longFile, 1000, {bumpy(300000)});
    test::writeWavFloat(shortFile, 1000, {bumpy(2000)});
    PreviewCache cache;
    Loader loader(cache);
    const auto first = loader.select(longFile);
    loader.pump();
    const auto second = loader.select(shortFile);
    CHECK(pumpForOverview(loader, second));
    for (int i = 0; i < 50; ++i) loader.pump();
    CHECK_FALSE(loader.overview(first));
}

TEST_CASE("Loader leaves out the overview of a file that cannot be opened", "[overview]")
{
    TempDir dir;
    PreviewCache cache;
    Loader loader(cache);
    const auto generation = loader.select(dir.path() / "gone.wav");
    loader.pump();
    CHECK_FALSE(loader.overview(generation));
}

TEST_CASE("Loader keeps a streamed file fed while it reads it through", "[overview]")
{
    TempDir dir;
    const auto file = dir.path() / "long.wav";
    test::writeWavFloat(file, 1000, {bumpy(300000)}); // 300 s: streams
    PreviewCache cache;
    Loader loader(cache);
    const auto generation = loader.select(file);
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    loader.release(generation);
    p->source->hint(200000, 1); // the playhead jumps deep into the file
    loader.pump();
    CHECK(p->source->ready(200000));      // playback got its block first
    CHECK_FALSE(loader.overview(generation)); // the picture is still coming
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[overview],[loader],[engine]"`

Expected: the build stops:

```
tests/test_overview.cpp:4:10: fatal error: 'asma/audio/Overview.h' file not found
tests/test_audition_engine.cpp:379:28: error: no member named 'overview' in 'asma::audio::AuditionEngine'
```

- [ ] **Step 3: Implement**

In `audio/include/asma/audio/AuditionEngine.h`, apply (`git apply` takes it as
is):

```diff
diff --git a/audio/include/asma/audio/AuditionEngine.h b/audio/include/asma/audio/AuditionEngine.h
index 15e43c2..b587780 100644
--- a/audio/include/asma/audio/AuditionEngine.h
+++ b/audio/include/asma/audio/AuditionEngine.h
@@ -13,6 +13,7 @@
 #include <atomic>
 #include <cstdint>
 #include <filesystem>
+#include <memory>
 #include <vector>

 namespace asma::audio {
@@ -62,6 +63,8 @@ public:
     // The newest selection's generation; status() reports on it once the
     // audio thread holds it.
     std::uint64_t selected() const { return selected_.load(); }
+    // The newest selection's waveform once the loader has built it. Any thread.
+    std::shared_ptr<const Overview> overview() const { return loader_.overview(selected()); }
     void play();
     void stop();
     void setEdits(const Edits& edits);
```

In `audio/include/asma/audio/Loader.h`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/include/asma/audio/Loader.h b/audio/include/asma/audio/Loader.h
index adaf1f8..0e4609a 100644
--- a/audio/include/asma/audio/Loader.h
+++ b/audio/include/asma/audio/Loader.h
@@ -1,10 +1,12 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #pragma once

+#include "asma/audio/Overview.h"
 #include "asma/audio/PreviewCache.h"
 #include "asma/audio/SampleInfo.h"
 #include "asma/audio/SampleSource.h"
 #include "asma/audio/SpscQueue.h"
+#include "asma/core/AudioReader.h"

 #include <atomic>
 #include <condition_variable>
@@ -67,9 +69,17 @@ public:
     // a preview with no source and the reason.
     bool pump();

+    // The newest selection's waveform once it is built, else null: at once
+    // for a file decoded whole, after a read-through that follows playback
+    // for a streamed one. Any thread.
+    std::shared_ptr<const Overview> overview(std::uint64_t generation) const;
+
     // Previews alive, for tests.
     std::size_t liveCount() const;

+    // Frames the read-through for an overview reads per pump().
+    static constexpr std::uint64_t kOverviewChunk = 65536;
+
 private:
     struct Request {
         std::uint64_t generation = 0;
@@ -81,9 +91,19 @@ private:
         std::shared_ptr<Preview> preview;
         bool delivered = false;
     };
+    // The read-through of a streamed file for its overview.
+    struct OverviewJob {
+        std::uint64_t generation = 0;
+        std::filesystem::path path;
+        std::unique_ptr<AudioReader> reader;
+        std::optional<OverviewBuilder> builder;
+    };

     // Opens the file and measures what playing it needs. Throws.
     void load(Preview& preview, const std::filesystem::path& path);
+    // Reads the next chunk for the overview; false when there was nothing to do.
+    bool stepOverview(std::uint64_t newest);
+    void publishOverview(std::uint64_t generation, Overview overview);

     PreviewCache& cache_;
     Opener opener_;
@@ -98,6 +118,12 @@ private:
     std::atomic<std::uint64_t> seen_{0};         // newest generation takeReady popped
     std::atomic<std::uint64_t> inUseFrom_{1};    // nothing older is in use

+    std::optional<OverviewJob> overviewJob_; // pump() only
+    std::vector<std::vector<float>> overviewScratch_;
+    mutable std::mutex overviewMutex_;
+    std::uint64_t overviewGeneration_ = 0;
+    std::shared_ptr<const Overview> overview_;
+
     std::thread thread_;
     std::atomic<bool> stop_{false};
 };
```

Create `audio/include/asma/audio/Overview.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleSource.h"

#include <cstdint>
#include <vector>

namespace asma::audio {

// What the UI draws for a file: the lowest and highest sample of each of
// kPoints equal stretches of it, per channel. Point i covers the frames f
// with f * kPoints / frames == i; a file shorter than kPoints frames leaves
// some points empty, at zero.
struct Overview {
    static constexpr int kPoints = 2048;
    std::int64_t frames = 0;
    int sampleRate = 0;
    std::vector<std::vector<float>> min, max; // [channel][point]

    int channels() const { return static_cast<int>(min.size()); }
    double seconds() const { return sampleRate > 0 ? static_cast<double>(frames) / sampleRate : 0.0; }
};

// Builds an Overview from the frames of a file of known length, a chunk at a
// time and in order, so a long file can be summarised as it is read.
class OverviewBuilder {
public:
    OverviewBuilder(std::int64_t frames, int channels, int sampleRate);
    // The next n frames, planar, channels() buffers.
    void add(const float* const* in, std::int64_t n);
    bool done() const { return seen_ >= overview_.frames; }
    const Overview& overview() const { return overview_; }

private:
    Overview overview_;
    std::int64_t seen_ = 0;
    std::vector<bool> touched_; // per point: has a frame landed in it yet
};

Overview makeOverview(const AudioBuffer& buffer);

} // namespace asma::audio
```

In `audio/src/Loader.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/src/Loader.cpp b/audio/src/Loader.cpp
index 0598e87..4fba144 100644
--- a/audio/src/Loader.cpp
+++ b/audio/src/Loader.cpp
@@ -75,6 +75,55 @@ void Loader::load(Preview& preview, const std::filesystem::path& path)
         preview.info.lufs = l.lufs;
         preview.info.peak = l.peak;
     }
+    if (const auto* memory = dynamic_cast<const MemorySource*>(preview.source.get()))
+        publishOverview(preview.generation, makeOverview(memory->buffer()));
+    else if (preview.source)
+        overviewJob_ = OverviewJob{preview.generation, path, nullptr, std::nullopt};
+}
+
+bool Loader::stepOverview(std::uint64_t newest)
+{
+    if (!overviewJob_) return false;
+    if (overviewJob_->generation != newest) { // the selection moved on
+        overviewJob_.reset();
+        return true;
+    }
+    // A failure here only costs the picture: the preview plays on.
+    try {
+        OverviewJob& job = *overviewJob_;
+        if (!job.reader) {
+            job.reader = AudioReader::open(job.path);
+            job.builder.emplace(static_cast<std::int64_t>(job.reader->frames()), job.reader->channels(),
+                                job.reader->sampleRate());
+            overviewScratch_.assign(static_cast<std::size_t>(job.reader->channels()), std::vector<float>(kOverviewChunk));
+        }
+        std::vector<float*> out;
+        for (auto& c : overviewScratch_) out.push_back(c.data());
+        const std::uint64_t got = job.reader->read(out.data(), kOverviewChunk);
+        std::vector<const float*> in(out.begin(), out.end());
+        job.builder->add(in.data(), static_cast<std::int64_t>(got));
+        if (job.builder->done() || got == 0) {
+            publishOverview(job.generation, job.builder->overview());
+            overviewJob_.reset();
+        }
+    } catch (...) {
+        overviewJob_.reset();
+    }
+    return true;
+}
+
+void Loader::publishOverview(std::uint64_t generation, Overview overview)
+{
+    auto shared = std::make_shared<const Overview>(std::move(overview));
+    const std::lock_guard lock(overviewMutex_);
+    overviewGeneration_ = generation;
+    overview_ = std::move(shared);
+}
+
+std::shared_ptr<const Overview> Loader::overview(std::uint64_t generation) const
+{
+    const std::lock_guard lock(overviewMutex_);
+    return generation == overviewGeneration_ ? overview_ : nullptr;
 }

 bool Loader::pump()
@@ -91,6 +140,7 @@ bool Loader::pump()
         preview->generation = request->generation;
         preview->info = std::move(request->info);
         preview->autoplay = request->autoplay;
+        overviewJob_.reset(); // a new selection: the old read-through is moot
         // Nothing a load throws may escape: it would end the loader thread,
         // and the whole host with it.
         try {
@@ -120,6 +170,8 @@ bool Loader::pump()
             if (auto* stream = dynamic_cast<StreamSource*>(l.preview->source.get())) did |= stream->fill() > 0;

     const Preview* newest = live_.empty() ? nullptr : live_.back().preview.get();
+    // After the streams: playing comes before drawing.
+    did |= stepOverview(newest ? newest->generation : 0);
     const auto before = live_.size();
     std::erase_if(live_, [&](const Live& l) {
         if (!l.delivered) return l.preview.get() != newest; // superseded before the audio thread saw it
```

Create `audio/src/Overview.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Overview.h"

#include <algorithm>

namespace asma::audio {

OverviewBuilder::OverviewBuilder(std::int64_t frames, int channels, int sampleRate)
{
    overview_.frames = std::max<std::int64_t>(frames, 0);
    overview_.sampleRate = sampleRate;
    const auto points = static_cast<std::size_t>(Overview::kPoints);
    overview_.min.assign(static_cast<std::size_t>(channels), std::vector<float>(points, 0.0f));
    overview_.max.assign(static_cast<std::size_t>(channels), std::vector<float>(points, 0.0f));
    touched_.assign(points, false);
}

void OverviewBuilder::add(const float* const* in, std::int64_t n)
{
    n = std::min(n, overview_.frames - seen_);
    for (std::int64_t i = 0; i < n; ++i) {
        const auto point = static_cast<std::size_t>((seen_ + i) * Overview::kPoints / overview_.frames);
        const bool first = !touched_[point];
        touched_[point] = true;
        for (std::size_t c = 0; c < overview_.min.size(); ++c) {
            const float x = in[c][i];
            float& lo = overview_.min[c][point];
            float& hi = overview_.max[c][point];
            lo = first ? x : std::min(lo, x);
            hi = first ? x : std::max(hi, x);
        }
    }
    seen_ += std::max<std::int64_t>(n, 0);
}

Overview makeOverview(const AudioBuffer& buffer)
{
    OverviewBuilder builder(buffer.frames(), buffer.channelCount(), buffer.sampleRate);
    std::vector<const float*> in;
    for (const auto& c : buffer.channels) in.push_back(c.data());
    builder.add(in.data(), buffer.frames());
    return builder.overview();
}

} // namespace asma::audio
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[overview],[loader],[engine]"`

Expected: `All tests passed (148 assertions in 34 test cases)`

- [ ] **Step 5: Commit**

```sh
git add audio/include/asma/audio/AuditionEngine.h audio/include/asma/audio/Loader.h audio/include/asma/audio/Overview.h audio/src/Loader.cpp audio/src/Overview.cpp tests/test_audition_engine.cpp tests/test_overview.cpp
git commit -m "audio: build each selection's waveform in the loader, at once when decoded and after playback starts when streamed"
```

---

### Task 4: Edits belong to the selected sample

Spec section 9 (Editing in the preview): selecting another sample resets trim,
direction and loop mode, so moving through the table plays each sample as it is.
The old sample must fade out as it was, so the reset reaches the engine without
restarting it.

**Files:**

- Modify: `audio/include/asma/audio/AuditionEngine.h`
- Modify: `audio/src/AuditionEngine.cpp`
- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaProcessor.cpp`
- Modify: `plugin/src/AsmaProcessor.h`
- Modify: `tests/plugin/test_processor_audio.cpp` (test)
- Modify: `tests/test_audition_engine.cpp` (test)

**Interfaces:**

- Consumes: `AuditionEngine::select`, `PluginState::edits`.
- Produces: `AuditionEngine::setEdits(const Edits&, bool restart = true)`;
  `std::uint64_t AsmaProcessor::select(const std::filesystem::path&, const audio::SampleInfo&)`
  (records the selection, drops its edits, plays it);
  `void AsmaProcessor::setEdits(const audio::Edits&)` (saves and applies,
  restarting).

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_processor_audio.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_processor_audio.cpp b/tests/plugin/test_processor_audio.cpp
index f49ea26..e370185 100644
--- a/tests/plugin/test_processor_audio.cpp
+++ b/tests/plugin/test_processor_audio.cpp
@@ -1,6 +1,7 @@
 // SPDX-License-Identifier: GPL-3.0-only
 #include "PluginTestUtil.h"
 #include "Signals.h"
+#include "asma/core/Fs.h"

 #include <catch2/catch_approx.hpp>
 #include <catch2/catch_test_macros.hpp>
@@ -114,3 +115,24 @@ TEST_CASE("a quantised start lands on the bar inside a block split by MIDI", "[p
     CHECK(first == 240);
     rig.p.setPlayHead(nullptr);
 }
+
+TEST_CASE("edits belong to the selected sample", "[processor]")
+{
+    Rig rig;
+    const auto a = rig.dir.path() / "a.wav";
+    const auto b = rig.dir.path() / "b.wav";
+    asma::test::writeWavFloat(a, kRate, {constant(kRate, 0.5f)});
+    asma::test::writeWavFloat(b, kRate, {constant(kRate, 0.25f)});
+    rig.p.select(a, {});
+    asma::audio::Edits e;
+    e.direction = asma::audio::Direction::Reverse;
+    e.trimStart = 0.1;
+    rig.p.setEdits(e);
+    CHECK(rig.p.pluginState().edits.direction == asma::audio::Direction::Reverse); // saved with the project
+    CHECK(rig.p.pluginState().edits.trimStart == 0.1);
+    rig.p.select(b, {});
+    const auto state = rig.p.pluginState();
+    CHECK(state.edits.direction == asma::audio::Direction::Forward); // b plays as it is
+    CHECK(state.edits.trimStart == 0.0);
+    CHECK(fs::equivalent(asma::fromUtf8(state.selected), b));
+}
```

In `tests/test_audition_engine.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/test_audition_engine.cpp b/tests/test_audition_engine.cpp
index 9f4c907..27b662f 100644
--- a/tests/test_audition_engine.cpp
+++ b/tests/test_audition_engine.cpp
@@ -385,3 +385,21 @@ TEST_CASE("AuditionEngine hands the UI the selection's waveform", "[engine]")
     rig.engine.select(rig.file("b.wav", counting(2400)), {}, false);
     CHECK_FALSE(rig.engine.overview()); // the old one is not the selection's
 }
+
+TEST_CASE("AuditionEngine can take edits for the next selection without restarting this one", "[engine]")
+{
+    Rig rig;
+    const auto samples = counting(48000);
+    rig.engine.select(rig.file("a.wav", samples), {}, true);
+    rig.run(2048);
+    Edits reversed;
+    reversed.direction = Direction::Reverse;
+    rig.engine.setEdits(reversed, false);
+    auto out = rig.run(kBlock);
+    CHECK(out[0] == samples[2048]); // carries on forward, no fade
+    CHECK(out[kBlock - 1] == samples[2048 + kBlock - 1]);
+    rig.engine.select(rig.file("b.wav", samples), {}, true);
+    out = rig.run(1024);
+    // The old one fades out, then the new one plays backwards from its end.
+    CHECK(out[2 * kFade + 10] == samples[47999 - kFade - 10]);
+}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target asma_tests asma_plugin_tests`

Expected: the build stops:

```
tests/test_audition_engine.cpp:397:35: error: too many arguments to function call, expected single argument 'edits', have 2 arguments
plugin/test_processor_audio.cpp:126:11: error: no member named 'select' in 'asma::app::AsmaProcessor'
plugin/test_processor_audio.cpp:130:11: error: no member named 'setEdits' in 'asma::app::AsmaProcessor'
```

- [ ] **Step 3: Implement**

In `audio/include/asma/audio/AuditionEngine.h`, apply (`git apply` takes it as
is):

```diff
diff --git a/audio/include/asma/audio/AuditionEngine.h b/audio/include/asma/audio/AuditionEngine.h
index b587780..5a7bcf9 100644
--- a/audio/include/asma/audio/AuditionEngine.h
+++ b/audio/include/asma/audio/AuditionEngine.h
@@ -67,7 +67,9 @@ public:
     std::shared_ptr<const Overview> overview() const { return loader_.overview(selected()); }
     void play();
     void stop();
-    void setEdits(const Edits& edits);
+    // An edit restarts what plays; restart false keeps it playing as it is
+    // and applies the edits from the next start (a new selection's).
+    void setEdits(const Edits& edits, bool restart = true);
     // hostBpm here is the standalone's manual tempo; a transport tempo wins.
     void setSync(const SyncSettings& sync);
     void setGainMatch(bool on);
```

In `audio/src/AuditionEngine.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/audio/src/AuditionEngine.cpp b/audio/src/AuditionEngine.cpp
index 387b472..7d92ff6 100644
--- a/audio/src/AuditionEngine.cpp
+++ b/audio/src/AuditionEngine.cpp
@@ -54,11 +54,12 @@ void AuditionEngine::stop()
     push(c);
 }

-void AuditionEngine::setEdits(const Edits& edits)
+void AuditionEngine::setEdits(const Edits& edits, bool restart)
 {
     Command c;
     c.type = Command::Type::Edits;
     c.edits = edits;
+    c.flag = restart;
     push(c);
 }

@@ -103,7 +104,7 @@ void AuditionEngine::handle(const Command& c)
         break;
     case Command::Type::Edits:
         edits_ = c.edits;
-        if (sounding && !(state_ == State::Stopping && !restartAfterFade_)) requestStart(0);
+        if (c.flag && sounding && !(state_ == State::Stopping && !restartAfterFade_)) requestStart(0);
         break;
     case Command::Type::Sync: {
         // A new manual tempo alone is followed as the loop plays, like a host
```

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index 0b82f13..fa802f3 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -268,9 +268,7 @@ void AsmaEditor::selectedRowsChanged(int lastRowSelected)
 {
     if (quietSelection_ || lastRowSelected < 0) return;
     scanMessage_.clear();
-    const auto path = browser_.path(lastRowSelected);
-    processor_.engine().select(path, browser_.info(lastRowSelected), true);
-    processor_.updateState([&](PluginState& s) { s.selected = toUtf8(path); });
+    processor_.select(browser_.path(lastRowSelected), browser_.info(lastRowSelected));
     updateStatus();
 }

```

In `plugin/src/AsmaProcessor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.cpp b/plugin/src/AsmaProcessor.cpp
index e577652..54b440e 100644
--- a/plugin/src/AsmaProcessor.cpp
+++ b/plugin/src/AsmaProcessor.cpp
@@ -127,6 +127,22 @@ void AsmaProcessor::setPluginState(const PluginState& given)
     }
 }

+std::uint64_t AsmaProcessor::select(const std::filesystem::path& path, const audio::SampleInfo& info)
+{
+    updateState([&](PluginState& s) {
+        s.selected = toUtf8(path);
+        s.edits = {};
+    });
+    engine_.setEdits({}, false); // the old sample fades out as it was
+    return engine_.select(path, info, true);
+}
+
+void AsmaProcessor::setEdits(const audio::Edits& edits)
+{
+    updateState([&](PluginState& s) { s.edits = edits; });
+    engine_.setEdits(edits);
+}
+
 void AsmaProcessor::setManualBpm(double bpm)
 {
     audio::SyncSettings sync;
```

In `plugin/src/AsmaProcessor.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaProcessor.h b/plugin/src/AsmaProcessor.h
index e3c1b40..c6824d8 100644
--- a/plugin/src/AsmaProcessor.h
+++ b/plugin/src/AsmaProcessor.h
@@ -53,6 +53,13 @@ public:

     audio::AuditionEngine& engine() { return engine_; }

+    // Message thread. Auditions a file as it is, without the last one's edits,
+    // and remembers it as the project's selection.
+    std::uint64_t select(const std::filesystem::path& path, const audio::SampleInfo& info);
+    // Message thread. The selection's edits: saved with the project and
+    // applied at once, restarting what plays.
+    void setEdits(const audio::Edits& edits);
+
     // Any thread but the audio thread. Setting applies the settings to the
     // engine and selects the saved sample again, without playing it.
     PluginState pluginState() const;
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_tests && ./build/tests/asma_tests "[engine]" && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[processor]"`

Expected: `All tests passed (61 assertions in 17 test cases)`, then
`All tests passed (29 assertions in 8 test cases)`

- [ ] **Step 5: Commit**

```sh
git add audio/include/asma/audio/AuditionEngine.h audio/src/AuditionEngine.cpp plugin/src/AsmaEditor.cpp plugin/src/AsmaProcessor.cpp plugin/src/AsmaProcessor.h tests/plugin/test_processor_audio.cpp tests/test_audition_engine.cpp
git commit -m "app: edits belong to the selected sample; a new selection plays as it is"
```

---

### Task 5: The Tempo chip's words

Spec section 9 (Tempo chip). One JUCE-free function says what the chip shows, so
every case is tested without pixels. Numbers are formatted with integer
arithmetic, never through the locale. The UI's arrows and middle dots are UTF-8
in the source, so MSVC gets `/utf-8` for `asma_ui` (the plugin targets do not go
through `asma_set_warnings`, which sets it for the core).

**Files:**

- Modify: `plugin/CMakeLists.txt`
- Create: `plugin/src/TempoChip.cpp`
- Create: `plugin/src/TempoChip.h`
- Create: `tests/plugin/test_tempo_chip.cpp` (test)

**Interfaces:**

- Consumes: task 2's `SyncPlan::tempoClamped`; `planSync`.
- Produces: `enum class Tone { Synced, Warning, Muted }`;
  `struct ChipText { std::string text; Tone tone; }`;
  `ChipText tempoChip(const audio::SampleInfo&, const audio::SyncSettings& /*hostBpm = tempo in force*/, bool failed)`;
  `std::string bpmText(double)` ("120", "97.5"); `std::string ratioText(double)`
  ("1.50").

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_tempo_chip.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TempoChip.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::ChipText;
using app::Tone;
using app::tempoChip;

namespace {

audio::SampleInfo loop(double bpm, double confidence = 0.9)
{
    audio::SampleInfo s;
    s.bpm = bpm;
    s.bpmConfidence = confidence;
    s.isLoop = true;
    return s;
}

audio::SyncSettings at(double bpm)
{
    audio::SyncSettings s;
    s.hostBpm = bpm;
    return s;
}

void check(const ChipText& chip, const char* text, Tone tone)
{
    CHECK(chip.text == text);
    CHECK(chip.tone == tone);
}

} // namespace

TEST_CASE("the Tempo chip says what sync does, and why when it does nothing", "[chip]")
{
    check(tempoChip(loop(120), at(180), false), "120 → 180 · x1.50", Tone::Synced);
    check(tempoChip(loop(120), at(20), false), "120 → 30 · x0.25 max", Tone::Warning);
    check(tempoChip(loop(60), at(300), false), "60 → 240 · x4.00 max", Tone::Warning);
    check(tempoChip(loop(97.3, 0.21), at(120), false), "~97 ? · plays as is", Tone::Warning);

    audio::SampleInfo kick;
    kick.isLoop = false;
    check(tempoChip(kick, at(120), false), "one-shot · plays as is", Tone::Muted);
    check(tempoChip(audio::SampleInfo{}, at(120), false), "not a loop · plays as is", Tone::Muted); // a long stem

    audio::SyncSettings off = at(120);
    off.tempo = false;
    check(tempoChip(loop(120), off, false), "off", Tone::Muted);
    check(tempoChip(loop(120), at(0), false), "no tempo · plays as is", Tone::Muted); // a host that sends none
    check(tempoChip(loop(120), at(120), true), "can't read this file", Tone::Warning);
}

TEST_CASE("a loop the library knows no tempo for is a guess with nothing to show", "[chip]")
{
    audio::SampleInfo s;
    s.isLoop = true;
    check(tempoChip(s, at(120), false), "? · plays as is", Tone::Warning);
}

TEST_CASE("tempos and ratios read the same in every locale", "[chip]")
{
    CHECK(app::bpmText(120.0) == "120");
    CHECK(app::bpmText(97.46) == "97.5");
    CHECK(app::bpmText(127.96) == "128");
    CHECK(app::ratioText(1.5) == "1.50");
    CHECK(app::ratioText(0.25) == "0.25");
    CHECK(app::ratioText(1.0 / 3.0) == "0.33");
    CHECK(app::ratioText(1.006) == "1.01"); // rounds, never truncates
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[chip]"`

Expected: the build stops:

```
plugin/test_tempo_chip.cpp:2:10: fatal error: 'TempoChip.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/CMakeLists.txt`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/CMakeLists.txt b/plugin/CMakeLists.txt
index 2c4e7f0..34c8219 100644
--- a/plugin/CMakeLists.txt
+++ b/plugin/CMakeLists.txt
@@ -12,6 +12,9 @@ target_link_libraries(asma_ui INTERFACE
   juce::juce_audio_processors
   juce::juce_gui_basics
   juce::juce_recommended_config_flags)
+# The UI's text is UTF-8 in the source (arrows, middle dots): MSVC must read
+# it so and keep it so, whatever the system code page.
+target_compile_options(asma_ui INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
 target_compile_definitions(asma_ui INTERFACE
   JUCE_WEB_BROWSER=0
   JUCE_USE_CURL=0
```

Create `plugin/src/TempoChip.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TempoChip.h"

#include <cmath>

namespace asma::app {

std::string bpmText(double bpm)
{
    const long long tenths = std::llround(bpm * 10.0);
    std::string text = std::to_string(tenths / 10);
    if (tenths % 10 != 0) text += "." + std::to_string(std::llabs(tenths % 10));
    return text;
}

std::string ratioText(double ratio)
{
    const long long hundredths = std::llround(ratio * 100.0);
    const long long cents = hundredths % 100;
    return std::to_string(hundredths / 100) + "." + (cents < 10 ? "0" : "") + std::to_string(cents);
}

ChipText tempoChip(const audio::SampleInfo& info, const audio::SyncSettings& sync, bool failed)
{
    if (failed) return {"can't read this file", Tone::Warning};
    if (!sync.tempo) return {"off", Tone::Muted};
    if (!info.isLoop) return {"not a loop · plays as is", Tone::Muted};
    if (!*info.isLoop) return {"one-shot · plays as is", Tone::Muted};
    if (sync.hostBpm <= 0.0) return {"no tempo · plays as is", Tone::Muted};
    const audio::SyncPlan plan = audio::planSync(info, sync);
    if (!plan.tempoSynced) {
        const std::string guess = info.bpm ? "~" + std::to_string(std::llround(*info.bpm)) + " " : "";
        return {guess + "? · plays as is", Tone::Warning};
    }
    std::string text = bpmText(*info.bpm) + " → " + bpmText(*info.bpm * plan.ratio) + " · x" + ratioText(plan.ratio);
    if (plan.tempoClamped) return {text + " max", Tone::Warning};
    return {text, Tone::Synced};
}

} // namespace asma::app
```

Create `plugin/src/TempoChip.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"
#include "asma/audio/Sync.h"

#include <string>

namespace asma::app {

// How the chip is coloured: green, amber or grey in the Anode theme.
enum class Tone { Synced, Warning, Muted };

struct ChipText {
    std::string text; // UTF-8
    Tone tone = Tone::Muted;
};

// What the Tempo chip says about the selected sample: what tempo sync does to
// it, and why when it does nothing. sync.hostBpm is the tempo in force (the
// host's, Link's or the manual one); failed: the file could not be opened.
ChipText tempoChip(const audio::SampleInfo& info, const audio::SyncSettings& sync, bool failed);

// A tempo or ratio as the UI writes it, whatever the locale: tenths, with a
// whole number left whole ("120", "97.5"); a ratio to hundredths ("1.50").
std::string bpmText(double bpm);
std::string ratioText(double ratio);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[chip]"`

Expected: `All tests passed (27 assertions in 3 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/CMakeLists.txt plugin/src/TempoChip.cpp plugin/src/TempoChip.h tests/plugin/test_tempo_chip.cpp
git commit -m "app: the Tempo chip's words, for every case sync meets"
```

---

### Task 6: Say what a drag-out carries

Spec section 9 (Footer). The drag and the footer must agree, so both build their
`RenderSettings` in one place.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Create: `plugin/src/DragOut.cpp`
- Create: `plugin/src/DragOut.h`
- Create: `tests/plugin/test_drag_out.cpp` (test)

**Interfaces:**

- Consumes: `audio::RenderSettings`, `audio::SyncPlan`, task 5's
  `bpmText`/`ratioText`.
- Produces:
  `audio::RenderSettings dragSettings(const audio::Edits&, const audio::SyncPlan&, int sampleRate)`;
  `std::string dragSummary(const audio::RenderSettings&, std::optional<double> bpm)`
  ("Drag out: the original file" or "Drag out renders: reversed, trimmed,
  stretched to 180 BPM, transposed +2").

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_drag_out.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "DragOut.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::dragSettings;
using app::dragSummary;

TEST_CASE("a drag-out carries the selection's edits and what sync does now", "[drag]")
{
    audio::Edits e;
    e.trimStart = 0.5;
    e.loop = audio::LoopMode::On;
    audio::SyncPlan plan;
    plan.ratio = 1.5;
    plan.semitones = -2.0;
    const audio::RenderSettings s = dragSettings(e, plan, 48000);
    CHECK(s.edits.trimStart == 0.5);
    CHECK(s.ratio == 1.5);
    CHECK(s.semitones == -2.0);
    CHECK(s.sampleRate == 48000);
}

TEST_CASE("the footer says what a drag-out will carry", "[drag]")
{
    audio::RenderSettings s;
    CHECK(dragSummary(s, 120.0) == "Drag out: the original file");
    s.sampleRate = 96000; // the DAW converts on import: not worth a render
    CHECK(dragSummary(s, 120.0) == "Drag out: the original file");
    s.edits.loop = audio::LoopMode::On; // looping is not baked in
    CHECK(dragSummary(s, 120.0) == "Drag out: the original file");

    s.edits.direction = audio::Direction::Reverse;
    s.edits.trimEnd = 6.8;
    s.ratio = 1.5;
    CHECK(dragSummary(s, 120.0) == "Drag out renders: reversed, trimmed, stretched to 180 BPM");
    CHECK(dragSummary(s, std::nullopt) == "Drag out renders: reversed, trimmed, stretched x1.50");

    audio::RenderSettings t;
    t.edits.direction = audio::Direction::PingPong;
    t.semitones = 2.0;
    CHECK(dragSummary(t, 120.0) == "Drag out renders: ping-pong, transposed +2");
    t.semitones = -3.0;
    CHECK(dragSummary(t, 120.0) == "Drag out renders: ping-pong, transposed -3");
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[drag],[editor]"`

Expected: the build stops:

```
plugin/test_drag_out.cpp:2:10: fatal error: 'DragOut.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/src/AsmaEditor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/AsmaEditor.cpp b/plugin/src/AsmaEditor.cpp
index fa802f3..87c01bf 100644
--- a/plugin/src/AsmaEditor.cpp
+++ b/plugin/src/AsmaEditor.cpp
@@ -2,6 +2,7 @@
 #include "AsmaEditor.h"

 #include "AsmaProcessor.h"
+#include "DragOut.h"
 #include "asma/audio/Render.h"
 #include "asma/core/Fs.h"

@@ -289,12 +290,8 @@ bool AsmaEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTar
     const PluginState state = processor_.pluginState();
     audio::SyncSettings sync = state.sync;
     if (processor_.hostBpm() > 0.0) sync.hostBpm = processor_.hostBpm();
-    const audio::SyncPlan plan = audio::planSync(browser_.info(row), sync);
-    audio::RenderSettings settings;
-    settings.edits = state.edits;
-    settings.ratio = plan.ratio;
-    settings.semitones = plan.semitones;
-    settings.sampleRate = static_cast<int>(processor_.sampleRate());
+    const audio::RenderSettings settings =
+        dragSettings(state.edits, audio::planSync(browser_.info(row), sync), static_cast<int>(processor_.sampleRate()));
     std::filesystem::path file = path;
     try {
         file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, browser_.contentHash(row));
```

Create `plugin/src/DragOut.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "DragOut.h"

#include "TempoChip.h"

#include <cmath>
#include <vector>

namespace asma::app {

audio::RenderSettings dragSettings(const audio::Edits& edits, const audio::SyncPlan& plan, int sampleRate)
{
    audio::RenderSettings settings;
    settings.edits = edits;
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones;
    settings.sampleRate = sampleRate;
    return settings;
}

std::string dragSummary(const audio::RenderSettings& settings, std::optional<double> bpm)
{
    if (!settings.changesAudio()) return "Drag out: the original file";
    std::vector<std::string> parts;
    if (settings.edits.direction == audio::Direction::Reverse) parts.push_back("reversed");
    if (settings.edits.direction == audio::Direction::PingPong) parts.push_back("ping-pong");
    if (settings.edits.trimStart > 0.0 || settings.edits.trimEnd >= 0.0) parts.push_back("trimmed");
    if (settings.ratio < 1.0 || settings.ratio > 1.0)
        parts.push_back(bpm ? "stretched to " + bpmText(*bpm * settings.ratio) + " BPM" : "stretched x" + ratioText(settings.ratio));
    if (settings.semitones < 0.0 || settings.semitones > 0.0) {
        const long long st = std::llround(settings.semitones);
        parts.push_back("transposed " + std::string(st > 0 ? "+" : "") + std::to_string(st));
    }
    std::string text = "Drag out renders: ";
    for (std::size_t i = 0; i < parts.size(); ++i) text += (i ? ", " : "") + parts[i];
    return text;
}

} // namespace asma::app
```

Create `plugin/src/DragOut.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"
#include "asma/audio/Render.h"
#include "asma/audio/Sync.h"

#include <optional>
#include <string>

namespace asma::app {

// What dragging the selection out bakes in: its edits and what sync does to
// it now, at the host's sample rate (0 keeps the file's).
audio::RenderSettings dragSettings(const audio::Edits& edits, const audio::SyncPlan& plan, int sampleRate);

// The footer's account of a drag-out: "Drag out: the original file", or
// "Drag out renders: reversed, trimmed, stretched to 180 BPM, transposed +2".
// bpm: the sample's own tempo, to say where a stretch lands; without it the
// ratio is given.
std::string dragSummary(const audio::RenderSettings& settings, std::optional<double> bpm);

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[drag],[editor]"`

Expected: `All tests passed (59 assertions in 10 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/DragOut.cpp plugin/src/DragOut.h tests/plugin/test_drag_out.cpp
git commit -m "app: one place that says what a drag-out carries, and a line for the footer"
```

---

### Task 7: The Anode theme: palette, fonts and LookAndFeel

Spec section 9 (Look). The palette is copied, not shared (anode-common is
proprietary). The three families are embedded as static instances (SIL OFL),
loaded once, and the LookAndFeel maps JUCE's default sans and mono fonts to them
so no system face leaks in. Sources under `plugin/src/ui/` are compiled into
`asma_ui` like the rest.

**Files:**

- Modify: `plugin/CMakeLists.txt`
- Create: `plugin/fonts/Inter-Medium.ttf`
- Create: `plugin/fonts/Inter-Regular.ttf`
- Create: `plugin/fonts/Inter-SemiBold.ttf`
- Create: `plugin/fonts/JetBrainsMono-Regular.ttf`
- Create: `plugin/fonts/OFL-inter.txt`
- Create: `plugin/fonts/OFL-jetbrainsmono.txt`
- Create: `plugin/fonts/OFL-spacegrotesk.txt`
- Create: `plugin/fonts/SpaceGrotesk-SemiBold.ttf`
- Create: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Create: `plugin/src/ui/AsmaLookAndFeel.h`
- Create: `plugin/src/ui/Theme.cpp`
- Create: `plugin/src/ui/Theme.h`
- Create: `tests/plugin/test_theme.cpp` (test)

**Interfaces:**

- Consumes: task 5's `Tone`.
- Produces: `namespace theme` colours (`ground`, `panel`, `surface`, `raised`,
  `border`, `faint`, `text`, `muted`, `amber`, `amberLight`, `cyan`, `cyanDim`,
  `cyanFaint`, `green`), sizes (`kRadius`, `kCardRadius`, `kTopBarHeight` 56,
  `kChipRowHeight` 44, `kSidebarWidth` 220, `kPreviewHeight` 236,
  `kSimilarWidth` 284, `kFooterHeight` 26, `kRowHeight` 30, `kHeaderRowHeight`
  30), `enum class Face { Heading, Text, Medium, SemiBold, Mono }`,
  `juce::Font font(Face, float size)`, `juce::Typeface::Ptr typeface(Face)`,
  `juce::Colour colourFor(Tone)`; `class AsmaLookAndFeel` reading button
  properties `asma.segment` ("first"/"middle"/"last"), `asma.accent`,
  `asma.quiet`, `asma.size`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_theme.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma::app;

TEST_CASE("the theme's fonts are the embedded Anode faces", "[theme]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    CHECK(theme::typeface(theme::Face::Heading)->getName() == "Space Grotesk");
    CHECK(theme::typeface(theme::Face::Text)->getName() == "Inter");
    CHECK(theme::typeface(theme::Face::Mono)->getName() == "JetBrains Mono");
    CHECK(theme::font(theme::Face::Text, 13.0f).getTypefacePtr() == theme::typeface(theme::Face::Text));
}

TEST_CASE("plain fonts come out in Inter, not a system face", "[theme]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaLookAndFeel lnf;
    CHECK(lnf.getTypefaceForFont(juce::Font(juce::FontOptions(14.0f)))->getName() == "Inter");
    CHECK(lnf.getTypefaceForFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, 0)))
              ->getName()
          == "JetBrains Mono");
}

TEST_CASE("a pressed accent segment is amber, a pressed plain one is lifted", "[theme]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaLookAndFeel lnf;
    juce::TextButton b("x");
    b.setLookAndFeel(&lnf);
    b.setBounds(0, 0, 40, 28);
    b.setClickingTogglesState(true);
    b.getProperties().set("asma.segment", "middle");
    const auto colourAt = [&](int x, int y) {
        juce::Image image(juce::Image::ARGB, 40, 28, true, juce::SoftwareImageType{});
        juce::Graphics g(image);
        b.paintEntireComponent(g, false);
        return image.getPixelAt(x, y);
    };
    CHECK(colourAt(30, 4).getAlpha() == 0); // off: transparent, the switch's ground shows
    b.setToggleState(true, juce::dontSendNotification);
    CHECK(colourAt(30, 4) == theme::raised);
    b.getProperties().set("asma.accent", true);
    CHECK(colourAt(30, 4) == theme::amber);
    CHECK(colourAt(0, 4) == theme::border); // the line before a middle segment
    b.setLookAndFeel(nullptr);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[theme]"`

Expected: the build stops:

```
plugin/test_theme.cpp:2:10: fatal error: 'ui/AsmaLookAndFeel.h' file not found
```

- [ ] **Step 3: Implement**

Fetch the fonts (static instances, as Google Fonts serves them to an old
browser) and their licences, then check the files are the ones the prototype
used:

```sh
mkdir -p plugin/fonts && cd plugin/fonts
curl -sfo Inter-Regular.ttf https://fonts.gstatic.com/s/inter/v20/UcCO3FwrK3iLTeHuS_nVMrMxCp50SjIw2boKoduKmMEVuLyfMZg.ttf
curl -sfo Inter-Medium.ttf https://fonts.gstatic.com/s/inter/v20/UcCO3FwrK3iLTeHuS_nVMrMxCp50SjIw2boKoduKmMEVuI6fMZg.ttf
curl -sfo Inter-SemiBold.ttf https://fonts.gstatic.com/s/inter/v20/UcCO3FwrK3iLTeHuS_nVMrMxCp50SjIw2boKoduKmMEVuGKYMZg.ttf
curl -sfo JetBrainsMono-Regular.ttf https://fonts.gstatic.com/s/jetbrainsmono/v24/tDbY2o-flEEny0FZhsfKu5WU4zr3E_BX0PnT8RD8yKxjPQ.ttf
curl -sfo SpaceGrotesk-SemiBold.ttf https://fonts.gstatic.com/s/spacegrotesk/v22/V8mQoQDjQSkFtoMM3T6r8E7mF71Q-gOoraIAEj42Vksj.ttf
for f in inter jetbrainsmono spacegrotesk; do
  curl -sfo OFL-$f.txt https://raw.githubusercontent.com/google/fonts/main/ofl/$f/OFL.txt
done
shasum -a 256 *.ttf
cd ../..
```

Expected:

```
8c883f63b2c4157d997319f2c8bc6995ed4357ef371940d31ca159004a4aae63  Inter-Medium.ttf
1b08e7fc267a5c7e1d614100f604b83e7e8a0be241f0f288faa2b3ac93a683ba  Inter-Regular.ttf
e7a1aaf7eda9f2fad4131725fa556265ec75ca7b2d756260173a040363e8d4f7  Inter-SemiBold.ttf
44ce4a84f20d60f24539bd0cef11f79c29e38609e0f8adf18551c9794a5d9dc3  JetBrainsMono-Regular.ttf
6c0346b8d297ebdc225832833e03e884a26ad99d265ecd3924d46e1ba285ea87  SpaceGrotesk-SemiBold.ttf
```

If a URL has moved, the same files come from
`https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400&family=Space+Grotesk:wght@600`
fetched with `curl -A Mozilla/4.0`; the hashes must still match.

In `plugin/CMakeLists.txt`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/CMakeLists.txt b/plugin/CMakeLists.txt
index 34c8219..8a98260 100644
--- a/plugin/CMakeLists.txt
+++ b/plugin/CMakeLists.txt
@@ -2,10 +2,20 @@
 # asma_ui holds the app's own JUCE code. It is an INTERFACE library so the
 # plugin and the plugin tests each compile it, and JUCE, once.
 add_library(asma_ui INTERFACE)
-file(GLOB ASMA_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
+file(GLOB ASMA_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
 target_sources(asma_ui INTERFACE ${ASMA_UI_SOURCES})
 target_include_directories(asma_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
+# The Anode fonts (SIL OFL, licences beside them), compiled in.
+juce_add_binary_data(asma_fonts NAMESPACE AsmaFonts HEADER_NAME AsmaFonts.h SOURCES
+  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf
+  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Medium.ttf
+  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf
+  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-Regular.ttf
+  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/SpaceGrotesk-SemiBold.ttf)
+set_target_properties(asma_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON) # the LV2 plugin is a shared library
+
 target_link_libraries(asma_ui INTERFACE
+  asma_fonts
   asma::audio
   Ableton::Link
   juce::juce_audio_utils
```

Create `plugin/src/ui/AsmaLookAndFeel.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"

#include "ui/Theme.h"

namespace asma::app {

AsmaLookAndFeel::AsmaLookAndFeel()
{
    using namespace theme;
    setColourScheme({surface, panel, ground, border, text, raised, ground, amber, text});
    setColour(juce::ResizableWindow::backgroundColourId, surface);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::buttonOnColourId, raised);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, text);
    setColour(juce::TextEditor::backgroundColourId, ground);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, border);
    setColour(juce::TextEditor::focusedOutlineColourId, border);
    setColour(juce::TextEditor::highlightColourId, amber.withAlpha(0.3f));
    setColour(juce::CaretComponent::caretColourId, amber);
    setColour(juce::ListBox::backgroundColourId, surface);
    setColour(juce::ListBox::textColourId, text);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TableHeaderComponent::backgroundColourId, surface);
    setColour(juce::TableHeaderComponent::textColourId, muted);
    setColour(juce::TableHeaderComponent::outlineColourId, border);
    setColour(juce::ScrollBar::thumbColourId, border);
    setColour(juce::Label::textColourId, text);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, raised);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::Typeface::Ptr AsmaLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return theme::typeface(theme::Face::Text);
    if (font.getTypefaceName() == juce::Font::getDefaultMonospacedFontName()) return theme::typeface(theme::Face::Mono);
    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font AsmaLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return theme::font(theme::Face::Text, 13.0f); }

void AsmaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted,
                                           bool down)
{
    const auto& props = button.getProperties();
    const auto segment = props["asma.segment"].toString();
    const bool on = button.getToggleState();
    juce::Colour fill = theme::raised;
    if (on && props["asma.accent"]) fill = theme::amber;
    else if (!on && (props["asma.quiet"] || button.getClickingTogglesState())) fill = juce::Colours::transparentBlack;
    if (highlighted || down) fill = fill.isTransparent() ? theme::raised.withAlpha(0.6f) : fill.brighter(0.06f);

    auto r = button.getLocalBounds().toFloat();
    if (segment.isEmpty()) {
        r = r.reduced(0.5f);
        g.setColour(fill);
        g.fillRoundedRectangle(r, theme::kRadius);
        g.setColour(theme::border);
        g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
        return;
    }
    // Inside a segmented switch the group draws the frame; a segment draws
    // its fill and the line that parts it from the one before.
    g.setColour(fill);
    g.fillRect(r);
    if (segment == "middle" || segment == "last") {
        g.setColour(theme::border);
        g.fillRect(r.withWidth(1.0f));
    }
}

void AsmaLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto& props = button.getProperties();
    const bool on = button.getToggleState();
    const bool accent = on && props["asma.accent"];
    const bool quiet = !on && (props["asma.quiet"] || button.getClickingTogglesState());
    const float size = props.contains("asma.size") ? static_cast<float>(props["asma.size"]) : 13.0f;
    g.setFont(theme::font(accent ? theme::Face::SemiBold : theme::Face::Text, size));
    g.setColour(accent ? theme::ground : (quiet ? theme::muted : theme::text));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
}

void AsmaLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll(theme::surface);
    g.setColour(theme::border);
    g.fillRect(0, header.getHeight() - 1, header.getWidth(), 1);
}

void AsmaLookAndFeel::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name,
                                            int, int width, int height, bool, bool, int)
{
    g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.06f));
    g.setColour(theme::muted);
    g.drawText(name.toUpperCase(), 0, 0, width, height - 1, juce::Justification::centredLeft, true);
}

void AsmaLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                    int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int>(x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int>(thumbStart, y, thumbSize, height);
    g.setColour(mouseOver || mouseDown ? theme::faint : theme::border);
    g.fillRoundedRectangle(thumb.toFloat().reduced(3.0f), 2.0f);
}

void AsmaLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour(theme::ground);
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                           theme::kRadius);
}

void AsmaLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour(theme::border);
    g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f),
                           theme::kRadius, 1.0f);
}

} // namespace asma::app
```

Create `plugin/src/ui/AsmaLookAndFeel.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace asma::app {

// The Anode theme for JUCE's own widgets. Buttons read properties:
// "asma.segment" ("first", "middle" or "last") draws one as part of a
// segmented switch; "asma.accent" fills it amber when on; "asma.quiet" leaves
// it transparent and muted when off; "asma.size" sets its text size (13).
// A toggling button is quiet when off.
class AsmaLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AsmaLookAndFeel();

    // Fonts that name no typeface get the embedded Inter, never a system face.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down) override;

    void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override;
    void drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& name,
                               int columnId, int width, int height, bool mouseOver, bool mouseDown,
                               int columnFlags) override;

    int getDefaultScrollbarWidth() override { return 10; }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                       int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override;

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
};

} // namespace asma::app
```

Create `plugin/src/ui/Theme.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Theme.h"

#include <AsmaFonts.h>

namespace asma::app::theme {

juce::Typeface::Ptr typeface(Face face)
{
    // Made once, on first use, and kept for the life of the process.
    static const std::array<juce::Typeface::Ptr, 5> faces = [] {
        const auto load = [](const char* data, int size) {
            return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
        };
        return std::array<juce::Typeface::Ptr, 5>{
            load(AsmaFonts::SpaceGroteskSemiBold_ttf, AsmaFonts::SpaceGroteskSemiBold_ttfSize),
            load(AsmaFonts::InterRegular_ttf, AsmaFonts::InterRegular_ttfSize),
            load(AsmaFonts::InterMedium_ttf, AsmaFonts::InterMedium_ttfSize),
            load(AsmaFonts::InterSemiBold_ttf, AsmaFonts::InterSemiBold_ttfSize),
            load(AsmaFonts::JetBrainsMonoRegular_ttf, AsmaFonts::JetBrainsMonoRegular_ttfSize),
        };
    }();
    return faces[static_cast<std::size_t>(face)];
}

juce::Font font(Face face, float size) { return juce::Font(juce::FontOptions(typeface(face)).withPointHeight(size)); }

juce::Colour colourFor(Tone tone)
{
    switch (tone) {
    case Tone::Synced: return green;
    case Tone::Warning: return amber;
    case Tone::Muted: break;
    }
    return muted;
}

} // namespace asma::app::theme
```

Create `plugin/src/ui/Theme.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "TempoChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The Anode look, copied from the identity rather than shared: anode-common
// is proprietary. Spec section 9, Look.
namespace asma::app::theme {

// Grounds, darkest first.
inline const juce::Colour ground{0xff0b0c0e};  // inputs, waveform, footer
inline const juce::Colour panel{0xff111316};   // top bar, sidebar, bottom panel
inline const juce::Colour surface{0xff15171a}; // the window
inline const juce::Colour raised{0xff1c1f23};  // pressed switches, row lines
inline const juce::Colour border{0xff2a2d33};
inline const juce::Colour faint{0xff3a3e45};   // empty stars, disabled text
inline const juce::Colour text{0xffe8e9eb};
inline const juce::Colour muted{0xff8a8f98};
// Accents: amber only for what is active or selected, cyan for the waveform,
// green only for a synced tempo.
inline const juce::Colour amber{0xffe8a33d};
inline const juce::Colour amberLight{0xfff2bd6b};
inline const juce::Colour cyan{0xff4fd1e6};
inline const juce::Colour cyanDim{0xff3aa3b5};  // the waveform after the playhead
inline const juce::Colour cyanFaint{0xff2b5c66}; // the waveform outside the trim
inline const juce::Colour green{0xff7de38e};

constexpr float kRadius = 4.0f;
constexpr float kCardRadius = 8.0f;

// Sizes from the approved design, in px at 100%.
constexpr int kTopBarHeight = 56;
constexpr int kChipRowHeight = 44;
constexpr int kSidebarWidth = 220;
constexpr int kPreviewHeight = 236;
constexpr int kSimilarWidth = 284;
constexpr int kFooterHeight = 26;
constexpr int kRowHeight = 30;
constexpr int kHeaderRowHeight = 30;

enum class Face {
    Heading,  // Space Grotesk 600: the wordmark, the file name, section labels
    Text,     // Inter 400
    Medium,   // Inter 500
    SemiBold, // Inter 600
    Mono,     // JetBrains Mono 400: numbers
};

// The embedded face at a CSS-style size (the em, as in the design).
juce::Font font(Face face, float size);
juce::Typeface::Ptr typeface(Face face);

juce::Colour colourFor(Tone tone);

} // namespace asma::app::theme
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[theme]"`

Expected: `All tests passed (10 assertions in 3 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/fonts plugin/CMakeLists.txt plugin/src/ui/AsmaLookAndFeel.cpp plugin/src/ui/AsmaLookAndFeel.h plugin/src/ui/Theme.cpp plugin/src/ui/Theme.h tests/plugin/test_theme.cpp
git commit -m "app: the Anode theme: palette, embedded fonts and a LookAndFeel"
```

---

### Task 8: A waveform with trim handles and a playhead

Spec section 9 (Editing in the preview). The mouse handlers only call `press`,
`drag`, `release` and `doubleClick`, which tests call directly. A drag reports
once, on release, so the sound is not restarted at every step. Both handles
stand inside the kept region, as in the design.

**Files:**

- Modify: `plugin/src/TempoChip.cpp`
- Modify: `plugin/src/TempoChip.h`
- Create: `plugin/src/ui/WaveformView.cpp`
- Create: `plugin/src/ui/WaveformView.h`
- Modify: `tests/plugin/test_tempo_chip.cpp` (test)
- Create: `tests/plugin/test_waveform_view.cpp` (test)

**Interfaces:**

- Consumes: task 3's `Overview`; task 7's theme.
- Produces: `class WaveformView` with
  `setOverview(std::shared_ptr<const audio::Overview>)`,
  `setTrim(double start, double end /*<0: file end*/)`,
  `setPlayhead(std::optional<double>)`, `trimStart()`, `trimEnd()`,
  `onTrimChanged(double start, double end)`, `onPlay()`,
  `press/drag/release/doubleClick(float x)`, `secondsAt(float)`, `xFor(double)`,
  `kMinGap` 0.01, `kGrab` 6; `std::string secondsText(double)` ("6.80 s") beside
  the other formatters.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_tempo_chip.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_tempo_chip.cpp b/tests/plugin/test_tempo_chip.cpp
index 9e08a21..920516d 100644
--- a/tests/plugin/test_tempo_chip.cpp
+++ b/tests/plugin/test_tempo_chip.cpp
@@ -69,4 +69,5 @@ TEST_CASE("tempos and ratios read the same in every locale", "[chip]")
     CHECK(app::ratioText(0.25) == "0.25");
     CHECK(app::ratioText(1.0 / 3.0) == "0.33");
     CHECK(app::ratioText(1.006) == "1.01"); // rounds, never truncates
+    CHECK(app::secondsText(6.8) == "6.80 s");
 }
```

Create `tests/plugin/test_waveform_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Theme.h"
#include "ui/WaveformView.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::WaveformView;

namespace {

// 10 s of a full-scale square wave at 1 kHz sample rate, one channel.
std::shared_ptr<const audio::Overview> tenSeconds()
{
    audio::AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(10000)};
    for (std::size_t i = 0; i < b.channels[0].size(); ++i) b.channels[0][i] = (i / 5) % 2 ? 0.9f : -0.9f;
    return std::make_shared<const audio::Overview>(audio::makeOverview(b));
}

struct Rig {
    const juce::ScopedJuceInitialiser_GUI gui;
    WaveformView view;
    double start = -1.0, end = -2.0;
    int changes = 0, plays = 0;
    Rig()
    {
        view.setBounds(0, 0, 1000, 104); // 100 px a second
        view.setOverview(tenSeconds());
        view.onTrimChanged = [this](double s, double e) {
            start = s;
            end = e;
            ++changes;
        };
        view.onPlay = [this] { ++plays; };
    }
    juce::Colour at(int x, int y)
    {
        juce::Image image(juce::Image::ARGB, 1000, 104, true, juce::SoftwareImageType{});
        juce::Graphics g(image);
        view.paintEntireComponent(g, false);
        return image.getPixelAt(x, y);
    }
};

} // namespace

TEST_CASE("the waveform maps seconds across its width", "[waveform]")
{
    Rig rig;
    CHECK(rig.view.secondsAt(0.0f) == 0.0);
    CHECK(rig.view.secondsAt(250.0f) == Catch::Approx(2.5));
    CHECK(rig.view.secondsAt(5000.0f) == Catch::Approx(10.0)); // clamped
    CHECK(rig.view.xFor(7.5) == Catch::Approx(750.0f));
}

TEST_CASE("dragging a handle trims, once, when the mouse lets go", "[waveform]")
{
    Rig rig;
    rig.view.press(3.0f); // within reach of the start handle at 0
    rig.view.drag(150.0f);
    rig.view.drag(200.0f);
    CHECK(rig.changes == 0); // nothing restarts while the hand moves
    rig.view.release();
    CHECK(rig.changes == 1);
    CHECK(rig.start == Catch::Approx(2.0));
    CHECK(rig.end == -1.0); // the end was never touched
    CHECK(rig.plays == 0);

    rig.view.press(998.0f); // the end handle
    rig.view.drag(680.0f);
    rig.view.release();
    CHECK(rig.start == Catch::Approx(2.0));
    CHECK(rig.end == Catch::Approx(6.8));
}

TEST_CASE("the handles keep 10 ms apart and the end handle at the end means untrimmed", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.press(200.0f);
    rig.view.drag(900.0f); // past the end handle
    rig.view.release();
    CHECK(rig.start == Catch::Approx(6.0 - WaveformView::kMinGap));

    rig.view.setTrim(2.0, 6.0);
    rig.view.press(600.0f);
    rig.view.drag(1200.0f); // off the right edge
    rig.view.release();
    CHECK(rig.end == -1.0);
}

TEST_CASE("double-clicking a handle puts it back; clicking elsewhere plays", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.doubleClick(201.0f);
    CHECK(rig.start == 0.0);
    CHECK(rig.end == Catch::Approx(6.0));
    rig.view.doubleClick(599.0f);
    CHECK(rig.end == -1.0);
    CHECK(rig.changes == 2);
    rig.view.press(400.0f);
    rig.view.release();
    CHECK(rig.plays == 1);
    CHECK(rig.changes == 2);
}

TEST_CASE("nothing can be trimmed before the waveform arrives", "[waveform]")
{
    Rig rig;
    rig.view.setOverview(nullptr);
    rig.view.press(0.0f);
    rig.view.drag(300.0f);
    rig.view.release();
    CHECK(rig.changes == 0);
    CHECK(rig.plays == 1); // a click still plays
}

TEST_CASE("the trimmed-off parts are dimmed and the handles amber", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.setPlayhead(4.0);
    using namespace app::theme;
    CHECK(rig.at(300, 52) == cyan);     // inside the trim, before the playhead
    CHECK(rig.at(500, 52) == cyanDim);  // after it
    CHECK(rig.at(400, 30) == text);     // the playhead
    CHECK(rig.at(100, 52).getBrightness() < cyanFaint.getBrightness()); // outside, under the dimming
    CHECK(rig.at(200, 60) == amber);    // the start handle
    CHECK(rig.at(599, 60) == amber);    // the end handle, inside what is kept
    CHECK(rig.at(601, 60) != amber);
}

TEST_CASE("a trim past the end of a file that got shorter stops at the file's end", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 50.0); // saved when the file was longer
    CHECK(rig.at(998, 60) == app::theme::amber); // the end handle, at the edge
    rig.view.press(200.0f);
    rig.view.drag(990.0f);
    rig.view.release();
    CHECK(rig.start <= 10.0); // never beyond the file
}

TEST_CASE("a file shorter than the handles' gap keeps them in order", "[waveform]")
{
    Rig rig;
    audio::AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(5, 0.5f)}; // 5 ms
    rig.view.setOverview(std::make_shared<const audio::Overview>(audio::makeOverview(b)));
    rig.view.press(0.0f);
    rig.view.drag(1000.0f);
    rig.view.release();
    CHECK(rig.start == 0.0);
    CHECK(rig.end == -1.0);
    rig.view.press(999.0f);
    rig.view.drag(0.0f);
    rig.view.release();
    CHECK(rig.start <= std::max(rig.end, 0.0));
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[waveform],[chip]"`

Expected: the build stops:

```
plugin/test_waveform_view.cpp:3:10: fatal error: 'ui/WaveformView.h' file not found
plugin/test_tempo_chip.cpp:72:16: error: no member named 'secondsText' in namespace 'asma::app'
```

- [ ] **Step 3: Implement**

In `plugin/src/TempoChip.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/TempoChip.cpp b/plugin/src/TempoChip.cpp
index 9eeabdf..9d7e100 100644
--- a/plugin/src/TempoChip.cpp
+++ b/plugin/src/TempoChip.cpp
@@ -20,6 +20,8 @@ std::string ratioText(double ratio)
     return std::to_string(hundredths / 100) + "." + (cents < 10 ? "0" : "") + std::to_string(cents);
 }

+std::string secondsText(double seconds) { return ratioText(seconds) + " s"; }
+
 ChipText tempoChip(const audio::SampleInfo& info, const audio::SyncSettings& sync, bool failed)
 {
     if (failed) return {"can't read this file", Tone::Warning};
```

In `plugin/src/TempoChip.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/TempoChip.h b/plugin/src/TempoChip.h
index dad4700..f9bc764 100644
--- a/plugin/src/TempoChip.h
+++ b/plugin/src/TempoChip.h
@@ -25,5 +25,7 @@ ChipText tempoChip(const audio::SampleInfo& info, const audio::SyncSettings& syn
 // whole number left whole ("120", "97.5"); a ratio to hundredths ("1.50").
 std::string bpmText(double bpm);
 std::string ratioText(double ratio);
+// Seconds to hundredths, with the unit: "6.80 s".
+std::string secondsText(double seconds);

 } // namespace asma::app
```

Create `plugin/src/ui/WaveformView.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/WaveformView.h"

#include "TempoChip.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace asma::app {

void WaveformView::setOverview(std::shared_ptr<const audio::Overview> overview)
{
    if (overview == overview_) return;
    overview_ = std::move(overview);
    repaint();
}

void WaveformView::setTrim(double start, double end)
{
    if (dragging_ != Handle::None) return; // the user's hand wins
    start_ = start;
    end_ = end;
    repaint();
}

void WaveformView::setPlayhead(std::optional<double> seconds)
{
    if (seconds == playhead_) return;
    playhead_ = seconds;
    repaint();
}

double WaveformView::secondsAt(float x) const
{
    const auto w = wave();
    if (w.getWidth() <= 0.0f) return 0.0;
    return std::clamp(static_cast<double>((x - w.getX()) / w.getWidth()), 0.0, 1.0) * length();
}

float WaveformView::xFor(double seconds) const
{
    const auto w = wave();
    const double len = length();
    if (len <= 0.0) return w.getX();
    return w.getX() + static_cast<float>(std::clamp(seconds / len, 0.0, 1.0)) * w.getWidth();
}

WaveformView::Handle WaveformView::handleAt(float x) const
{
    if (!overview_) return Handle::None;
    const float s = std::abs(x - xFor(start_));
    const float e = std::abs(x - xFor(endSeconds()));
    if (s > kGrab && e > kGrab) return Handle::None;
    return s <= e ? Handle::Start : Handle::End;
}

void WaveformView::press(float x)
{
    dragging_ = handleAt(x);
    if (dragging_ == Handle::None && onPlay) onPlay();
}

void WaveformView::drag(float x)
{
    const double s = secondsAt(x);
    if (dragging_ == Handle::Start) start_ = std::clamp(s, 0.0, std::max(0.0, endSeconds() - kMinGap));
    else if (dragging_ == Handle::End) end_ = std::clamp(s, std::min(length(), start_ + kMinGap), length());
    else return;
    repaint();
}

void WaveformView::release()
{
    if (dragging_ == Handle::None) return;
    dragging_ = Handle::None;
    if (end_ >= length()) end_ = -1.0; // dragged to the end: untrimmed there
    if (onTrimChanged) onTrimChanged(start_, end_);
}

void WaveformView::doubleClick(float x)
{
    const Handle h = handleAt(x);
    dragging_ = Handle::None;
    if (h == Handle::None) return;
    if (h == Handle::Start) start_ = 0.0;
    else end_ = -1.0;
    repaint();
    if (onTrimChanged) onTrimChanged(start_, end_);
}

juce::MouseCursor WaveformView::getMouseCursor()
{
    const auto x = static_cast<float>(getMouseXYRelative().x);
    return handleAt(x) == Handle::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor;
}

void WaveformView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::ground);
    g.fillRoundedRectangle(bounds, theme::kRadius);
    const float mid = std::round(bounds.getCentreY());
    g.setColour(juce::Colour(0xff23262b));
    g.fillRect(bounds.getX(), mid, bounds.getWidth(), 1.0f);

    if (overview_ && overview_->frames > 0 && overview_->channels() > 0) {
        const float x0 = xFor(start_), x1 = xFor(endSeconds());
        const float head = playhead_ ? xFor(*playhead_) : x0;
        const float half = (bounds.getHeight() - 12.0f) / 2.0f;
        const int width = getWidth();
        const int points = audio::Overview::kPoints;
        for (int px = 0; px < width; ++px) {
            // Every overview point under this column, all channels: one lane.
            const int from = px * points / width;
            const int to = std::max(from + 1, (px + 1) * points / width);
            float lo = 0.0f, hi = 0.0f;
            for (int c = 0; c < overview_->channels(); ++c)
                for (int i = from; i < to; ++i) {
                    lo = std::min(lo, overview_->min[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]);
                    hi = std::max(hi, overview_->max[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]);
                }
            const float x = static_cast<float>(px);
            const float top = mid - std::min(hi, 1.0f) * half;
            const float bottom = mid - std::max(lo, -1.0f) * half;
            const bool inside = x >= x0 && x < x1;
            g.setColour(!inside ? theme::cyanFaint : (x < head ? theme::cyan : theme::cyanDim));
            g.fillRect(x, top, 1.0f, std::max(1.0f, bottom - top));
        }

        // Outside the trim, dimmed; the handles over it.
        g.setColour(theme::ground.withAlpha(0.55f));
        g.fillRect(bounds.withRight(x0));
        g.fillRect(bounds.withLeft(x1));
        // Both handles stand inside what is kept: the start's line right of
        // its point, the end's left of it.
        g.setColour(theme::amber);
        for (const float hx : {std::round(x0), std::round(x1) - 2.0f}) {
            const float lx = std::clamp(hx, bounds.getX(), bounds.getRight() - 2.0f);
            g.fillRect(lx, bounds.getY(), 2.0f, bounds.getHeight());
            juce::Path tab;
            tab.addRoundedRectangle(lx - 5.0f, bounds.getY(), 12.0f, 12.0f, 3.0f, 3.0f, false, false, true, true);
            g.fillPath(tab);
        }
        g.setFont(theme::font(theme::Face::Mono, 10.0f));
        g.drawText(secondsText(start_), juce::Rectangle<float>(x0 + 6.0f, bounds.getBottom() - 16.0f, 80.0f, 14.0f),
                   juce::Justification::centredLeft, false);
        g.drawText(secondsText(endSeconds()), juce::Rectangle<float>(x1 - 86.0f, bounds.getBottom() - 16.0f, 80.0f, 14.0f),
                   juce::Justification::centredRight, false);
        if (playhead_) {
            g.setColour(theme::text);
            g.fillRect(std::round(head), bounds.getY(), 1.0f, bounds.getHeight());
        }
    }

    g.setColour(theme::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme::kRadius, 1.0f);
}

} // namespace asma::app
```

Create `plugin/src/ui/WaveformView.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Overview.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <optional>

namespace asma::app {

// The selected file's waveform with its trim handles and the playhead.
// Dragging a handle moves that end of the trim, at least kMinGap seconds from
// the other; double-clicking one puts it back at the file's edge; a click
// anywhere else plays. Nothing can be trimmed before the overview arrives.
class WaveformView : public juce::Component {
public:
    static constexpr double kMinGap = 0.01; // seconds between the handles
    static constexpr float kGrab = 6.0f;    // px either side of a handle that take it

    void setOverview(std::shared_ptr<const audio::Overview> overview);
    // Seconds; end < 0 is the end of the file, as audio::Edits has it.
    void setTrim(double start, double end);
    void setPlayhead(std::optional<double> seconds);

    double trimStart() const { return start_; }
    double trimEnd() const { return end_; }

    // A drag's outcome, once, when the mouse lets go; end -1 when at the file's end.
    std::function<void(double start, double end)> onTrimChanged;
    std::function<void()> onPlay;

    // What the mouse handlers do, in component coordinates.
    void press(float x);
    void drag(float x);
    void release();
    void doubleClick(float x);

    double secondsAt(float x) const;
    float xFor(double seconds) const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override { press(e.position.x); }
    void mouseDrag(const juce::MouseEvent& e) override { drag(e.position.x); }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { doubleClick(e.position.x); }
    juce::MouseCursor getMouseCursor() override;

private:
    enum class Handle { None, Start, End };
    double length() const { return overview_ ? overview_->seconds() : 0.0; }
    double endSeconds() const { return end_ < 0.0 ? length() : end_; }
    Handle handleAt(float x) const;
    juce::Rectangle<float> wave() const { return getLocalBounds().toFloat(); }

    std::shared_ptr<const audio::Overview> overview_;
    double start_ = 0.0;
    double end_ = -1.0;
    std::optional<double> playhead_;
    Handle dragging_ = Handle::None;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[waveform],[chip]"`

Expected: `All tests passed (61 assertions in 11 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/TempoChip.cpp plugin/src/TempoChip.h plugin/src/ui/WaveformView.cpp plugin/src/ui/WaveformView.h tests/plugin/test_tempo_chip.cpp tests/plugin/test_waveform_view.cpp
git commit -m "app: a waveform with trim handles and a playhead"
```

---

### Task 9: Chips and segmented switches

The preview's Tempo, Key, Match loudness and Start chips, and the direction and
loop-mode switches. A segmented switch is real `TextButton`s in a radio group,
so keyboard and screen readers work; the arrows get titles ("Forward",
"Reverse", "Ping-pong").

**Files:**

- Modify: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Create: `plugin/src/ui/Controls.cpp`
- Create: `plugin/src/ui/Controls.h`
- Create: `tests/plugin/test_controls.cpp` (test)

**Interfaces:**

- Consumes: task 7's theme and `AsmaLookAndFeel` button properties.
- Produces: `class ChipButton : juce::Button` with
  `setDetail(const juce::String&, juce::Colour)`,
  `setDot(juce::Colour, bool filled)`, `detail()`, `idealWidth()`;
  `class SegmentedControl(const juce::StringArray& labels, bool accent, const juce::StringArray& titles = {})`
  with `setSelected(int, juce::NotificationType)`, `selected()`, `segment(int)`,
  `idealWidth()`, `onChange(int)`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_controls.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma::app;

namespace {

juce::Image render(juce::Component& c)
{
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType{});
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
    return image;
}

} // namespace

TEST_CASE("a segmented switch keeps one choice and reports the user's", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SegmentedControl s({"Auto", "On", "Off"}, false);
    s.setBounds(0, 0, 150, 28);
    int changed = -1, calls = 0;
    s.onChange = [&](int i) {
        changed = i;
        ++calls;
    };
    CHECK(s.selected() == 0);
    s.segment(2).triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // clicks arrive through the message loop
    CHECK(changed == 2);
    CHECK(s.selected() == 2);
    CHECK_FALSE(s.segment(0).getToggleState());
    s.setSelected(1, juce::dontSendNotification); // from saved state: no echo
    CHECK(calls == 1);
    CHECK(s.segment(1).getToggleState());
    CHECK_FALSE(s.segment(2).getToggleState());
}

TEST_CASE("a segmented switch tells screen readers what its arrows mean", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SegmentedControl s({"→", "←", "↔"}, true, {"Forward", "Reverse", "Ping-pong"});
    CHECK(s.segment(1).getTitle() == "Reverse");
}

TEST_CASE("an accent switch fills its choice amber", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaLookAndFeel lnf;
    SegmentedControl s({"A", "B", "C"}, true);
    s.setLookAndFeel(&lnf);
    s.setBounds(0, 0, 120, 28);
    s.setSelected(1, juce::dontSendNotification);
    const auto image = render(s);
    CHECK(image.getPixelAt(46, 6) == theme::amber);
    CHECK(image.getPixelAt(10, 6).getAlpha() == 0);
    s.setLookAndFeel(nullptr);
}

TEST_CASE("a chip shows its dot and its detail", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipButton chip("Tempo");
    chip.setClickingTogglesState(true);
    chip.setDetail("120 -> 180", theme::green);
    chip.setDot(theme::green, true);
    chip.setToggleState(true, juce::dontSendNotification);
    chip.setBounds(0, 0, chip.idealWidth(), 28);
    CHECK(chip.idealWidth() > 100);
    const auto on = render(chip);
    CHECK(on.getPixelAt(14, 14) == theme::green); // the dot's centre
    CHECK(on.getPixelAt(chip.getWidth() - 4, 4) == theme::raised);
    chip.setToggleState(false, juce::dontSendNotification);
    chip.setDot(theme::green, false);
    const auto off = render(chip);
    CHECK(off.getPixelAt(14, 14).getAlpha() == 0); // a ring: hollow
    CHECK(off.getPixelAt(chip.getWidth() - 4, 4).getAlpha() == 0);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[controls]"`

Expected: the build stops:

```
plugin/test_controls.cpp:3:10: fatal error: 'ui/Controls.h' file not found
```

- [ ] **Step 3: Implement**

In `plugin/src/ui/AsmaLookAndFeel.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.cpp b/plugin/src/ui/AsmaLookAndFeel.cpp
index a8482ed..ecc91dd 100644
--- a/plugin/src/ui/AsmaLookAndFeel.cpp
+++ b/plugin/src/ui/AsmaLookAndFeel.cpp
@@ -67,9 +67,14 @@ void AsmaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& butt
         return;
     }
     // Inside a segmented switch the group draws the frame; a segment draws
-    // its fill and the line that parts it from the one before.
+    // its fill, rounded on the switch's outer corners, and the line that
+    // parts it from the one before.
+    const bool first = segment == "first", last = segment == "last";
+    juce::Path shape;
+    shape.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), theme::kRadius, theme::kRadius, first, last,
+                              first, last);
     g.setColour(fill);
-    g.fillRect(r);
+    g.fillPath(shape);
     if (segment == "middle" || segment == "last") {
         g.setColour(theme::border);
         g.fillRect(r.withWidth(1.0f));
```

Create `plugin/src/ui/Controls.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Controls.h"

#include "ui/Theme.h"

namespace asma::app {

namespace {

int textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text)));
}

} // namespace

ChipButton::ChipButton(const juce::String& label) : juce::Button(label)
{
    setTitle(label);
    detailColour_ = theme::muted;
    dotColour_ = theme::amber;
}

void ChipButton::setDetail(const juce::String& detail, juce::Colour colour)
{
    if (detail == detail_ && colour == detailColour_) return;
    detail_ = detail;
    detailColour_ = colour;
    setDescription(detail);
    repaint();
}

void ChipButton::setDot(juce::Colour colour, bool filled)
{
    if (colour == dotColour_ && filled == dotFilled_) return;
    dotColour_ = colour;
    dotFilled_ = filled;
    repaint();
}

int ChipButton::idealWidth() const
{
    int w = 10 + 8 + 8 + textWidth(theme::font(theme::Face::Text, 12.0f), getButtonText()) + 10;
    if (detail_.isNotEmpty()) w += 8 + textWidth(theme::font(theme::Face::Mono, 11.0f), detail_);
    return w;
}

void ChipButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    if (on || highlighted || down) {
        g.setColour(on ? theme::raised : theme::raised.withAlpha(0.6f));
        g.fillRoundedRectangle(r, theme::kRadius);
    }
    g.setColour(theme::border);
    g.drawRoundedRectangle(r, theme::kRadius, 1.0f);

    auto area = getLocalBounds().reduced(10, 0);
    const auto dot = area.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat();
    if (dotFilled_) {
        g.setColour(dotColour_);
        g.fillEllipse(dot);
    } else {
        g.setColour(theme::muted);
        g.drawEllipse(dot.reduced(0.75f), 1.5f);
    }
    area.removeFromLeft(8);
    const auto labelFont = theme::font(theme::Face::Text, 12.0f);
    g.setFont(labelFont);
    g.setColour(on ? theme::text : theme::muted);
    g.drawText(getButtonText(), area.removeFromLeft(textWidth(labelFont, getButtonText())), juce::Justification::centredLeft, false);
    if (detail_.isNotEmpty()) {
        area.removeFromLeft(8);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(detailColour_);
        g.drawText(detail_, area, juce::Justification::centredLeft, true);
    }
}

SegmentedControl::SegmentedControl(const juce::StringArray& labels, bool accent, const juce::StringArray& titles)
{
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = segments_.add(new juce::TextButton(labels[i]));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1);
        b->getProperties().set("asma.segment", i == 0 ? "first" : (i == labels.size() - 1 ? "last" : "middle"));
        b->getProperties().set("asma.accent", accent);
        b->getProperties().set("asma.size", 12.0f);
        if (i < titles.size()) b->setTitle(titles[i]);
        b->onClick = [this, i] {
            if (segments_[i]->getToggleState() && selected_ != i) setSelected(i);
        };
        addAndMakeVisible(b);
    }
    if (!segments_.isEmpty()) segments_[0]->setToggleState(true, juce::dontSendNotification);
}

void SegmentedControl::setSelected(int index, juce::NotificationType notification)
{
    if (index < 0 || index >= segments_.size()) return;
    selected_ = index;
    segments_[index]->setToggleState(true, juce::dontSendNotification);
    if (notification != juce::dontSendNotification && onChange) onChange(index);
}

int SegmentedControl::idealWidth() const
{
    int w = 0;
    const auto font = theme::font(theme::Face::SemiBold, 12.0f);
    for (auto* b : segments_) w += textWidth(font, b->getButtonText()) + 20;
    return w;
}

void SegmentedControl::resized()
{
    auto area = getLocalBounds();
    const int total = idealWidth();
    const auto font = theme::font(theme::Face::SemiBold, 12.0f);
    for (int i = 0; i < segments_.size(); ++i) {
        const int w = i == segments_.size() - 1 ? area.getWidth()
                                                : (textWidth(font, segments_[i]->getButtonText()) + 20) * getWidth() / std::max(1, total);
        segments_[i]->setBounds(area.removeFromLeft(w));
    }
}

void SegmentedControl::paintOverChildren(juce::Graphics& g)
{
    g.setColour(theme::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), theme::kRadius, 1.0f);
}

} // namespace asma::app
```

Create `plugin/src/ui/Controls.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// A switch for one setting that also says what it does: a dot, a label and
// a detail, as the preview's Tempo, Key, Match loudness and Start chips.
class ChipButton : public juce::Button {
public:
    explicit ChipButton(const juce::String& label);

    // The detail after the label, in its own colour ("120 -> 180 x1.50").
    void setDetail(const juce::String& detail, juce::Colour colour);
    // The dot: filled in `colour` when on, a grey ring when empty.
    void setDot(juce::Colour colour, bool filled);
    const juce::String& detail() const { return detail_; }
    // The width that fits label and detail.
    int idealWidth() const;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::String detail_;
    juce::Colour detailColour_;
    juce::Colour dotColour_;
    bool dotFilled_ = false;
};

// One choice of a few, as touching buttons: direction and loop mode. Accent
// fills the chosen one amber; otherwise it is lifted.
class SegmentedControl : public juce::Component {
public:
    // titles: what a screen reader says for each label (the arrows say nothing).
    SegmentedControl(const juce::StringArray& labels, bool accent, const juce::StringArray& titles = {});

    void setSelected(int index, juce::NotificationType notification = juce::sendNotification);
    int selected() const { return selected_; }
    juce::TextButton& segment(int index) { return *segments_[index]; }
    int idealWidth() const;

    std::function<void(int)> onChange;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    juce::OwnedArray<juce::TextButton> segments_;
    int selected_ = 0;
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[controls]"`

Expected: `All tests passed (15 assertions in 4 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/ui/AsmaLookAndFeel.cpp plugin/src/ui/Controls.cpp plugin/src/ui/Controls.h tests/plugin/test_controls.cpp
git commit -m "app: chips and segmented switches for the preview's settings"
```

---

### Task 10: The preview panel and the footer

Spec section 9 (Bottom panel, Footer). Both are components that show what they
are told and report what the user does; task 11 joins them to the processor. The
controls wrap onto a second row in narrow windows and the waveform takes what
height is left. The Key chip opens a menu of the 24 keys; the Start chip cycles
at once, beat, bar.

**Files:**

- Create: `plugin/src/ui/Footer.cpp`
- Create: `plugin/src/ui/Footer.h`
- Create: `plugin/src/ui/PreviewPanel.cpp`
- Create: `plugin/src/ui/PreviewPanel.h`
- Create: `tests/plugin/test_preview_panel.cpp` (test)

**Interfaces:**

- Consumes: tasks 5, 7, 8 and 9.
- Produces: `class PreviewPanel` with `setFile(name, line)`,
  `setEdits(const audio::Edits&)`, `setPlaying(bool)`,
  `setTempo(bool on, const ChipText&)`,
  `setKey(bool on, const std::string& key, const std::string& status)`,
  `setGainMatch(bool)`, `setQuantise(double beats)`, callbacks
  `onEditsChanged(const audio::Edits&)`, `onPlayStop()`, `onTempoSync(bool)`,
  `onKeySync(std::optional<std::string>)`, `onGainMatch(bool)`,
  `onQuantise(double)`, `chooseKey(int item /*0 off, 1..24 keys*/)`,
  `static keys()`, `static fileLine(folder, sampleRate, channels, seconds)`, and
  accessors for its parts; `class Footer` with `setDrag`, `setStatus`,
  `setRenderBytes`, `onClearRenders`, `static sizeText(std::uintmax_t)`,
  `dragText()`, `rightText()`, `clearButton()`.

- [ ] **Step 1: Write the failing test**

Create `tests/plugin/test_preview_panel.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Footer.h"
#include "ui/PreviewPanel.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::Footer;
using app::PreviewPanel;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

std::shared_ptr<const audio::Overview> eightSeconds()
{
    audio::AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(8000, 0.5f)};
    return std::make_shared<const audio::Overview>(audio::makeOverview(b));
}

struct Rig {
    const juce::ScopedJuceInitialiser_GUI gui;
    PreviewPanel panel;
    std::vector<audio::Edits> edits;
    Rig()
    {
        panel.setBounds(0, 0, 964, 236);
        panel.setFile("Bass_Loop_Am_120.wav", "Loops");
        panel.waveform().setOverview(eightSeconds());
        panel.onEditsChanged = [this](const audio::Edits& e) { edits.push_back(e); };
    }
};

} // namespace

TEST_CASE("the preview names the file's folder, rate, channels and length", "[preview]")
{
    CHECK(PreviewPanel::fileLine("Loops/Bass", 44100, 2, 8.0) == juce::String::fromUTF8("Loops / Bass · 44.1 kHz · stereo · 8.00 s"));
    CHECK(PreviewPanel::fileLine("", 48000, 1, 0.0) == juce::String::fromUTF8("48 kHz · mono"));
    CHECK(PreviewPanel::fileLine("Kicks", 0, 0, 0.25) == juce::String::fromUTF8("Kicks · 0.25 s"));
}

TEST_CASE("the direction switch, the loop switch and the trim handles all edit", "[preview]")
{
    Rig rig;
    CHECK_FALSE(rig.panel.resetButton().isEnabled()); // nothing to reset
    rig.panel.direction().segment(1).triggerClick();
    settle();
    REQUIRE(rig.edits.size() == 1);
    CHECK(rig.edits.back().direction == audio::Direction::Reverse);
    rig.panel.loop().segment(2).triggerClick();
    settle();
    CHECK(rig.edits.back().loop == audio::LoopMode::Off);
    CHECK(rig.edits.back().direction == audio::Direction::Reverse); // the earlier edit stays
    auto& wave = rig.panel.waveform();
    wave.press(wave.xFor(0.0) + 1.0f);
    wave.drag(wave.xFor(1.0));
    wave.release();
    CHECK(rig.edits.back().trimStart > 0.9);
    CHECK(rig.edits.back().trimStart < 1.1);
    CHECK(rig.panel.resetButton().isEnabled());
    rig.panel.resetButton().triggerClick();
    settle();
    CHECK(rig.edits.back().direction == audio::Direction::Forward);
    CHECK(rig.edits.back().trimStart == 0.0);
    CHECK(rig.panel.direction().selected() == 0);
}

TEST_CASE("edits from the project show without being sent back", "[preview]")
{
    Rig rig;
    audio::Edits e;
    e.direction = audio::Direction::PingPong;
    e.trimEnd = 6.8;
    rig.panel.setEdits(e);
    CHECK(rig.edits.empty());
    CHECK(rig.panel.direction().selected() == 2);
    CHECK(rig.panel.waveform().trimEnd() == 6.8);
    CHECK(rig.panel.resetButton().isEnabled());
}

TEST_CASE("the chips show what sync does and switch it", "[preview]")
{
    Rig rig;
    rig.panel.setTempo(true, {"120 → 180 · x1.50", app::Tone::Synced});
    CHECK(rig.panel.tempoChip().detail() == juce::String::fromUTF8("120 → 180 · x1.50"));
    CHECK(rig.panel.tempoChip().getToggleState());
    bool tempo = true;
    rig.panel.onTempoSync = [&](bool on) { tempo = on; };
    rig.panel.tempoChip().triggerClick();
    settle();
    CHECK_FALSE(tempo);

    std::optional<std::string> key = "unset";
    rig.panel.onKeySync = [&](std::optional<std::string> k) { key = k; };
    rig.panel.chooseKey(22); // the 22nd key: Am
    CHECK(key == "Am");
    CHECK(rig.panel.keyChip().detail() == "Am");
    rig.panel.setKey(true, "Am", "+2");
    CHECK(rig.panel.keyChip().detail() == "Am +2");
    rig.panel.chooseKey(0);
    CHECK_FALSE(key);
    CHECK(rig.panel.keyChip().detail() == "off");

    std::vector<double> starts;
    rig.panel.onQuantise = [&](double b) { starts.push_back(b); };
    for (int i = 0; i < 3; ++i) {
        rig.panel.startChip().triggerClick();
        settle();
    }
    CHECK(starts == std::vector<double>{1.0, 4.0, 0.0}); // beat, bar, at once
    CHECK(rig.panel.startChip().detail() == "now");
}

TEST_CASE("the controls fit the panel at every width the window allows", "[preview]")
{
    Rig rig;
    rig.panel.setTempo(true, {"120 → 180 · x1.50", app::Tone::Synced});
    for (const int width : {964, 800, 580}) { // 1280 and 1100 wide windows, and the 900 minimum
        rig.panel.setBounds(0, 0, width, 236);
        const auto inside = rig.panel.getLocalBounds();
        for (auto* c : rig.panel.getChildren()) {
            CHECK(inside.contains(c->getBounds()));
        }
        CHECK(rig.panel.waveform().getHeight() >= 60);
        CHECK(rig.panel.startChip().getBottom() <= 236 - 12);
    }
}

TEST_CASE("the play button and a click on the waveform both play", "[preview]")
{
    Rig rig;
    int plays = 0;
    rig.panel.onPlayStop = [&] { ++plays; };
    rig.panel.playButton().triggerClick();
    settle();
    rig.panel.waveform().press(400.0f);
    rig.panel.waveform().release();
    CHECK(plays == 2);
}

TEST_CASE("the footer gives sizes as a person reads them", "[footer]")
{
    CHECK(Footer::sizeText(0) == "0 B");
    CHECK(Footer::sizeText(820) == "820 B");
    CHECK(Footer::sizeText(820'000) == "820 KB");
    CHECK(Footer::sizeText(41'200'000) == "41 MB");
    CHECK(Footer::sizeText(1'240'000'000) == "1.2 GB");
    CHECK(Footer::sizeText(9'960) == "10 KB"); // rounds up into whole numbers
}

TEST_CASE("the footer offers to clear renders only when there are some", "[footer]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Footer footer;
    footer.setBounds(0, 0, 1280, 26);
    footer.setStatus("Scan finished: 585 added");
    CHECK_FALSE(footer.clearButton().isVisible());
    CHECK(footer.rightText() == "Scan finished: 585 added");
    footer.setRenderBytes(41'000'000);
    CHECK(footer.clearButton().isVisible());
    CHECK(footer.rightText() == juce::String::fromUTF8("Scan finished: 585 added · renders 41 MB · "));
    int clears = 0;
    footer.onClearRenders = [&] { ++clears; };
    footer.clearButton().triggerClick();
    settle();
    CHECK(clears == 1);
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[preview],[footer]"`

Expected: the build stops:

```
plugin/test_preview_panel.cpp:2:10: fatal error: 'ui/Footer.h' file not found
```

- [ ] **Step 3: Implement**

Create `plugin/src/ui/Footer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Footer.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {


void Footer::LinkLook::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(highlighted ? theme::amberLight : theme::amber);
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centredRight, false);
}

Footer::Footer()
{
    clear_.setLookAndFeel(&linkLook_);
    clear_.onClick = [this] {
        if (onClearRenders) onClearRenders();
    };
    addChildComponent(clear_);
}

Footer::~Footer() { clear_.setLookAndFeel(nullptr); }

juce::String Footer::sizeText(std::uintmax_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    // Whole numbers from 10 up; one decimal below, so 1.2 GB is not "1 GB".
    const long long tenths = std::llround(value * 10.0);
    const juce::String number = (unit == 0 || tenths >= 100) ? juce::String(std::llround(value))
                                                               : juce::String(tenths / 10) + "." + juce::String(tenths % 10);
    return number + " " + units[unit];
}

void Footer::setDrag(const juce::String& text)
{
    if (text == drag_) return;
    drag_ = text;
    repaint();
}

void Footer::setStatus(const juce::String& text)
{
    if (text == status_) return;
    status_ = text;
    resized();
    repaint();
}

void Footer::setRenderBytes(std::uintmax_t bytes)
{
    if (bytes == bytes_ && clear_.isVisible() == (bytes > 0)) return;
    bytes_ = bytes;
    clear_.setVisible(bytes > 0);
    resized();
    repaint();
}

juce::String Footer::rightText() const
{
    juce::StringArray parts;
    if (status_.isNotEmpty()) parts.add(status_);
    if (bytes_ > 0) parts.add("renders " + sizeText(bytes_));
    juce::String text = parts.joinIntoString(juce::String::fromUTF8(" · "));
    if (bytes_ > 0) text << juce::String::fromUTF8(" · ");
    return text;
}

void Footer::resized()
{
    auto area = getLocalBounds().reduced(16, 0);
    const auto font = theme::font(theme::Face::Mono, 11.0f);
    if (clear_.isVisible()) {
        const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, clear_.getButtonText())));
        clear_.setBounds(area.removeFromRight(w));
    }
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, rightText())));
    rightArea_ = area.removeFromRight(std::min(w, area.getWidth() / 2 + 120));
}

void Footer::paint(juce::Graphics& g)
{
    g.fillAll(theme::ground);
    g.setColour(theme::border);
    g.fillRect(0, 0, getWidth(), 1);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(theme::muted);
    g.drawText(rightText(), rightArea_, juce::Justification::centredRight, true);
    g.drawText(drag_, getLocalBounds().reduced(16, 0).withRight(rightArea_.getX() - 16), juce::Justification::centredLeft, true);
}

} // namespace asma::app
```

Create `plugin/src/ui/Footer.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>

namespace asma::app {

// The line at the foot of the window: on the left what a drag-out carries,
// on the right the scan's progress or outcome, the kept renders' size and
// "Clear renders".
class Footer : public juce::Component {
public:
    Footer();
    ~Footer() override;

    void setDrag(const juce::String& text);
    void setStatus(const juce::String& text);
    void setRenderBytes(std::uintmax_t bytes);

    std::function<void()> onClearRenders;

    // "41 MB", "820 KB", "1.2 GB": the size a person reads.
    static juce::String sizeText(std::uintmax_t bytes);

    juce::String dragText() const { return drag_; }
    juce::String rightText() const;
    juce::TextButton& clearButton() { return clear_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String drag_, status_;
    std::uintmax_t bytes_ = 0;
    // "Clear renders" is a link: amber text, nothing else.
    struct LinkLook final : juce::LookAndFeel_V4 {
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool highlighted, bool) override;
    };
    LinkLook linkLook_;
    juce::TextButton clear_{"Clear renders"};
    juce::Rectangle<int> rightArea_;
};

} // namespace asma::app
```

Create `plugin/src/ui/PreviewPanel.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/PreviewPanel.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kControlHeight = 28;
constexpr int kGap = 10;
constexpr int kPadX = 16, kPadY = 12;
constexpr int kTitleHeight = 20;

// Play when stopped, stop when playing; amber while it plays.
class PlayButton final : public juce::Button {
public:
    PlayButton() : juce::Button("Play") { setTitle("Play"); }
    void setPlaying(bool playing)
    {
        if (playing == playing_) return;
        playing_ = playing;
        setTitle(playing ? "Stop" : "Play");
        repaint();
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(playing_ ? theme::amber.withAlpha(0.12f) : (highlighted || down ? theme::raised : theme::panel));
        g.fillRoundedRectangle(r, theme::kRadius);
        g.setColour(playing_ ? theme::amber : theme::border);
        g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
        const auto c = r.getCentre();
        if (playing_) {
            g.setColour(theme::amber);
            g.fillRoundedRectangle(juce::Rectangle<float>(10.0f, 10.0f).withCentre(c), 1.0f);
        } else {
            juce::Path p;
            p.addTriangle(c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
            g.setColour(theme::text);
            g.fillPath(p);
        }
    }

private:
    bool playing_ = false;
};

} // namespace

const juce::StringArray& PreviewPanel::keys()
{
    static const juce::StringArray k{"C",  "C#",  "D",  "D#",  "E",  "F",  "F#",  "G",  "G#",  "A",  "A#",  "B",
                                     "Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"};
    return k;
}

juce::String PreviewPanel::fileLine(const std::string& folder, int sampleRate, int channels, double seconds)
{
    juce::StringArray parts;
    if (!folder.empty()) parts.add(juce::String::fromUTF8(folder.c_str()).replace("/", " / "));
    if (sampleRate > 0) {
        const long long tenths = std::llround(sampleRate / 100.0);
        parts.add(juce::String(tenths / 10) + (tenths % 10 ? "." + juce::String(tenths % 10) : juce::String()) + " kHz");
    }
    if (channels == 1) parts.add("mono");
    if (channels == 2) parts.add("stereo");
    if (seconds > 0.0) parts.add(juce::String::fromUTF8(secondsText(seconds).c_str()));
    return parts.joinIntoString(juce::String::fromUTF8(" · "));
}

PreviewPanel::PreviewPanel()
    : direction_({juce::String::fromUTF8("→"), juce::String::fromUTF8("←"), juce::String::fromUTF8("↔")},
                 true, {"Forward", "Reverse", "Ping-pong"}),
      loop_({"Loop auto", "On", "Off"}, false, {"Loop when the sample is a loop", "Always loop", "Play once"})
{
    auto* play = new PlayButton();
    play_.reset(play);
    play->onClick = [this] {
        if (onPlayStop) onPlayStop();
    };
    addAndMakeVisible(*play_);
    addAndMakeVisible(waveform_);
    waveform_.onPlay = [this] {
        if (onPlayStop) onPlayStop();
    };
    waveform_.onTrimChanged = [this](double start, double end) {
        edits_.trimStart = start;
        edits_.trimEnd = end;
        editsChanged();
    };
    direction_.onChange = [this](int i) {
        edits_.direction = static_cast<audio::Direction>(i);
        editsChanged();
    };
    loop_.onChange = [this](int i) {
        edits_.loop = static_cast<audio::LoopMode>(i);
        editsChanged();
    };
    reset_.getProperties().set("asma.quiet", true);
    reset_.getProperties().set("asma.size", 12.0f);
    reset_.onClick = [this] {
        setEdits({});
        editsChanged();
    };
    for (auto* c : {&tempo_, &gain_}) c->setClickingTogglesState(true);
    tempo_.onClick = [this] {
        if (onTempoSync) onTempoSync(tempo_.getToggleState());
    };
    gain_.onClick = [this] {
        if (onGainMatch) onGainMatch(gain_.getToggleState());
    };
    key_.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Off", true, !key_.getToggleState());
        menu.addSeparator();
        for (int i = 0; i < keys().size(); ++i)
            menu.addItem(i + 2, keys()[i], true, key_.getToggleState() && keys()[i].toStdString() == keyName_);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&key_), [this](int result) {
            if (result > 0) chooseKey(result - 1);
        });
    };
    start_.onClick = [this] {
        const double next = quantise_ < 1.0 ? 1.0 : (quantise_ < 4.0 ? 4.0 : 0.0);
        setQuantise(next);
        if (onQuantise) onQuantise(next);
    };
    for (juce::Component* c : {static_cast<juce::Component*>(&direction_), static_cast<juce::Component*>(&loop_),
                               static_cast<juce::Component*>(&reset_), static_cast<juce::Component*>(&tempo_),
                               static_cast<juce::Component*>(&key_), static_cast<juce::Component*>(&gain_),
                               static_cast<juce::Component*>(&start_)})
        addAndMakeVisible(c);
    gain_.setDot(theme::amber, false);
    setKey(false, {}, {});
    setQuantise(0.0);
    setFile({}, {});
}

void PreviewPanel::setFile(const juce::String& name, const juce::String& line)
{
    name_ = name;
    line_ = line;
    const bool any = name.isNotEmpty();
    for (juce::Component* c : {static_cast<juce::Component*>(play_.get()), static_cast<juce::Component*>(&direction_),
                               static_cast<juce::Component*>(&loop_)})
        c->setEnabled(any);
    updateReset();
    repaint();
}

void PreviewPanel::setEdits(const audio::Edits& edits)
{
    edits_ = edits;
    waveform_.setTrim(edits.trimStart, edits.trimEnd);
    direction_.setSelected(static_cast<int>(edits.direction), juce::dontSendNotification);
    loop_.setSelected(static_cast<int>(edits.loop), juce::dontSendNotification);
    updateReset();
}

void PreviewPanel::updateReset()
{
    reset_.setEnabled(name_.isNotEmpty() && (edits_.changesAudio() || edits_.loop != audio::LoopMode::Auto));
}

void PreviewPanel::editsChanged()
{
    updateReset();
    if (onEditsChanged) onEditsChanged(edits_);
}

void PreviewPanel::setPlaying(bool playing) { static_cast<PlayButton*>(play_.get())->setPlaying(playing); }

void PreviewPanel::setTempo(bool on, const ChipText& chip)
{
    tempo_.setToggleState(on, juce::dontSendNotification);
    tempo_.setDot(theme::colourFor(chip.tone), on && chip.tone != Tone::Muted);
    tempo_.setDetail(juce::String::fromUTF8(chip.text.c_str()), theme::colourFor(chip.tone));
    resized();
}

void PreviewPanel::setKey(bool on, const std::string& key, const std::string& status)
{
    keyName_ = key;
    key_.setToggleState(on, juce::dontSendNotification);
    key_.setDot(theme::amber, on);
    juce::String detail = on && !key.empty() ? juce::String(key) : juce::String("off");
    if (on && !status.empty()) detail << " " << juce::String::fromUTF8(status.c_str());
    key_.setDetail(detail, on ? theme::text : theme::muted);
    resized();
}

void PreviewPanel::chooseKey(int item)
{
    if (item <= 0 || item > keys().size()) {
        setKey(false, keyName_, {});
        if (onKeySync) onKeySync(std::nullopt);
        return;
    }
    const std::string key = keys()[item - 1].toStdString();
    setKey(true, key, {});
    if (onKeySync) onKeySync(key);
}

void PreviewPanel::setGainMatch(bool on)
{
    gain_.setToggleState(on, juce::dontSendNotification);
    gain_.setDot(theme::amber, on);
}

void PreviewPanel::setQuantise(double beats)
{
    quantise_ = beats;
    start_.setToggleState(beats > 0.0, juce::dontSendNotification);
    start_.setDot(theme::amber, beats > 0.0);
    start_.setDetail(beats >= 4.0 ? "bar" : (beats >= 1.0 ? "beat" : "now"), beats > 0.0 ? theme::text : theme::muted);
    resized();
}

void PreviewPanel::paint(juce::Graphics& g)
{
    g.setColour(theme::border);
    g.fillRect(separator_);
    auto title = getLocalBounds().reduced(kPadX, kPadY).removeFromTop(kTitleHeight);
    if (name_.isEmpty()) {
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::muted);
        g.drawText("Select a sample to hear it", title, juce::Justification::centredLeft, true);
        return;
    }
    const auto nameFont = theme::font(theme::Face::Heading, 15.0f);
    const int nameWidth = std::min(title.getWidth(),
                                   static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(nameFont, name_))));
    g.setFont(nameFont);
    g.setColour(theme::text);
    g.drawText(name_, title.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);
    title.removeFromLeft(12);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(theme::muted);
    g.drawText(line_, title.withTrimmedTop(3), juce::Justification::centredLeft, true);
}

int PreviewPanel::controlRows(int width) const
{
    // The controls in order with their widths; a row breaks where they no longer fit.
    const int widths[] = {32, direction_.idealWidth(), loop_.idealWidth(), 90, 1, tempo_.idealWidth(),
                          key_.idealWidth(), gain_.idealWidth(), start_.idealWidth()};
    int rows = 1, x = 0;
    for (const int w : widths) {
        if (x > 0 && x + w > width) {
            ++rows;
            x = 0;
        }
        x += w + kGap;
    }
    return rows;
}

void PreviewPanel::layoutControls(juce::Rectangle<int> area)
{
    struct Item {
        juce::Component* c;
        int w;
    };
    const Item items[] = {{play_.get(), 32},
                          {&direction_, direction_.idealWidth()},
                          {&loop_, loop_.idealWidth()},
                          {&reset_, 90},
                          {nullptr, 1}, // the line between edits and settings
                          {&tempo_, tempo_.idealWidth()},
                          {&key_, key_.idealWidth()},
                          {&gain_, gain_.idealWidth()},
                          {&start_, start_.idealWidth()}};
    int x = area.getX(), y = area.getY();
    for (const auto& item : items) {
        if (x > area.getX() && x + item.w > area.getRight()) {
            x = area.getX();
            y += kControlHeight + kGap;
        }
        if (item.c) item.c->setBounds(x, y, item.w, kControlHeight);
        else separator_ = {x, y + 3, 1, kControlHeight - 6};
        x += item.w + kGap;
    }
    // A line that ends a row parts nothing.
    if (tempo_.getY() != separator_.getY() - 3) separator_ = {};
}

void PreviewPanel::resized()
{
    auto area = getLocalBounds().reduced(kPadX, kPadY);
    area.removeFromTop(kTitleHeight + kGap);
    const int rows = controlRows(area.getWidth());
    const int controls = rows * kControlHeight + (rows - 1) * kGap;
    layoutControls(area.removeFromBottom(controls));
    area.removeFromBottom(kGap);
    waveform_.setBounds(area);
}

} // namespace asma::app
```

Create `plugin/src/ui/PreviewPanel.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "TempoChip.h"
#include "asma/audio/Edits.h"
#include "ui/Controls.h"
#include "ui/WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
#include <string>

namespace asma::app {

// The bottom panel's audition side: the selected file's name, its waveform
// and the controls that change how it plays. It shows what it is told and
// reports what the user does; the editor joins it to the processor.
class PreviewPanel : public juce::Component {
public:
    PreviewPanel();

    // Empty name: nothing selected, and the controls that need a file go dim.
    void setFile(const juce::String& name, const juce::String& line);
    void setEdits(const audio::Edits& edits);
    void setPlaying(bool playing);
    void setTempo(bool on, const ChipText& chip);
    // key: the project key, empty for none; status: what key sync does to
    // the selection ("+2", "?"), may be empty.
    void setKey(bool on, const std::string& key, const std::string& status);
    void setGainMatch(bool on);
    // Beats; 0 starts at once, 1 on the beat, 4 on the bar.
    void setQuantise(double beats);

    std::function<void(const audio::Edits&)> onEditsChanged;
    std::function<void()> onPlayStop;
    std::function<void(bool)> onTempoSync;
    // nullopt: key sync off; else on, with that project key.
    std::function<void(std::optional<std::string>)> onKeySync;
    std::function<void(bool)> onGainMatch;
    std::function<void(double)> onQuantise;

    // What the Key chip's menu does with a pick: 0 is "Off", then the keys
    // in kKeys order. Public for tests; the menu calls it.
    void chooseKey(int item);
    static const juce::StringArray& keys();

    // "Loops / Bass · 44.1 kHz · stereo · 8.00 s"; parts that are unknown are left out.
    static juce::String fileLine(const std::string& folder, int sampleRate, int channels, double seconds);

    WaveformView& waveform() { return waveform_; }
    SegmentedControl& direction() { return direction_; }
    SegmentedControl& loop() { return loop_; }
    juce::Button& playButton() { return *play_; }
    juce::TextButton& resetButton() { return reset_; }
    ChipButton& tempoChip() { return tempo_; }
    ChipButton& keyChip() { return key_; }
    ChipButton& gainChip() { return gain_; }
    ChipButton& startChip() { return start_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void editsChanged();
    void updateReset(); // enabled when a file is shown and its edits are not the defaults
    void layoutControls(juce::Rectangle<int> area);
    int controlRows(int width) const;

    juce::String name_, line_;
    audio::Edits edits_;
    double quantise_ = 0.0;
    std::string keyName_;
    juce::Rectangle<int> separator_; // between the edits and the settings
    std::unique_ptr<juce::Button> play_;
    WaveformView waveform_;
    SegmentedControl direction_;
    SegmentedControl loop_;
    juce::TextButton reset_{"Reset edits"};
    ChipButton tempo_{"Tempo"};
    ChipButton key_{"Key"};
    ChipButton gain_{"Match loudness"};
    ChipButton start_{"Start"};
};

} // namespace asma::app
```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[preview],[footer]"`

Expected: `All tests passed (73 assertions in 8 test cases)`

- [ ] **Step 5: Commit**

```sh
git add plugin/src/ui/Footer.cpp plugin/src/ui/Footer.h plugin/src/ui/PreviewPanel.cpp plugin/src/ui/PreviewPanel.h tests/plugin/test_preview_panel.cpp
git commit -m "app: the preview panel and the footer, as components"
```

---

### Task 11: The editor in the approved layout

Spec section 9: top bar, table, preview panel and footer at the design's sizes;
the sidebar, the chip row and the Similar list keep their places empty for 3c2b.
The status line goes: library messages move into the table area, the scan into
the footer, sync into the Tempo chip. The standalone's tempo becomes a
`TempoBox` (minus, value, plus, as designed). A 30 Hz timer moves the playhead
and checks the library every sixth tick. "Clear renders" asks first, since a DAW
playing a render in place would lose it, and says so when a render could not be
deleted.

**Files:**

- Modify: `plugin/src/AsmaEditor.cpp`
- Modify: `plugin/src/AsmaEditor.h`
- Modify: `plugin/src/Browser.cpp`
- Modify: `plugin/src/Browser.h`
- Modify: `plugin/src/LibraryView.cpp`
- Modify: `plugin/src/LibraryView.h`
- Modify: `plugin/src/PluginState.h`
- Modify: `plugin/src/ui/AsmaLookAndFeel.cpp`
- Modify: `plugin/src/ui/AsmaLookAndFeel.h`
- Create: `plugin/src/ui/TopBar.cpp`
- Create: `plugin/src/ui/TopBar.h`
- Modify: `plugin/src/ui/WaveformView.h`
- Modify: `tests/plugin/test_browser.cpp` (test)
- Modify: `tests/plugin/test_editor.cpp` (test)

**Interfaces:**

- Consumes: everything above.
- Produces: `AsmaEditor` accessors `preview()`, `footer()`, `topBar()`,
  `bpmBox()` (a `TempoBox` with
  `getValue()`/`setValue(double, juce::NotificationType)`), `linkToggle()` (a
  `juce::Button&`), `addFolderButton()`, `emptyText()`, `clearRenders()`,
  `kDefaultWidth` 1100, `kDefaultHeight` 720, `kMinWidth` 900, `kMinHeight` 600;
  `std::int64_t LibraryView::sampleCount()`;
  `std::int64_t Browser::total() const`;
  `const audio::Overview* WaveformView::overview() const`; `PluginState`
  defaults 1100×720. Removed: `statusText()`, `tempoSyncToggle()`,
  `keySyncToggle()`, `gainMatchToggle()`.

- [ ] **Step 1: Write the failing test**

In `tests/plugin/test_browser.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_browser.cpp b/tests/plugin/test_browser.cpp
index d8b786f..b185162 100644
--- a/tests/plugin/test_browser.cpp
+++ b/tests/plugin/test_browser.cpp
@@ -18,10 +18,12 @@ TEST_CASE("Browser searches as the model changes", "[browser]")
     Browser browser(library);
     browser.poll();
     CHECK(browser.rows().size() == 3);
+    CHECK(browser.total() == 3);
     SearchModel m;
     m.text = "kick";
     browser.setSearch(m);
     REQUIRE(browser.rows().size() == 1);
+    CHECK(browser.total() == 3); // "1 of 3"
     CHECK(fs::equivalent(browser.path(0), f.kick));
     CHECK(browser.searchModel().text == "kick");
     CHECK(browser.contentHash(0).size() == 16);
```

In `tests/plugin/test_editor.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/plugin/test_editor.cpp b/tests/plugin/test_editor.cpp
index 8e32cbd..f88a1af 100644
--- a/tests/plugin/test_editor.cpp
+++ b/tests/plugin/test_editor.cpp
@@ -83,15 +83,26 @@ TEST_CASE("selecting a row plays it and space stops it", "[editor]")
     CHECK(rig.playing());
 }

-TEST_CASE("the status line says when a sync is a guess", "[editor]")
+TEST_CASE("the Tempo chip says what sync does to the selection", "[editor]")
 {
-    EditorRig rig;
-    CHECK(rig.editor->statusText().isNotEmpty());
+    EditorRig rig(AsmaProcessor::Mode::Standalone); // at its manual 120 BPM
     rig.type("bass");
     rig.editor->table().selectRow(0);
     REQUIRE(rig.playing());
     rig.editor->poll();
-    CHECK(rig.editor->statusText().contains("120")); // the loop's own tempo, from its name
+    CHECK(rig.editor->preview().tempoChip().detail() == juce::String::fromUTF8("120 \u2192 120 \u00b7 x1.00"));
+    rig.editor->bpmBox().setValue(180.0, juce::sendNotificationSync);
+    juce::AudioBuffer<float> buffer(2, 512);
+    juce::MidiBuffer midi;
+    rig.p->processBlock(buffer, midi); // the new tempo reaches the processor's transport
+    rig.editor->poll();
+    CHECK(rig.editor->preview().tempoChip().detail() == juce::String::fromUTF8("120 \u2192 180 \u00b7 x1.50"));
+
+    EditorRig plugin; // no host playing: no tempo to sync to
+    plugin.type("bass");
+    plugin.editor->table().selectRow(0);
+    plugin.editor->poll();
+    CHECK(plugin.editor->preview().tempoChip().detail() == juce::String::fromUTF8("no tempo \u00b7 plays as is"));
 }

 TEST_CASE("dragging a row out hands over the original or a render", "[editor]")
@@ -170,7 +181,7 @@ TEST_CASE("the standalone adds a folder and shows the scan in the table", "[edit
     while (rig.p->scans()->busy() && std::chrono::steady_clock::now() < deadline)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
     rig.editor->poll();
-    CHECK(rig.editor->statusText().contains("1 added"));
+    CHECK(rig.editor->footer().rightText().contains("Scan finished: 1 added"));
     CHECK(rig.editor->table().getNumRows() == 4);
 }

@@ -186,13 +197,150 @@ TEST_CASE("loading state with the editor open updates it, and old settings stay
     rig.editor->poll();
     CHECK(rig.editor->searchBox().getText() == "snare");
     CHECK(rig.editor->table().getNumRows() == 1);
-    CHECK_FALSE(rig.editor->tempoSyncToggle().getToggleState());
-    CHECK_FALSE(rig.editor->gainMatchToggle().getToggleState());
+    CHECK_FALSE(rig.editor->preview().tempoChip().getToggleState());
+    CHECK_FALSE(rig.editor->preview().gainChip().getToggleState());

-    rig.editor->keySyncToggle().setToggleState(true, juce::sendNotificationSync);
+    rig.editor->preview().chooseKey(1); // key sync on, in C
     const app::PluginState after = rig.p->pluginState();
     CHECK(after.sync.key);
     CHECK_FALSE(after.sync.tempo); // not written back from the editor's old view
     CHECK_FALSE(after.gainMatch);
     CHECK(after.search.text == "snare");
 }
+
+TEST_CASE("the table area says why it has nothing to show", "[editor]")
+{
+    test::LibraryFixture f; // no scan: no library
+    const juce::ScopedJuceInitialiser_GUI gui;
+    {
+        AsmaProcessor p;
+        std::unique_ptr<AsmaEditor> editor(dynamic_cast<AsmaEditor*>(p.createEditorAndMakeActive()));
+        editor->poll();
+        CHECK(editor->emptyText().contains("Open the asma app")); // a plugin never makes one
+        p.editorBeingDeleted(editor.get());
+    }
+    {
+        AsmaProcessor p(AsmaProcessor::Mode::Standalone);
+        std::unique_ptr<AsmaEditor> editor(dynamic_cast<AsmaEditor*>(p.createEditorAndMakeActive()));
+        editor->poll();
+        CHECK(editor->emptyText().contains("Add a folder"));
+        p.editorBeingDeleted(editor.get());
+    }
+    EditorRig rig;
+    CHECK(rig.editor->emptyText().isEmpty());
+    rig.type("nothing like this");
+    CHECK(rig.editor->emptyText() == "No samples match.");
+    CHECK(rig.editor->topBar().countText() == "0 of 3");
+    rig.type("kick");
+    CHECK(rig.editor->emptyText().isEmpty());
+    CHECK(rig.editor->topBar().countText() == "1 of 3");
+}
+
+TEST_CASE("a plugin shows the host's tempo where the standalone sets its own", "[editor]")
+{
+    EditorRig rig;
+    test::FakePlayHead host;
+    host.bpm = 124.0;
+    rig.p->setPlayHead(&host);
+    juce::AudioBuffer<float> buffer(2, 512);
+    juce::MidiBuffer midi;
+    rig.p->processBlock(buffer, midi);
+    rig.editor->poll();
+    CHECK(rig.editor->topBar().hostTempoText() == "host 124 BPM");
+    rig.p->setPlayHead(nullptr);
+}
+
+TEST_CASE("an edit in the preview reaches the project, the engine and the footer, and a new selection drops it",
+          "[editor]")
+{
+    EditorRig rig;
+    rig.type("kick");
+    rig.editor->table().selectRow(0);
+    REQUIRE(rig.playing());
+    rig.editor->poll();
+    CHECK(rig.editor->footer().dragText() == "Drag out: the original file");
+    rig.editor->preview().direction().segment(1).triggerClick();
+    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
+    CHECK(rig.p->pluginState().edits.direction == audio::Direction::Reverse);
+    rig.editor->poll();
+    CHECK(rig.editor->footer().dragText() == "Drag out renders: reversed");
+
+    rig.type("");
+    rig.editor->table().selectRow(rig.editor->table().getSelectedRow() == 0 ? 1 : 0);
+    CHECK(rig.p->pluginState().edits.direction == audio::Direction::Forward);
+    CHECK(rig.editor->preview().direction().selected() == 0);
+}
+
+TEST_CASE("the preview draws the selection's waveform", "[editor]")
+{
+    EditorRig rig;
+    rig.type("kick");
+    rig.editor->table().selectRow(0);
+    REQUIRE(rig.playing());
+    rig.editor->poll();
+    REQUIRE(rig.editor->preview().waveform().overview());
+    CHECK(rig.editor->preview().waveform().overview()->sampleRate == 48000);
+}
+
+TEST_CASE("Clear renders empties the renders folder", "[editor]")
+{
+    EditorRig rig;
+    rig.type("kick");
+    rig.editor->table().selectRow(0);
+    app::PluginState s = rig.p->pluginState();
+    s.edits.direction = audio::Direction::Reverse;
+    rig.p->setPluginState(s);
+    juce::StringArray files;
+    bool canMove = true;
+    const juce::DragAndDropTarget::SourceDetails details({}, rig.editor.get(), {});
+    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
+    CHECK(rig.editor->footer().clearButton().isVisible());
+    rig.editor->clearRenders();
+    CHECK(audio::RenderStore(audio::RenderStore::defaultDir()).bytes() == 0);
+    CHECK_FALSE(rig.editor->footer().clearButton().isVisible());
+}
+
+TEST_CASE("the window opens at its default size and keeps its minimum", "[editor]")
+{
+    EditorRig rig;
+    CHECK(rig.editor->getWidth() == AsmaEditor::kDefaultWidth);
+    CHECK(rig.editor->getHeight() == AsmaEditor::kDefaultHeight);
+    REQUIRE(rig.editor->getConstrainer());
+    CHECK(rig.editor->getConstrainer()->getMinimumWidth() == AsmaEditor::kMinWidth);
+    CHECK(rig.editor->getConstrainer()->getMinimumHeight() == AsmaEditor::kMinHeight);
+}
+
+TEST_CASE("a project whose sample has gone opens with nothing selected", "[editor]")
+{
+    EditorRig rig;
+    app::PluginState s = rig.p->pluginState();
+    s.selected = toUtf8(rig.f.dir.path() / "gone.wav");
+    s.edits.direction = audio::Direction::Reverse;
+    rig.p->setPluginState(s);
+    rig.editor->poll();
+    CHECK(rig.editor->table().getSelectedRow() < 0);
+    CHECK(rig.editor->footer().dragText().isEmpty()); // nothing to drag
+    CHECK_FALSE(rig.editor->preview().waveform().overview());
+}
+
+#if !JUCE_WINDOWS
+TEST_CASE("Clear renders says when a render could not be deleted", "[editor]")
+{
+    EditorRig rig;
+    rig.type("kick");
+    rig.editor->table().selectRow(0);
+    app::PluginState s = rig.p->pluginState();
+    s.edits.direction = audio::Direction::Reverse;
+    rig.p->setPluginState(s);
+    juce::StringArray files;
+    bool canMove = true;
+    const juce::DragAndDropTarget::SourceDetails details({}, rig.editor.get(), {});
+    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
+    const auto dir = audio::RenderStore::defaultDir();
+    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec); // nothing in it can be deleted
+    rig.editor->clearRenders();
+    fs::permissions(dir, fs::perms::owner_all);
+    CHECK(rig.editor->footer().rightText().contains("could not be deleted"));
+    CHECK(rig.editor->footer().clearButton().isVisible()); // still there to try again
+}
+#endif
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests`

Expected: the build stops:

```
plugin/test_browser.cpp:21:19: error: no member named 'total' in 'asma::app::Browser'
plugin/test_editor.cpp:93:23: error: no member named 'preview' in 'asma::app::AsmaEditor'
```

- [ ] **Step 3: Implement**

Replace the whole of `plugin/src/AsmaEditor.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"

#include "AsmaProcessor.h"
#include "DragOut.h"
#include "TempoChip.h"
#include "asma/audio/Render.h"
#include "asma/core/Fs.h"
#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kTimerHz = 30;      // the playhead moves smoothly
constexpr int kLibraryEvery = 6;  // ticks between library checks: 5 a second
constexpr int kRendersEvery = 60; // ticks between measuring the renders: every 2 s

juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

} // namespace

AsmaEditor::AsmaEditor(AsmaProcessor& owner)
    : juce::AudioProcessorEditor(owner), processor_(owner),
      library_(owner.libraryPath(), owner.isStandalone() ? LibraryView::Access::MayMigrate : LibraryView::Access::ReadOnly),
      top_(owner.isStandalone())
{
    setLookAndFeel(&lookAndFeel_);
    const PluginState state = processor_.pluginState();

    top_.searchBox().onTextChange = [this] { searchChanged(); };
    addAndMakeVisible(top_);
    if (processor_.isStandalone()) {
        top_.tempoBox().onValueChange = [this] {
            const double bpm = top_.tempoBox().getValue();
            processor_.setManualBpm(bpm);
            if (top_.linkChip().getToggleState()) processor_.setLinkTempo(bpm);
        };
        top_.linkChip().onClick = [this] { processor_.setLinkEnabled(top_.linkChip().getToggleState()); };
        top_.addFolderButton().onClick = [this] { chooseFolder(); };
        emptyAddFolder_.onClick = [this] { chooseFolder(); };
    }

    auto& header = table_.getHeader();
    header.addColumn("Name", kName, 400, 120, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("Type", kType, 76, 60, 120, juce::TableHeaderComponent::notSortable);
    header.addColumn("BPM", kBpm, 70, 50, 120, juce::TableHeaderComponent::notSortable);
    header.addColumn("Key", kKey, 56, 40, 100, juce::TableHeaderComponent::notSortable);
    header.addColumn("Length", kLength, 72, 50, 120, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive(true);
    table_.setHeaderHeight(theme::kHeaderRowHeight);
    table_.setRowHeight(theme::kRowHeight);
    table_.setMultipleSelectionEnabled(false);
    table_.setTitle("Samples");
    addAndMakeVisible(table_);

    empty_.setJustificationType(juce::Justification::centred);
    empty_.setFont(theme::font(theme::Face::Text, 13.0f));
    empty_.setColour(juce::Label::textColourId, theme::muted);
    addChildComponent(empty_);
    addChildComponent(emptyAddFolder_);

    preview_.onPlayStop = [this] {
        if (processor_.engine().status().playing) processor_.engine().stop();
        else processor_.engine().play();
    };
    preview_.onEditsChanged = [this](const audio::Edits& edits) {
        processor_.setEdits(edits);
        updateReadouts();
    };
    preview_.onTempoSync = [this](bool on) {
        audio::SyncSettings sync = processor_.pluginState().sync;
        sync.tempo = on;
        syncChanged(sync);
    };
    preview_.onKeySync = [this](std::optional<std::string> key) {
        audio::SyncSettings sync = processor_.pluginState().sync;
        sync.key = key.has_value();
        if (key) sync.projectKey = audio::KeyName(*key);
        syncChanged(sync);
    };
    preview_.onGainMatch = [this](bool on) {
        processor_.updateState([&](PluginState& s) { s.gainMatch = on; });
        processor_.engine().setGainMatch(on);
    };
    preview_.onQuantise = [this](double beats) {
        processor_.updateState([&](PluginState& s) { s.quantise = beats; });
        processor_.engine().setQuantise(beats);
    };
    addAndMakeVisible(preview_);

    footer_.onClearRenders = [this] {
        const auto bytes = audio::RenderStore(audio::RenderStore::defaultDir()).bytes();
        juce::NativeMessageBox::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Clear renders",
            "Delete " + Footer::sizeText(bytes) + " of rendered drag-outs? A project that plays a render from where it lies "
                "(Reaper, or Live without Collect All and Save) will lose that audio.",
            this, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<AsmaEditor>(this)](int ok) {
                if (ok != 0 && safe) safe->clearRenders();
            }));
    };
    addAndMakeVisible(footer_);

    setResizable(true, true);
    setResizeLimits(kMinWidth, kMinHeight, 8000, 8000);
    setSize(std::max(state.width, kMinWidth), std::max(state.height, kMinHeight));
    setWantsKeyboardFocus(true);
    loadState();
    updateRenderSize();
    poll();
    startTimerHz(kTimerHz);
}

AsmaEditor::~AsmaEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void AsmaEditor::loadState()
{
    loadedStates_ = processor_.stateLoads();
    const PluginState state = processor_.pluginState();
    top_.searchBox().setText(state.search.text, false);
    preview_.setEdits(state.edits);
    preview_.setGainMatch(state.gainMatch);
    preview_.setQuantise(state.quantise);
    preview_.setKey(state.sync.key, std::string(state.sync.projectKey.view()), {});
    if (processor_.isStandalone()) {
        top_.tempoBox().setValue(state.sync.hostBpm > 0.0 ? state.sync.hostBpm : 120.0, juce::dontSendNotification);
        top_.linkChip().setToggleState(state.link, juce::dontSendNotification);
    }
    browser_.setSearch(state.search);
    table_.updateContent();
    showSelection();
}

void AsmaEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    auto area = getLocalBounds();
    area.removeFromTop(theme::kTopBarHeight);
    area.removeFromBottom(theme::kFooterHeight);
    auto bottom = area.removeFromBottom(theme::kPreviewHeight);
    // The bottom panel, with the Similar list's place (plan 3c2b) on its right.
    g.setColour(theme::panel);
    g.fillRect(bottom);
    g.setColour(theme::border);
    g.fillRect(bottom.getX(), bottom.getY(), bottom.getWidth(), 1);
    g.fillRect(bottom.getRight() - theme::kSimilarWidth, bottom.getY(), 1, bottom.getHeight());
    // The sidebar's place (plan 3c2b).
    const auto sidebar = area.removeFromLeft(theme::kSidebarWidth);
    g.setColour(theme::panel);
    g.fillRect(sidebar);
    g.setColour(theme::border);
    g.fillRect(sidebar.getRight() - 1, sidebar.getY(), 1, sidebar.getHeight());
    // The chip row's place (plan 3c2b).
    g.fillRect(area.getX(), area.getY() + theme::kChipRowHeight - 1, area.getWidth(), 1);
}

void AsmaEditor::resized()
{
    auto area = getLocalBounds();
    top_.setBounds(area.removeFromTop(theme::kTopBarHeight));
    footer_.setBounds(area.removeFromBottom(theme::kFooterHeight));
    auto bottom = area.removeFromBottom(theme::kPreviewHeight);
    bottom.removeFromTop(1);
    bottom.removeFromRight(theme::kSimilarWidth);
    preview_.setBounds(bottom);
    area.removeFromLeft(theme::kSidebarWidth);
    area.removeFromTop(theme::kChipRowHeight);
    table_.setBounds(area);
    empty_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 520), 60).translated(0, -20));
    emptyAddFolder_.setBounds(area.withSizeKeepingCentre(120, 30).translated(0, 30));
    processor_.updateState([&](PluginState& s) {
        s.width = getWidth();
        s.height = getHeight();
    });
}

bool AsmaEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) {
        if (processor_.engine().status().playing) processor_.engine().stop();
        else processor_.engine().play();
        return true;
    }
    return false;
}

void AsmaEditor::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>("Add a sample folder");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser) {
                              const juce::File folder = chooser.getResult();
                              if (folder != juce::File()) addFolder(fromUtf8(folder.getFullPathName().toStdString()));
                          });
}

void AsmaEditor::addFolder(const std::filesystem::path& folder)
{
    ScanJob* scans = processor_.scans();
    if (!scans) return;
    std::string why;
    scanMessage_ = scans->addAndScan(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
    updateReadouts();
}

void AsmaEditor::clearRenders()
{
    audio::RenderStore store(audio::RenderStore::defaultDir());
    store.clear();
    // A render a program still holds open, or a folder asma may not write,
    // stays: say so rather than leave the size standing unexplained.
    if (store.bytes() > 0) scanMessage_ = "Some renders could not be deleted; another program may be using them.";
    updateRenderSize();
    updateReadouts();
}

void AsmaEditor::updateRenderSize()
{
    std::uintmax_t bytes = 0;
    try {
        bytes = audio::RenderStore(audio::RenderStore::defaultDir()).bytes();
    } catch (const std::exception&) {
        // No renders folder yet, or not readable: nothing to offer.
    }
    footer_.setRenderBytes(bytes);
}

void AsmaEditor::timerCallback()
{
    ++ticks_;
    if (ticks_ % kRendersEvery == 0) updateRenderSize();
    if (ticks_ % kLibraryEvery == 0) poll();
    else updateReadouts(); // the playhead and the chips keep up between library checks
}

void AsmaEditor::poll()
{
    // A host or preset menu loaded state while the window was open.
    if (processor_.stateLoads() != loadedStates_) loadState();
    if (ScanJob* scans = processor_.scans())
        if (const auto report = scans->takeReport()) {
            using Result = ScanReport::Result;
            switch (report->result) {
            case Result::Finished:
                scanMessage_ = "Scan finished: " + juce::String(report->index.added) + " added";
                break;
            case Result::Locked: scanMessage_ = "Another asma is scanning this library; try again when it is done."; break;
            case Result::Cancelled: scanMessage_ = "Scan cancelled."; break;
            case Result::Failed:
            case Result::Crashed: scanMessage_ = "Scan failed: " + juce::String(report->message); break;
            }
        }
    if (browser_.poll()) {
        table_.updateContent();
        showSelection();
    }
    updateReadouts();
}

void AsmaEditor::searchChanged()
{
    SearchModel model = browser_.searchModel();
    model.text = top_.searchBox().getText().toStdString();
    browser_.setSearch(model);
    processor_.updateState([&](PluginState& s) { s.search = model; });
    table_.updateContent();
    showSelection();
    updateReadouts();
}

void AsmaEditor::syncChanged(const audio::SyncSettings& sync)
{
    processor_.updateState([&](PluginState& s) { s.sync = sync; });
    processor_.engine().setSync(sync);
    updateReadouts();
}

void AsmaEditor::showSelection()
{
    int row = -1;
    try {
        row = browser_.rowOf(fromUtf8(processor_.pluginState().selected));
    } catch (const std::exception&) {
        // A saved path that is not valid UTF-8 selects nothing.
    }
    const juce::ScopedValueSetter quiet(quietSelection_, true);
    if (row >= 0) table_.selectRow(row);
    else table_.deselectAllRows();
    selectionChanged();
}

void AsmaEditor::selectionChanged()
{
    selectedRow_ = table_.getSelectedRow();
    selectedInfo_ = selectedRow_ >= 0 ? browser_.info(selectedRow_) : audio::SampleInfo{};
    selectedFolder_.clear();
    if (selectedRow_ >= 0) {
        const auto& rel = browser_.rows()[static_cast<std::size_t>(selectedRow_)].relPath;
        const auto slash = rel.find_last_of('/');
        if (slash != std::string::npos) selectedFolder_ = rel.substr(0, slash);
    }
}

void AsmaEditor::updateReadouts()
{
    const PluginState state = processor_.pluginState();
    const audio::EngineStatus status = processor_.engine().status();
    const bool current = status.generation == processor_.engine().selected(); // the status is the selection's

    // The top bar.
    top_.setCount(static_cast<int>(browser_.rows().size()), static_cast<int>(browser_.total()));
    if (!processor_.isStandalone()) top_.setHostBpm(processor_.hostBpm());

    // The table, or what it says instead.
    juce::String empty;
    const bool standalone = processor_.isStandalone();
    if (library_.state() == LibraryState::Missing)
        empty = standalone ? "No library yet. Add a folder of samples to start."
                           : "No library yet. Open the asma app and add a folder of samples.";
    else if (library_.state() != LibraryState::Open)
        empty = juce::String(library_.message());
    else if (browser_.rows().empty())
        empty = browser_.searchModel().text.empty() ? (standalone ? "The library is empty. Add a folder of samples."
                                                                  : "The library is empty.")
                                                    : "No samples match.";
    if (empty != empty_.getText()) empty_.setText(empty, juce::dontSendNotification);
    empty_.setVisible(empty.isNotEmpty());
    emptyAddFolder_.setVisible(standalone && empty.isNotEmpty() && library_.state() != LibraryState::Outdated
                               && library_.state() != LibraryState::Unreadable && browser_.searchModel().text.empty());

    // The preview.
    if (selectedRow_ >= 0 && selectedRow_ < static_cast<int>(browser_.rows().size())) {
        const SearchRow& r = browser_.rows()[static_cast<std::size_t>(selectedRow_)];
        const auto overview = processor_.engine().overview();
        preview_.waveform().setOverview(overview);
        preview_.setFile(utf8(r.name), overview ? PreviewPanel::fileLine(selectedFolder_, overview->sampleRate,
                                                                          overview->channels(), overview->seconds())
                                                : PreviewPanel::fileLine(selectedFolder_, 0, 0, r.duration));
        preview_.waveform().setPlayhead(current && status.playing ? std::optional<double>(status.position) : std::nullopt);
    } else {
        preview_.waveform().setOverview(nullptr);
        preview_.setFile({}, {});
    }
    preview_.setPlaying(status.playing);
    audio::SyncSettings sync = state.sync;
    sync.hostBpm = processor_.hostBpm();
    preview_.setTempo(state.sync.tempo, selectedRow_ >= 0 ? tempoChip(selectedInfo_, sync, current && status.failed)
                                                          : ChipText{state.sync.tempo ? "" : "off", Tone::Muted});
    std::string keyStatus;
    if (current && status.keyUnsure) keyStatus = "?";
    else if (current && status.keySynced && (status.semitones < 0.0 || status.semitones > 0.0))
        keyStatus = (status.semitones > 0.0 ? "+" : "") + std::to_string(std::llround(status.semitones));
    preview_.setKey(state.sync.key, std::string(state.sync.projectKey.view()), keyStatus);

    // The footer.
    if (selectedRow_ >= 0) {
        const audio::RenderSettings drag =
            dragSettings(state.edits, audio::planSync(selectedInfo_, sync), static_cast<int>(processor_.sampleRate()));
        footer_.setDrag(utf8(dragSummary(drag, selectedInfo_.bpm)));
    } else {
        footer_.setDrag({});
    }
    ScanJob* scans = processor_.scans();
    footer_.setStatus(scans && scans->busy() ? juce::String(scans->progress()) : scanMessage_);
}

int AsmaEditor::getNumRows() { return static_cast<int>(browser_.rows().size()); }

void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int width, int height, bool selected)
{
    if (selected) {
        g.fillAll(theme::amber.withAlpha(0.10f));
        g.setColour(theme::amber);
        g.fillRect(0, 0, 2, height);
    }
    g.setColour(theme::raised);
    g.fillRect(0, height - 1, width, 1);
}

void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const SearchRow& r = browser_.rows()[static_cast<std::size_t>(row)];
    juce::String text;
    juce::Font font = theme::font(theme::Face::Mono, 12.0f);
    juce::Colour colour = theme::text;
    int x = 0;
    switch (column) {
    case kName:
        text = utf8(r.name);
        font = theme::font(theme::Face::Text, 13.0f);
        x = AsmaLookAndFeel::kTableMargin;
        break;
    case kType:
        text = !r.isLoop ? "" : (*r.isLoop ? "loop" : "one-shot");
        font = theme::font(theme::Face::Text, 13.0f);
        colour = theme::muted;
        break;
    case kBpm: text = r.bpm ? utf8(bpmText(*r.bpm)) : juce::String(); break;
    case kKey: text = r.key ? utf8(*r.key) : juce::String(); break;
    case kLength:
        text = utf8(secondsText(r.duration));
        colour = theme::muted;
        break;
    default: break;
    }
    g.setFont(font);
    g.setColour(colour);
    g.drawText(text, x, 0, width - x - 6, height - 1, juce::Justification::centredLeft, true);
}

void AsmaEditor::selectedRowsChanged(int lastRowSelected)
{
    selectionChanged();
    if (quietSelection_ || lastRowSelected < 0) return;
    scanMessage_.clear();
    processor_.select(browser_.path(lastRowSelected), selectedInfo_);
    preview_.setEdits({}); // a new selection plays as it is
    updateReadouts();
}

void AsmaEditor::returnKeyPressed(int) { processor_.engine().play(); }

juce::var AsmaEditor::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var("asma-sample");
}

bool AsmaEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails&,
                                                      juce::StringArray& files, bool& canMoveFiles)
{
    const int row = table_.getSelectedRow();
    if (row < 0) return false;
    canMoveFiles = false;
    const auto path = browser_.path(row);
    const PluginState state = processor_.pluginState();
    audio::SyncSettings sync = state.sync;
    if (processor_.hostBpm() > 0.0) sync.hostBpm = processor_.hostBpm();
    const audio::RenderSettings settings =
        dragSettings(state.edits, audio::planSync(browser_.info(row), sync), static_cast<int>(processor_.sampleRate()));
    std::filesystem::path file = path;
    try {
        file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, browser_.contentHash(row));
    } catch (const std::exception&) {
        // A render that fails still leaves the original to drag.
    }
    files.add(juce::String::fromUTF8(toUtf8(file).c_str()));
    updateRenderSize();
    return true;
}

} // namespace asma::app
```

Replace the whole of `plugin/src/AsmaEditor.h` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Browser.h"
#include "LibraryView.h"
#include "ui/AsmaLookAndFeel.h"
#include "ui/Footer.h"
#include "ui/PreviewPanel.h"
#include "ui/TopBar.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace asma::app {

class AsmaProcessor;

// asma's window, laid out as spec section 9 and the approved design: the
// top bar, the sample table, the preview panel and the footer. The sidebar,
// the chip row and the Similar list keep their places empty until plan 3c2b.
class AsmaEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer,
                   private juce::TableListBoxModel,
                   private juce::Timer {
public:
    static constexpr int kDefaultWidth = 1100, kDefaultHeight = 720;
    static constexpr int kMinWidth = 900, kMinHeight = 600;

    explicit AsmaEditor(AsmaProcessor& owner);
    ~AsmaEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    // Opens a library that appeared, refreshes the rows when it changed, and
    // brings every readout up to date. The timer calls it; tests call it directly.
    void poll();
    // The file a drag out of the window carries: the original, or a render
    // of it when edits or sync change the audio. Never moved.
    bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& details,
                                              juce::StringArray& files, bool& canMoveFiles) override;
    juce::TableListBox& table() { return table_; }
    juce::TextEditor& searchBox() { return top_.searchBox(); }
    PreviewPanel& preview() { return preview_; }
    Footer& footer() { return footer_; }
    TopBar& topBar() { return top_; }
    // The standalone's tempo source and folders; hidden in a plugin.
    juce::Button& linkToggle() { return top_.linkChip(); }
    TempoBox& bpmBox() { return top_.tempoBox(); }
    juce::TextButton& addFolderButton() { return top_.addFolderButton(); }
    // What the table area says when it has no rows to show; empty when it has.
    juce::String emptyText() const { return empty_.getText(); }
    // Standalone: adds a sample folder and scans it (the button's chooser
    // ends here).
    void addFolder(const std::filesystem::path& folder);
    // Deletes every kept render; the footer's button asks first.
    void clearRenders();

private:
    enum Column { kName = 1, kType, kBpm, kKey, kLength };
    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    void timerCallback() override;
    void searchChanged();
    void syncChanged(const audio::SyncSettings& sync);
    void chooseFolder();
    void showSelection();   // selects the saved file's row without playing it
    void loadState();       // every control from the processor's state
    void selectionChanged(); // re-reads what the readouts need about the selection
    void updateReadouts();  // the preview, the chips, the footer, the empty state
    void updateRenderSize();

    AsmaProcessor& processor_;
    AsmaLookAndFeel lookAndFeel_; // first in, last out: every child uses it
    LibraryView library_;
    Browser browser_{library_};
    TopBar top_;
    juce::TableListBox table_{"Samples", this};
    juce::Label empty_;
    juce::TextButton emptyAddFolder_{juce::String::fromUTF8("Add folder…")};
    PreviewPanel preview_;
    Footer footer_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String scanMessage_; // the last scan's outcome, until the next selection
    bool quietSelection_ = false;    // selection changes that must not play
    std::uint64_t loadedStates_ = 0; // the processor's stateLoads() the controls show
    int ticks_ = 0;
    // What the readouts know about the selection.
    int selectedRow_ = -1;
    audio::SampleInfo selectedInfo_;
    std::string selectedFolder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};

} // namespace asma::app
```

In `plugin/src/Browser.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/Browser.cpp b/plugin/src/Browser.cpp
index f52a67b..b0176b3 100644
--- a/plugin/src/Browser.cpp
+++ b/plugin/src/Browser.cpp
@@ -7,6 +7,7 @@ void Browser::setSearch(SearchModel model)
 {
     model_ = std::move(model);
     rows_ = library_.search(model_);
+    total_ = library_.sampleCount();
 }

 bool Browser::poll()
@@ -14,6 +15,7 @@ bool Browser::poll()
     library_.refresh();
     if (!library_.changed()) return false;
     rows_ = library_.search(model_);
+    total_ = library_.sampleCount();
     return true;
 }

```

In `plugin/src/Browser.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/Browser.h b/plugin/src/Browser.h
index 6b7074e..479c620 100644
--- a/plugin/src/Browser.h
+++ b/plugin/src/Browser.h
@@ -18,6 +18,8 @@ public:
     void setSearch(SearchModel model);
     const SearchModel& searchModel() const { return model_; }
     const std::vector<SearchRow>& rows() const { return rows_; }
+    // Samples in the library, whatever the search: "48 of 585".
+    std::int64_t total() const { return total_; }

     // Opens the library if it has appeared and re-runs the search when it
     // changed. Returns whether the rows may have changed.
@@ -38,6 +40,7 @@ private:
     LibraryView& library_;
     SearchModel model_;
     std::vector<SearchRow> rows_;
+    std::int64_t total_ = 0;
 };

 } // namespace asma::app
```

In `plugin/src/LibraryView.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.cpp b/plugin/src/LibraryView.cpp
index 883c546..370cddf 100644
--- a/plugin/src/LibraryView.cpp
+++ b/plugin/src/LibraryView.cpp
@@ -82,6 +82,18 @@ std::vector<SearchRow> LibraryView::search(const SearchModel& model)
     return guarded([&] { return asma::search(*db_, model); }, std::vector<SearchRow>{});
 }

+std::int64_t LibraryView::sampleCount()
+{
+    return guarded(
+        [&] {
+            // The same files search() starts from, before any filter.
+            auto s = db_->prepare("SELECT COUNT(*) FROM files f JOIN roots r ON r.id = f.root_id "
+                                  "WHERE f.status = 'ok' AND r.enabled = 1");
+            return s.step() ? s.getInt(0) : std::int64_t{0};
+        },
+        std::int64_t{0});
+}
+
 audio::SampleInfo LibraryView::info(std::int64_t fileId)
 {
     return guarded(
```

In `plugin/src/LibraryView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/LibraryView.h b/plugin/src/LibraryView.h
index c48cfe8..5b901dc 100644
--- a/plugin/src/LibraryView.h
+++ b/plugin/src/LibraryView.h
@@ -45,6 +45,8 @@ public:

     // Empty unless open.
     std::vector<SearchRow> search(const SearchModel& model);
+    // Every sample a search could find: readable files in enabled folders. 0 unless open.
+    std::int64_t sampleCount();
     audio::SampleInfo info(std::int64_t fileId);
     // The file's content hash, or empty when unknown.
     std::string contentHash(std::int64_t fileId);
```

In `plugin/src/PluginState.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/PluginState.h b/plugin/src/PluginState.h
index 0121ef8..3657192 100644
--- a/plugin/src/PluginState.h
+++ b/plugin/src/PluginState.h
@@ -20,8 +20,8 @@ struct PluginState {
     bool gainMatch = true;
     double quantise = 0.0; // beats; 0 is off
     audio::Edits edits;    // of the selected sample
-    int width = 900;
-    int height = 600;
+    int width = 1100; // the window; spec section 9, Window
+    int height = 720;
 };

 std::string toJson(const PluginState& state);
```

In `plugin/src/ui/AsmaLookAndFeel.cpp`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.cpp b/plugin/src/ui/AsmaLookAndFeel.cpp
index ecc91dd..a4b9618 100644
--- a/plugin/src/ui/AsmaLookAndFeel.cpp
+++ b/plugin/src/ui/AsmaLookAndFeel.cpp
@@ -100,12 +100,14 @@ void AsmaLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHe
     g.fillRect(0, header.getHeight() - 1, header.getWidth(), 1);
 }

-void AsmaLookAndFeel::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name,
-                                            int, int width, int height, bool, bool, int)
+void AsmaLookAndFeel::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& name,
+                                            int columnId, int width, int height, bool, bool, int)
 {
+    // The first column starts at the table's margin, as its cells do.
+    const int x = header.getIndexOfColumnId(columnId, true) == 0 ? kTableMargin : 0;
     g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.06f));
     g.setColour(theme::muted);
-    g.drawText(name.toUpperCase(), 0, 0, width, height - 1, juce::Justification::centredLeft, true);
+    g.drawText(name.toUpperCase(), x, 0, width - x, height - 1, juce::Justification::centredLeft, true);
 }

 void AsmaLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
@@ -117,16 +119,16 @@ void AsmaLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x,
     g.fillRoundedRectangle(thumb.toFloat().reduced(3.0f), 2.0f);
 }

-void AsmaLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&)
+void AsmaLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
 {
-    g.setColour(theme::ground);
+    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
     g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                            theme::kRadius);
 }

-void AsmaLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor&)
+void AsmaLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
 {
-    g.setColour(theme::border);
+    g.setColour(editor.findColour(juce::TextEditor::outlineColourId));
     g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f),
                            theme::kRadius, 1.0f);
 }
```

In `plugin/src/ui/AsmaLookAndFeel.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/AsmaLookAndFeel.h b/plugin/src/ui/AsmaLookAndFeel.h
index 576af8c..e53b165 100644
--- a/plugin/src/ui/AsmaLookAndFeel.h
+++ b/plugin/src/ui/AsmaLookAndFeel.h
@@ -12,6 +12,7 @@ namespace asma::app {
 // A toggling button is quiet when off.
 class AsmaLookAndFeel : public juce::LookAndFeel_V4 {
 public:
+    static constexpr int kTableMargin = 14; // px before a table's first column's text
     AsmaLookAndFeel();

     // Fonts that name no typeface get the embedded Inter, never a system face.
```

Create `plugin/src/ui/TopBar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/TopBar.h"

#include "TempoChip.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace asma::app {

TempoBox::TempoBox()
{
    minus_.setButtonText(juce::String::fromUTF8("−"));
    plus_.setButtonText("+");
    minus_.setTitle("Slower");
    plus_.setTitle("Faster");
    for (auto* b : {&minus_, &plus_}) {
        b->getProperties().set("asma.quiet", true);
        b->getProperties().set("asma.segment", "middle"); // no frame of its own: the box draws it
        addAndMakeVisible(b);
    }
    minus_.onClick = [this] { setValue(value_ - 1.0); };
    plus_.onClick = [this] { setValue(value_ + 1.0); };
    text_.setFont(theme::font(theme::Face::Mono, 13.0f));
    text_.setJustificationType(juce::Justification::centred);
    text_.setEditable(false, true, false);
    text_.setTitle("Tempo");
    text_.onTextChange = [this] { setValue(text_.getText().getDoubleValue()); };
    addAndMakeVisible(text_);
    setValue(value_, juce::dontSendNotification);
}

void TempoBox::setValue(double bpm, juce::NotificationType notification)
{
    const double v = std::round(std::clamp(bpm, kMin, kMax) * 10.0) / 10.0;
    const bool changed = v < value_ || v > value_;
    value_ = v;
    const long long tenths = std::llround(v * 10.0);
    text_.setText(juce::String(tenths / 10) + "." + juce::String(tenths % 10), juce::dontSendNotification);
    if (changed && notification != juce::dontSendNotification && onValueChange) onValueChange();
}

void TempoBox::resized()
{
    auto area = getLocalBounds();
    minus_.setBounds(area.removeFromLeft(26));
    plus_.setBounds(area.removeFromRight(26));
    area.removeFromRight(34); // "BPM"
    text_.setBounds(area);
}

void TempoBox::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::ground);
    g.fillRoundedRectangle(r, theme::kRadius);
    g.setColour(theme::border);
    g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText("BPM", getLocalBounds().withTrimmedRight(26).removeFromRight(34), juce::Justification::centredLeft, false);
}

TopBar::TopBar(bool standalone) : standalone_(standalone)
{
    search_.setTextToShowWhenEmpty("Search samples", theme::muted);
    search_.setFont(theme::font(theme::Face::Text, 13.0f));
    search_.setIndents(30, 9);
    search_.setTitle("Search");
    // The bar draws the field's frame, with the magnifier and the count in it.
    search_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(search_);
    link_.setClickingTogglesState(true);
    addChildComponent(link_);
    addChildComponent(tempo_);
    addChildComponent(addFolder_);
    link_.setVisible(standalone);
    tempo_.setVisible(standalone);
    addFolder_.setVisible(standalone);
    link_.onStateChange = [this] { link_.setDot(theme::amber, link_.getToggleState()); };
}

void TopBar::setCount(int shown, int total)
{
    const juce::String text = shown == total ? juce::String(total) : juce::String(shown) + " of " + juce::String(total);
    if (text == count_) return;
    count_ = text;
    repaint();
}

void TopBar::setHostBpm(double bpm)
{
    const juce::String text = bpm > 0.0 ? "host " + juce::String::fromUTF8(bpmText(bpm).c_str()) + " BPM" : "host tempo unknown";
    if (text == hostTempo_) return;
    hostTempo_ = text;
    repaint();
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced(16, 0);
    area.removeFromLeft(188 + 16); // the mark and the wordmark
    auto right = area.removeFromRight(standalone_ ? 104 + 8 + 140 + 8 + link_.idealWidth() : 160);
    if (standalone_) {
        const int y = (getHeight() - 30) / 2;
        auto row = right.withY(y).withHeight(30);
        addFolder_.setBounds(row.removeFromRight(104));
        row.removeFromRight(8);
        tempo_.setBounds(row.removeFromRight(140));
        row.removeFromRight(8);
        link_.setBounds(row.removeFromRight(link_.idealWidth()));
    }
    area.removeFromRight(16);
    searchArea_ = area.withWidth(std::min(area.getWidth(), 520)).withSizeKeepingCentre(std::min(area.getWidth(), 520), 34);
    search_.setBounds(searchArea_.withTrimmedRight(80));
}

void TopBar::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    // The mark: an amber ring with its gap at half past one.
    const juce::Point<float> c(16.0f + 11.0f, static_cast<float>(getHeight()) / 2.0f);
    juce::Path ring;
    ring.addCentredArc(c.x, c.y, 9.0f, 9.0f, 0.0f, juce::degreesToRadians(75.0f), juce::degreesToRadians(405.0f) - 0.5f, true);
    g.setColour(theme::amber);
    g.strokePath(ring, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(theme::text);
    g.setFont(theme::font(theme::Face::Heading, 18.0f).withExtraKerningFactor(0.06f));
    g.drawText("asma", juce::Rectangle<int>(16 + 32, 0, 120, getHeight()), juce::Justification::centredLeft, false);

    // The search field's frame holds the count and a magnifier.
    const auto frame = searchArea_.toFloat().reduced(0.5f);
    g.setColour(theme::ground);
    g.fillRoundedRectangle(frame, theme::kRadius);
    g.setColour(theme::border);
    g.drawRoundedRectangle(frame, theme::kRadius, 1.0f);
    const juce::Point<float> m(frame.getX() + 18.0f, frame.getCentreY() - 1.0f);
    g.setColour(theme::muted);
    g.drawEllipse(m.x - 4.5f, m.y - 4.5f, 9.0f, 9.0f, 1.5f);
    g.drawLine(m.x + 3.5f, m.y + 3.5f, m.x + 7.0f, m.y + 7.0f, 1.5f);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.drawText(count_, searchArea_.withTrimmedRight(12).removeFromRight(80), juce::Justification::centredRight, false);

    if (!standalone_) {
        g.setFont(theme::font(theme::Face::Mono, 12.0f));
        g.setColour(theme::muted);
        g.drawText(hostTempo_, getLocalBounds().reduced(16, 0), juce::Justification::centredRight, false);
    }
}

} // namespace asma::app
```

Create `plugin/src/ui/TopBar.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// The standalone's manual tempo: minus, the value, plus. Double-click the
// value to type one.
class TempoBox : public juce::Component {
public:
    static constexpr double kMin = 20.0, kMax = 300.0;

    TempoBox();
    double getValue() const { return value_; }
    // Clamped to kMin..kMax, to tenths.
    void setValue(double bpm, juce::NotificationType notification = juce::sendNotificationAsync);
    std::function<void()> onValueChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    double value_ = 120.0;
    juce::TextButton minus_, plus_;
    juce::Label text_;
};

// The top of the window: the asma mark, the search field with its result
// count, and the tempo source: in the standalone Link, the manual tempo and
// "Add folder…"; in a plugin the host's tempo, read-only.
class TopBar : public juce::Component {
public:
    explicit TopBar(bool standalone);

    juce::TextEditor& searchBox() { return search_; }
    ChipButton& linkChip() { return link_; }
    TempoBox& tempoBox() { return tempo_; }
    juce::TextButton& addFolderButton() { return addFolder_; }

    void setCount(int shown, int total);
    // A plugin's host tempo; 0 when the host sends none.
    void setHostBpm(double bpm);
    juce::String hostTempoText() const { return hostTempo_; }
    juce::String countText() const { return count_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    const bool standalone_;
    juce::TextEditor search_;
    juce::String count_;
    juce::String hostTempo_;
    ChipButton link_{"Link"};
    TempoBox tempo_;
    juce::TextButton addFolder_{juce::String::fromUTF8("Add folder…")};
    juce::Rectangle<int> searchArea_;
};

} // namespace asma::app
```

In `plugin/src/ui/WaveformView.h`, apply (`git apply` takes it as is):

```diff
diff --git a/plugin/src/ui/WaveformView.h b/plugin/src/ui/WaveformView.h
index ba481ec..2a5137e 100644
--- a/plugin/src/ui/WaveformView.h
+++ b/plugin/src/ui/WaveformView.h
@@ -24,6 +24,7 @@ public:
     void setTrim(double start, double end);
     void setPlayhead(std::optional<double> seconds);

+    const audio::Overview* overview() const { return overview_.get(); }
     double trimStart() const { return start_; }
     double trimEnd() const { return end_; }

```

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests`

Expected: `All tests passed (429 assertions in 74 test cases)`; the build shows
no warnings from asma's code.

- [ ] **Step 5: Commit**

```sh
git add plugin/src/AsmaEditor.cpp plugin/src/AsmaEditor.h plugin/src/Browser.cpp plugin/src/Browser.h plugin/src/LibraryView.cpp plugin/src/LibraryView.h plugin/src/PluginState.h plugin/src/ui/AsmaLookAndFeel.cpp plugin/src/ui/AsmaLookAndFeel.h plugin/src/ui/TopBar.cpp plugin/src/ui/TopBar.h plugin/src/ui/WaveformView.h tests/plugin/test_browser.cpp tests/plugin/test_editor.cpp
git commit -m "app: the editor in the approved layout: top bar, table, preview panel and footer"
```

---

### Task 12: Hold the editor to the approved design

Spec section 12 (UI fidelity). The reference is the design rebuilt in HTML with
the demo data (see its README), rendered by Chrome; the test renders the editor
with JUCE's software renderer at 1280×800 and compares area by area at the
design's own coordinates, so a regression in one area is not lost in the dark
ground of the rest. On the prototype a correct editor differs by 1.55% (top
bar), 2.40% (table), 2.70% (preview) and 2.49% (footer), all glyph edges; with
the preview panel 20 px short the preview area reached 4.26% and failed. macOS
only: other systems draw text differently.

**Files:**

- Modify: `tests/CMakeLists.txt` (test)
- Create: `tests/plugin/test_ui_fidelity.cpp` (test)
- Create: `tests/ui/reference/README.md` (test)
- Create: `tests/ui/reference/main.html` (test)
- Create: `tests/ui/reference/main.png` (test)

**Interfaces:**

- Consumes: task 11's editor; `LibraryFixture`-style demo files; `PluginState`.
- Produces: `tests/ui/reference/main.html`, `main.png`, `README.md`;
  `ASMA_TEST_UI` (the `tests/ui` path) for the plugin tests; `ASMA_UI_OUT`
  (where `current.png` and `diff.png` go).

- [ ] **Step 1: Write the failing test**

In `tests/CMakeLists.txt`, apply (`git apply` takes it as is):

```diff
diff --git a/tests/CMakeLists.txt b/tests/CMakeLists.txt
index b708c30..c500d95 100644
--- a/tests/CMakeLists.txt
+++ b/tests/CMakeLists.txt
@@ -34,6 +34,7 @@ if(ASMA_BUILD_PLUGIN)
   target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
   target_compile_definitions(asma_plugin_tests PRIVATE
     ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
+    ASMA_TEST_UI="${CMAKE_CURRENT_SOURCE_DIR}/ui"
     ASMA_CLI_PATH="$<TARGET_FILE:asma>"
     ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>"
     JUCE_MODAL_LOOPS_PERMITTED=1) # lets a test run the message loop
```

Create `tests/plugin/test_ui_fidelity.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// The editor against the approved design: tests/ui/reference/main.png is the
// design's main artboard filled with this file's demo library, the 3c2b
// areas blanked (tests/ui/reference/main.html renders it; see README.md
// there). Font rendering differs between systems, so this runs on macOS
// only; the behaviour tests run everywhere.
#include "AsmaEditor.h"
#include "PluginTestUtil.h"
#include "Signals.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

using namespace asma;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 1280, kHeight = 800;
// A pixel differs when a channel is off by more than this. Text is drawn by
// Chrome in the reference and by JUCE here, so glyph edges never match
// exactly: each area of the design differs by 1.5% to 2.7% when the editor is
// right. Areas are judged apart, at the design's own coordinates, so a
// regression in one is not lost in the dark ground of the rest: the preview
// panel 20 px short differs by 4.5% there.
constexpr int kTolerance = 48;
constexpr double kMaxMismatch = 0.035;

struct Area {
    const char* name;
    juce::Rectangle<int> bounds;
};
const Area kAreas[] = {
    {"top bar", {0, 0, kWidth, 56}},
    {"table", {220, 56, kWidth - 220, 482}},
    {"preview", {0, 538, kWidth - 284, 236}},
    {"footer", {0, 774, kWidth, 26}},
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

struct Demo {
    test::TempDir dir;
    std::string dataDir = (dir.path() / "data").string();
    test::ScopedEnv env{"ASMA_DATA_DIR", dataDir.c_str()};
    fs::path lib = dir.path() / "Samples";
    Demo()
    {
        constexpr int rate = 8000; // small files; the waveform is not compared
        for (const auto& f : kDemo) test::writeWavFloat(lib / "Loops" / f.name, rate, {test::sine(55.0, f.seconds, 0.6, rate)});
        Db db = Db::open(dir.path() / "data" / "library.db");
        Library library(db);
        scanRoot(db, library.addRoot(lib));
    }
};

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
    const juce::Rectangle<int> masked[] = {wave.reduced(2, 14), {kWidth - 18, kHeight - 18, 18, 18}};
    juce::Image diff(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType{});
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) diff.setPixelAt(x, y, reference.getPixelAt(x, y).withMultipliedAlpha(0.25f));
    for (const Area& area : kAreas) {
        std::int64_t compared = 0, mismatched = 0;
        for (int y = area.bounds.getY(); y < area.bounds.getBottom(); ++y)
            for (int x = area.bounds.getX(); x < area.bounds.getRight(); ++x) {
                bool skip = false;
                for (const auto& m : masked) skip |= m.contains(x, y);
                if (skip) continue;
                const auto a = current.getPixelAt(x, y), b = reference.getPixelAt(x, y);
                const int d = std::max({std::abs(a.getRed() - b.getRed()), std::abs(a.getGreen() - b.getGreen()),
                                        std::abs(a.getBlue() - b.getBlue())});
                ++compared;
                if (d > kTolerance) {
                    ++mismatched;
                    diff.setPixelAt(x, y, juce::Colours::magenta);
                }
            }
        const double mismatch = static_cast<double>(mismatched) / static_cast<double>(compared);
        INFO(area.name << ": " << mismatch * 100.0 << "% of pixels differ; see " << outDir().string() << "/diff.png");
        CHECK(mismatch <= kMaxMismatch);
    }
    writePng(diff, outDir() / "diff.png");
    p.editorBeingDeleted(editor.get());
}
```

- [ ] **Step 2: Run it to see it fail**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fidelity]"`

Expected: with the test and the CMake change but before `main.png` exists:

```
tests/plugin/test_ui_fidelity.cpp:119: FAILED:
  REQUIRE( referenceFile.existsAsFile() )
```

- [ ] **Step 3: Implement**

Create `tests/ui/reference/README.md`:

````markdown
# UI reference

`main.png` is what `tests/plugin/test_ui_fidelity.cpp` compares the editor with.
It is the approved design's main artboard (a private design canvas, "asma 3c2a
UI") rebuilt as `main.html`, with these changes and nothing else:

- The rows, the search, the count, the file line and the tempo are the test's
  demo library and state, not the design's sample data.
- The areas plan 3c2b fills are blank: the sidebar, the chip row, the Similar
  list, and the favourite, rating and tags columns.
- The sample is stopped (a play button, no playhead), and the Key and Start
  chips say what they say when off ("off", "now"), so the picture is
  deterministic.
- The footer's right side is empty: the test has no scan to report and no
  renders.

The test leaves out the waveform's own shape (the demo audio is not the
design's) and the window's resize corner.

## Making it again

After a deliberate change to the design, edit `main.html` and render it with
Chrome at exactly 1280×800, scale 1:

```sh
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=1280,800 --screenshot="$PWD/main.png" "file://$PWD/main.html"
```

`main.html` loads the fonts from `plugin/fonts`, the same files the app embeds.
The test writes `current.png` and `diff.png` (mismatched pixels in magenta) to
`$ASMA_UI_OUT`, or `asma-ui` in the system's temporary directory.
````

Create `tests/ui/reference/main.html`:

<!-- prettier-ignore -->
````html
<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- The approved design's main artboard (a private design canvas, "asma 3c2a UI"),
     filled with tests/plugin/test_ui_fidelity.cpp's demo library, with the
     areas plan 3c2b fills (sidebar, chip row, Similar, the favourite, rating
     and tags columns) left blank. See README.md beside it. -->
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
.head, .row { height: 30px; display: grid; grid-template-columns: minmax(0, 1fr) 76px 70px 56px 72px; align-items: center; }
.head { border-bottom: 1px solid #2a2d33; font-family: "Space Grotesk"; font-size: 11px; font-weight: 600;
  letter-spacing: 0.06em; color: #8a8f98; }
.row { border-bottom: 1px solid #1c1f23; }
.row .mono { font-size: 12px; }
.head span:first-child, .row .name { padding-left: 14px; }
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
      <span class="count">13</span>
    </div>
    <div class="spacer"></div>
    <div class="right">
      <div class="chip"><span class="ring"></span>Link</div>
      <div class="tempo"><button>&#8722;</button><span class="v">180.0</span><span class="u">BPM</span><button>+</button></div>
      <button class="add">Add folder&#8230;</button>
    </div>
  </header>
  <div class="middle">
    <nav></nav>
    <main>
      <div class="chips"></div>
      <div class="head"><span>NAME</span><span>TYPE</span><span>BPM</span><span>KEY</span><span>LENGTH</span></div>
      <div class="row"><span class="name">Bass_Loop_132_rolling.wav</span><span class="muted">loop</span><span class="mono">132</span><span class="mono"></span><span class="mono muted">7.27 s</span></div>
      <div class="row"><span class="name">Bass_Loop_A#m_128_wobble.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">A#m</span><span class="mono muted">7.50 s</span></div>
      <div class="row"><span class="name">Bass_Loop_A#m_130.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">A#m</span><span class="mono muted">7.38 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Am_118.wav</span><span class="muted">loop</span><span class="mono">118</span><span class="mono">Am</span><span class="mono muted">8.14 s</span></div>
      <div class="row sel"><span class="name">Bass_Loop_Am_120.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Am</span><span class="mono muted">8.00 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Am_130_acid.wav</span><span class="muted">loop</span><span class="mono">130</span><span class="mono">Am</span><span class="mono muted">7.38 s</span></div>
      <div class="row"><span class="name">Bass_Loop_C_126_fingered.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono"></span><span class="mono muted">7.62 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Cm_126_wet.wav</span><span class="muted">loop</span><span class="mono">126</span><span class="mono">Cm</span><span class="mono muted">7.62 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Dm_120_dusty.wav</span><span class="muted">loop</span><span class="mono">120</span><span class="mono">Dm</span><span class="mono muted">4.00 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Em_124.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono">Em</span><span class="mono muted">3.87 s</span></div>
      <div class="row"><span class="name">Bass_Loop_F_124_sub.wav</span><span class="muted">loop</span><span class="mono">124</span><span class="mono"></span><span class="mono muted">7.74 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Fm_128.wav</span><span class="muted">loop</span><span class="mono">128</span><span class="mono">Fm</span><span class="mono muted">3.75 s</span></div>
      <div class="row"><span class="name">Bass_Loop_Gm_122.wav</span><span class="muted">loop</span><span class="mono">122</span><span class="mono">Gm</span><span class="mono muted">7.87 s</span></div>
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
    <aside></aside>
  </section>
  <footer><span>Drag out renders: reversed, trimmed, stretched to 180 BPM</span><span></span></footer>
</div>
</body>
</html>
````

Render the reference:

```sh
cd tests/ui/reference
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=1280,800 --screenshot="$PWD/main.png" "file://$PWD/main.html"
cd ../../..
```

Expected: a 1280×800 `main.png`
(`sips -g pixelWidth -g pixelHeight tests/ui/reference/main.png`).

- [ ] **Step 4: Run it to see it pass**

Run:
`cmake --build build --target asma_plugin_tests && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[fidelity]"`

Expected: `All tests passed (13 assertions in 1 test case)`. Add `-s` to see
each area's figure; `diff.png` in `$ASMA_UI_OUT` (default: `asma-ui` in the
temporary directory) shows mismatched pixels in magenta.

- [ ] **Step 5: Commit**

```sh
git add tests/CMakeLists.txt tests/plugin/test_ui_fidelity.cpp tests/ui/reference/README.md tests/ui/reference/main.html tests/ui/reference/main.png
git commit -m "test: hold the editor to the approved design, area by area, on macOS"
```

---

### Task 13: Docs

The README describes the window and the fonts' licence; the spec's Tempo chip
table gains the two cases the prototype met that it lacked (a loop with no tempo
known, a host that sends no tempo).

**Files:**

- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`

**Interfaces:**

- Consumes: tasks 1 to 12.
- Produces: nothing code depends on.

- [ ] **Step 1: Write the docs**

In `README.md`, apply (`git apply` takes it as is):

```diff
diff --git a/README.md b/README.md
index 7048364..e40767d 100644
--- a/README.md
+++ b/README.md
@@ -73,6 +73,24 @@ says so and leaves the sample alone. Renders stay in `renders` in the data
 directory until you clear them: a DAW may play a dragged file from where it
 lies. `--renders DIR` overrides the folder.

+## The app
+
+The window is the sample table with a preview panel under it: the selected
+sample's waveform, with trim handles to drag (double-click one to put it back),
+a forward, reverse or ping-pong switch, loop mode, and chips for tempo sync, key
+sync, loudness matching and a quantised start. The Tempo chip says what sync
+does to the selection and, when it does nothing, why: a one-shot, a long stem
+with no loop verdict, a tempo asma is unsure of, or no tempo from the host.
+Selecting another sample drops the edits, so moving through the table always
+plays each sample as it is. The footer says what a drag-out will carry and
+offers to clear the kept renders. Space plays and stops.
+
+The look is checked against the approved design by `[fidelity]` in
+`asma_plugin_tests`, on macOS only; `tests/ui/reference/README.md` says how the
+reference picture is made.
+
 ## License

-GPLv3. See `LICENSE`.
+GPLv3. See `LICENSE`. The fonts in `plugin/fonts` (Inter, JetBrains Mono, Space
+Grotesk) are under the SIL Open Font License 1.1; their licences are beside
+them.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, apply (`git apply` takes
it as is):

```diff
diff --git a/docs/superpowers/specs/2026-09-25-asma-design.md b/docs/superpowers/specs/2026-09-25-asma-design.md
index 1818614..a614b41 100644
--- a/docs/superpowers/specs/2026-09-25-asma-design.md
+++ b/docs/superpowers/specs/2026-09-25-asma-design.md
@@ -348,6 +348,8 @@ nothing:
 | loop, synced            | `120 → 180 · x1.50`        | green  |
 | loop, at 0.25x or 4x    | `120 → 30 · x0.25 max`     | amber  |
 | loop, tempo below 0.3   | `~97 ? · plays as is`      | amber  |
+| loop, no tempo known    | `? · plays as is`          | amber  |
+| no tempo from the host  | `no tempo · plays as is`   | muted  |
 | one-shot                | `one-shot · plays as is`   | muted  |
 | no loop verdict (stems) | `not a loop · plays as is` | muted  |
 | tempo sync switched off | `off`                      | muted  |
```

- [ ] **Step 2: Check them**

Run:
`prettier --check README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: both files pass; no em-dashes.

- [ ] **Step 3: Commit**

```sh
git add README.md docs/superpowers/specs/2026-09-25-asma-design.md
git commit -m "docs: the app's window, its fonts' licence, and the Tempo chip's two missing cases"
```

---

### Task 14: Verify and merge

- [ ] **Step 1: Everything, locally**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/asma_tests "[.perf]"
TOOLS=build/validators ci/validate-plugins.sh build
```

Expected: no warnings from asma's code; `100% tests passed out of 390`;
`search took` under 50 ms; pluginval `SUCCESS` and clap-validator `0 failed`.

- [ ] **Step 2: Try the app by hand (macOS)**

Make a folder with a loop whose name gives its tempo (for example
`drums_loop_120_bpm.wav`) and a one-shot, then:

```sh
export ASMA_DATA_DIR=$(mktemp -d)
build/plugin/asma_plugin_artefacts/Release/Standalone/asma.app/Contents/MacOS/asma
```

(`open` does not hand the app `ASMA_DATA_DIR`; run the binary.)

Expected: the table area says there is no library yet, with "Add folder…";
adding the folder scans it (progress, then the outcome, in the footer); the
window looks like `tests/ui/reference/main.png`; selecting the loop plays it and
draws its waveform; dragging a handle trims it and the sound restarts once on
release; the direction switch reverses it; at 180 BPM the Tempo chip says
`120 → 180 · x1.50` in green; the one-shot's chip says `one-shot · plays as is`;
selecting the one-shot drops the loop's edits; the footer says what a drag
carries; dragging the edited loop to the desktop leaves a render; "Clear
renders" asks, then empties `$ASMA_DATA_DIR/renders`. Then load the AU or VST3
in a DAW: the host's tempo shows in the top bar and there is no "Add folder…".

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-3c2a-app
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green, including "Validate plugins". This is
the first time the 3c2a code builds off macOS: look out for MSVC warnings in
`plugin/src/ui/`, and for the `[fidelity]` test on the macOS runner (it runs
there; a large difference from the local figures means the runner draws text
differently, and the fix is a runner-side tolerance, not a new reference).

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-3c2a-app
git -C product/asma worktree remove .worktrees/plan-3c2a
git -C product/asma branch -d plan-3c2a-app
```

Push `main` once the user agrees, and delete the remote branch.
