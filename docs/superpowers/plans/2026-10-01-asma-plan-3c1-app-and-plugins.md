# asma Plan 3c1: App and Plugins Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** asma runs as a JUCE 9 standalone and as VST3, AU, CLAP and LV2
plugins: a plain but complete browser that searches the library, auditions as
the selection moves, syncs to the host or Ableton Link, saves its state with the
project, drags out the original or a render, and (in the standalone) adds
folders and scans them; validated by pluginval and clap-validator on all three
platforms.

**Architecture:** One `juce_add_plugin` target builds every format plus the
Standalone. Its code lives in `plugin/` as the `asma_ui` INTERFACE library,
which a headless JUCE console app (`asma_plugin_tests`) also links. The
`AsmaProcessor` feeds the host's MIDI and transport, sample-accurately, into
plan 3b's `AuditionEngine`; in the standalone, Ableton Link or a manual tempo
stands in for the host. JUCE-free pieces carry the logic the UI shows:
`PluginState` (versioned JSON), `LibraryView` (read-only library access that
notices other writers), `Browser` (search and rows) and `ScanJob` (the
standalone's add-folder and scan, through plan 3a's `ScanSupervisor`). The
`AsmaEditor` is a thin, plain JUCE layer over them; plan 3c2 replaces its looks.
Renders for drag-out move from an evicting cache to a kept store.

**Tech Stack:** C++20, JUCE 9.0.3 (AGPLv3), clap-juce-extensions (MIT), Ableton
Link 4.1 (GPLv2+), Catch2, pluginval 1.0.4, clap-validator 0.4.1.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (sections 2, 3, 4,
8 Plugin output, 9, 11 and 12). Task 11 amends it.

**How this plan was checked:** every task was built on a clone of `main`
(d06457f), then replayed commit by commit on a Release build: with only the
task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The code blocks below were checked by applying them in
order to a fresh `main` and comparing the result with the prototype: they match
file for file. Unlike earlier plans, the prototype also ran on CI before the
plan was written (a throwaway branch, since deleted): macOS, Windows and Ubuntu
build, test and validate green, with no warnings from asma's code. The fixes CI
asked for are already in the tasks (Linux needs `libxi-dev` for JUCE 9,
position-independent static libraries for the LV2 plugin, and GCC's
`-Wfloat-equal` under JUCE's warning set).

## Where this sits

Plan 3 was split in three, and its last part in two (the user's call,
2026-09-30):

- **3a, 3b (merged):** organise data and scan supervision; the audition engine
  and drag-out renders.
- **3c1 (this plan):** the JUCE build, the processor, plugin state, library
  access, a plain browser, the standalone's tempo and scanning, and plugin
  validation in CI.
- **3c2:** the real UI from spec section 9 (sidebar, facet chips, waveform with
  trim handles, Similar strip, Problems panel, ratings through the CLI helper,
  the Anode theme), on top of 3c1's behaviour.

Plan 4 (file manager) and plan 5 (packaging) are unchanged.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (CMake, shell and YAML files:
  `# SPDX-...`). JUCE is used under AGPLv3, Link under GPLv2+.
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11), no
  floating-point `std::to_chars`/`std::from_chars` (macOS 12). The plugin build
  uses JUCE's warning set, which includes `-Wfloat-equal`: compare floats with
  `<`/`>`, never `==`/`!=`.
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE. `-DASMA_BUILD_PLUGIN=OFF` still
  builds the core, the CLI and their tests.
- The plugin never writes the library inside the host: `LibraryView` opens it
  read-only and never creates it; only the standalone adds folders and scans,
  and the scan runs in `asma-scan`, outside the app.
- Nothing on the audio thread allocates, locks or blocks: `processBlock` reads
  standalone settings from atomics, never from the state mutex.
- Paths cross JUCE as UTF-8 (`fromUtf8`/`toUtf8`), never through a narrow
  `std::string` constructor of `std::filesystem::path`.
- No em-dashes in code, comments, docs or commit messages. No AI references or
  co-author trailers. Worktree, then a direct merge; no PR.

## Decisions made while prototyping

- **JUCE 9.0.3, the 3c1/3c2 split, and kept renders** were the user's calls
  (2026-09-30). JUCE 9 shipped in July with the same AGPLv3 option;
  clap-juce-extensions has supported it since 2026-07-21.
- **Renders are kept, not evicted.** DAWs that play a dragged file in place
  (Reaper; Live unless the set is collected) lose audio when a render goes.
  macOS does not purge `~/Library/Caches` (checked: its `deleted` service only
  purges registered system caches and files flagged purgeable), but cleaner apps
  empty it, so renders move to `<data dir>/renders`. `RenderStore` replaces
  `RenderCache`; `asma renders [clear]` reports and clears; `clear()` also
  removes temporary files a crash left behind. `defaultCacheDir` goes: it has no
  other user.
- **`asma_ui` is an INTERFACE library** holding `plugin/src`. JUCE modules
  compile their sources into each consumer, so the plugin and the test app each
  compile JUCE once and nothing is linked twice.
- **JUCE's warning flags apply to the plugin target only.** The tests keep
  Catch2's exact float comparisons, which `-Wfloat-equal` would flag.
- **One preview cache per process** (`juce::SharedResourcePointer`), so ten
  instances in a session do not hold ten copies of the same samples.
- **The processor keeps the sample rate `prepareToPlay` gave it.**
  `getSampleRate()` is 0 until a wrapper calls `setRateAndBufferSizeDetails`; a
  test calling `prepareToPlay` directly got a NaN host position from it.
- **Blocks are split at MIDI events**, and each piece gets the host position
  advanced to where it starts, so notes and quantised starts land on their own
  sample.
- **Plain major keys were lost by 3a's saved-search reader** (and refused by
  `--key`): `parseKeyToken` rejects a bare "C" on purpose, for file names.
  `canonicalKey` accepts exactly the keys asma writes. Found by the plugin-state
  round trip.
- **Plugin state is one versioned JSON object** built with `JsonLine`; the
  search model rides along as a JSON string in the same format a saved search
  uses. Every field is range-checked on the way in, so a hand-edited or newer
  project loads what it can.
- **A restored selection looks its tempo and key up in the library.** Without it
  a restored loop played unsynced.
- **`LibraryView` reports why it cannot read** (missing, older, newer,
  unreadable) and tries again whenever asked, so a plugin picks up a library a
  first scan creates, or that the standalone migrates.
- **Headless editor tests pump the message loop**: `TextEditor` delivers its
  change notifications asynchronously. `JUCE_MODAL_LOOPS_PERMITTED=1` is set for
  the test app only.
- **Drag-out uses `DragAndDropContainer::shouldDropFilesWhenDraggedExternally`**
  and renders on the message thread when edits are active: a loop renders in
  well under a second. 3c2 can move it off the thread if it shows.
- **Ableton Link** is made only in the standalone; its audio-session capture is
  real-time safe. Its CMake config sets `CMAKE_CXX_STANDARD` to 17, so it is
  included inside a function to keep that out of asma's scope.
- **Tests force standalone behaviour through `AsmaProcessor::Mode`**; in a real
  build JUCE's `wrapperType` decides.
- **The standalone owns the `ScanJob`**, so closing the window does not cancel a
  scan, and ships `asma-scan` beside its executable (copied after each build).
- **Validation:** pluginval at strictness 10 for VST3 everywhere and AU on the
  macOS runner (macOS only finds an installed AU, and the runner is throwaway).
  clap-validator skips `param-conversions`, which divides by the parameter count
  and crashes on itself with none (it says so). Its `state-invalid-random`
  warning is expected: asma loads unreadable state as the defaults.
- **Linux CI** needs JUCE's development packages plus `libxi-dev` (JUCE 9's
  XInput2), `xvfb` for the UI tests and pluginval, and position-independent
  static libraries for the LV2 plugin, which is a shared object.

## Review Focus

1. **A library that is not there yet, is being written, or comes from another
   asma version.** The plugin waits, picks it up when it appears, refreshes on
   other writers' commits, and explains what it cannot read. Pinned in Task 6
   (all four `LibraryView` tests) and Task 7 ("Browser picks up a library that
   appears and changes later").
2. **A project from another asma version or a broken host.** It loads what it
   can and never crashes. Pinned in Task 5 ("plugin state from another version
   loads what it can", the garbage case in "the processor saves its state").
3. **Notes and quantised starts in the middle of a block, and tempo changes.**
   Pinned in Task 3 (all four `[processor]` audio tests).
4. **The plugin writing the library from inside a host.** It must not. Pinned in
   Task 6 ("a reader never creates it") and Task 9 ("only the standalone
   scans").
5. **Windows and Linux hosts.** Pinned in Task 10: pluginval at strictness 10
   and clap-validator run on all three platforms in CI.

---

## File Structure

```
plugin/                      the app and plugins (JUCE)
  CMakeLists.txt             asma_ui (INTERFACE) and the asma_plugin target
  src/AsmaProcessor.h/.cpp   the AudioProcessor: engine, state, tempo, scans
  src/AsmaEditor.h/.cpp      the plain browser
  src/PluginState.h/.cpp     what a project saves (JSON)
  src/LibraryView.h/.cpp     read-only library access with change notices
  src/Browser.h/.cpp         the search and its rows
  src/ScanJob.h/.cpp         the standalone's add-folder and scan
ci/validate-plugins.sh       pluginval and clap-validator
tests/plugin/                the JUCE side, headless (asma_plugin_tests)
  PluginTestUtil.h, LibraryFixture.h
  test_processor.cpp, test_processor_audio.cpp, test_plugin_state.cpp,
  test_library_view.cpp, test_browser.cpp, test_editor.cpp,
  test_standalone_tempo.cpp, test_scan_job.cpp
```

Modified: `CMakeLists.txt`, `cmake/Dependencies.cmake` (JUCE,
clap-juce-extensions, Link), `.github/workflows/ci.yml`,
`audio/include/asma/audio/Render.h`, `audio/src/Render.cpp` (`RenderStore`),
`audio/include/asma/audio/AuditionEngine.h`, `audio/src/AuditionEngine.cpp`
(`allNotesOff`), `core/include/asma/core/Fs.h`, `core/src/Fs.cpp`
(`defaultCacheDir` removed), `core/include/asma/core/NameParse.h`,
`core/src/NameParse.cpp`, `core/src/Query.cpp`, `apps/SearchArgs.cpp`
(`canonicalKey`), `apps/asma_main.cpp` (`renders`), `tests/CMakeLists.txt`, core
tests, `README.md`, the spec.

Commands: `cmake -B build && cmake --build build`, then `asma_tests` as before
and the JUCE tests with
`./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[tag]"`.
Configure again whenever a task adds files: the source lists are globs. The
first configure with JUCE downloads JUCE, clap-juce-extensions (with CLAP) and
later Link; a cold build of JUCE takes a few minutes.

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-3c1 -b plan-3c1-app
```

All paths below are relative to `product/asma/.worktrees/plan-3c1`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.

---

### Task 1: Keep renders until the user clears them

3b's `RenderCache` evicted the least recently used renders past 2 GB, from the
platform cache directory. A DAW that plays a dragged file where it lies would
then lose audio. `RenderStore` keeps every render in `<data dir>/renders` until
the user clears it, and reports its size.

**Files:**

- Modify: `audio/include/asma/audio/Render.h`, `audio/src/Render.cpp`
  (`RenderCache` becomes `RenderStore`), `core/include/asma/core/Fs.h`,
  `core/src/Fs.cpp` (`defaultCacheDir` removed), `apps/asma_main.cpp`
  (`--renders`, `asma renders`), `tests/test_render.cpp`, `tests/test_fs.cpp`,
  `tests/test_cli_e2e.cpp`

**Interfaces:**

- Consumes: `renderToFile`, `RenderSettings`, `probeFile`, `contentHash`,
  `defaultDataDir` (plans 1 and 3b).
- Produces: `class asma::audio::RenderStore(std::filesystem::path dir)` with
  `static path defaultDir()` (`defaultDataDir() / "renders"`),
  `path fileFor(const path& source, const RenderSettings&, std::string contentHash = {})`,
  `static std::string fileName(std::string_view, const RenderSettings&, int)`,
  `const path& dir() const`, `std::uintmax_t bytes() const`,
  `std::size_t clear()`. CLI: `asma render ... [--renders DIR]`,
  `asma renders [clear] [--renders DIR]`.

Behaviour the tests pin:

- Every render stays on disk however many are made; `bytes()` counts them and
  any leftover temporary file; `clear()` deletes renders and temporary files
  only, and returns how many.
- The store lives in the data directory (`ASMA_DATA_DIR` moves it).
- `asma renders` prints `N renders, X MB`; `asma renders clear` prints
  `N renders removed`; anything else is a usage error.

- [ ] **Step 1: Write the failing tests**

In `tests/test_cli_e2e.cpp`, replace:

```cpp
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = cli.lib / "Loops" / "Bass_Loop_Am_128.wav";
    const auto kick = cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav");
    const std::string cache = " --cache " + quote(cli.dir.path() / "renders");
    const auto printed = [](const RunResult& r) { return asma::fromUtf8(r.out.substr(0, r.out.find('\n'))); };

    // The name says 128 and loop: at 100 it stretches by 100/128.
```

with:

```cpp
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = cli.lib / "Loops" / "Bass_Loop_Am_128.wav";
    const auto kick = cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav");
    const std::string cache = " --renders " + quote(cli.dir.path() / "renders");
    const auto printed = [](const RunResult& r) { return asma::fromUtf8(r.out.substr(0, r.out.find('\n'))); };

    // The name says 128 and loop: at 100 it stretches by 100/128.
```

In `tests/test_cli_e2e.cpp`, replace:

```cpp
    CHECK(cli.runAsma("render " + quote(kick) + " --rate 10" + cache).exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --rate 44100.5" + cache).exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --trim-start 1 --reverse" + cache).exitCode == 1); // 0.1 s file
}
```

with:

```cpp
    CHECK(cli.runAsma("render " + quote(kick) + " --rate 10" + cache).exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --rate 44100.5" + cache).exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --trim-start 1 --reverse" + cache).exitCode == 1); // 0.1 s file

    // Two renders so far: the stretched loop and the reversed kick.
    const RunResult size = cli.runAsma("renders" + cache);
    CHECK(size.exitCode == 0);
    CHECK(size.out.rfind("2 renders, ", 0) == 0);
    CHECK(cli.runAsma("renders clear" + cache).out == "2 renders removed\n");
    CHECK(cli.runAsma("renders" + cache).out == "0 renders, 0 MB\n");
    CHECK(cli.runAsma("renders shrink" + cache).exitCode == 2);
}
```

In `tests/test_fs.cpp`, replace:

```cpp
    CHECK(asma::defaultDataDir() == dir.path());
}

TEST_CASE("defaultCacheDir honours ASMA_CACHE_DIR and differs from the data dir", "[fs]")
{
    CHECK(asma::defaultCacheDir() != asma::defaultDataDir());
    TempDir dir;
    const std::string wanted = dir.path().string();
    asma::test::ScopedEnv env("ASMA_CACHE_DIR", wanted.c_str());
    CHECK(asma::defaultCacheDir() == dir.path());
}

TEST_CASE("fileTimeToInt orders later times after earlier ones", "[fs]")
{
    const auto now = fs::file_time_type::clock::now();
```

with:

```cpp
    CHECK(asma::defaultDataDir() == dir.path());
}

TEST_CASE("fileTimeToInt orders later times after earlier ones", "[fs]")
{
    const auto now = fs::file_time_type::clock::now();
```

In `tests/test_render.cpp`, replace:

```cpp
    CHECK(b.channels[0][100] == Catch::Approx(99.0f / 100000.0f));
}

TEST_CASE("RenderCache hands out the original when nothing changes the audio", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 48000, {counting(4800)});
    RenderCache cache(dir.path() / "renders");
    RenderSettings s;
    s.sampleRate = 44100; // not an edit
    s.edits.loop = LoopMode::On;
```

with:

```cpp
    CHECK(b.channels[0][100] == Catch::Approx(99.0f / 100000.0f));
}

TEST_CASE("RenderStore hands out the original when nothing changes the audio", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 48000, {counting(4800)});
    RenderStore cache(dir.path() / "renders");
    RenderSettings s;
    s.sampleRate = 44100; // not an edit
    s.edits.loop = LoopMode::On;
```

In `tests/test_render.cpp`, replace:

```cpp
    CHECK_FALSE(fs::exists(dir.path() / "renders"));
}

TEST_CASE("RenderCache renders once per content and settings", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 48000, {counting(4800)});
    RenderCache cache(dir.path() / "renders");
    RenderSettings s;
    s.edits.direction = Direction::Reverse;
    const auto first = cache.fileFor(src, s, "00000000000000aa");
```

with:

```cpp
    CHECK_FALSE(fs::exists(dir.path() / "renders"));
}

TEST_CASE("RenderStore renders once per content and settings", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 48000, {counting(4800)});
    RenderStore cache(dir.path() / "renders");
    RenderSettings s;
    s.edits.direction = Direction::Reverse;
    const auto first = cache.fileFor(src, s, "00000000000000aa");
```

In `tests/test_render.cpp`, replace:

```cpp
    CHECK(hashed.filename().string().size() == other.filename().string().size());
}

TEST_CASE("RenderCache names do not follow the locale", "[render]")
{
    RenderSettings s;
    s.edits.trimStart = 0.5;
    s.ratio = 1.25;
    const std::string before = RenderCache::fileName("h", s, 44100);
    const char* previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = previous ? previous : "C";
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "it_IT.UTF-8", "German_Germany.1252"})
        if (std::setlocale(LC_NUMERIC, name)) break;
    const std::string after = RenderCache::fileName("h", s, 44100);
    std::setlocale(LC_NUMERIC, saved.c_str());
    CHECK(before == "h-s500000-eend-f-x1250000-c0-44100.wav");
    CHECK(after == before);
}

TEST_CASE("RenderCache evicts the least recently used past its capacity", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 1000, {counting(1000)});
    // Each reversed render: 1000 frames of float, just over 4000 bytes.
    RenderCache cache(dir.path() / "renders", 10000);
    RenderSettings s;
    s.edits.direction = Direction::Reverse;
    const auto a = cache.fileFor(src, s, "aaaaaaaaaaaaaaaa");
    fs::last_write_time(a, fs::last_write_time(a) - std::chrono::hours(2));
    const auto b = cache.fileFor(src, s, "bbbbbbbbbbbbbbbb");
    fs::last_write_time(b, fs::last_write_time(b) - std::chrono::hours(1));
    CHECK(cache.fileFor(src, s, "aaaaaaaaaaaaaaaa") == a); // a used again: now b is the oldest
    const auto c = cache.fileFor(src, s, "cccccccccccccccc");
    CHECK(fs::exists(a));
    CHECK_FALSE(fs::exists(b));
    CHECK(fs::exists(c));

    RenderCache tiny(dir.path() / "renders", 1); // smaller than any render
    const auto d = tiny.fileFor(src, s, "dddddddddddddddd");
    CHECK(fs::exists(d)); // the file being dragged always stays
    CHECK_FALSE(fs::exists(a));
}

TEST_CASE("renderToFile refuses an empty region and an absurd sample rate", "[render]")
```

with:

```cpp
    CHECK(hashed.filename().string().size() == other.filename().string().size());
}

TEST_CASE("RenderStore names do not follow the locale", "[render]")
{
    RenderSettings s;
    s.edits.trimStart = 0.5;
    s.ratio = 1.25;
    const std::string before = RenderStore::fileName("h", s, 44100);
    const char* previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = previous ? previous : "C";
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "it_IT.UTF-8", "German_Germany.1252"})
        if (std::setlocale(LC_NUMERIC, name)) break;
    const std::string after = RenderStore::fileName("h", s, 44100);
    std::setlocale(LC_NUMERIC, saved.c_str());
    CHECK(before == "h-s500000-eend-f-x1250000-c0-44100.wav");
    CHECK(after == before);
}

TEST_CASE("RenderStore keeps every render until it is cleared", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 1000, {counting(1000)});
    RenderStore store(dir.path() / "renders");
    CHECK(store.bytes() == 0); // no folder yet
    RenderSettings s;
    s.edits.direction = Direction::Reverse;
    std::vector<fs::path> made;
    for (const char* hash : {"aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", "cccccccccccccccc"})
        made.push_back(store.fileFor(src, s, hash));
    for (const auto& f : made) CHECK(fs::exists(f)); // a DAW may still use any of them
    const std::uintmax_t one = fs::file_size(made[0]);
    CHECK(store.bytes() == 3 * one);

    test::writeBytes(dir.path() / "renders" / "x.wav.tmp123", "half a render"); // left by a crash
    CHECK(store.bytes() == 3 * one + 13);
    CHECK(store.clear() == 4);
    CHECK(store.bytes() == 0);
    for (const auto& f : made) CHECK_FALSE(fs::exists(f));
    CHECK(fs::exists(src)); // only renders go
}

TEST_CASE("RenderStore lives in the data directory, not a cache", "[render]")
{
    TempDir dir;
    const std::string wanted = dir.path().string();
    asma::test::ScopedEnv env("ASMA_DATA_DIR", wanted.c_str());
    CHECK(RenderStore::defaultDir() == dir.path() / "renders");
}

TEST_CASE("renderToFile refuses an empty region and an absurd sample rate", "[render]")
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_render.cpp:99:5: error: unknown type name 'RenderStore'`

- [ ] **Step 3: Implement**

In `apps/asma_main.cpp`, replace:

```cpp
    "  collection add|remove <name> <file>... | --id N...\n"
    "  search list | save <name> [query options] [words...] | delete <name>\n"
    "  render <file> [--trim-start S] [--trim-end S] [--reverse | --ping-pong]\n"
    "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--cache DIR]\n"
    "                          print the file to drag, rendering edits if any\n"
    "  --version\n";

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
```

with:

```cpp
    "  collection add|remove <name> <file>... | --id N...\n"
    "  search list | save <name> [query options] [words...] | delete <name>\n"
    "  render <file> [--trim-start S] [--trim-end S] [--reverse | --ping-pong]\n"
    "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--renders DIR]\n"
    "                          print the file to drag, rendering edits if any\n"
    "  renders [clear] [--renders DIR]   size of the kept renders, or delete them\n"
    "  --version\n";

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
```

In `apps/asma_main.cpp`, replace:

```cpp
            throw UsageError("--rate wants a whole number of Hz from 1000 to 768000");
        settings.sampleRate = static_cast<int>(rate);
    }
    const auto cacheOption = args.option("cache");
    const std::filesystem::path cacheDir = cacheOption ? fromUtf8(*cacheOption) : defaultCacheDir() / "renders";
    const auto target = args.positional();
    rejectLeftovers(args);
    if (!target) throw UsageError("render needs a file");
```

with:

```cpp
            throw UsageError("--rate wants a whole number of Hz from 1000 to 768000");
        settings.sampleRate = static_cast<int>(rate);
    }
    const auto rendersOption = args.option("renders");
    const std::filesystem::path rendersDir = rendersOption ? fromUtf8(*rendersOption) : audio::RenderStore::defaultDir();
    const auto target = args.positional();
    rejectLeftovers(args);
    if (!target) throw UsageError("render needs a file");
```

In `apps/asma_main.cpp`, replace:

```cpp
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones + transpose;

    audio::RenderCache cache(cacheDir);
    std::cout << toUtf8(cache.fileFor(source, settings, hash)) << "\n";
    return kOk;
}
```

with:

```cpp
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones + transpose;

    audio::RenderStore store(rendersDir);
    std::cout << toUtf8(store.fileFor(source, settings, hash)) << "\n";
    return kOk;
}

int cmdRenders(Args& args, Db&)
{
    const auto rendersOption = args.option("renders");
    audio::RenderStore store(rendersOption ? fromUtf8(*rendersOption) : audio::RenderStore::defaultDir());
    const auto action = args.positional();
    rejectLeftovers(args);
    if (action && *action == "clear") {
        std::cout << store.clear() << " renders removed\n";
        return kOk;
    }
    if (action) throw UsageError("renders takes nothing or clear");
    std::size_t count = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(store.dir(), ec))
        if (e.path().extension() == ".wav") ++count;
    std::cout << count << " renders, " << (store.bytes() + (1u << 19)) / (1u << 20) << " MB\n";
    return kOk;
}
```

In `apps/asma_main.cpp`, replace:

```cpp
            {"collection", cmdCollection},
            {"search", cmdSearch},
            {"render", cmdRender},
        };
        if (*command == "scan") {
            Db db = Db::open(dbPath);
```

with:

```cpp
            {"collection", cmdCollection},
            {"search", cmdSearch},
            {"render", cmdRender},
            {"renders", cmdRenders},
        };
        if (*command == "scan") {
            Db db = Db::open(dbPath);
```

In `audio/include/asma/audio/Render.h`, replace:

```cpp
void renderToFile(const std::filesystem::path& source, const RenderSettings& settings,
                  const std::filesystem::path& out);

// Rendered drag-out files, <dir>/<content hash>-<settings>.wav, least
// recently used out first once they pass the capacity. Not thread-safe.
class RenderCache {
public:
    static constexpr std::uintmax_t kDefaultCapacity = 2ull << 30; // 2 GB

    explicit RenderCache(std::filesystem::path dir, std::uintmax_t capacity = kDefaultCapacity)
        : dir_(std::move(dir)), capacity_(capacity)
    {
    }

    // The file to drag: `source` itself when nothing changes the audio,
    // else a render, made on a miss. contentHash is the library's; empty
```

with:

```cpp
void renderToFile(const std::filesystem::path& source, const RenderSettings& settings,
                  const std::filesystem::path& out);

// Rendered drag-out files, <dir>/<content hash>-<settings>.wav. Nothing is
// deleted behind the user's back: a DAW may play a dragged file from where it
// lies, so a render stays until clear(). Not thread-safe.
class RenderStore {
public:
    explicit RenderStore(std::filesystem::path dir) : dir_(std::move(dir)) {}

    // <data dir>/renders: user data, so cleaner apps that empty caches leave it.
    static std::filesystem::path defaultDir();

    // The file to drag: `source` itself when nothing changes the audio,
    // else a render, made on a miss. contentHash is the library's; empty
```

In `audio/include/asma/audio/Render.h`, replace:

```cpp
    static std::string fileName(std::string_view contentHash, const RenderSettings& settings, int sampleRate);

    const std::filesystem::path& dir() const { return dir_; }

private:
    void evict(const std::filesystem::path& keep);

    std::filesystem::path dir_;
    std::uintmax_t capacity_;
};

} // namespace asma::audio
```

with:

```cpp
    static std::string fileName(std::string_view contentHash, const RenderSettings& settings, int sampleRate);

    const std::filesystem::path& dir() const { return dir_; }
    // Bytes on disk, temporary files left by an interrupted render included.
    std::uintmax_t bytes() const;
    // Deletes every render and leftover temporary file; returns how many.
    std::size_t clear();

private:
    std::filesystem::path dir_;
};

} // namespace asma::audio
```

In `audio/src/Render.cpp`, replace:

```cpp
    }
}

std::string RenderCache::fileName(std::string_view contentHash, const RenderSettings& s, int sampleRate)
{
    const auto micros = [](double seconds) { return std::to_string(std::llround(seconds * 1e6)); };
    const char direction = s.edits.direction == Direction::Forward ? 'f' : s.edits.direction == Direction::Reverse ? 'r' : 'p';
```

with:

```cpp
    }
}

fs::path RenderStore::defaultDir() { return defaultDataDir() / "renders"; }

std::string RenderStore::fileName(std::string_view contentHash, const RenderSettings& s, int sampleRate)
{
    const auto micros = [](double seconds) { return std::to_string(std::llround(seconds * 1e6)); };
    const char direction = s.edits.direction == Direction::Forward ? 'f' : s.edits.direction == Direction::Reverse ? 'r' : 'p';
```

In `audio/src/Render.cpp`, replace:

```cpp
         + "-" + std::to_string(sampleRate) + ".wav";
}

fs::path RenderCache::fileFor(const fs::path& source, const RenderSettings& settings, std::string contentHash)
{
    if (!settings.changesAudio()) return source;
    const ProbeResult probe = probeFile(source);
```

with:

```cpp
         + "-" + std::to_string(sampleRate) + ".wav";
}

fs::path RenderStore::fileFor(const fs::path& source, const RenderSettings& settings, std::string contentHash)
{
    if (!settings.changesAudio()) return source;
    const ProbeResult probe = probeFile(source);
```

In `audio/src/Render.cpp`, replace:

```cpp
    const int rate = settings.sampleRate > 0 ? settings.sampleRate : probe.sampleRate;
    const fs::path out = dir_ / fromUtf8(fileName(contentHash, settings, rate));
    std::error_code ec;
    if (fs::exists(out, ec)) {
        fs::last_write_time(out, fs::file_time_type::clock::now(), ec); // used: most recent again
        return out;
    }
    fs::create_directories(dir_);
    renderToFile(source, settings, out);
    evict(out);
    return out;
}

void RenderCache::evict(const fs::path& keep)
{
    struct Entry {
        fs::path path;
        std::uintmax_t size;
        fs::file_time_type time;
    };
    std::vector<Entry> entries;
    std::uintmax_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".wav") continue;
        const auto size = e.file_size(ec);
        if (ec) continue;
        entries.push_back({e.path(), size, e.last_write_time(ec)});
        total += size;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& x, const Entry& y) { return x.time < y.time; });
    for (const Entry& e : entries) {
        if (total <= capacity_) break;
        if (e.path == keep) continue;
        if (fs::remove(e.path, ec)) total -= e.size;
    }
}

} // namespace asma::audio
```

with:

```cpp
    const int rate = settings.sampleRate > 0 ? settings.sampleRate : probe.sampleRate;
    const fs::path out = dir_ / fromUtf8(fileName(contentHash, settings, rate));
    std::error_code ec;
    if (fs::exists(out, ec)) return out;
    fs::create_directories(dir_);
    renderToFile(source, settings, out);
    return out;
}

std::uintmax_t RenderStore::bytes() const
{
    std::uintmax_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const auto size = e.file_size(ec);
        if (!ec) total += size;
    }
    return total;
}

std::size_t RenderStore::clear()
{
    std::size_t removed = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const std::string name = toUtf8(e.path().filename());
        const bool render = e.path().extension() == ".wav" || name.find(".wav.tmp") != std::string::npos;
        if (render && e.is_regular_file(ec) && fs::remove(e.path(), ec)) ++removed;
    }
    return removed;
}

} // namespace asma::audio
```

In `core/include/asma/core/Fs.h`, replace:

```cpp
// The directory is not created.
std::filesystem::path defaultDataDir();

// Per-user cache directory, for files that can be made again. ASMA_CACHE_DIR
// wins when set; otherwise
// macOS: ~/Library/Caches/Anode Labs/asma
// Windows: %LOCALAPPDATA%\Anode Labs\asma\Cache
// Linux: $XDG_CACHE_HOME/anode-labs/asma, else ~/.cache/anode-labs/asma
// The directory is not created.
std::filesystem::path defaultCacheDir();

// Last-write time as an opaque integer in the file clock's native ticks. Only
// meaningful for equality and ordering on the same machine.
std::int64_t fileTimeToInt(std::filesystem::file_time_type time);
```

with:

```cpp
// The directory is not created.
std::filesystem::path defaultDataDir();

// Last-write time as an opaque integer in the file clock's native ticks. Only
// meaningful for equality and ordering on the same machine.
std::int64_t fileTimeToInt(std::filesystem::file_time_type time);
```

In `core/src/Fs.cpp`, replace:

```cpp
#endif
}

fs::path defaultCacheDir()
{
    if (auto dir = envPath("ASMA_CACHE_DIR")) return *dir;
#if defined(_WIN32)
    if (auto local = envPath("LOCALAPPDATA")) return *local / "Anode Labs" / "asma" / "Cache";
    return fs::temp_directory_path() / "Anode Labs" / "asma";
#elif defined(__APPLE__)
    if (auto home = envPath("HOME")) return *home / "Library" / "Caches" / "Anode Labs" / "asma";
    return fs::temp_directory_path() / "Anode Labs" / "asma";
#else
    if (auto xdg = envPath("XDG_CACHE_HOME")) return *xdg / "anode-labs" / "asma";
    if (auto home = envPath("HOME")) return *home / ".cache" / "anode-labs" / "asma";
    return fs::temp_directory_path() / "anode-labs" / "asma";
#endif
}

std::int64_t fileTimeToInt(fs::file_time_type time)
{
    return static_cast<std::int64_t>(time.time_since_epoch().count());
```

with:

```cpp
#endif
}

std::int64_t fileTimeToInt(fs::file_time_type time)
{
    return static_cast<std::int64_t>(time.time_since_epoch().count());
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_tests "[render],[fs]"`:
  `All tests passed (51 assertions in 15 test cases)`
- `./build/tests/asma_tests "asma render prints the file to drag"`:
  `All tests passed (20 assertions in 1 test case)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 302`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: keep renders in the data directory until the user clears them"
```

---

### Task 2: JUCE 9 and the plugin targets

JUCE 9.0.3 and clap-juce-extensions arrive through FetchContent. One
`juce_add_plugin` target builds the Standalone, VST3, CLAP, AU on macOS and LV2
on Linux. The processor is still a silent stereo instrument; the editor an empty
window. `asma_plugin_tests` is a JUCE console app running Catch2. CI learns
JUCE's Linux packages and runs the tests under `xvfb-run`; every library is
built position independent, which the Linux plugins (shared objects) need.

**Files:**

- Create: `plugin/CMakeLists.txt`, `plugin/src/AsmaProcessor.h`,
  `plugin/src/AsmaProcessor.cpp`, `plugin/src/AsmaEditor.h`,
  `plugin/src/AsmaEditor.cpp`, `tests/plugin/test_processor.cpp`
- Modify: `CMakeLists.txt`, `cmake/Dependencies.cmake`, `tests/CMakeLists.txt`,
  `.github/workflows/ci.yml`

**Interfaces:**

- Consumes: `asma::audio`, `AuditionEngine`, `PreviewCache` (plan 3b).
- Produces: option `ASMA_BUILD_PLUGIN` (default ON); targets `asma_ui`
  (INTERFACE), `asma_plugin` (and its `_Standalone`, `_VST3`, `_AU`, `_CLAP`,
  `_LV2` formats), `asma_plugin_tests`; `class asma::app::AsmaProcessor` with
  `audio::AuditionEngine& engine()`; `class asma::app::AsmaEditor`;
  `createPluginFilter()`.

Behaviour the tests pin:

- The processor is a stereo instrument with no input that takes MIDI and refuses
  a mono layout.
- With nothing selected it writes silence over whatever the host left in the
  buffer.
- The editor opens.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/test_processor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

using asma::app::AsmaProcessor;

TEST_CASE("the processor is a stereo instrument that takes MIDI", "[processor]")
{
    AsmaProcessor p;
    CHECK(p.getName() == "asma");
    CHECK(p.acceptsMidi());
    CHECK_FALSE(p.producesMidi());
    CHECK(p.getTotalNumInputChannels() == 0);
    CHECK(p.getTotalNumOutputChannels() == 2);
    AsmaProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK_FALSE(p.checkBusesLayoutSupported(mono));
}

TEST_CASE("the processor is silent with nothing selected", "[processor]")
{
    AsmaProcessor p;
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.applyGain(0.0f);
    buffer.setSample(0, 10, 0.5f); // anything the host left there goes
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    CHECK_FALSE(buffer.getMagnitude(0, 512) > 0.0f);
}

TEST_CASE("the editor opens", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    REQUIRE(editor);
    CHECK(editor->getWidth() > 0);
    p.editorBeingDeleted(editor.get());
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(asma_tests)
```

with:

```cmake
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(asma_tests)

if(ASMA_BUILD_PLUGIN)
  # The JUCE side: the processor and the UI, headless.
  juce_add_console_app(asma_plugin_tests PRODUCT_NAME "asma plugin tests")
  file(GLOB ASMA_PLUGIN_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.cpp)
  target_sources(asma_plugin_tests PRIVATE ${ASMA_PLUGIN_TEST_SOURCES})
  target_include_directories(asma_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_CLI_PATH="$<TARGET_FILE:asma>")
  catch_discover_tests(asma_plugin_tests)
endif()
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build -DASMA_BUILD_PLUGIN=ON`

Expected: FAIL at configure:

```
CMake Error at tests/CMakeLists.txt:30 (juce_add_console_app):
  Unknown CMake command "juce_add_console_app".
```

- [ ] **Step 3: Implement**

Create `plugin/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
# asma_ui holds the app's own JUCE code. It is an INTERFACE library so the
# plugin and the plugin tests each compile it, and JUCE, once.
add_library(asma_ui INTERFACE)
file(GLOB ASMA_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
target_sources(asma_ui INTERFACE ${ASMA_UI_SOURCES})
target_include_directories(asma_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_ui INTERFACE
  asma::audio
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
target_compile_definitions(asma_ui INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_REPORT_APP_USAGE=0)

set(ASMA_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND ASMA_FORMATS AU)
elseif(UNIX)
  list(APPEND ASMA_FORMATS LV2)
endif()

juce_add_plugin(asma_plugin
  COMPANY_NAME "Anode Labs"
  COMPANY_WEBSITE "https://anode-labs.com"
  COMPANY_EMAIL "support@anode-labs.com"
  PLUGIN_MANUFACTURER_CODE Anod
  PLUGIN_CODE Asma
  PRODUCT_NAME "asma"
  VERSION ${PROJECT_VERSION}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Instrument Sampler
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  LV2URI "https://anode-labs.com/asma"
  BUNDLE_ID com.anodelabs.asma
  FORMATS ${ASMA_FORMATS})
target_link_libraries(asma_plugin PRIVATE asma_ui)

clap_juce_extensions_plugin(TARGET asma_plugin
  CLAP_ID "com.anodelabs.asma"
  CLAP_FEATURES instrument sampler)
```

Create `plugin/src/AsmaEditor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"

#include "AsmaProcessor.h"

namespace asma::app {

AsmaEditor::AsmaEditor(AsmaProcessor& owner) : juce::AudioProcessorEditor(owner)
{
    setSize(900, 600);
}

void AsmaEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

} // namespace asma::app
```

Create `plugin/src/AsmaEditor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::app {

class AsmaProcessor;

class AsmaEditor : public juce::AudioProcessorEditor {
public:
    explicit AsmaEditor(AsmaProcessor& owner);
    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};

} // namespace asma::app
```

Create `plugin/src/AsmaProcessor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"

#include "AsmaEditor.h"

namespace asma::app {

AsmaProcessor::AsmaProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    engine_.loader().start();
}

AsmaProcessor::~AsmaProcessor() = default;

void AsmaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(static_cast<int>(sampleRate), samplesPerBlock);
}

bool AsmaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AsmaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }

void AsmaProcessor::getStateInformation(juce::MemoryBlock&) {}

void AsmaProcessor::setStateInformation(const void*, int) {}

} // namespace asma::app

// The entry point every plugin format's wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new asma::app::AsmaProcessor(); }
```

Create `plugin/src/AsmaProcessor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::app {

// The plugin and the standalone: an instrument with MIDI in and stereo out
// whose audio is the audition engine's.
class AsmaProcessor : public juce::AudioProcessor {
public:
    AsmaProcessor();
    ~AsmaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "asma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    audio::AuditionEngine& engine() { return engine_; }

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaProcessor)
};

} // namespace asma::app
```

In `.github/workflows/ci.yml`, replace:

```text
      - uses: lukka/get-cmake@latest
      - uses: ilammy/msvc-dev-cmd@v1
        if: runner.os == 'Windows'
      - name: Configure
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --test-dir build --output-on-failure
      - name: Performance (informational)
        continue-on-error: true
        shell: bash
```

with:

```text
      - uses: lukka/get-cmake@latest
      - uses: ilammy/msvc-dev-cmd@v1
        if: runner.os == 'Windows'
      - name: JUCE dependencies (Linux)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends libasound2-dev libjack-jackd2-dev \
            libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev \
            libxext-dev libxi-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev \
            mesa-common-dev xvfb
      - name: Configure
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build
      - name: Test
        shell: bash
        # The UI tests need a display on Linux.
        run: |
          if [ "$RUNNER_OS" = Linux ]; then
            xvfb-run -a ctest --test-dir build --output-on-failure
          else
            ctest --test-dir build --output-on-failure
          fi
      - name: Performance (informational)
        continue-on-error: true
        shell: bash
```

In `CMakeLists.txt`, replace:

```cmake
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASMA_BUILD_TESTS "Build the test suite" ON)

find_package(Threads REQUIRED)
```

with:

```cmake
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASMA_BUILD_TESTS "Build the test suite" ON)
option(ASMA_BUILD_PLUGIN "Build the app and plugins (needs JUCE)" ON)

# The plugins are shared libraries that take in asma's static libraries,
# which on Linux must then be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
```

In `CMakeLists.txt`, replace:

```cmake
add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(apps)

if(ASMA_BUILD_TESTS)
  enable_testing()
```

with:

```cmake
add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(apps)
if(ASMA_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()

if(ASMA_BUILD_TESTS)
  enable_testing()
```

In `cmake/Dependencies.cmake`, replace:

```cmake
if(UNIX AND NOT APPLE)
  target_link_libraries(asma_ebur128 PUBLIC m)
endif()
```

with:

```cmake
if(UNIX AND NOT APPLE)
  target_link_libraries(asma_ebur128 PUBLIC m)
endif()

if(ASMA_BUILD_PLUGIN)
  # AGPLv3, which suits asma's GPLv3.
  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG be29c81492b6151c8ea8d14c840e1311963b3a83 # 9.0.3
    GIT_SHALLOW FALSE)
  # CLAP for JUCE plugins; clones CLAP itself as submodules.
  FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(juce clap_juce_extensions)
endif()
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[processor]"`:
  `All tests passed (9 assertions in 3 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 305`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: build asma as a JUCE 9 standalone and VST3, AU, CLAP and LV2 plugin"
```

---

### Task 3: The engine on the host's MIDI and transport

`processBlock` passes the host's tempo, position and play state to the engine
and renders up to each MIDI event before handling it, advancing the position for
each piece. Note-on, note-off (including note-on at velocity 0) and
all-notes-off reach the engine, which gains `allNotesOff`.

**Files:**

- Create: `tests/plugin/PluginTestUtil.h`,
  `tests/plugin/test_processor_audio.cpp`
- Modify: `plugin/src/AsmaProcessor.h`, `plugin/src/AsmaProcessor.cpp`,
  `audio/include/asma/audio/AuditionEngine.h`, `audio/src/AuditionEngine.cpp`,
  `plugin/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `AuditionEngine::setTransport`, `process`, `noteOn`, `noteOff` (plan
  3b).
- Produces: `void AuditionEngine::allNotesOff()`; test helpers
  `asma::test::FakePlayHead` (`bpm`, `ppq`, `playing`) and
  `bool waitForPreview(AsmaProcessor&, std::uint64_t generation, int block = 512)`.

Behaviour the tests pin:

- A note sounds from the sample it arrives on, on both channels, after its 2 ms
  attack.
- Note-off, a note-on at velocity 0 and all-notes-off each release.
- The host's tempo reaches the engine, and a tempo change follows.
- A quantised start lands on the bar (240 frames into the block) even when a
  MIDI event splits the block first.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/PluginTestUtil.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "AsmaProcessor.h"
#include "TestUtil.h"

#include <chrono>
#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::test {

// A host timeline the tests control.
class FakePlayHead final : public juce::AudioPlayHead {
public:
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};

// Runs empty blocks until the engine holds `generation` (the loader runs on
// its own thread), or fails after five seconds.
inline bool waitForPreview(app::AsmaProcessor& p, std::uint64_t generation, int block = 512)
{
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        p.processBlock(buffer, midi);
        if (p.engine().status().generation == generation) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace asma::test
```

Create `tests/plugin/test_processor_audio.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginTestUtil.h"
#include "Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using asma::app::AsmaProcessor;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 512;

std::vector<float> constant(int frames, float value) { return std::vector<float>(static_cast<std::size_t>(frames), value); }

struct Rig {
    TempDir dir;
    AsmaProcessor p;
    juce::AudioBuffer<float> buffer{2, kBlock};
    juce::MidiBuffer midi;
    Rig()
    {
        p.prepareToPlay(kRate, kBlock);
        p.engine().setGainMatch(false); // exact levels
    }
    std::uint64_t select(const std::vector<float>& samples, asma::audio::SampleInfo info = {}, bool autoplay = false)
    {
        const auto path = dir.path() / ("s" + std::to_string(counter++) + ".wav");
        asma::test::writeWavFloat(path, kRate, {samples});
        const auto g = p.engine().select(path, info, autoplay);
        REQUIRE(asma::test::waitForPreview(p, g));
        return g;
    }
    void block()
    {
        p.processBlock(buffer, midi);
        midi.clear();
    }
    int counter = 0;
};

} // namespace

TEST_CASE("MIDI notes sound from the sample they arrive on", "[processor]")
{
    Rig rig;
    rig.select(constant(kRate, 0.5f));
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 300);
    rig.block();
    CHECK(rig.buffer.getSample(0, 299) == 0.0f);
    CHECK(rig.buffer.getSample(0, 300) > 0.0f);           // the attack starts here
    CHECK(rig.buffer.getSample(1, 450) == Catch::Approx(0.5f)); // past the 2 ms attack, both sides
    CHECK(rig.p.engine().status().voices == 1);
}

TEST_CASE("note-off, velocity-0 note-on and all-notes-off all release", "[processor]")
{
    Rig rig;
    rig.select(constant(kRate, 0.5f));
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
    rig.block();
    CHECK(rig.p.engine().status().voices == 3);
    rig.midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(0)), 0);
    for (int i = 0; i < 10; ++i) rig.block(); // 80 ms release
    CHECK(rig.p.engine().status().voices == 1);
    rig.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    for (int i = 0; i < 10; ++i) rig.block();
    CHECK(rig.p.engine().status().voices == 0);
}

TEST_CASE("the host's tempo reaches the engine", "[processor]")
{
    Rig rig;
    asma::test::FakePlayHead host;
    host.bpm = 90.0;
    host.playing = true;
    rig.p.setPlayHead(&host);
    asma::audio::SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 120.0;
    loop.bpmConfidence = 0.9;
    rig.select(asma::test::sine(220.0, 2.0, 0.5, kRate), loop, true);
    rig.block();
    CHECK(rig.p.engine().status().tempoSynced);
    CHECK(rig.p.engine().status().ratio == Catch::Approx(0.75));
    host.bpm = 150.0;
    rig.block();
    CHECK(rig.p.engine().status().ratio == Catch::Approx(1.25));
    rig.p.setPlayHead(nullptr);
}

TEST_CASE("a quantised start lands on the bar inside a block split by MIDI", "[processor]")
{
    Rig rig;
    asma::test::FakePlayHead host;
    host.bpm = 120.0;
    host.ppq = 3.99; // 0.01 beats before bar 2: 240 frames at 48 kHz
    host.playing = true;
    rig.p.setPlayHead(&host);
    rig.p.engine().setQuantise(4.0);
    rig.select(constant(kRate, 0.25f), {}, false);
    rig.p.engine().play();
    rig.midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 100); // splits the block at 100
    rig.block();
    int first = 0;
    while (first < kBlock && rig.buffer.getSample(0, first) == 0.0f) ++first;
    CHECK(first == 240);
    rig.p.setPlayHead(nullptr);
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
  target_include_directories(asma_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_CLI_PATH="$<TARGET_FILE:asma>")
  catch_discover_tests(asma_plugin_tests)
endif()
```

with:

```cmake
  target_include_directories(asma_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ASMA_CLI_PATH="$<TARGET_FILE:asma>")
  catch_discover_tests(asma_plugin_tests)
endif()
```

- [ ] **Step 2: Build and run to see the failure**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[processor]"`

Expected: the build succeeds and four of the `[processor]` tests fail at
`REQUIRE( asma::test::waitForPreview(p, g) )` with `false`: nothing calls the
engine yet, so no preview ever arrives.

- [ ] **Step 3: Implement**

In `audio/include/asma/audio/AuditionEngine.h`, replace:

```cpp
    void setTransport(const Transport& transport) { transport_ = transport; }
    void noteOn(int note, float velocity);
    void noteOff(int note);
    // Writes n stereo frames; more than maxBlock is fine.
    void process(float* const* out, int n);
```

with:

```cpp
    void setTransport(const Transport& transport) { transport_ = transport; }
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void allNotesOff();
    // Writes n stereo frames; more than maxBlock is fine.
    void process(float* const* out, int n);
```

In `audio/src/AuditionEngine.cpp`, replace:

```cpp

void AuditionEngine::noteOff(int note) { voices_.noteOff(note); }

void AuditionEngine::process(float* const* out, int n)
{
    // Hosts may send more than they announced: work in prepared-size chunks.
```

with:

```cpp

void AuditionEngine::noteOff(int note) { voices_.noteOff(note); }

void AuditionEngine::allNotesOff() { voices_.releaseAll(); }

void AuditionEngine::process(float* const* out, int n)
{
    // Hosts may send more than they announced: work in prepared-size chunks.
```

In `plugin/CMakeLists.txt`, replace:

```cmake
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
target_compile_definitions(asma_ui INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
```

with:

```cmake
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags)
target_compile_definitions(asma_ui INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
```

In `plugin/CMakeLists.txt`, replace:

```cmake
  LV2URI "https://anode-labs.com/asma"
  BUNDLE_ID com.anodelabs.asma
  FORMATS ${ASMA_FORMATS})
target_link_libraries(asma_plugin PRIVATE asma_ui)

clap_juce_extensions_plugin(TARGET asma_plugin
  CLAP_ID "com.anodelabs.asma"
```

with:

```cmake
  LV2URI "https://anode-labs.com/asma"
  BUNDLE_ID com.anodelabs.asma
  FORMATS ${ASMA_FORMATS})
# JUCE's warning set for the plugin build; the tests keep Catch2's exact
# float comparisons, which -Wfloat-equal would flag.
target_link_libraries(asma_plugin PRIVATE asma_ui juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET asma_plugin
  CLAP_ID "com.anodelabs.asma"
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp

void AsmaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(static_cast<int>(sampleRate), samplesPerBlock);
}
```

with:

```cpp

void AsmaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Kept rather than read back with getSampleRate(), which is 0 until a
    // wrapper has called setRateAndBufferSizeDetails.
    sampleRate_ = sampleRate;
    engine_.prepare(static_cast<int>(sampleRate), samplesPerBlock);
}
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AsmaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }
```

with:

```cpp
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AsmaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    audio::Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition()) {
            transport.bpm = position->getBpm().orFallback(0.0);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }

    // Render up to each MIDI event, so a note starts on its own sample and a
    // quantised start still sees the right position.
    int done = 0;
    const auto renderTo = [&](int end) {
        if (end <= done) return;
        audio::Transport t = transport;
        t.ppq += done * t.bpm / (60.0 * sampleRate_);
        engine_.setTransport(t);
        float* out[2] = {buffer.getWritePointer(0, done), buffer.getWritePointer(1, done)};
        engine_.process(out, end - done);
        done = end;
    };
    for (const auto event : midi) {
        renderTo(std::clamp(event.samplePosition, done, n));
        const juce::MidiMessage m = event.getMessage();
        if (m.isNoteOn()) engine_.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine_.noteOff(m.getNoteNumber()); // includes note-on at velocity 0
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine_.allNotesOff();
    }
    renderTo(n);
}

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaProcessor)
};
```

with:

```cpp
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaProcessor)
};
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[processor]"`:
  `All tests passed (24 assertions in 7 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 309`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: play the engine from the host's MIDI and transport, sample-accurately"
```

---

### Task 4: Plain major keys in saved searches

A 3a bug that plugin state would carry: `parseKeyToken` refuses a bare "C" so
that "Kick_A" is not read as A major, but the saved-search reader and
`asma query --key` used it too, so a search for C, D, E, F, G, A or B major lost
its key. `canonicalKey` accepts exactly the keys asma writes.

**Files:**

- Modify: `core/include/asma/core/NameParse.h`, `core/src/NameParse.cpp`,
  `core/src/Query.cpp`, `apps/SearchArgs.cpp`, `tests/test_name_parse.cpp`,
  `tests/test_search_model_json.cpp`, `tests/test_cli_e2e.cpp`

**Interfaces:**

- Consumes: `parseKeyToken`, `searchModelFromJson` (plans 1, 3a).
- Produces: `std::optional<std::string> asma::canonicalKey(std::string_view)`.

Behaviour the tests pin:

- `canonicalKey` takes "C", "C#", "A", "Am", "F#m", "B" and refuses "", "c",
  "Bb", "H", "Amin", "C#M", "Am ".
- A saved search keeps C and G next to Am and F#.
- `asma query --key C` is accepted.

- [ ] **Step 1: Write the failing tests**

In `tests/test_cli_e2e.cpp`, replace:

```cpp
    CHECK(cli.runAsma("frobnicate").exitCode == 2);
    CHECK(cli.runAsma("query --bogus").exitCode == 2);
    CHECK(cli.runAsma("query --key H").exitCode == 2);
    CHECK(cli.runScan("").exitCode == 2);
}
```

with:

```cpp
    CHECK(cli.runAsma("frobnicate").exitCode == 2);
    CHECK(cli.runAsma("query --bogus").exitCode == 2);
    CHECK(cli.runAsma("query --key H").exitCode == 2);
    CHECK(cli.runAsma("query --key C").exitCode == 0); // a plain major key, as asma prints it
    CHECK(cli.runScan("").exitCode == 2);
}
```

In `tests/test_name_parse.cpp`, replace:

```cpp
    CHECK_FALSE(parseKeyToken("").has_value());
}

TEST_CASE("parseName reads BPM, key and loop from a typical loop", "[name]")
{
    const NameInfo n = parseName("Loops/Bass_Loop_Am_128.wav");
```

with:

```cpp
    CHECK_FALSE(parseKeyToken("").has_value());
}

TEST_CASE("canonicalKey takes the keys asma writes and nothing else", "[name]")
{
    for (const char* key : {"C", "C#", "A", "Am", "F#m", "B"}) CHECK(canonicalKey(key) == key);
    for (const char* key : {"", "c", "Bb", "H", "Amin", "C#M", "Am "}) CHECK_FALSE(canonicalKey(key));
}

TEST_CASE("parseName reads BPM, key and loop from a typical loop", "[name]")
{
    const NameInfo n = parseName("Loops/Bass_Loop_Am_128.wav");
```

In `tests/test_search_model_json.cpp`, replace:

```cpp
    CHECK(back.offset == 0);
}

TEST_CASE("unreadable text gives nothing", "[searchjson]")
{
    CHECK_FALSE(searchModelFromJson(""));
```

with:

```cpp
    CHECK(back.offset == 0);
}

TEST_CASE("natural major keys survive a round trip", "[searchjson]")
{
    SearchModel m;
    m.keys = {"C", "Am", "G", "F#"};
    const auto back = searchModelFromJson(searchModelToJson(m));
    REQUIRE(back);
    CHECK(back->keys == m.keys);
}

TEST_CASE("unreadable text gives nothing", "[searchjson]")
{
    CHECK_FALSE(searchModelFromJson(""));
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_name_parse.cpp:45:70: error: use of undeclared identifier 'canonicalKey'`

- [ ] **Step 3: Implement**

In `apps/SearchArgs.cpp`, replace:

```cpp
        }
    }
    for (const auto& key : args.options("key")) {
        const auto canonical = parseKeyToken(key);
        if (!canonical) throw UsageError("not a key: " + key + " (try Am, C#m, Eb, F#maj)");
        m.keys.push_back(*canonical);
    }
```

with:

```cpp
        }
    }
    for (const auto& key : args.options("key")) {
        auto canonical = canonicalKey(key);
        if (!canonical) canonical = parseKeyToken(key);
        if (!canonical) throw UsageError("not a key: " + key + " (try Am, C#m, Eb, F#maj)");
        m.keys.push_back(*canonical);
    }
```

In `core/include/asma/core/NameParse.h`, replace:

```cpp
// "C#m"). nextToken lets "E Minor" be read as one key.
std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken = {});

struct NameInfo {
    std::optional<double> bpm;
    std::optional<std::string> key;
```

with:

```cpp
// "C#m"). nextToken lets "E Minor" be read as one key.
std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken = {});

// The key when `text` is exactly a canonical key ("C", "F#m"), else nothing.
// For text asma wrote itself: parseKeyToken refuses a bare "C", which in a
// file name is more often a word than a key.
std::optional<std::string> canonicalKey(std::string_view text);

struct NameInfo {
    std::optional<double> bpm;
    std::optional<std::string> key;
```

In `core/src/NameParse.cpp`, replace:

```cpp
    return out;
}

std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken)
{
    if (token.empty()) return std::nullopt;
```

with:

```cpp
    return out;
}

std::optional<std::string> canonicalKey(std::string_view text)
{
    static constexpr std::array<std::string_view, 12> kNames = {"C", "C#", "D", "D#", "E", "F",
                                                                "F#", "G", "G#", "A", "A#", "B"};
    const std::string_view root = !text.empty() && text.back() == 'm' ? text.substr(0, text.size() - 1) : text;
    for (const auto name : kNames)
        if (name == root) return std::string(text);
    return std::nullopt;
}

std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken)
{
    if (token.empty()) return std::nullopt;
```

In `core/src/Query.cpp`, replace:

```cpp
    m.bpmMin = finiteNumber(doc.get("bpm_min"));
    m.bpmMax = finiteNumber(doc.get("bpm_max"));
    // Keep only keys search can match, in canonical spelling.
    for (const auto& key : stringArray(doc.get("keys")))
        if (const auto canonical = parseKeyToken(key)) m.keys.push_back(*canonical);
    m.tags = stringArray(doc.get("tags"));
    m.durationMin = finiteNumber(doc.get("duration_min"));
    m.durationMax = finiteNumber(doc.get("duration_max"));
```

with:

```cpp
    m.bpmMin = finiteNumber(doc.get("bpm_min"));
    m.bpmMax = finiteNumber(doc.get("bpm_max"));
    // Keep only keys search can match, in canonical spelling.
    for (const auto& key : stringArray(doc.get("keys"))) {
        if (auto canonical = canonicalKey(key)) m.keys.push_back(std::move(*canonical));
        else if (auto parsed = parseKeyToken(key)) m.keys.push_back(std::move(*parsed));
    }
    m.tags = stringArray(doc.get("tags"));
    m.durationMin = finiteNumber(doc.get("duration_min"));
    m.durationMax = finiteNumber(doc.get("duration_max"));
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_tests "[name],[searchjson]"`:
  `All tests passed (103 assertions in 16 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 311`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: keep plain major keys in saved searches and --key"
```

---

### Task 5: Plugin state

What a project remembers, as one versioned JSON object: the selected sample, the
search, the sync switches, gain matching, quantise, the edits and the window
size. Restoring applies the settings to the engine and selects the saved sample
again without playing it.

**Files:**

- Create: `plugin/src/PluginState.h`, `plugin/src/PluginState.cpp`,
  `tests/plugin/test_plugin_state.cpp`
- Modify: `plugin/src/AsmaProcessor.h`, `plugin/src/AsmaProcessor.cpp`

**Interfaces:**

- Consumes: `JsonLine`, `parseJson`, `searchModelToJson`/`FromJson`,
  `keyInterval`, `Edits`, `SyncSettings` (plans 3a, 3b).
- Produces:
  `struct asma::app::PluginState { std::string selected; SearchModel search; audio::SyncSettings sync; bool gainMatch = true; double quantise = 0; audio::Edits edits; int width = 900; int height = 600; }`;
  `std::string toJson(const PluginState&)`;
  `PluginState pluginStateFromJson(std::string_view)`;
  `AsmaProcessor::pluginState()`, `setPluginState(const PluginState&)`.

Behaviour the tests pin:

- Every field survives a round trip.
- Unknown fields, wrong types and out-of-range values are skipped; anything that
  is not a JSON object gives the defaults.
- A restore reselects the saved file without playing it; a saved file that has
  gone is remembered anyway; garbage from a host leaves the defaults.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/test_plugin_state.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginState.h"
#include "PluginTestUtil.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::PluginState;
using asma::test::TempDir;

namespace {

PluginState everything()
{
    PluginState s;
    s.selected = "/Samples/Café/Loop_Am_120.wav";
    s.search.text = "dusty";
    s.search.type = SampleType::Loop;
    s.search.keys = {"Am", "C"};
    s.sync.tempo = false;
    s.sync.key = true;
    s.sync.projectKey = "F#m";
    s.sync.hostBpm = 96.5;
    s.gainMatch = false;
    s.quantise = 4.0;
    s.edits.trimStart = 0.25;
    s.edits.trimEnd = 1.5;
    s.edits.direction = audio::Direction::PingPong;
    s.edits.loop = audio::LoopMode::Off;
    s.width = 1200;
    s.height = 700;
    return s;
}

} // namespace

TEST_CASE("plugin state survives a round trip through JSON", "[state]")
{
    const PluginState a = everything();
    const PluginState b = app::pluginStateFromJson(app::toJson(a));
    CHECK(b.selected == a.selected);
    CHECK(searchModelToJson(b.search) == searchModelToJson(a.search));
    CHECK(b.sync.tempo == false);
    CHECK(b.sync.key == true);
    CHECK(b.sync.projectKey.view() == "F#m");
    CHECK(b.sync.hostBpm == 96.5);
    CHECK(b.gainMatch == false);
    CHECK(b.quantise == 4.0);
    CHECK(b.edits.trimStart == 0.25);
    CHECK(b.edits.trimEnd == 1.5);
    CHECK(b.edits.direction == audio::Direction::PingPong);
    CHECK(b.edits.loop == audio::LoopMode::Off);
    CHECK(b.width == 1200);
    CHECK(b.height == 700);
}

TEST_CASE("plugin state from another version loads what it can", "[state]")
{
    // Unknown fields, wrong types and out-of-range values are skipped.
    const PluginState s = app::pluginStateFromJson(
        R"({"v":7,"selected":42,"future":{"x":1},"gain_match":false,"quantise":-3,"direction":"sideways",)"
        R"("loop":"on","trim_start":"soon","width":5,"height":800,"project_key":"H","search":"{\"text\":\"kick\"}"})");
    CHECK(s.selected.empty());
    CHECK(s.gainMatch == false);
    CHECK(s.quantise == 0.0);
    CHECK(s.edits.direction == audio::Direction::Forward);
    CHECK(s.edits.loop == audio::LoopMode::On);
    CHECK(s.edits.trimStart == 0.0);
    CHECK(s.width == PluginState{}.width); // too small to be a window
    CHECK(s.height == 800);
    CHECK(s.sync.projectKey.empty());
    CHECK(s.search.text == "kick");

    for (const char* junk : {"", "not json", "[1,2]", "{\"search\":\"{\""}) {
        INFO(junk);
        const PluginState d = app::pluginStateFromJson(junk);
        CHECK(d.selected.empty());
        CHECK(d.gainMatch == true);
    }
}

TEST_CASE("the processor saves its state and a restore reselects the sample", "[state]")
{
    TempDir dir;
    const auto file = dir.path() / "a.wav";
    test::writeWavFloat(file, 48000, {std::vector<float>(4800, 0.25f)});

    app::AsmaProcessor a;
    PluginState s = everything();
    s.selected = toUtf8(file);
    a.setPluginState(s);
    juce::MemoryBlock saved;
    a.getStateInformation(saved);

    app::AsmaProcessor b;
    b.prepareToPlay(48000.0, 512);
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(b.pluginState().selected == toUtf8(file));
    CHECK(b.pluginState().sync.projectKey.view() == "F#m");
    CHECK(asma::test::waitForPreview(b, 1)); // selected, not played
    CHECK_FALSE(b.engine().status().playing);

    app::AsmaProcessor c; // a project that saved a file since deleted
    PluginState gone;
    gone.selected = toUtf8(dir.path() / "gone.wav");
    c.setPluginState(gone);
    c.prepareToPlay(48000.0, 512);
    CHECK(c.pluginState().selected == gone.selected); // kept: the drive may come back

    app::AsmaProcessor d; // garbage from a broken host
    d.setStateInformation("\xff\x00junk", 6);
    CHECK(d.pluginState().gainMatch == true);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/plugin/test_plugin_state.cpp:2:10: fatal error: 'PluginState.h' file not found`

- [ ] **Step 3: Implement**

Create `plugin/src/PluginState.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginState.h"

#include "asma/core/Json.h"

namespace asma::app {

namespace {

const char* directionName(audio::Direction d)
{
    switch (d) {
    case audio::Direction::Forward: return "forward";
    case audio::Direction::Reverse: return "reverse";
    case audio::Direction::PingPong: return "pingpong";
    }
    return "forward";
}

const char* loopName(audio::LoopMode m)
{
    switch (m) {
    case audio::LoopMode::Auto: return "auto";
    case audio::LoopMode::On: return "on";
    case audio::LoopMode::Off: return "off";
    }
    return "auto";
}

} // namespace

std::string toJson(const PluginState& s)
{
    return JsonLine()
        .num("v", 1)
        .str("selected", s.selected)
        .str("search", searchModelToJson(s.search))
        .boolean("tempo_sync", s.sync.tempo)
        .boolean("key_sync", s.sync.key)
        .str("project_key", s.sync.projectKey.view())
        .real("manual_bpm", s.sync.hostBpm)
        .boolean("gain_match", s.gainMatch)
        .real("quantise", s.quantise)
        .real("trim_start", s.edits.trimStart)
        .real("trim_end", s.edits.trimEnd)
        .str("direction", directionName(s.edits.direction))
        .str("loop", loopName(s.edits.loop))
        .num("width", s.width)
        .num("height", s.height)
        .build();
}

PluginState pluginStateFromJson(std::string_view json)
{
    PluginState s;
    JsonValue v;
    try {
        v = parseJson(json);
    } catch (const JsonError&) {
        return s;
    }
    if (!v.isObject()) return s;
    const auto text = [&](const char* key) -> const std::string* {
        const JsonValue* f = v.get(key);
        return f ? f->asString() : nullptr;
    };
    const auto number = [&](const char* key) -> std::optional<double> {
        const JsonValue* f = v.get(key);
        return f ? f->asNumber() : std::nullopt;
    };
    const auto flag = [&](const char* key) -> std::optional<bool> {
        const JsonValue* f = v.get(key);
        return f ? f->asBool() : std::nullopt;
    };

    if (const auto* x = text("selected")) s.selected = *x;
    if (const auto* x = text("search"))
        if (auto model = searchModelFromJson(*x)) s.search = std::move(*model);
    if (const auto x = flag("tempo_sync")) s.sync.tempo = *x;
    if (const auto x = flag("key_sync")) s.sync.key = *x;
    if (const auto* x = text("project_key"); x && audio::keyInterval(*x, "C")) s.sync.projectKey = audio::KeyName(*x);
    if (const auto x = number("manual_bpm"); x && *x >= 0.0 && *x <= 999.0) s.sync.hostBpm = *x;
    if (const auto x = flag("gain_match")) s.gainMatch = *x;
    if (const auto x = number("quantise"); x && *x >= 0.0 && *x <= 64.0) s.quantise = *x;
    if (const auto x = number("trim_start"); x && *x >= 0.0) s.edits.trimStart = *x;
    if (const auto x = number("trim_end")) s.edits.trimEnd = *x;
    if (const auto* x = text("direction")) {
        if (*x == "reverse") s.edits.direction = audio::Direction::Reverse;
        else if (*x == "pingpong") s.edits.direction = audio::Direction::PingPong;
    }
    if (const auto* x = text("loop")) {
        if (*x == "on") s.edits.loop = audio::LoopMode::On;
        else if (*x == "off") s.edits.loop = audio::LoopMode::Off;
    }
    if (const auto x = number("width"); x && *x >= 400 && *x <= 8000) s.width = static_cast<int>(*x);
    if (const auto x = number("height"); x && *x >= 300 && *x <= 8000) s.height = static_cast<int>(*x);
    return s;
}

} // namespace asma::app
```

Create `plugin/src/PluginState.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"
#include "asma/audio/Sync.h"
#include "asma/core/Query.h"

#include <string>
#include <string_view>

namespace asma::app {

// What a project remembers: the selected sample, the search (as a saved
// search would store it) and view state. Never the library itself.
struct PluginState {
    std::string selected; // UTF-8 path; empty when nothing is selected
    SearchModel search;
    audio::SyncSettings sync; // hostBpm: the standalone's manual tempo
    bool gainMatch = true;
    double quantise = 0.0; // beats; 0 is off
    audio::Edits edits;    // of the selected sample
    int width = 900;
    int height = 600;
};

std::string toJson(const PluginState& state);

// Reads what toJson wrote, possibly by another asma version: unknown fields,
// wrong types and out-of-range values are skipped, and anything that is not a
// JSON object gives the defaults.
PluginState pluginStateFromJson(std::string_view json);

} // namespace asma::app
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
#include "AsmaProcessor.h"

#include "AsmaEditor.h"

namespace asma::app {
```

with:

```cpp
#include "AsmaProcessor.h"

#include "AsmaEditor.h"
#include "asma/core/Fs.h"

#include <filesystem>

namespace asma::app {
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }

void AsmaProcessor::getStateInformation(juce::MemoryBlock&) {}

void AsmaProcessor::setStateInformation(const void*, int) {}

} // namespace asma::app
```

with:

```cpp

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }

PluginState AsmaProcessor::pluginState() const
{
    const std::lock_guard lock(stateMutex_);
    return state_;
}

void AsmaProcessor::setPluginState(const PluginState& state)
{
    {
        const std::lock_guard lock(stateMutex_);
        state_ = state;
    }
    engine_.setSync(state.sync);
    engine_.setGainMatch(state.gainMatch);
    engine_.setQuantise(state.quantise);
    engine_.setEdits(state.edits);
    if (!state.selected.empty()) {
        const auto path = fromUtf8(state.selected);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) engine_.select(path, {}, false);
    }
}

void AsmaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string json = toJson(pluginState());
    destData.replaceAll(json.data(), json.size());
}

void AsmaProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (!data || sizeInBytes <= 0) return;
    setPluginState(pluginStateFromJson({static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes)}));
}

} // namespace asma::app
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::app {
```

with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "PluginState.h"
#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>

namespace asma::app {
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp

    audio::AuditionEngine& engine() { return engine_; }

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaProcessor)
};
```

with:

```cpp

    audio::AuditionEngine& engine() { return engine_; }

    // Any thread but the audio thread. Setting applies the settings to the
    // engine and selects the saved sample again, without playing it.
    PluginState pluginState() const;
    void setPluginState(const PluginState& state);

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};
    double sampleRate_ = 44100.0;
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
    PluginState state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaProcessor)
};
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[state]"`:
  `All tests passed (38 assertions in 3 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 314`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: save the selection, search and settings with the project"
```

---

### Task 6: Read-only library access

`LibraryView` is how the UI reads the library inside a host: read-only, never
creating it, watching for other processes' commits, and saying why it cannot
read a library when it cannot.

**Files:**

- Create: `plugin/src/LibraryView.h`, `plugin/src/LibraryView.cpp`,
  `tests/plugin/test_library_view.cpp`

**Interfaces:**

- Consumes: `Db::openReadOnly`, `SchemaMismatchError`, `ChangeWatcher`,
  `search`, `Library`, `sampleInfo`, `currentSchemaVersion` (plans 1 to 3b).
- Produces:
  `enum class asma::app::LibraryState { Missing, Open, Outdated, TooNew, Unreadable }`;
  `class LibraryView(std::filesystem::path dbPath)` with
  `LibraryState refresh()`, `state()`, `std::string message() const`,
  `bool changed()`, `std::vector<SearchRow> search(const SearchModel&)`,
  `audio::SampleInfo info(std::int64_t fileId)`,
  `std::string contentHash(std::int64_t fileId)`,
  `static std::filesystem::path pathOf(const SearchRow&)`.

Behaviour the tests pin:

- A missing library is waited for, never created, and picked up once a scan
  makes it; opening counts as one change.
- Search results, sample info and content hashes come from the library.
- Another process's commit is noticed once.
- An older, a newer and a non-library file each get their own state and message;
  a library migrated later opens on the next `refresh()`.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/test_library_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryView.h"
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/Schema.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

using namespace asma;
using app::LibraryState;
using app::LibraryView;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    TempDir dir;
    fs::path dbPath = dir.path() / "data" / "library.db";
    fs::path lib = dir.path() / "Samples";
    // What the app or asma-scan would do: create the library and scan.
    std::int64_t scan()
    {
        test::WavSpec spec;
        test::writeWav(lib / "Loops" / "Bass_Loop_Am_120.wav", spec);
        spec.seed = 2;
        test::writeWav(lib / "Drums" / "Kick_01.wav", spec);
        Db db = Db::open(dbPath);
        Library library(db);
        const auto root = library.addRoot(lib);
        scanRoot(db, root);
        return library.fileByPath(root, "Loops/Bass_Loop_Am_120.wav")->id;
    }
};

// Stamps a schema version on the file without migrating anything.
void setVersion(const fs::path& path, int version)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(toUtf8(path).c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, ("PRAGMA user_version = " + std::to_string(version)).c_str(), nullptr, nullptr, nullptr)
            == SQLITE_OK);
    sqlite3_close(db);
}

} // namespace

TEST_CASE("LibraryView waits for a library that does not exist yet", "[libview]")
{
    Fixture f;
    LibraryView view(f.dbPath);
    CHECK(view.refresh() == LibraryState::Missing);
    CHECK(view.search({}).empty());
    CHECK_FALSE(view.message().empty());
    CHECK_FALSE(fs::exists(f.dbPath)); // a reader never creates it

    f.scan();
    CHECK(view.refresh() == LibraryState::Open);
    CHECK(view.changed()); // appearing counts as a change
    CHECK_FALSE(view.changed());
}

TEST_CASE("LibraryView searches and describes files", "[libview]")
{
    Fixture f;
    const auto loopId = f.scan();
    LibraryView view(f.dbPath);
    REQUIRE(view.refresh() == LibraryState::Open);
    SearchModel m;
    m.text = "bass";
    const auto rows = view.search(m);
    REQUIRE(rows.size() == 1);
    CHECK(fs::equivalent(LibraryView::pathOf(rows[0]), f.lib / "Loops" / "Bass_Loop_Am_120.wav")); // roots are stored canonical
    const audio::SampleInfo info = view.info(rows[0].id);
    CHECK(info.bpm == 120.0);
    CHECK(info.key == "Am");
    CHECK(info.isLoop == true);
    CHECK(view.contentHash(loopId).size() == 16);
    CHECK(view.contentHash(9999).empty());
    CHECK(view.search({}).size() == 2);
}

TEST_CASE("LibraryView notices what other processes write", "[libview]")
{
    Fixture f;
    const auto loopId = f.scan();
    LibraryView view(f.dbPath);
    view.refresh();
    view.changed();
    {
        Db writer = Db::open(f.dbPath); // the asma CLI a plugin runs to rate a file
        UserData(writer).setRating(loopId, 5);
    }
    CHECK(view.changed());
    CHECK_FALSE(view.changed());
    SearchModel rated;
    rated.minRating = 5;
    CHECK(view.search(rated).size() == 1);
}

TEST_CASE("LibraryView explains a library it cannot read", "[libview]")
{
    Fixture f;
    f.scan();
    setVersion(f.dbPath, 1); // left behind by an older asma
    LibraryView older(f.dbPath);
    CHECK(older.refresh() == LibraryState::Outdated);
    CHECK(older.message().find("asma scan") != std::string::npos);
    CHECK(older.search({}).empty());
    setVersion(f.dbPath, currentSchemaVersion()); // what migrating it would leave
    CHECK(older.refresh() == LibraryState::Open); // tries again each time

    setVersion(f.dbPath, 99);
    LibraryView newer(f.dbPath);
    CHECK(newer.refresh() == LibraryState::TooNew);

    TempDir other;
    const auto junk = other.path() / "library.db";
    test::writeBytes(junk, "this is not a database, not even close to one");
    LibraryView broken(junk);
    CHECK(broken.refresh() == LibraryState::Unreadable);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/plugin/test_library_view.cpp:2:10: fatal error: 'LibraryView.h' file not found`

- [ ] **Step 3: Implement**

Create `plugin/src/LibraryView.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryView.h"

#include "asma/audio/Sync.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Schema.h"

namespace asma::app {

LibraryView::LibraryView(std::filesystem::path dbPath) : path_(std::move(dbPath)) {}

LibraryView::~LibraryView() = default;

LibraryState LibraryView::refresh()
{
    if (state_ == LibraryState::Open) return state_;
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return state_ = LibraryState::Missing;
    try {
        db_.emplace(Db::openReadOnly(path_));
        watcher_ = std::make_unique<ChangeWatcher>(*db_);
        opened_ = true;
        return state_ = LibraryState::Open;
    } catch (const SchemaMismatchError& e) {
        state_ = e.found() < currentSchemaVersion() ? LibraryState::Outdated : LibraryState::TooNew;
    } catch (const DbError&) {
        state_ = LibraryState::Unreadable;
    }
    watcher_.reset();
    db_.reset();
    return state_;
}

std::string LibraryView::message() const
{
    switch (state_) {
    case LibraryState::Missing: return "No library yet. Add a sample folder in the asma app, or run asma scan.";
    case LibraryState::Open: return {};
    case LibraryState::Outdated:
        return "This library was made by an older asma. Open the asma app once, or run asma scan, to update it.";
    case LibraryState::TooNew: return "This library was made by a newer asma. Update asma to read it.";
    case LibraryState::Unreadable: return "The library file cannot be read: " + toUtf8(path_);
    }
    return {};
}

bool LibraryView::changed()
{
    if (!watcher_) return false;
    const bool fromOthers = watcher_->changed();
    const bool result = opened_ || fromOthers;
    opened_ = false;
    return result;
}

std::vector<SearchRow> LibraryView::search(const SearchModel& model)
{
    if (!db_) return {};
    return asma::search(*db_, model);
}

audio::SampleInfo LibraryView::info(std::int64_t fileId)
{
    if (!db_) return {};
    Library library(*db_);
    return audio::sampleInfo(library, fileId);
}

std::string LibraryView::contentHash(std::int64_t fileId)
{
    if (!db_) return {};
    const auto file = Library(*db_).fileById(fileId);
    return file ? file->contentHash : std::string();
}

std::filesystem::path LibraryView::pathOf(const SearchRow& row)
{
    return fromUtf8(row.rootPath) / fromUtf8(row.relPath);
}

} // namespace asma::app
```

Create `plugin/src/LibraryView.h`:

```cpp
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

    static std::filesystem::path pathOf(const SearchRow& row);

private:
    std::filesystem::path path_;
    LibraryState state_ = LibraryState::Missing;
    std::optional<Db> db_;
    std::unique_ptr<ChangeWatcher> watcher_;
    bool opened_ = false; // reported by the next changed()
};

} // namespace asma::app
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[libview]"`:
  `All tests passed (31 assertions in 4 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 318`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: read the library from the host without writing it, and notice changes"
```

---

### Task 7: A plain browser

`Browser` holds the search and its rows and re-runs it when the library changes.
`AsmaEditor` becomes a working browser: a search box, a results table, sync
switches and a status line with the "?" badge. Selecting a row (click or arrow
keys) plays it, space plays or stops, and dragging out of the window hands over
the original file or a render. A restored selection gets its tempo and key from
the library.

**Files:**

- Create: `plugin/src/Browser.h`, `plugin/src/Browser.cpp`,
  `tests/plugin/LibraryFixture.h`, `tests/plugin/test_browser.cpp`,
  `tests/plugin/test_editor.cpp`
- Modify: `plugin/src/AsmaEditor.h`, `plugin/src/AsmaEditor.cpp`,
  `plugin/src/AsmaProcessor.h`, `plugin/src/AsmaProcessor.cpp`,
  `plugin/src/LibraryView.h`, `plugin/src/LibraryView.cpp` (`infoFor`),
  `audio/include/asma/audio/Render.h` (no float `!=`), `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `LibraryView`, `PluginState`, `RenderStore`, `planSync` (Tasks 1, 5,
  6; plan 3b).
- Produces: `class asma::app::Browser(LibraryView&)` with
  `setSearch(SearchModel)`, `searchModel()`, `rows()`, `bool poll()`,
  `path(int)`, `info(int)`, `contentHash(int)`, `int rowOf(const path&)`;
  `LibraryView::infoFor(const path&)`; `AsmaProcessor::updateState(F)`,
  `libraryPath()`, `hostBpm()`, `sampleRate()`; `AsmaEditor::poll()`, `table()`,
  `searchBox()`, `statusText()`, `shouldDropFilesWhenDraggedExternally(...)`.
  Test helper `asma::test::LibraryFixture`.

Behaviour the tests pin:

- The browser searches as the model changes, picks up a library that appears and
  a rating written by another process, and finds a file's row again after a
  refresh.
- Typing filters the table and is saved with the project.
- Selecting a row plays it and saves the selection; space stops and plays.
- The status line shows the sample's tempo from its name.
- A drag hands over the file itself without edits and a render with them, never
  moving anything.
- A restored loop syncs to the host as it did.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/LibraryFixture.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// A scanned library in a temporary data directory, as the app would leave it.
#pragma once

#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <string>

namespace asma::test {

struct LibraryFixture {
    TempDir dir;
    std::string dataDir = (dir.path() / "data").string();
    ScopedEnv env{"ASMA_DATA_DIR", dataDir.c_str()};
    fs::path dbPath = dir.path() / "data" / "library.db";
    fs::path lib = dir.path() / "Samples";
    fs::path loop = lib / "Loops" / "Bass_Loop_Am_120.wav";
    fs::path kick = lib / "Drums" / "Kick_01.wav";
    fs::path snare = lib / "Drums" / "Snare_02.wav";

    void scan()
    {
        writeWavFloat(loop, 48000, {sine(110.0, 2.0, 0.4, 48000)});
        writeWavFloat(kick, 48000, {kickHit(48000)});
        writeWavFloat(snare, 48000, {hatHit(48000, 3)});
        Db db = Db::open(dbPath);
        Library library(db);
        scanRoot(db, library.addRoot(lib));
    }
};

} // namespace asma::test
```

Create `tests/plugin/test_browser.cpp`:

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
    CHECK(browser.rows().size() == 3);
    SearchModel m;
    m.text = "kick";
    browser.setSearch(m);
    REQUIRE(browser.rows().size() == 1);
    CHECK(fs::equivalent(browser.path(0), f.kick));
    CHECK(browser.searchModel().text == "kick");
    CHECK(browser.contentHash(0).size() == 16);
    CHECK(browser.path(5).empty()); // out of range
}

TEST_CASE("Browser picks up a library that appears and changes later", "[browser]")
{
    test::LibraryFixture f;
    LibraryView library(f.dbPath);
    Browser browser(library);
    CHECK_FALSE(browser.poll());
    CHECK(browser.rows().empty());
    f.scan();
    CHECK(browser.poll()); // the first scan finished
    CHECK(browser.rows().size() == 3);
    CHECK_FALSE(browser.poll());

    SearchModel rated;
    rated.minRating = 4;
    browser.setSearch(rated);
    CHECK(browser.rows().empty());
    {
        Db writer = Db::open(f.dbPath);
        const auto id = Library(writer).fileByAbsolutePath(f.kick)->id;
        UserData(writer).setRating(id, 5);
    }
    CHECK(browser.poll());
    CHECK(browser.rows().size() == 1);
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
```

Create `tests/plugin/test_editor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"
#include "LibraryFixture.h"
#include "PluginTestUtil.h"
#include "asma/audio/Render.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace asma;
namespace fs = std::filesystem;
using app::AsmaEditor;
using app::AsmaProcessor;

namespace {

struct EditorRig {
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<AsmaProcessor> p;
    std::unique_ptr<AsmaEditor> editor;
    EditorRig()
    {
        f.scan();
        p = std::make_unique<AsmaProcessor>();
        p->prepareToPlay(48000.0, 512);
        editor.reset(dynamic_cast<AsmaEditor*>(p->createEditorAndMakeActive()));
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

} // namespace

TEST_CASE("typing in the search box filters the table", "[editor]")
{
    EditorRig rig;
    CHECK(rig.editor->table().getNumRows() == 3);
    rig.type("snare");
    CHECK(rig.editor->table().getNumRows() == 1);
    CHECK(rig.p->pluginState().search.text == "snare"); // saved with the project
}

TEST_CASE("selecting a row plays it and space stops it", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    CHECK(rig.playing());
    CHECK(fs::equivalent(fromUtf8(rig.p->pluginState().selected), rig.f.kick));
    CHECK(rig.editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4; ++i) rig.p->processBlock(buffer, midi);
    CHECK_FALSE(rig.p->engine().status().playing);
    CHECK(rig.editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))); // and again plays
    CHECK(rig.playing());
}

TEST_CASE("the status line says when a sync is a guess", "[editor]")
{
    EditorRig rig;
    CHECK(rig.editor->statusText().isNotEmpty());
    rig.type("bass");
    rig.editor->table().selectRow(0);
    REQUIRE(rig.playing());
    rig.editor->poll();
    CHECK(rig.editor->statusText().contains("120")); // the loop's own tempo, from its name
}

TEST_CASE("dragging a row out hands over the original or a render", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    juce::StringArray files;
    bool canMove = true;
    const juce::DragAndDropTarget::SourceDetails details({}, rig.editor.get(), {});
    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
    CHECK_FALSE(canMove); // never move a sample out of the library
    REQUIRE(files.size() == 1);
    CHECK(fs::equivalent(fromUtf8(files[0].toStdString()), rig.f.kick)); // no edits: the file itself

    app::PluginState s = rig.p->pluginState();
    s.edits.direction = audio::Direction::Reverse;
    rig.p->setPluginState(s);
    files.clear();
    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
    const auto rendered = fromUtf8(files[0].toStdString());
    CHECK(rendered.parent_path() == audio::RenderStore::defaultDir());
    CHECK(fs::exists(rendered));
}

TEST_CASE("a restored project's loop syncs as it did", "[editor]")
{
    test::LibraryFixture f;
    f.scan();
    AsmaProcessor p;
    p.prepareToPlay(48000.0, 512);
    test::FakePlayHead host;
    host.bpm = 90.0;
    host.playing = true;
    p.setPlayHead(&host);
    app::PluginState s;
    s.selected = toUtf8(f.loop);
    p.setPluginState(s);
    REQUIRE(test::waitForPreview(p, 1));
    p.engine().play();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    CHECK(p.engine().status().tempoSynced); // 120 from the library, stretched to 90
    p.setPlayHead(nullptr);
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
  target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ASMA_CLI_PATH="$<TARGET_FILE:asma>")
  catch_discover_tests(asma_plugin_tests)
endif()
```

with:

```cmake
  target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ASMA_CLI_PATH="$<TARGET_FILE:asma>"
    JUCE_MODAL_LOOPS_PERMITTED=1) # lets a test run the message loop
  catch_discover_tests(asma_plugin_tests)
endif()
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/plugin/test_browser.cpp:2:10: fatal error: 'Browser.h' file not found`

- [ ] **Step 3: Implement**

Create `plugin/src/Browser.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Browser.h"

namespace asma::app {

void Browser::setSearch(SearchModel model)
{
    model_ = std::move(model);
    rows_ = library_.search(model_);
}

bool Browser::poll()
{
    library_.refresh();
    if (!library_.changed()) return false;
    rows_ = library_.search(model_);
    return true;
}

std::filesystem::path Browser::path(int row) const
{
    return valid(row) ? LibraryView::pathOf(rows_[static_cast<std::size_t>(row)]) : std::filesystem::path();
}

audio::SampleInfo Browser::info(int row)
{
    return valid(row) ? library_.info(rows_[static_cast<std::size_t>(row)].id) : audio::SampleInfo{};
}

std::string Browser::contentHash(int row)
{
    return valid(row) ? library_.contentHash(rows_[static_cast<std::size_t>(row)].id) : std::string();
}

int Browser::rowOf(const std::filesystem::path& file) const
{
    // Roots are stored canonical, so compare against the canonical form.
    std::error_code ec;
    const auto wanted = std::filesystem::weakly_canonical(file, ec);
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (LibraryView::pathOf(rows_[i]) == (ec ? file : wanted)) return static_cast<int>(i);
    return -1;
}

} // namespace asma::app
```

Create `plugin/src/Browser.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <filesystem>
#include <string>
#include <vector>

namespace asma::app {

// The search the UI shows and its results, re-run when the library changes.
// Message thread only.
class Browser {
public:
    explicit Browser(LibraryView& library) : library_(library) {}

    void setSearch(SearchModel model);
    const SearchModel& searchModel() const { return model_; }
    const std::vector<SearchRow>& rows() const { return rows_; }

    // Opens the library if it has appeared and re-runs the search when it
    // changed. Returns whether the rows may have changed.
    bool poll();

    // Row accessors; out of range gives empty values.
    std::filesystem::path path(int row) const;
    audio::SampleInfo info(int row);
    std::string contentHash(int row);
    // The row showing this file, or -1.
    int rowOf(const std::filesystem::path& file) const;

    LibraryView& library() { return library_; }

private:
    bool valid(int row) const { return row >= 0 && row < static_cast<int>(rows_.size()); }

    LibraryView& library_;
    SearchModel model_;
    std::vector<SearchRow> rows_;
};

} // namespace asma::app
```

In `audio/include/asma/audio/Render.h`, replace:

```cpp

    // Without these the original file is what gets dragged. A sample-rate
    // change alone is not an edit: the DAW converts on import.
    bool changesAudio() const { return edits.changesAudio() || ratio != 1.0 || semitones != 0.0; }
};

// Renders one pass of `source` with the edits baked in, as a 32-bit float WAV
```

with:

```cpp

    // Without these the original file is what gets dragged. A sample-rate
    // change alone is not an edit: the DAW converts on import.
    bool changesAudio() const
    {
        // Ordered comparisons: the plugin build warns on float == and !=.
        return edits.changesAudio() || ratio < 1.0 || ratio > 1.0 || semitones < 0.0 || semitones > 0.0;
    }
};

// Renders one pass of `source` with the edits baked in, as a 32-bit float WAV
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
#include "AsmaEditor.h"

#include "AsmaProcessor.h"

namespace asma::app {

AsmaEditor::AsmaEditor(AsmaProcessor& owner) : juce::AudioProcessorEditor(owner)
{
    setSize(900, 600);
}

void AsmaEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

} // namespace asma::app
```

with:

```cpp
#include "AsmaEditor.h"

#include "AsmaProcessor.h"
#include "asma/audio/Render.h"
#include "asma/core/Fs.h"

#include <cmath>

namespace asma::app {

namespace {

const char* const kKeys[] = {"C",  "C#",  "D",  "D#",  "E",  "F",  "F#",  "G",  "G#",  "A",  "A#",  "B",
                             "Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"};

juce::String bpmText(const std::optional<double>& bpm)
{
    if (!bpm) return {};
    return juce::String(std::round(*bpm * 100.0) / 100.0);
}

} // namespace

AsmaEditor::AsmaEditor(AsmaProcessor& owner)
    : juce::AudioProcessorEditor(owner), processor_(owner), library_(owner.libraryPath())
{
    const PluginState state = processor_.pluginState();

    search_.setTextToShowWhenEmpty("Search samples", juce::Colours::grey);
    search_.setText(state.search.text, false);
    search_.onTextChange = [this] { searchChanged(); };
    addAndMakeVisible(search_);

    tempoSync_.setToggleState(state.sync.tempo, juce::dontSendNotification);
    keySync_.setToggleState(state.sync.key, juce::dontSendNotification);
    gainMatch_.setToggleState(state.gainMatch, juce::dontSendNotification);
    for (int i = 0; i < static_cast<int>(std::size(kKeys)); ++i) projectKey_.addItem(kKeys[i], i + 1);
    projectKey_.setTextWhenNothingSelected("Project key");
    for (int i = 0; i < static_cast<int>(std::size(kKeys)); ++i)
        if (state.sync.projectKey.view() == kKeys[i]) projectKey_.setSelectedId(i + 1, juce::dontSendNotification);
    for (auto* b : {&tempoSync_, &keySync_, &gainMatch_}) {
        b->onClick = [this] { syncChanged(); };
        addAndMakeVisible(*b);
    }
    projectKey_.onChange = [this] { syncChanged(); };
    addAndMakeVisible(projectKey_);

    auto& header = table_.getHeader();
    header.addColumn("Name", kName, 320);
    header.addColumn("BPM", kBpm, 70);
    header.addColumn("Key", kKey, 60);
    header.addColumn("Type", kType, 80);
    header.addColumn("Length", kLength, 80);
    table_.setMultipleSelectionEnabled(false);
    addAndMakeVisible(table_);

    status_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status_);

    browser_.setSearch(state.search);
    setResizable(true, true);
    setResizeLimits(600, 400, 8000, 8000);
    setSize(state.width, state.height);
    setWantsKeyboardFocus(true);
    poll();
    showSelection();
    startTimerHz(5);
}

AsmaEditor::~AsmaEditor() { stopTimer(); }

void AsmaEditor::paint(juce::Graphics& g) { g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId)); }

void AsmaEditor::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(28);
    search_.setBounds(top.removeFromLeft(top.getWidth() / 2).reduced(0, 2));
    for (juce::Component* c : {static_cast<juce::Component*>(&tempoSync_), static_cast<juce::Component*>(&keySync_),
                               static_cast<juce::Component*>(&projectKey_), static_cast<juce::Component*>(&gainMatch_)})
        c->setBounds(top.removeFromLeft(top.getWidth() / 4).reduced(4, 2));
    status_.setBounds(area.removeFromBottom(24));
    table_.setBounds(area.reduced(0, 4));
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

void AsmaEditor::poll()
{
    if (browser_.poll()) {
        table_.updateContent();
        showSelection();
    }
    updateStatus();
}

void AsmaEditor::searchChanged()
{
    SearchModel model = browser_.searchModel();
    model.text = search_.getText().toStdString();
    browser_.setSearch(model);
    processor_.updateState([&](PluginState& s) { s.search = model; });
    table_.updateContent();
    showSelection();
}

void AsmaEditor::syncChanged()
{
    audio::SyncSettings sync = processor_.pluginState().sync;
    sync.tempo = tempoSync_.getToggleState();
    sync.key = keySync_.getToggleState();
    const int id = projectKey_.getSelectedId();
    sync.projectKey = id > 0 ? audio::KeyName(kKeys[id - 1]) : audio::KeyName();
    const bool gain = gainMatch_.getToggleState();
    processor_.updateState([&](PluginState& s) {
        s.sync = sync;
        s.gainMatch = gain;
    });
    processor_.engine().setSync(sync);
    processor_.engine().setGainMatch(gain);
}

void AsmaEditor::showSelection()
{
    const int row = browser_.rowOf(fromUtf8(processor_.pluginState().selected));
    const juce::ScopedValueSetter quiet(quietSelection_, true);
    if (row >= 0) table_.selectRow(row);
    else table_.deselectAllRows();
}

void AsmaEditor::updateStatus()
{
    if (library_.state() != LibraryState::Open) {
        status_.setText(library_.message(), juce::dontSendNotification);
        return;
    }
    const int row = table_.getSelectedRow();
    if (row < 0) {
        status_.setText(juce::String(browser_.rows().size()) + " samples", juce::dontSendNotification);
        return;
    }
    const SearchRow& r = browser_.rows()[static_cast<std::size_t>(row)];
    const audio::EngineStatus st = processor_.engine().status();
    juce::String text = juce::String::fromUTF8(r.name.c_str());
    if (r.bpm) text << "   " << bpmText(r.bpm) << " BPM" << (st.tempoUnsure ? " ?" : "");
    if (st.tempoSynced) text << " (synced x" << juce::String(st.ratio, 2) << ")";
    if (r.key) text << "   " << juce::String(*r.key) << (st.keyUnsure ? " ?" : "");
    if (st.keySynced && (st.semitones < 0.0 || st.semitones > 0.0)) text << " (" << (st.semitones > 0 ? "+" : "") << juce::String(st.semitones, 0) << ")";
    status_.setText(text, juce::dontSendNotification);
}

int AsmaEditor::getNumRows() { return static_cast<int>(browser_.rows().size()); }

void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int, int, bool selected)
{
    if (selected) g.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId));
}

void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const SearchRow& r = browser_.rows()[static_cast<std::size_t>(row)];
    juce::String text;
    switch (column) {
    case kName: text = juce::String::fromUTF8(r.name.c_str()); break;
    case kBpm: text = bpmText(r.bpm); break;
    case kKey: text = r.key ? juce::String(*r.key) : juce::String(); break;
    case kType: text = !r.isLoop ? "" : (*r.isLoop ? "loop" : "one-shot"); break;
    case kLength: text = juce::String(r.duration, 2) + " s"; break;
    default: break;
    }
    g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void AsmaEditor::selectedRowsChanged(int lastRowSelected)
{
    if (quietSelection_ || lastRowSelected < 0) return;
    const auto path = browser_.path(lastRowSelected);
    processor_.engine().select(path, browser_.info(lastRowSelected), true);
    processor_.updateState([&](PluginState& s) { s.selected = toUtf8(path); });
    updateStatus();
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
    const audio::SyncPlan plan = audio::planSync(browser_.info(row), sync);
    audio::RenderSettings settings;
    settings.edits = state.edits;
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones;
    settings.sampleRate = static_cast<int>(processor_.sampleRate());
    std::filesystem::path file = path;
    try {
        file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, browser_.contentHash(row));
    } catch (const std::exception&) {
        // A render that fails still leaves the original to drag.
    }
    files.add(juce::String::fromUTF8(toUtf8(file).c_str()));
    return true;
}

} // namespace asma::app
```

In `plugin/src/AsmaEditor.h`, replace:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::app {

class AsmaProcessor;

class AsmaEditor : public juce::AudioProcessorEditor {
public:
    explicit AsmaEditor(AsmaProcessor& owner);
    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};
```

with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Browser.h"
#include "LibraryView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace asma::app {

class AsmaProcessor;

// A plain browser: search, results, sync switches, a status line, and
// drag-out. Plan 3c2 replaces the looks; the behaviour stays.
class AsmaEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer,
                   private juce::TableListBoxModel,
                   private juce::Timer {
public:
    explicit AsmaEditor(AsmaProcessor& owner);
    ~AsmaEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Opens a library that appeared, refreshes the rows when it changed, and
    // updates the status line. The timer calls it; tests call it directly.
    void poll();

    // The file a drag out of the window carries: the original, or a render
    // of it when edits or sync change the audio. Never moved.
    bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& details,
                                              juce::StringArray& files, bool& canMoveFiles) override;

    juce::TableListBox& table() { return table_; }
    juce::TextEditor& searchBox() { return search_; }
    juce::String statusText() const { return status_.getText(); }

private:
    enum Column { kName = 1, kBpm, kKey, kType, kLength };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;

    void timerCallback() override { poll(); }
    void searchChanged();
    void syncChanged();
    void showSelection(); // selects the saved file's row without playing it
    void updateStatus();

    AsmaProcessor& processor_;
    LibraryView library_;
    Browser browser_{library_};
    juce::TextEditor search_;
    juce::ToggleButton tempoSync_{"Tempo sync"};
    juce::ToggleButton keySync_{"Key sync"};
    juce::ComboBox projectKey_;
    juce::ToggleButton gainMatch_{"Match loudness"};
    juce::TableListBox table_{"results", this};
    juce::Label status_;
    bool quietSelection_ = false; // selection changes that must not play

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
#include "AsmaProcessor.h"

#include "AsmaEditor.h"
#include "asma/core/Fs.h"

#include <filesystem>
```

with:

```cpp
#include "AsmaProcessor.h"

#include "AsmaEditor.h"
#include "LibraryView.h"
#include "asma/core/Fs.h"

#include <filesystem>
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
namespace asma::app {

AsmaProcessor::AsmaProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    engine_.loader().start();
}
```

with:

```cpp
namespace asma::app {

AsmaProcessor::AsmaProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      libraryPath_(defaultDataDir() / "library.db")
{
    engine_.loader().start();
}
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }

    // Render up to each MIDI event, so a note starts on its own sample and a
    // quantised start still sees the right position.
```

with:

```cpp
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }
    hostBpm_.store(transport.bpm, std::memory_order_relaxed);

    // Render up to each MIDI event, so a note starts on its own sample and a
    // quantised start still sees the right position.
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
    if (!state.selected.empty()) {
        const auto path = fromUtf8(state.selected);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) engine_.select(path, {}, false);
    }
}
```

with:

```cpp
    if (!state.selected.empty()) {
        const auto path = fromUtf8(state.selected);
        std::error_code ec;
        // Its tempo and key come from the library, so a restored loop syncs.
        if (std::filesystem::exists(path, ec)) {
            LibraryView library(libraryPath_);
            library.refresh();
            engine_.select(path, library.infoFor(path), false);
        }
    }
}
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>

namespace asma::app {
```

with:

```cpp
#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <filesystem>
#include <mutex>

namespace asma::app {
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
    // engine and selects the saved sample again, without playing it.
    PluginState pluginState() const;
    void setPluginState(const PluginState& state);

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};
    double sampleRate_ = 44100.0;
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
    PluginState state_;
```

with:

```cpp
    // engine and selects the saved sample again, without playing it.
    PluginState pluginState() const;
    void setPluginState(const PluginState& state);
    // Records what the UI changed; the UI tells the engine itself.
    template <typename Change>
    void updateState(Change&& change)
    {
        const std::lock_guard lock(stateMutex_);
        change(state_);
    }

    // <data dir>/library.db, fixed when the processor is made.
    const std::filesystem::path& libraryPath() const { return libraryPath_; }
    // The host's tempo in the last block, 0 when it gave none.
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
    double sampleRate() const { return sampleRate_; }

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};
    double sampleRate_ = 44100.0;
    std::filesystem::path libraryPath_;
    std::atomic<double> hostBpm_{0.0};
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
    PluginState state_;
```

In `plugin/src/LibraryView.cpp`, replace:

```cpp
    return file ? file->contentHash : std::string();
}

std::filesystem::path LibraryView::pathOf(const SearchRow& row)
{
    return fromUtf8(row.rootPath) / fromUtf8(row.relPath);
```

with:

```cpp
    return file ? file->contentHash : std::string();
}

audio::SampleInfo LibraryView::infoFor(const std::filesystem::path& file)
{
    if (!db_) return {};
    Library library(*db_);
    const auto record = library.fileByAbsolutePath(file);
    return record ? audio::sampleInfo(library, record->id) : audio::SampleInfo{};
}

std::filesystem::path LibraryView::pathOf(const SearchRow& row)
{
    return fromUtf8(row.rootPath) / fromUtf8(row.relPath);
```

In `plugin/src/LibraryView.h`, replace:

```cpp
    audio::SampleInfo info(std::int64_t fileId);
    // The file's content hash, or empty when unknown.
    std::string contentHash(std::int64_t fileId);

    static std::filesystem::path pathOf(const SearchRow& row);
```

with:

```cpp
    audio::SampleInfo info(std::int64_t fileId);
    // The file's content hash, or empty when unknown.
    std::string contentHash(std::int64_t fileId);
    // What the library knows about a file by its path; empty when it is not
    // in the library (or the library is not open).
    audio::SampleInfo infoFor(const std::filesystem::path& file);

    static std::filesystem::path pathOf(const SearchRow& row);
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[browser],[editor]"`:
  `All tests passed (43 assertions in 8 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 326`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: a plain browser that searches, auditions as you move, syncs and drags out"
```

---

### Task 8: The standalone's tempo

The standalone has no host. Its tempo comes from Ableton Link when Link is on
(tempo, beat position and start/stop from the session) and from a manual tempo
otherwise; both reach the audio thread through atomics. The editor shows a BPM
field and a Link switch in the standalone only.

**Files:**

- Create: `tests/plugin/test_standalone_tempo.cpp`
- Modify: `cmake/Dependencies.cmake`, `plugin/CMakeLists.txt`,
  `plugin/src/PluginState.h`, `plugin/src/PluginState.cpp` (`link`),
  `plugin/src/AsmaProcessor.h`, `plugin/src/AsmaProcessor.cpp`,
  `plugin/src/AsmaEditor.h`, `plugin/src/AsmaEditor.cpp`,
  `tests/plugin/test_editor.cpp`

**Interfaces:**

- Consumes: Task 7's editor and state.
- Produces: `AsmaProcessor::Mode { FromWrapper, Standalone }` and the
  `AsmaProcessor(Mode)` constructor; `isStandalone()`, `setManualBpm(double)`,
  `setLinkEnabled(bool)`, `setLinkTempo(double)`; `PluginState::link`;
  `AsmaEditor::linkToggle()`, `bpmBox()`.

Behaviour the tests pin:

- The standalone plays at its manual tempo, and a new one is saved.
- With Link on, the tempo follows the Link session; off, it falls back.
- A plugin keeps the settings but ignores them: its tempo is the host's.
- Only the standalone's editor shows the tempo controls, and they reach the
  state.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/test_standalone_tempo.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginTestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using asma::app::AsmaProcessor;

namespace {

void block(AsmaProcessor& p)
{
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
}

} // namespace

TEST_CASE("the standalone plays at its manual tempo", "[standalone]")
{
    AsmaProcessor p(AsmaProcessor::Mode::Standalone);
    CHECK(p.isStandalone());
    p.prepareToPlay(48000.0, 512);
    asma::app::PluginState s;
    s.sync.hostBpm = 93.0;
    p.setPluginState(s);
    block(p);
    CHECK(p.hostBpm() == 93.0);
    p.setManualBpm(128.0);
    block(p);
    CHECK(p.hostBpm() == 128.0);
    CHECK(p.pluginState().sync.hostBpm == 128.0);
}

TEST_CASE("the standalone follows Ableton Link when it is on", "[standalone]")
{
    AsmaProcessor p(AsmaProcessor::Mode::Standalone);
    p.prepareToPlay(48000.0, 512);
    p.setManualBpm(100.0);
    p.setLinkEnabled(true);
    CHECK(p.pluginState().link);
    p.setLinkTempo(137.0); // what a peer (or asma's own tempo field) would set
    block(p);
    CHECK(p.hostBpm() == Catch::Approx(137.0));
    p.setLinkEnabled(false);
    block(p);
    CHECK(p.hostBpm() == 100.0);
}

TEST_CASE("a plugin ignores the standalone's tempo settings", "[standalone]")
{
    AsmaProcessor p; // inside a host
    CHECK_FALSE(p.isStandalone());
    p.prepareToPlay(48000.0, 512);
    p.setManualBpm(128.0);
    p.setLinkEnabled(true);
    block(p);
    CHECK(p.hostBpm() == 0.0); // no play head, no host tempo
}
```

In `tests/plugin/test_editor.cpp`, replace:

```cpp
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<AsmaProcessor> p;
    std::unique_ptr<AsmaEditor> editor;
    EditorRig()
    {
        f.scan();
        p = std::make_unique<AsmaProcessor>();
        p->prepareToPlay(48000.0, 512);
        editor.reset(dynamic_cast<AsmaEditor*>(p->createEditorAndMakeActive()));
        REQUIRE(editor);
```

with:

```cpp
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<AsmaProcessor> p;
    std::unique_ptr<AsmaEditor> editor;
    explicit EditorRig(AsmaProcessor::Mode mode = AsmaProcessor::Mode::FromWrapper)
    {
        f.scan();
        p = std::make_unique<AsmaProcessor>(mode);
        p->prepareToPlay(48000.0, 512);
        editor.reset(dynamic_cast<AsmaEditor*>(p->createEditorAndMakeActive()));
        REQUIRE(editor);
```

In `tests/plugin/test_editor.cpp`, replace:

```cpp
    CHECK(p.engine().status().tempoSynced); // 120 from the library, stretched to 90
    p.setPlayHead(nullptr);
}
```

with:

```cpp
    CHECK(p.engine().status().tempoSynced); // 120 from the library, stretched to 90
    p.setPlayHead(nullptr);
}

TEST_CASE("the standalone shows its tempo controls, a plugin does not", "[editor]")
{
    EditorRig plugin;
    CHECK_FALSE(plugin.editor->linkToggle().isVisible());
    CHECK_FALSE(plugin.editor->bpmBox().isVisible());

    EditorRig standalone(AsmaProcessor::Mode::Standalone);
    REQUIRE(standalone.editor->linkToggle().isVisible());
    REQUIRE(standalone.editor->bpmBox().isVisible());
    standalone.editor->bpmBox().setValue(126.0, juce::sendNotificationSync);
    CHECK(standalone.p->pluginState().sync.hostBpm == 126.0);
    standalone.editor->linkToggle().setToggleState(true, juce::sendNotificationSync);
    CHECK(standalone.p->pluginState().link);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/plugin/test_standalone_tempo.cpp:22:36: error: no member named 'Mode' in 'asma::app::AsmaProcessor'`

- [ ] **Step 3: Implement**

In `cmake/Dependencies.cmake`, replace:

```cmake
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(juce clap_juce_extensions)
endif()
```

with:

```cmake
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
    GIT_SHALLOW FALSE)
  # Tempo sync for the standalone; GPLv2 or later.
  FetchContent_Declare(ableton_link
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG 9c9091275e707ab09d09a5a608fcdb84bf0dec85 # Link-4.1
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR _none)
  FetchContent_MakeAvailable(juce clap_juce_extensions ableton_link)
  # Link's config sets CMAKE_CXX_STANDARD to 17; a function keeps that out of
  # our scope, while the Ableton::Link target it defines stays global.
  function(asma_add_ableton_link)
    include(${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake)
  endfunction()
  asma_add_ableton_link()
endif()
```

In `plugin/CMakeLists.txt`, replace:

```cmake
target_include_directories(asma_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_ui INTERFACE
  asma::audio
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
```

with:

```cmake
target_include_directories(asma_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_ui INTERFACE
  asma::audio
  Ableton::Link
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
    projectKey_.onChange = [this] { syncChanged(); };
    addAndMakeVisible(projectKey_);

    auto& header = table_.getHeader();
    header.addColumn("Name", kName, 320);
    header.addColumn("BPM", kBpm, 70);
```

with:

```cpp
    projectKey_.onChange = [this] { syncChanged(); };
    addAndMakeVisible(projectKey_);

    if (processor_.isStandalone()) {
        bpm_.setRange(20.0, 300.0, 0.1);
        bpm_.setValue(state.sync.hostBpm > 0.0 ? state.sync.hostBpm : 120.0, juce::dontSendNotification);
        bpm_.setTextValueSuffix(" BPM");
        bpm_.onValueChange = [this] {
            processor_.setManualBpm(bpm_.getValue());
            if (link_.getToggleState()) processor_.setLinkTempo(bpm_.getValue());
        };
        link_.setToggleState(state.link, juce::dontSendNotification);
        link_.onClick = [this] { processor_.setLinkEnabled(link_.getToggleState()); };
        addAndMakeVisible(bpm_);
        addAndMakeVisible(link_);
    }

    auto& header = table_.getHeader();
    header.addColumn("Name", kName, 320);
    header.addColumn("BPM", kBpm, 70);
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
    for (juce::Component* c : {static_cast<juce::Component*>(&tempoSync_), static_cast<juce::Component*>(&keySync_),
                               static_cast<juce::Component*>(&projectKey_), static_cast<juce::Component*>(&gainMatch_)})
        c->setBounds(top.removeFromLeft(top.getWidth() / 4).reduced(4, 2));
    status_.setBounds(area.removeFromBottom(24));
    table_.setBounds(area.reduced(0, 4));
    processor_.updateState([&](PluginState& s) {
```

with:

```cpp
    for (juce::Component* c : {static_cast<juce::Component*>(&tempoSync_), static_cast<juce::Component*>(&keySync_),
                               static_cast<juce::Component*>(&projectKey_), static_cast<juce::Component*>(&gainMatch_)})
        c->setBounds(top.removeFromLeft(top.getWidth() / 4).reduced(4, 2));
    if (processor_.isStandalone()) {
        auto row = area.removeFromTop(28);
        bpm_.setBounds(row.removeFromLeft(180).reduced(0, 2));
        link_.setBounds(row.removeFromLeft(140).reduced(4, 2));
    }
    status_.setBounds(area.removeFromBottom(24));
    table_.setBounds(area.reduced(0, 4));
    processor_.updateState([&](PluginState& s) {
```

In `plugin/src/AsmaEditor.h`, replace:

```cpp
    juce::TableListBox& table() { return table_; }
    juce::TextEditor& searchBox() { return search_; }
    juce::String statusText() const { return status_.getText(); }

private:
    enum Column { kName = 1, kBpm, kKey, kType, kLength };
```

with:

```cpp
    juce::TableListBox& table() { return table_; }
    juce::TextEditor& searchBox() { return search_; }
    juce::String statusText() const { return status_.getText(); }
    // The standalone's tempo source; hidden in a plugin.
    juce::ToggleButton& linkToggle() { return link_; }
    juce::Slider& bpmBox() { return bpm_; }

private:
    enum Column { kName = 1, kBpm, kKey, kType, kLength };
```

In `plugin/src/AsmaEditor.h`, replace:

```cpp
    juce::ToggleButton keySync_{"Key sync"};
    juce::ComboBox projectKey_;
    juce::ToggleButton gainMatch_{"Match loudness"};
    juce::TableListBox table_{"results", this};
    juce::Label status_;
    bool quietSelection_ = false; // selection changes that must not play
```

with:

```cpp
    juce::ToggleButton keySync_{"Key sync"};
    juce::ComboBox projectKey_;
    juce::ToggleButton gainMatch_{"Match loudness"};
    juce::ToggleButton link_{"Ableton Link"};
    juce::Slider bpm_{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft};
    juce::TableListBox table_{"results", this};
    juce::Label status_;
    bool quietSelection_ = false; // selection changes that must not play
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
#include "LibraryView.h"
#include "asma/core/Fs.h"

#include <filesystem>

namespace asma::app {

AsmaProcessor::AsmaProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      libraryPath_(defaultDataDir() / "library.db")
{
    engine_.loader().start();
}
```

with:

```cpp
#include "LibraryView.h"
#include "asma/core/Fs.h"

#include <ableton/Link.hpp>
#include <filesystem>

namespace asma::app {

AsmaProcessor::AsmaProcessor(Mode mode)
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      libraryPath_(defaultDataDir() / "library.db"),
      standalone_(mode == Mode::Standalone || wrapperType == wrapperType_Standalone)
{
    if (standalone_) link_ = std::make_unique<ableton::Link>(120.0);
    engine_.loader().start();
}
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    audio::Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition()) {
            transport.bpm = position->getBpm().orFallback(0.0);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
```

with:

```cpp
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    audio::Transport transport;
    if (standalone_) {
        if (linkOn_.load(std::memory_order_relaxed)) {
            // Capturing the audio session state is real-time safe.
            const auto session = link_->captureAudioSessionState();
            transport.bpm = session.tempo();
            transport.ppq = session.beatAtTime(link_->clock().micros(), 4.0);
            transport.playing = session.isPlaying();
        } else {
            transport.bpm = manualBpm_.load(std::memory_order_relaxed);
        }
    } else if (auto* head = getPlayHead())
        if (const auto position = head->getPosition()) {
            transport.bpm = position->getBpm().orFallback(0.0);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
        const std::lock_guard lock(stateMutex_);
        state_ = state;
    }
    engine_.setSync(state.sync);
    engine_.setGainMatch(state.gainMatch);
    engine_.setQuantise(state.quantise);
```

with:

```cpp
        const std::lock_guard lock(stateMutex_);
        state_ = state;
    }
    manualBpm_.store(state.sync.hostBpm, std::memory_order_relaxed);
    if (link_) link_->enable(state.link);
    linkOn_.store(standalone_ && state.link, std::memory_order_relaxed);
    engine_.setSync(state.sync);
    engine_.setGainMatch(state.gainMatch);
    engine_.setQuantise(state.quantise);
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
    }
}

void AsmaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string json = toJson(pluginState());
```

with:

```cpp
    }
}

void AsmaProcessor::setManualBpm(double bpm)
{
    audio::SyncSettings sync;
    updateState([&](PluginState& s) {
        s.sync.hostBpm = bpm;
        sync = s.sync;
    });
    manualBpm_.store(bpm, std::memory_order_relaxed);
    engine_.setSync(sync); // a tempo change alone does not restart playback
}

void AsmaProcessor::setLinkEnabled(bool on)
{
    updateState([&](PluginState& s) { s.link = on; });
    if (link_) link_->enable(on);
    linkOn_.store(standalone_ && on, std::memory_order_relaxed);
}

void AsmaProcessor::setLinkTempo(double bpm)
{
    if (!link_) return;
    auto session = link_->captureAppSessionState();
    session.setTempo(bpm, link_->clock().micros());
    link_->commitAppSessionState(session);
}

void AsmaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string json = toJson(pluginState());
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <filesystem>
#include <mutex>

namespace asma::app {

// The plugin and the standalone: an instrument with MIDI in and stereo out
// whose audio is the audition engine's.
class AsmaProcessor : public juce::AudioProcessor {
public:
    AsmaProcessor();
    ~AsmaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
```

with:

```cpp
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>

namespace ableton {
class Link;
}

namespace asma::app {

// The plugin and the standalone: an instrument with MIDI in and stereo out
// whose audio is the audition engine's.
class AsmaProcessor : public juce::AudioProcessor {
public:
    // FromWrapper: whatever JUCE's wrapper says; Standalone forces the
    // standalone's behaviour, for tests.
    enum class Mode { FromWrapper, Standalone };
    explicit AsmaProcessor(Mode mode = Mode::FromWrapper);
    ~AsmaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
    double sampleRate() const { return sampleRate_; }

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
```

with:

```cpp
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
    double sampleRate() const { return sampleRate_; }

    // The standalone has no host: its tempo is Ableton Link's when Link is
    // on, else the manual tempo. A plugin keeps these but uses the host's.
    bool isStandalone() const { return standalone_; }
    void setManualBpm(double bpm);
    void setLinkEnabled(bool on);
    // Proposes a tempo to the Link session (peers may change it again).
    void setLinkTempo(double bpm);

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
    double sampleRate_ = 44100.0;
    std::filesystem::path libraryPath_;
    std::atomic<double> hostBpm_{0.0};
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
    PluginState state_;
```

with:

```cpp
    double sampleRate_ = 44100.0;
    std::filesystem::path libraryPath_;
    std::atomic<double> hostBpm_{0.0};
    const bool standalone_;
    std::unique_ptr<ableton::Link> link_; // standalone only
    std::atomic<bool> linkOn_{false};
    std::atomic<double> manualBpm_{0.0};
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
    PluginState state_;
```

In `plugin/src/PluginState.cpp`, replace:

```cpp
        .boolean("key_sync", s.sync.key)
        .str("project_key", s.sync.projectKey.view())
        .real("manual_bpm", s.sync.hostBpm)
        .boolean("gain_match", s.gainMatch)
        .real("quantise", s.quantise)
        .real("trim_start", s.edits.trimStart)
```

with:

```cpp
        .boolean("key_sync", s.sync.key)
        .str("project_key", s.sync.projectKey.view())
        .real("manual_bpm", s.sync.hostBpm)
        .boolean("link", s.link)
        .boolean("gain_match", s.gainMatch)
        .real("quantise", s.quantise)
        .real("trim_start", s.edits.trimStart)
```

In `plugin/src/PluginState.cpp`, replace:

```cpp
    if (const auto x = flag("key_sync")) s.sync.key = *x;
    if (const auto* x = text("project_key"); x && audio::keyInterval(*x, "C")) s.sync.projectKey = audio::KeyName(*x);
    if (const auto x = number("manual_bpm"); x && *x >= 0.0 && *x <= 999.0) s.sync.hostBpm = *x;
    if (const auto x = flag("gain_match")) s.gainMatch = *x;
    if (const auto x = number("quantise"); x && *x >= 0.0 && *x <= 64.0) s.quantise = *x;
    if (const auto x = number("trim_start"); x && *x >= 0.0) s.edits.trimStart = *x;
```

with:

```cpp
    if (const auto x = flag("key_sync")) s.sync.key = *x;
    if (const auto* x = text("project_key"); x && audio::keyInterval(*x, "C")) s.sync.projectKey = audio::KeyName(*x);
    if (const auto x = number("manual_bpm"); x && *x >= 0.0 && *x <= 999.0) s.sync.hostBpm = *x;
    if (const auto x = flag("link")) s.link = *x;
    if (const auto x = flag("gain_match")) s.gainMatch = *x;
    if (const auto x = number("quantise"); x && *x >= 0.0 && *x <= 64.0) s.quantise = *x;
    if (const auto x = number("trim_start"); x && *x >= 0.0) s.edits.trimStart = *x;
```

In `plugin/src/PluginState.h`, replace:

```cpp
    std::string selected; // UTF-8 path; empty when nothing is selected
    SearchModel search;
    audio::SyncSettings sync; // hostBpm: the standalone's manual tempo
    bool gainMatch = true;
    double quantise = 0.0; // beats; 0 is off
    audio::Edits edits;    // of the selected sample
```

with:

```cpp
    std::string selected; // UTF-8 path; empty when nothing is selected
    SearchModel search;
    audio::SyncSettings sync; // hostBpm: the standalone's manual tempo
    bool link = false;        // the standalone follows Ableton Link
    bool gainMatch = true;
    double quantise = 0.0; // beats; 0 is off
    audio::Edits edits;    // of the selected sample
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[standalone],[editor]"`:
  `All tests passed (42 assertions in 9 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 330`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: give the standalone a tempo, from Ableton Link or set by hand"
```

---

### Task 9: Adding a folder in the standalone

`ScanJob` adds a folder to the library (creating it on first use) and scans it
on a background thread through plan 3a's `ScanSupervisor`, which runs
`asma-scan` outside the app. The standalone owns one; a plugin has none. The
build copies `asma-scan` beside the standalone's executable, where the app looks
for it. The editor gains "Add folder..." and shows progress and the outcome.

**Files:**

- Create: `plugin/src/ScanJob.h`, `plugin/src/ScanJob.cpp`,
  `tests/plugin/test_scan_job.cpp`
- Modify: `plugin/src/AsmaProcessor.h`, `plugin/src/AsmaProcessor.cpp`,
  `plugin/src/AsmaEditor.h`, `plugin/src/AsmaEditor.cpp`,
  `plugin/CMakeLists.txt`, `tests/CMakeLists.txt`,
  `tests/plugin/test_editor.cpp`

**Interfaces:**

- Consumes: `ScanSupervisor`, `ScanRequest`, `ScanReport`, `ScanEvent`,
  `Library::addRoot` (plan 3a).
- Produces: `class asma::app::ScanJob(path dbPath, path worker)` with
  `bool addAndScan(const path& folder, std::string* error = nullptr)`,
  `bool busy() const`, `std::string progress() const`,
  `std::optional<ScanReport> takeReport()`, `cancel()`, `setWorker(path)`,
  `static path workerNextTo(const path& executable)`; `AsmaProcessor::scans()`;
  `AsmaEditor::addFolderButton()`, `addFolder(const path&)`.

Behaviour the tests pin:

- A folder is added and scanned (and analysed) into a library that did not
  exist; one scan runs at a time; the report comes once.
- A missing worker ends in a failed report, not a hang; a folder that is not
  there is refused with a reason.
- The worker is looked for beside the app.
- Only the standalone has a scan job and the button; adding a folder shows the
  outcome in the status line and the new file in the table.

- [ ] **Step 1: Write the failing tests**

Create `tests/plugin/test_scan_job.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFixture.h"
#include "LibraryView.h"
#include "PluginTestUtil.h"
#include "ScanJob.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace asma;
using app::ScanJob;
namespace fs = std::filesystem;

namespace {

// Waits for the job to finish and returns its report.
std::optional<ScanReport> finish(ScanJob& job)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (job.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return job.takeReport();
}

void writeSamples(const fs::path& folder)
{
    test::writeWavFloat(folder / "Loops" / "Bass_Loop_Am_120.wav", 48000, {test::sine(110.0, 1.0, 0.4, 48000)});
    test::writeWavFloat(folder / "Kick_01.wav", 48000, {test::kickHit(48000)});
}

} // namespace

TEST_CASE("ScanJob adds a folder and scans it into a new library", "[scanjob]")
{
    test::LibraryFixture f; // nothing scanned: no library file yet
    writeSamples(f.lib);
    ScanJob job(f.dbPath, ASMA_SCAN_PATH);
    CHECK_FALSE(job.busy());
    REQUIRE(job.addAndScan(f.lib));
    CHECK_FALSE(job.addAndScan(f.lib)); // one scan at a time
    const auto report = finish(job);
    REQUIRE(report);
    CHECK(report->result == ScanReport::Result::Finished);
    CHECK(report->index.added == 2);
    CHECK_FALSE(job.takeReport()); // reported once
    CHECK(job.progress().empty()); // nothing running

    app::LibraryView view(f.dbPath);
    REQUIRE(view.refresh() == app::LibraryState::Open);
    CHECK(view.search({}).size() == 2);
    CHECK(view.info(view.search({})[0].id).lufs); // analysed too
}

TEST_CASE("ScanJob reports a missing worker instead of hanging", "[scanjob]")
{
    test::LibraryFixture f;
    writeSamples(f.lib);
    ScanJob job(f.dbPath, f.dir.path() / "no-such-asma-scan");
    REQUIRE(job.addAndScan(f.lib));
    const auto report = finish(job);
    REQUIRE(report);
    CHECK(report->result == ScanReport::Result::Failed);
    CHECK_FALSE(report->message.empty());
}

TEST_CASE("ScanJob refuses a folder that is not there", "[scanjob]")
{
    test::LibraryFixture f;
    ScanJob job(f.dbPath, ASMA_SCAN_PATH);
    std::string why;
    CHECK_FALSE(job.addAndScan(f.dir.path() / "gone", &why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(job.busy());
}

TEST_CASE("the scanner is looked for next to the app", "[scanjob]")
{
    const fs::path app = fs::path("apps") / "asma";
#ifdef _WIN32
    CHECK(ScanJob::workerNextTo(app) == fs::path("apps") / "asma-scan.exe");
#else
    CHECK(ScanJob::workerNextTo(app) == fs::path("apps") / "asma-scan");
#endif
}

TEST_CASE("only the standalone scans", "[scanjob]")
{
    app::AsmaProcessor plugin;
    CHECK(plugin.scans() == nullptr);
    app::AsmaProcessor standalone(app::AsmaProcessor::Mode::Standalone);
    CHECK(standalone.scans() != nullptr);
}
```

In `tests/CMakeLists.txt`, replace:

```cmake
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ASMA_CLI_PATH="$<TARGET_FILE:asma>"
    JUCE_MODAL_LOOPS_PERMITTED=1) # lets a test run the message loop
  catch_discover_tests(asma_plugin_tests)
endif()
```

with:

```cmake
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ASMA_CLI_PATH="$<TARGET_FILE:asma>"
    ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>"
    JUCE_MODAL_LOOPS_PERMITTED=1) # lets a test run the message loop
  add_dependencies(asma_plugin_tests asma asma-scan)
  catch_discover_tests(asma_plugin_tests)
endif()
```

In `tests/plugin/test_editor.cpp`, replace:

```cpp
    standalone.editor->linkToggle().setToggleState(true, juce::sendNotificationSync);
    CHECK(standalone.p->pluginState().link);
}
```

with:

```cpp
    standalone.editor->linkToggle().setToggleState(true, juce::sendNotificationSync);
    CHECK(standalone.p->pluginState().link);
}

TEST_CASE("the standalone adds a folder and shows the scan in the table", "[editor]")
{
    EditorRig plugin;
    CHECK_FALSE(plugin.editor->addFolderButton().isVisible());

    EditorRig rig(AsmaProcessor::Mode::Standalone);
    REQUIRE(rig.editor->addFolderButton().isVisible());
    const auto more = rig.f.dir.path() / "More";
    test::writeWavFloat(more / "Pad_Cm.wav", 48000, {test::sine(261.6, 1.0, 0.3, 48000)});
    // The app would find asma-scan beside itself; the test points at the build's.
    rig.p->scans()->setWorker(ASMA_SCAN_PATH);
    rig.editor->addFolder(more);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (rig.p->scans()->busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    rig.editor->poll();
    CHECK(rig.editor->statusText().contains("1 added"));
    CHECK(rig.editor->table().getNumRows() == 4);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/plugin/test_scan_job.cpp:5:10: fatal error: 'ScanJob.h' file not found`

- [ ] **Step 3: Implement**

Create `plugin/src/ScanJob.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "ScanJob.h"

#include "asma/core/Db.h"
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

    if (thread_.joinable()) thread_.join(); // the previous scan, already finished
    busy_.store(true);
    {
        const std::lock_guard lock(mutex_);
        progress_ = "Scanning";
        report_.reset();
    }
    std::filesystem::path worker;
    {
        const std::lock_guard lock(mutex_);
        worker = worker_;
    }
    thread_ = std::thread([this, rootId, worker] {
        ScanRequest request;
        request.worker = worker;
        request.db = dbPath_;
        request.rootId = rootId;
        const ScanReport report = supervisor_.run(request, [this](const ScanEvent& e) {
            const char* what = e.kind == ScanEvent::Kind::Progress          ? "Scanning"
                             : e.kind == ScanEvent::Kind::AnalyseProgress ? "Analysing"
                                                                            : nullptr;
            if (!what) return;
            const std::lock_guard lock(mutex_);
            progress_ = std::string(what) + " " + std::to_string(e.done) + " of " + std::to_string(e.total);
        });
        const std::lock_guard lock(mutex_);
        report_ = report;
        progress_.clear();
        busy_.store(false);
    });
    return true;
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

Create `plugin/src/ScanJob.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/ScanSupervisor.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace asma::app {

// The standalone's library writes: adding a sample folder and scanning it
// with asma-scan, which runs outside this process, on a background thread.
// Control calls from the message thread.
class ScanJob {
public:
    ScanJob(std::filesystem::path dbPath, std::filesystem::path worker);
    ~ScanJob(); // cancels a running scan and waits for it
    ScanJob(const ScanJob&) = delete;
    ScanJob& operator=(const ScanJob&) = delete;

    // Adds the folder to the library (creating the library on first use) and
    // starts scanning it. False, with the reason in `error`, while another
    // scan runs or when the folder cannot be added.
    bool addAndScan(const std::filesystem::path& folder, std::string* error = nullptr);
    bool busy() const { return busy_.load(); }
    // A line for the status bar while a scan runs; empty otherwise.
    std::string progress() const;
    // The finished scan's report, once.
    std::optional<ScanReport> takeReport();
    void cancel() { supervisor_.cancel(); }
    // Where asma-scan is; takes effect from the next scan.
    void setWorker(std::filesystem::path worker)
    {
        const std::lock_guard lock(mutex_);
        worker_ = std::move(worker);
    }

    // Where the app ships asma-scan: beside its own executable.
    static std::filesystem::path workerNextTo(const std::filesystem::path& executable);

private:
    std::filesystem::path dbPath_;
    std::filesystem::path worker_;
    ScanSupervisor supervisor_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    mutable std::mutex mutex_; // guards progress_ and report_
    std::string progress_;
    std::optional<ScanReport> report_;
};

} // namespace asma::app
```

In `plugin/CMakeLists.txt`, replace:

```cmake
clap_juce_extensions_plugin(TARGET asma_plugin
  CLAP_ID "com.anodelabs.asma"
  CLAP_FEATURES instrument sampler)
```

with:

```cmake
clap_juce_extensions_plugin(TARGET asma_plugin
  CLAP_ID "com.anodelabs.asma"
  CLAP_FEATURES instrument sampler)

# The standalone runs asma-scan from beside its own executable.
add_dependencies(asma_plugin_Standalone asma-scan)
add_custom_command(TARGET asma_plugin_Standalone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma-scan> $<TARGET_FILE_DIR:asma_plugin_Standalone>)
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
        link_.onClick = [this] { processor_.setLinkEnabled(link_.getToggleState()); };
        addAndMakeVisible(bpm_);
        addAndMakeVisible(link_);
    }

    auto& header = table_.getHeader();
```

with:

```cpp
        link_.onClick = [this] { processor_.setLinkEnabled(link_.getToggleState()); };
        addAndMakeVisible(bpm_);
        addAndMakeVisible(link_);
        addFolder_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Add a sample folder");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this](const juce::FileChooser& chooser) {
                                      const juce::File folder = chooser.getResult();
                                      if (folder != juce::File()) addFolder(fromUtf8(folder.getFullPathName().toStdString()));
                                  });
        };
        addAndMakeVisible(addFolder_);
    }

    auto& header = table_.getHeader();
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
        auto row = area.removeFromTop(28);
        bpm_.setBounds(row.removeFromLeft(180).reduced(0, 2));
        link_.setBounds(row.removeFromLeft(140).reduced(4, 2));
    }
    status_.setBounds(area.removeFromBottom(24));
    table_.setBounds(area.reduced(0, 4));
```

with:

```cpp
        auto row = area.removeFromTop(28);
        bpm_.setBounds(row.removeFromLeft(180).reduced(0, 2));
        link_.setBounds(row.removeFromLeft(140).reduced(4, 2));
        addFolder_.setBounds(row.removeFromRight(140).reduced(0, 2));
    }
    status_.setBounds(area.removeFromBottom(24));
    table_.setBounds(area.reduced(0, 4));
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
    return false;
}

void AsmaEditor::poll()
{
    if (browser_.poll()) {
        table_.updateContent();
        showSelection();
```

with:

```cpp
    return false;
}

void AsmaEditor::addFolder(const std::filesystem::path& folder)
{
    ScanJob* scans = processor_.scans();
    if (!scans) return;
    std::string why;
    scanMessage_ = scans->addAndScan(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
    updateStatus();
}

void AsmaEditor::poll()
{
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
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp

void AsmaEditor::updateStatus()
{
    if (library_.state() != LibraryState::Open) {
        status_.setText(library_.message(), juce::dontSendNotification);
        return;
```

with:

```cpp

void AsmaEditor::updateStatus()
{
    if (ScanJob* scans = processor_.scans(); scans && scans->busy()) {
        status_.setText(scans->progress(), juce::dontSendNotification);
        return;
    }
    if (scanMessage_.isNotEmpty()) {
        status_.setText(scanMessage_, juce::dontSendNotification);
        return;
    }
    if (library_.state() != LibraryState::Open) {
        status_.setText(library_.message(), juce::dontSendNotification);
        return;
```

In `plugin/src/AsmaEditor.cpp`, replace:

```cpp
void AsmaEditor::selectedRowsChanged(int lastRowSelected)
{
    if (quietSelection_ || lastRowSelected < 0) return;
    const auto path = browser_.path(lastRowSelected);
    processor_.engine().select(path, browser_.info(lastRowSelected), true);
    processor_.updateState([&](PluginState& s) { s.selected = toUtf8(path); });
```

with:

```cpp
void AsmaEditor::selectedRowsChanged(int lastRowSelected)
{
    if (quietSelection_ || lastRowSelected < 0) return;
    scanMessage_.clear();
    const auto path = browser_.path(lastRowSelected);
    processor_.engine().select(path, browser_.info(lastRowSelected), true);
    processor_.updateState([&](PluginState& s) { s.selected = toUtf8(path); });
```

In `plugin/src/AsmaEditor.h`, replace:

```cpp
    // The standalone's tempo source; hidden in a plugin.
    juce::ToggleButton& linkToggle() { return link_; }
    juce::Slider& bpmBox() { return bpm_; }

private:
    enum Column { kName = 1, kBpm, kKey, kType, kLength };
```

with:

```cpp
    // The standalone's tempo source; hidden in a plugin.
    juce::ToggleButton& linkToggle() { return link_; }
    juce::Slider& bpmBox() { return bpm_; }
    juce::TextButton& addFolderButton() { return addFolder_; }
    // Standalone: adds a sample folder and scans it (the button's chooser
    // ends here).
    void addFolder(const std::filesystem::path& folder);

private:
    enum Column { kName = 1, kBpm, kKey, kType, kLength };
```

In `plugin/src/AsmaEditor.h`, replace:

```cpp
    juce::ToggleButton gainMatch_{"Match loudness"};
    juce::ToggleButton link_{"Ableton Link"};
    juce::Slider bpm_{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft};
    juce::TableListBox table_{"results", this};
    juce::Label status_;
    bool quietSelection_ = false; // selection changes that must not play
```

with:

```cpp
    juce::ToggleButton gainMatch_{"Match loudness"};
    juce::ToggleButton link_{"Ableton Link"};
    juce::Slider bpm_{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft};
    juce::TextButton addFolder_{"Add folder..."};
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String scanMessage_; // the last scan's outcome, until the next selection
    juce::TableListBox table_{"results", this};
    juce::Label status_;
    bool quietSelection_ = false; // selection changes that must not play
```

In `plugin/src/AsmaProcessor.cpp`, replace:

```cpp
      libraryPath_(defaultDataDir() / "library.db"),
      standalone_(mode == Mode::Standalone || wrapperType == wrapperType_Standalone)
{
    if (standalone_) link_ = std::make_unique<ableton::Link>(120.0);
    engine_.loader().start();
}
```

with:

```cpp
      libraryPath_(defaultDataDir() / "library.db"),
      standalone_(mode == Mode::Standalone || wrapperType == wrapperType_Standalone)
{
    if (standalone_) {
        link_ = std::make_unique<ableton::Link>(120.0);
        const auto app = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        scans_ = std::make_unique<ScanJob>(libraryPath_, ScanJob::workerNextTo(fromUtf8(app.getFullPathName().toStdString())));
    }
    engine_.loader().start();
}
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
#pragma once

#include "PluginState.h"
#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
```

with:

```cpp
#pragma once

#include "PluginState.h"
#include "ScanJob.h"
#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
    void setLinkEnabled(bool on);
    // Proposes a tempo to the Link session (peers may change it again).
    void setLinkTempo(double bpm);

private:
    // One preview cache for every instance in the process.
```

with:

```cpp
    void setLinkEnabled(bool on);
    // Proposes a tempo to the Link session (peers may change it again).
    void setLinkTempo(double bpm);
    // Adding folders and scanning: the standalone only; null in a plugin,
    // which never writes the library from inside the host.
    ScanJob* scans() { return scans_.get(); }

private:
    // One preview cache for every instance in the process.
```

In `plugin/src/AsmaProcessor.h`, replace:

```cpp
    std::atomic<double> hostBpm_{0.0};
    const bool standalone_;
    std::unique_ptr<ableton::Link> link_; // standalone only
    std::atomic<bool> linkOn_{false};
    std::atomic<double> manualBpm_{0.0};
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
```

with:

```cpp
    std::atomic<double> hostBpm_{0.0};
    const bool standalone_;
    std::unique_ptr<ableton::Link> link_; // standalone only
    std::unique_ptr<ScanJob> scans_;      // standalone only
    std::atomic<bool> linkOn_{false};
    std::atomic<double> manualBpm_{0.0};
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build`, then:

- `./build/tests/asma_plugin_tests_artefacts/Release/asma_plugin_tests "[scanjob],[editor]"`:
  `All tests passed (60 assertions in 12 test cases)`

Expected: no compiler warnings; the results above. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 336`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "app: let the standalone add a sample folder and scan it"
```

---

### Task 10: Plugin validation in CI

`ci/validate-plugins.sh` downloads pinned releases of pluginval and
clap-validator and runs them on the built plugins. CI runs it on every platform
after the tests.

**Files:**

- Create: `ci/validate-plugins.sh`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add the script and the CI step**

Create `ci/validate-plugins.sh`:

```bash
#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Validates the built plugins: pluginval (VST3, and AU on macOS) and
# clap-validator (CLAP). Downloads pinned releases of both into $TOOLS.
# Usage: ci/validate-plugins.sh [build dir]
set -euo pipefail

BUILD=${1:-build}
ART="$BUILD/plugin/asma_plugin_artefacts/Release"
TOOLS=${TOOLS:-"$BUILD/validators"}
PLUGINVAL_VERSION=v1.0.4
CLAP_VALIDATOR_VERSION=0.4.1
CLAP_VALIDATOR_BUILD=0.4.1-127-g152b982

case "$(uname -s)" in
  Darwin) os=macOS; clap_os=macos-universal ;;
  Linux) os=Linux; clap_os=ubuntu-22.04 ;;
  *) os=Windows; clap_os=windows ;;
esac

mkdir -p "$TOOLS"
fetch() { # url, directory to unpack into
  mkdir -p "$2"
  curl -fsSL "$1" -o "$2/download.zip"
  (cd "$2" && unzip -oq download.zip && for t in *.tar.gz; do [ -e "$t" ] && tar xzf "$t"; done; true)
}
[ -d "$TOOLS/pluginval" ] || fetch \
  "https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_$os.zip" "$TOOLS/pluginval"
[ -d "$TOOLS/clap-validator" ] || fetch \
  "https://github.com/free-audio/clap-validator/releases/download/$CLAP_VALIDATOR_VERSION/clap-validator-$CLAP_VALIDATOR_BUILD-$clap_os.zip" \
  "$TOOLS/clap-validator"

pluginval=$(find "$TOOLS/pluginval" -type f \( -name pluginval -o -name pluginval.exe \) | head -1)
clapval=$(find "$TOOLS/clap-validator" -type f \( -name clap-validator -o -name clap-validator.exe \) | head -1)
chmod +x "$pluginval" "$clapval"

run_pluginval() {
  echo "== pluginval $1"
  "$pluginval" --strictness-level 10 --validate-in-process --validate "$1"
}

run_pluginval "$ART/VST3/asma.vst3"
if [ "$os" = macOS ] && [ -n "${CI:-}" ]; then
  # macOS only finds an AU that is installed; the CI runner is throwaway.
  mkdir -p ~/Library/Audio/Plug-Ins/Components
  cp -R "$ART/AU/asma.component" ~/Library/Audio/Plug-Ins/Components/
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  run_pluginval ~/Library/Audio/Plug-Ins/Components/asma.component
fi

echo "== clap-validator"
# param-conversions divides by the parameter count, and asma has none: the
# validator crashes on itself (it says so). Every other test runs.
"$clapval" validate --exclude '^param-conversions$' "$ART/CLAP/asma.clap"
```

In `.github/workflows/ci.yml`, replace:

```text
          else
            ctest --test-dir build --output-on-failure
          fi
      - name: Performance (informational)
        continue-on-error: true
        shell: bash
```

with:

```text
          else
            ctest --test-dir build --output-on-failure
          fi
      - name: Validate plugins
        shell: bash
        run: |
          if [ "$RUNNER_OS" = Linux ]; then
            xvfb-run -a ci/validate-plugins.sh build
          else
            ci/validate-plugins.sh build
          fi
      - name: Performance (informational)
        continue-on-error: true
        shell: bash
```

Make the script executable: `chmod +x ci/validate-plugins.sh`.

- [ ] **Step 2: Run it**

Run: `TOOLS=build/validators ci/validate-plugins.sh build`

Expected (macOS, outside CI, so the AU is not installed and not validated):
`== pluginval .../VST3/asma.vst3`, then `SUCCESS`; `== clap-validator`, then
`43 tests run, 30 passed, 0 failed, 1 warnings, 12 skipped`.

- [ ] **Step 3: Commit**

```sh
git add -A
git commit -m "ci: validate the VST3, AU and CLAP plugins with pluginval and clap-validator"
```

---

### Task 11: Docs

The spec moves to JUCE 9, describes the app and plugin targets, the standalone's
tempo, plugin state, kept renders and plugin validation, and says what 3c1 and
3c2 each deliver. The README covers building the app and plugins and
`asma renders`.

**Files:**

- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`, `README.md`

- [ ] **Step 1: Amend the spec and the README**

In `README.md`, replace:

```text
    cmake --build build
    ctest --test-dir build --output-on-failure

## Command line

    asma root add ~/Samples
```

with:

```text
    cmake --build build
    ctest --test-dir build --output-on-failure

This builds the command line tools and the app: a standalone and VST3, CLAP,
AU (macOS) and LV2 (Linux) plugins, in `build/plugin/asma_plugin_artefacts`.
`-DASMA_BUILD_PLUGIN=OFF` skips JUCE and builds only the core and the command
line. On Linux JUCE needs the ALSA, JACK, FreeType, fontconfig, X11 and GL
development packages (see `.github/workflows/ci.yml`).

## Command line

    asma root add ~/Samples
```

In `README.md`, replace:

```text
    asma render ~/Samples/Loops/Funk_96.wav --tempo 120   # stretched, same pitch
    asma render ~/Samples/Keys/Rhodes_Am.wav --key C#m --reverse
    asma render ~/Samples/Drums/Kick_01.wav               # no edits: the file itself

Sync only happens when asma is sure of the sample's tempo or key; otherwise it
says so and leaves the sample alone. Renders live in the platform cache
directory (on macOS `~/Library/Caches/Anode Labs/asma/renders`), capped at 2 GB;
`--cache DIR` or the `ASMA_CACHE_DIR` environment variable override it.

## License
```

with:

```text
    asma render ~/Samples/Loops/Funk_96.wav --tempo 120   # stretched, same pitch
    asma render ~/Samples/Keys/Rhodes_Am.wav --key C#m --reverse
    asma render ~/Samples/Drums/Kick_01.wav               # no edits: the file itself
    asma renders                                          # how many, how big
    asma renders clear

Sync only happens when asma is sure of the sample's tempo or key; otherwise it
says so and leaves the sample alone. Renders stay in `renders` in the data
directory until you clear them: a DAW may play a dragged file from where it
lies. `--renders DIR` overrides the folder.

## License
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text

## 2. Constraints

- **License:** GPLv3 for asma's own code. JUCE 8 is used under its AGPLv3
  option, which is compatible.
- **Repo:** `anode-audio/asma` on GitHub, standalone. It does **not** depend on
  `anode-common` (which is proprietary). The Anode look is reproduced by copying
```

with:

```text

## 2. Constraints

- **License:** GPLv3 for asma's own code. JUCE 9 is used under its AGPLv3
  option, which is compatible.
- **Repo:** `anode-audio/asma` on GitHub, standalone. It does **not** depend on
  `anode-common` (which is proprietary). The Anode look is reproduced by copying
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
- **Audition:** click to play, auto-play while navigating, tempo sync for loops,
  transpose to key, MIDI playback, start/end trim, reverse and ping-pong,
  waveform display.
- **Drag out** to DAW or file manager, rendering edits to a temp WAV when any
  edit is active.
- **File manager (standalone only):** rename (including batch rename by
  pattern), move, trash, convert format and sample rate, find duplicates by
  hash, export a collection to a folder. Every operation is journaled and
```

with:

```text
- **Audition:** click to play, auto-play while navigating, tempo sync for loops,
  transpose to key, MIDI playback, start/end trim, reverse and ping-pong,
  waveform display.
- **Drag out** to DAW or file manager, rendering edits to a WAV when any edit
  is active.
- **File manager (standalone only):** rename (including batch rename by
  pattern), move, trash, convert format and sample rate, find duplicates by
  hash, export a collection to a folder. Every operation is journaled and
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
   and the drag-out renders. Built on `asma-core`'s decoders and Signalsmith
   Stretch, so it runs and tests headless; the plugin's audio callback calls it
   directly.
5. **`asma-ui`** (JUCE component library): browser table, sidebar, filter bar,
   waveform, theme. Shared by both shells.
6. **Standalone and plugin shells**: thin wrappers around `asma-ui`. Only the
   standalone enables file operations and spawns the scanner. If the plugin is
   the only asma instance running, it can request a scan by launching
   `asma-scan`, which still runs outside the host process.

Each unit is testable without the ones above it: `analysis` with synthetic
buffers, `index` and `fileops` with temp directories, `query` against an
```

with:

```text
   and the drag-out renders. Built on `asma-core`'s decoders and Signalsmith
   Stretch, so it runs and tests headless; the plugin's audio callback calls it
   directly.
5. **`asma-ui`** (`plugin/`, JUCE): the processor, the editor and the parts
   they share (library view, browser, plugin state, scan job). An INTERFACE
   library, so the plugin and its headless tests each compile it.
6. **Standalone and plugin shells**: one JUCE plugin target builds VST3, AU
   (macOS), CLAP (through clap-juce-extensions), LV2 (Linux) and the
   Standalone; the processor tells the standalone from a plugin at run time.
   Only the standalone adds folders and spawns the scanner, which it ships
   beside its own executable. If the plugin is the only asma instance running,
   it can request a scan by launching `asma-scan`, which still runs outside the
   host process.

Each unit is testable without the ones above it: `analysis` with synthetic
buffers, `index` and `fileops` with temp directories, `query` against an
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
  ms); a new note beyond eight takes a releasing voice first, then the oldest.
- **Plugin output:** the preview is rendered into the plugin's audio output, so
  it's heard through the channel's inserts. By default the plugin stays silent
  while the host transport is stopped unless the user is auditioning.

## 9. UI

Native JUCE, dark theme by default, palette values copied from the Anode
identity.

- **Left sidebar:** roots, collections, saved searches, favourites.
- **Top bar:** search field, facet chips (type, BPM range, key, instrument,
```

with:

```text
  ms); a new note beyond eight takes a releasing voice first, then the oldest.
- **Plugin output:** the preview is rendered into the plugin's audio output, so
  it's heard through the channel's inserts. By default the plugin stays silent
  while the host transport is stopped unless the user is auditioning. The host's
  tempo, position and play state reach the engine every block; blocks are split
  at MIDI events so notes and quantised starts land on their own sample.
- **Standalone tempo:** Ableton Link when it is on (tempo, beat and start/stop
  from the session), otherwise the manual tempo.

## 9. UI

Native JUCE, dark theme by default, palette values copied from the Anode
identity. Plan 3c1 ships a plain browser with the behaviour below that already
works (search, audition as the selection moves, sync switches, a status line
with the "?" badge, drag-out, and in the standalone the tempo source and "Add
folder"); plan 3c2 builds the layout and looks described here.

- **Left sidebar:** roots, collections, saved searches, favourites.
- **Top bar:** search field, facet chips (type, BPM range, key, instrument,
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
- No edits active: drag the original file path. A sample-rate difference alone
  is not an edit; the DAW converts on import.
- Edits active (trim, reverse, stretch, pitch): render one pass to
  `<cache>/renders/<hash>-<params>.wav` (32-bit float, the file's channels) at
  the host sample rate if known, then drag that file. The length is exact, the
  trimmed pass divided by the tempo ratio, so a synced loop lands on the grid.
  The render cache is capped (default 2 GB, LRU eviction; the file being dragged
  is never evicted). `<cache>` is `~/Library/Caches/Anode Labs/asma`,
  `%LOCALAPPDATA%\Anode Labs\asma\Cache` or `$XDG_CACHE_HOME/anode-labs/asma`.

### Plugin state

Instrument plugin with MIDI input and audio output. Any number of instances
share the one database. The project saves only the selected sample, the search
model (as the same JSON a saved search uses) and view state, never the library
itself.

## 10. Error handling
```

with:

```text
- No edits active: drag the original file path. A sample-rate difference alone
  is not an edit; the DAW converts on import.
- Edits active (trim, reverse, stretch, pitch): render one pass to
  `<data dir>/renders/<hash>-<params>.wav` (32-bit float, the file's channels)
  at the host sample rate if known, then drag that file. The length is exact,
  the trimmed pass divided by the tempo ratio, so a synced loop lands on the
  grid.
- Renders are kept until the user clears them (`asma renders clear`, and a
  "Clear renders" action in the UI), never evicted automatically: DAWs that play
  a dragged file where it lies (Reaper, and Live unless the set is collected)
  would lose audio. They live in the data directory rather than a cache
  directory so that cleaner apps leave them alone. (macOS itself does not purge
  `~/Library/Caches`; only apps and cleaners do.)

### Plugin state

Instrument plugin with MIDI input and audio output. Any number of instances
share the one database and one preview cache. The project saves only the
selected sample, the search model (as the same JSON a saved search uses) and
view state (sync switches, project key, gain matching, quantise, edits, the
standalone's tempo source, window size), never the library itself. State is one
versioned JSON object; unknown fields and bad values are skipped, so a project
from another asma version loads what it can. A restored selection is loaded
without playing, with its tempo and key from the library.

## 10. Error handling
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text

| Dependency                        | Use                              | License               |
| --------------------------------- | -------------------------------- | --------------------- |
| JUCE 8                            | UI, audio, plugin formats        | AGPLv3                |
| clap-juce-extensions              | CLAP target                      | MIT                   |
| SQLite (with FTS5)                | database                         | public domain         |
| xxHash                            | content hashing                  | BSD-2                 |
```

with:

```text

| Dependency                        | Use                              | License               |
| --------------------------------- | -------------------------------- | --------------------- |
| JUCE 9                            | UI, audio, plugin formats        | AGPLv3                |
| clap-juce-extensions              | CLAP target                      | MIT                   |
| SQLite (with FTS5)                | database                         | public domain         |
| xxHash                            | content hashing                  | BSD-2                 |
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
  ACID chunks; nothing from that library is copied or uploaded.
- **CLI end to end:** `asma scan` and `asma query` over a fixture tree,
  asserting results.
- **Plugin validation:** pluginval (VST3, AU) and clap-validator (CLAP).
- **Performance:** scan and query timings over a synthetic 50k-file library,
  reported in CI (informational, not gating).
- **CI:** GitHub Actions on macOS, Windows and Linux for every push and PR.
```

with:

```text
  ACID chunks; nothing from that library is copied or uploaded.
- **CLI end to end:** `asma scan` and `asma query` over a fixture tree,
  asserting results.
- **Plugin tests:** the processor and editor run headless in a JUCE console app
  (MIDI timing, host transport, state, the browser, drag-out, scanning).
- **Plugin validation:** pluginval at strictness 10 (VST3 everywhere, AU on
  macOS) and clap-validator (CLAP) in CI on all three platforms. clap-validator's
  `param-conversions` test is skipped: it divides by the parameter count, and
  asma has no parameters.
- **Performance:** scan and query timings over a synthetic 50k-file library,
  reported in CI (informational, not gating).
- **CI:** GitHub Actions on macOS, Windows and Linux for every push and PR.
```

- [ ] **Step 2: Format and check**

Run: `prettier -w README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: no change beyond the blocks above.

- [ ] **Step 3: Commit**

```sh
git add -A
git commit -m "docs: JUCE 9, the app and plugins, kept renders, and 3c's split"
```

---

### Task 12: Verify and merge

- [ ] **Step 1: Everything, locally**

```sh
ctest --test-dir build --output-on-failure
./build/tests/asma_tests "[.perf]"
TOOLS=build/validators ci/validate-plugins.sh build
```

Expected: `100% tests passed out of 336`; `search took` under 50 ms; pluginval
`SUCCESS` and clap-validator `0 failed`.

- [ ] **Step 2: Try the app by hand (macOS)**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
build/plugin/asma_plugin_artefacts/Release/Standalone/asma.app/Contents/MacOS/asma
```

(`open` does not hand the app `ASMA_DATA_DIR`; run the binary.) Use a folder
that holds at least one loop with a known tempo, such as
`drums_loop_120_bpm.wav`: one-shots and full-length stems never tempo sync.

Expected: "No library yet" in the status line; "Add folder..." on a sample
folder shows `Scanning n of m`, then `Scan finished: n added`, and the files in
the table; arrowing through the table plays each sample; with the manual tempo
changed, the loop follows it exactly (`synced x1.50` at 180 for a 120 loop);
dragging that synced loop onto the desktop leaves a render from
`$ASMA_DATA_DIR/renders`. 3c1 has no edit controls, so reverse and trim renders
are covered by the tests and `asma render`, not by hand.

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-3c1-app
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green, including "Validate plugins". The
prototype passed this exact CI; a difference means the branch drifted from the
plan.

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-3c1-app
git -C product/asma worktree remove .worktrees/plan-3c1
git -C product/asma branch -d plan-3c1-app
```

Push `main` once the user agrees, and delete the remote branch.
