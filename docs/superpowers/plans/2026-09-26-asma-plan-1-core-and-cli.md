# asma Plan 1: Core Library and CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** A headless `asma-core` library plus the `asma` CLI and the `asma-scan`
worker that index sample folders into SQLite and search them, on macOS, Windows
and Linux, with CI on all three.

**Architecture:** `asma-core` is a static C++20 library with no JUCE or GUI
dependency. It owns the SQLite schema, header probing for WAV/AIFF/FLAC/MP3/Ogg,
content hashing, filename metadata parsing, the incremental scanner, the
single-writer lock and search. Two thin executables sit on top: `asma` (user
CLI) and `asma-scan` (the out-of-process scanner the UI will supervise in Plan
3, speaking JSON lines on stdout).

**Tech Stack:** C++20, CMake 3.25+, Ninja, SQLite 3.53.4 amalgamation (FTS5),
xxHash 0.8.4, dr_libs (dr_flac, dr_mp3), stb_vorbis, Catch2 3.16.0, GitHub
Actions.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md`

## Roadmap (this plan is 1 of 5)

The spec covers five independently shippable subsystems. Each gets its own plan,
written when the previous one lands:

1. **Core and CLI** (this plan): schema, scanning, probing, hashing, filename
   metadata, search, writer lock, `asma` and `asma-scan`.
2. **Analysis**: full decoding, BPM, key, loop/one-shot, LUFS, descriptors,
   feature vectors, `asma similar`, lazy re-analysis on `analysis_version`,
   accuracy corpus gating CI, scan/query timings reported in CI.
3. **UI, audition and plugin**: JUCE 8, `asma-ui`, audition engine (Signalsmith
   Stretch), tempo/key sync, MIDI, drag-out render cache, scan supervisor with
   crash recovery, `PRAGMA data_version` refresh, ratings/favourites/
   collections/saved searches, Ableton Link, standalone plus VST3/AU/CLAP/LV2,
   pluginval and clap-validator.
4. **File manager**: journal, undo, crash recovery, rename/batch rename, move,
   OS trash, convert, dedupe, export collection, nightly JSON sidecar, database
   corruption recovery.
5. **Packaging**: macOS universal build with Developer ID signing and
   notarisation (`.pkg`), unsigned Windows Inno Setup installer, Linux `.tar.gz`
   and AppImage, dispatch-only release workflow, website listing.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (CMake files: `# SPDX-...`).
- Language: C++20, `CMAKE_CXX_EXTENSIONS OFF`. Do not use `std::format` (GCC 11
  on Ubuntu 22.04 lacks it) or floating-point `std::to_chars` (unavailable on
  macOS 12). Use `snprintf` for number formatting.
- Platforms: macOS 12+ (`CMAKE_OSX_DEPLOYMENT_TARGET 12.0`), Windows x64 with
  MSVC, Linux x64 on Ubuntu 22.04 (GCC 11).
- `asma-core` never links JUCE or any GUI library.
- Paths are stored in the database as UTF-8 with `/` separators, produced only
  by `asma::toUtf8` and read back only via `asma::fromUtf8`.
- Nothing in this plan writes to, moves or deletes sample files. The scanner is
  read-only on disk.
- Dependencies are pinned exactly as in `cmake/Dependencies.cmake` (Task 1).
  Adding a dependency needs a line in the spec's dependency table.
- No em-dashes in any repo text (code, comments, docs, commit messages).
- No references to AI tools anywhere, and no `Co-Authored-By` trailers on
  commits.
- Work happens in a git worktree under `product/asma/.worktrees/`, merged
  directly to `main` when the plan is done (no PR).

## Review Focus

1. **Non-ASCII paths on Windows** (`Café Loops/Kick Ü.wav`): arguments, file
   opening and SQLite paths must all survive. Pinned by the Fs tests (Task 1)
   and the non-ASCII end-to-end test (Task 11).
2. **Drives that go away or directories that become unreadable**: files must
   become `missing`, never deleted, and come back with their id and user tags
   intact. Pinned in Task 8 (unmounted root, unreadable folder).
3. **Corrupt, empty or mislabelled audio files** (zero bytes, text renamed to
   `.wav`/`.mp3`/`.ogg`/`.flac`): marked `failed` with a reason, the scan
   carries on. Pinned in Tasks 3 and 8.
4. **Two writers at once** (a second `asma scan` while one runs): refused with
   exit code 3 and no database change. Pinned in Tasks 9 and 11.
5. **Search text containing FTS syntax** (quotes, `*`, `-`, `OR`, parentheses,
   only whitespace): never throws, empty text means no text filter. Pinned in
   Task 10.

Also pinned: duplicate copies of a file must not steal each other's rows during
re-linking (Task 8).

---

## File Structure

```
asma/
  CMakeLists.txt                  top-level project, options, subdirectories
  cmake/Dependencies.cmake        pinned third-party sources and their targets
  cmake/Warnings.cmake            asma_set_warnings(target)
  data/instrument_tokens.txt      instrument tag dictionary (Task 6)
  core/
    CMakeLists.txt                asma_core static library (globbed sources)
    include/asma/core/
      Fs.h                        UTF-8 paths, FILE helpers, data dir
      Db.h                        SQLite wrapper: Db, Statement, Transaction
      Schema.h                    migrations
      AudioProbe.h                format detection, header probing
      ContentHash.h               xxh3 over the audio payload
      NameParse.h                 tokens, BPM, key, loop from paths
      InstrumentTags.h            dictionary of instrument tags
      Library.h                   repository over roots/files/features/tags/FTS
      Scanner.h                   incremental scan of one root
      WriterLock.h                single-writer lock file
      Query.h                     search model to SQL, search
      Json.h                      JSON-lines output helper
    src/                          one .cpp per header, plus:
      Riff.h, Riff.cpp            internal WAV/AIFF chunk parser
      vendor_dr.c                 dr_flac/dr_mp3 implementation unit
      vendor_stb_vorbis.c         stb_vorbis implementation unit
      InstrumentTokensData.cpp.in data file embedded at configure time
  apps/
    CMakeLists.txt
    Args.h, Args.cpp              tiny argv parser (UTF-8 on Windows)
    CliCommon.h, CliCommon.cpp    db path resolution, console setup
    asma_main.cpp                 `asma` CLI
    asma_scan_main.cpp            `asma-scan` worker
  tests/
    CMakeLists.txt
    TestUtil.h                    TempDir, ScopedEnv, WAV/AIFF writers
    fixtures/tone.flac|mp3|ogg    tiny generated audio files
    test_*.cpp                    one test file per core unit, plus CLI e2e
  docs/scan-protocol.md           asma-scan JSON-lines protocol (Task 11)
  .github/workflows/ci.yml
```

Build and test commands used throughout (run from the repo root):

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/tests/asma_tests "[tag]"        # one test group
ctest --test-dir build --output-on-failure  # everything
```

---

### Task 1: Repository scaffold, build, CI and filesystem helpers

**Files:**

- Create: `.gitignore`, `LICENSE`, `README.md`, `CMakeLists.txt`,
  `cmake/Dependencies.cmake`, `cmake/Warnings.cmake`, `core/CMakeLists.txt`,
  `core/include/asma/core/Fs.h`, `core/src/Fs.cpp`, `apps/CMakeLists.txt`,
  `apps/asma_main.cpp`, `tests/CMakeLists.txt`, `tests/TestUtil.h`,
  `tests/test_fs.cpp`, `.github/workflows/ci.yml`
- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md` (section 11
  dependency table)

**Interfaces:**

- Consumes: nothing.
- Produces:
  - `std::string asma::toUtf8(const std::filesystem::path&)`
  - `std::filesystem::path asma::fromUtf8(std::string_view)`
  - `std::FILE* asma::openFileRead(const std::filesystem::path&)`
  - `bool asma::seekFile(std::FILE*, std::uint64_t)`
  - `asma::FilePtr` (`std::unique_ptr<std::FILE, asma::FileCloser>`)
  - `std::filesystem::path asma::defaultDataDir()`
  - `std::int64_t asma::fileTimeToInt(std::filesystem::file_time_type)`
  - CMake targets: `asma::core`, `asma_sqlite`, `asma_xxhash`, `asma_dr_libs`,
    `asma_stb`, function `asma_set_warnings(target)`
  - Test helpers `asma::test::TempDir`, `asma::test::ScopedEnv` in
    `tests/TestUtil.h`

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-1 -b plan-1-core-cli
```

All later paths are relative to `product/asma/.worktrees/plan-1`.

- [ ] **Step 2: Add repo boilerplate**

`.gitignore`:

```
/build/
/build-*/
/.worktrees/
.DS_Store
/.vscode/
/.idea/
CMakeUserPresets.json
```

`LICENSE`: the verbatim GPLv3 text.

```sh
curl -fsSL https://www.gnu.org/licenses/gpl-3.0.txt -o LICENSE
head -3 LICENSE   # expect "GNU GENERAL PUBLIC LICENSE" / "Version 3, 29 June 2007"
```

`README.md` (expanded in Task 11):

```markdown
# asma

Anode Labs Sample Manager. Free, open-source sample manager for macOS, Windows
and Linux. Work in progress.

## Build

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

## License

GPLv3. See `LICENSE`.
```

- [ ] **Step 3: Top-level CMake and dependencies**

`CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(asma VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASMA_BUILD_TESTS "Build the test suite" ON)

find_package(Threads REQUIRED)

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(apps)

if(ASMA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

`cmake/Warnings.cmake`:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
function(asma_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8>)
  else()
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wshadow>)
  endif()
endfunction()
```

`cmake/Dependencies.cmake` (the SQLite hash is the SHA3-256 published on
sqlite.org for this exact zip):

```cmake
# SPDX-License-Identifier: GPL-3.0-only
include(FetchContent)

# SOURCE_SUBDIR points at a directory that does not exist so FetchContent only
# downloads; we define our own targets below.
FetchContent_Declare(sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
  SOURCE_SUBDIR _none)
FetchContent_Declare(xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.4
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
FetchContent_Declare(dr_libs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
  SOURCE_SUBDIR _none)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb)

add_library(asma_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(asma_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(asma_sqlite PRIVATE
  SQLITE_ENABLE_FTS5
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
  SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(asma_sqlite PUBLIC Threads::Threads)
if(UNIX AND NOT APPLE)
  target_link_libraries(asma_sqlite PUBLIC m)
endif()

add_library(asma_xxhash STATIC ${xxhash_SOURCE_DIR}/xxhash.c)
target_include_directories(asma_xxhash SYSTEM PUBLIC ${xxhash_SOURCE_DIR})

add_library(asma_dr_libs INTERFACE)
target_include_directories(asma_dr_libs SYSTEM INTERFACE ${dr_libs_SOURCE_DIR})

add_library(asma_stb INTERFACE)
target_include_directories(asma_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
```

`core/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
file(GLOB ASMA_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

add_library(asma_core STATIC ${ASMA_CORE_SOURCES})
add_library(asma::core ALIAS asma_core)
target_include_directories(asma_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_core
  PUBLIC asma_sqlite
  PRIVATE asma_xxhash asma_dr_libs asma_stb Threads::Threads)
if(WIN32)
  target_compile_definitions(asma_core PUBLIC
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
asma_set_warnings(asma_core)
```

`apps/CMakeLists.txt` (restructured in Task 11):

```cmake
# SPDX-License-Identifier: GPL-3.0-only
add_executable(asma asma_main.cpp)
target_link_libraries(asma PRIVATE asma::core)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)
```

`apps/asma_main.cpp` (replaced in Task 11):

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include <cstring>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << "asma " << ASMA_VERSION << "\n";
        return 0;
    }
    std::cerr << "usage: asma --version\n";
    return 2;
}
```

`tests/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma::core Catch2::Catch2WithMain)
target_compile_definitions(asma_tests PRIVATE
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
asma_set_warnings(asma_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(asma_tests)
```

- [ ] **Step 4: Write the test helpers and the failing Fs tests**

`tests/TestUtil.h` (Task 3 appends audio writers to it):

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace asma::test {

namespace fs = std::filesystem;

// A fresh directory under the system temp dir, removed on destruction.
class TempDir {
public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        std::random_device rd;
        path_ = fs::temp_directory_path()
              / ("asma-test-" + std::to_string(rd()) + "-" + std::to_string(counter++));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// Sets an environment variable for the lifetime of the object.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        if (const char* old = std::getenv(name)) old_ = old;
        set(value);
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value ? value : "");
#else
        if (value) setenv(name_.c_str(), value, 1);
        else unsetenv(name_.c_str());
#endif
    }
    std::string name_;
    std::optional<std::string> old_;
};

inline void writeBytes(const fs::path& path, std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace asma::test
```

`tests/test_fs.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;
using asma::test::TempDir;

TEST_CASE("toUtf8 and fromUtf8 round-trip non-ASCII names", "[fs]")
{
    const std::string name = "Café Loops/Kick Ü 808.wav";
    CHECK(asma::toUtf8(asma::fromUtf8(name)) == name);
}

TEST_CASE("toUtf8 always uses forward slashes", "[fs]")
{
    CHECK(asma::toUtf8(fs::path("a") / "b" / "c.wav") == "a/b/c.wav");
}

TEST_CASE("openFileRead opens files with non-ASCII names", "[fs]")
{
    TempDir dir;
    const fs::path file = dir.path() / asma::fromUtf8("héllo ß.wav");
    asma::test::writeBytes(file, "abc");
    asma::FilePtr f(asma::openFileRead(file));
    REQUIRE(f);
    char buf[3];
    CHECK(std::fread(buf, 1, 3, f.get()) == 3);
}

TEST_CASE("seekFile moves to an absolute offset", "[fs]")
{
    TempDir dir;
    const fs::path file = dir.path() / "x.bin";
    asma::test::writeBytes(file, "0123456789");
    asma::FilePtr f(asma::openFileRead(file));
    REQUIRE(asma::seekFile(f.get(), 7));
    CHECK(std::fgetc(f.get()) == '7');
}

TEST_CASE("defaultDataDir honours ASMA_DATA_DIR", "[fs]")
{
    TempDir dir;
    const std::string wanted = dir.path().string();
    asma::test::ScopedEnv env("ASMA_DATA_DIR", wanted.c_str());
    CHECK(asma::defaultDataDir() == dir.path());
}

TEST_CASE("fileTimeToInt orders later times after earlier ones", "[fs]")
{
    const auto now = fs::file_time_type::clock::now();
    CHECK(asma::fileTimeToInt(now) < asma::fileTimeToInt(now + std::chrono::seconds(1)));
}
```

- [ ] **Step 5: Configure and build to see the failure**

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build`

Expected: configure succeeds and downloads the dependencies; the build FAILS
with `'asma/core/Fs.h' file not found` (the core library has no sources yet, so
CMake may also complain that `asma_core` has no sources; either counts as the
expected failure).

- [ ] **Step 6: Implement Fs**

`core/include/asma/core/Fs.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace asma {

// UTF-8 text of a path with '/' separators on every platform. This is the only
// form in which paths are stored in the database.
std::string toUtf8(const std::filesystem::path& path);

// Inverse of toUtf8.
std::filesystem::path fromUtf8(std::string_view utf8);

// Binary-mode fopen for reading that handles non-ASCII paths on Windows.
// Returns nullptr on failure.
std::FILE* openFileRead(const std::filesystem::path& path);

// 64-bit safe absolute seek. Returns false on failure.
bool seekFile(std::FILE* file, std::uint64_t offset);

struct FileCloser {
    void operator()(std::FILE* file) const noexcept
    {
        if (file) std::fclose(file);
    }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// Per-user data directory. ASMA_DATA_DIR wins when set; otherwise
// macOS: ~/Library/Application Support/Anode Labs/asma
// Windows: %APPDATA%\Anode Labs\asma
// Linux: $XDG_DATA_HOME/anode-labs/asma, else ~/.local/share/anode-labs/asma
// The directory is not created.
std::filesystem::path defaultDataDir();

// Last-write time as an opaque integer in the file clock's native ticks. Only
// meaningful for equality and ordering on the same machine.
std::int64_t fileTimeToInt(std::filesystem::file_time_type time);

} // namespace asma
```

`core/src/Fs.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Fs.h"

#include <cstdlib>
#include <cstring>
#include <optional>

#ifdef _WIN32
#include <cwchar>
#else
#include <sys/types.h>
#endif

namespace fs = std::filesystem;

namespace asma {

std::string toUtf8(const fs::path& path)
{
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path fromUtf8(std::string_view utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::FILE* openFileRead(const fs::path& path)
{
#ifdef _WIN32
    return _wfopen(path.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool seekFile(std::FILE* file, std::uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

namespace {

std::optional<fs::path> envPath(const char* name)
{
#ifdef _WIN32
    const std::wstring wide(name, name + std::strlen(name));
    if (const wchar_t* value = _wgetenv(wide.c_str()); value && *value) return fs::path(value);
#else
    if (const char* value = std::getenv(name); value && *value) return fs::path(value);
#endif
    return std::nullopt;
}

} // namespace

fs::path defaultDataDir()
{
    if (auto dir = envPath("ASMA_DATA_DIR")) return *dir;
#if defined(_WIN32)
    if (auto appData = envPath("APPDATA")) return *appData / "Anode Labs" / "asma";
    return fs::temp_directory_path() / "Anode Labs" / "asma";
#elif defined(__APPLE__)
    if (auto home = envPath("HOME"))
        return *home / "Library" / "Application Support" / "Anode Labs" / "asma";
    return fs::temp_directory_path() / "Anode Labs" / "asma";
#else
    if (auto xdg = envPath("XDG_DATA_HOME")) return *xdg / "anode-labs" / "asma";
    if (auto home = envPath("HOME")) return *home / ".local" / "share" / "anode-labs" / "asma";
    return fs::temp_directory_path() / "anode-labs" / "asma";
#endif
}

std::int64_t fileTimeToInt(fs::file_time_type time)
{
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace asma
```

- [ ] **Step 7: Build and run the Fs tests**

Run: `cmake --build build && ./build/tests/asma_tests "[fs]"`

Expected: `All tests passed (… assertions in 6 test cases)`. Also run
`./build/apps/asma --version` and expect `asma 0.1.0`.

- [ ] **Step 8: Add CI**

`.github/workflows/ci.yml`:

```yaml
# SPDX-License-Identifier: GPL-3.0-only
name: ci

on:
  push:
  pull_request:

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [macos-14, windows-2022, ubuntu-22.04]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: lukka/get-cmake@latest
      - uses: ilammy/msvc-dev-cmd@v1
        if: runner.os == 'Windows'
      - name: Configure
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 9: Record the decoder libraries in the spec**

In `docs/superpowers/specs/2026-09-25-asma-design.md`, section 11, add two rows
to the dependency table after the Signalsmith Stretch row, and one sentence
under the table:

```markdown
| dr_libs (dr_flac, dr_mp3, dr_wav) | decoding in asma-core | MIT-0 / public
domain | | stb_vorbis | Ogg Vorbis decoding in asma-core | MIT / public domain |
```

```markdown
asma-core decodes audio with dr_libs and stb_vorbis rather than JUCE, so the
core, the CLI and the scanner build without JUCE.
```

Run `prettier -w docs/superpowers/specs/2026-09-25-asma-design.md README.md`.

- [ ] **Step 10: Commit**

```sh
git add -A
git commit -m "build: scaffold asma with core Fs helpers, tests and CI"
```

---

### Task 2: Database layer and schema v1

**Files:**

- Create: `core/include/asma/core/Db.h`, `core/src/Db.cpp`,
  `core/include/asma/core/Schema.h`, `core/src/Schema.cpp`, `tests/test_db.cpp`

**Interfaces:**

- Consumes: `asma::toUtf8` (Task 1).
- Produces:
  - `class asma::DbError : std::runtime_error`
  - `class asma::Statement` with
    `bind(int, std::int64_t|int|double|std::string_view)`, `bindNull(int)`,
    `bindOptional(int, const std::optional<T>&)`, `bool step()`, `void run()`,
    `void reset()`, `bool isNull(int) const`, `std::int64_t getInt(int) const`,
    `double getDouble(int) const`, `std::string getText(int) const` (bind
    indices 1-based, columns 0-based)
  - `class asma::Db` with `static Db open(const std::filesystem::path&)`,
    `static Db openInMemory()`, `void exec(std::string_view)`,
    `Statement prepare(std::string_view)`, `std::int64_t lastInsertId() const`,
    `int schemaVersion()`, `sqlite3* handle() const`
  - `class asma::Transaction` (`BEGIN IMMEDIATE`, `commit()`, rollback in
    destructor)
  - `int asma::currentSchemaVersion()`, `void asma::migrate(Db&)`
  - Tables: `roots`, `files` (with `name`), `features`, `tags`, `file_tags`,
    `fts_files` exactly as in Step 3.

- [ ] **Step 1: Write the failing tests**

`tests/test_db.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Schema.h"

#include <catch2/catch_test_macros.hpp>

using asma::Db;
using asma::test::TempDir;

TEST_CASE("Db::open creates the file and parent dirs, enables WAL, migrates", "[db]")
{
    TempDir dir;
    const auto file = dir.path() / "sub" / "library.db";
    Db db = Db::open(file);
    CHECK(std::filesystem::exists(file));
    auto mode = db.prepare("PRAGMA journal_mode");
    REQUIRE(mode.step());
    CHECK(mode.getText(0) == "wal");
    CHECK(db.schemaVersion() == asma::currentSchemaVersion());
}

TEST_CASE("Reopening an up-to-date database keeps its data", "[db]")
{
    TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        Db db = Db::open(file);
        db.exec("INSERT INTO roots(path) VALUES ('/x')");
    }
    Db db = Db::open(file);
    auto count = db.prepare("SELECT COUNT(*) FROM roots");
    REQUIRE(count.step());
    CHECK(count.getInt(0) == 1);
}

TEST_CASE("A database from a newer asma is refused", "[db]")
{
    TempDir dir;
    const auto file = dir.path() / "library.db";
    {
        Db db = Db::open(file);
        db.exec("PRAGMA user_version = 999");
    }
    CHECK_THROWS_AS(Db::open(file), asma::DbError);
}

TEST_CASE("FTS5 is compiled in and prefix queries work", "[db]")
{
    Db db = Db::openInMemory();
    db.exec("INSERT INTO fts_files(rowid, name, folder, tags) VALUES (7, 'Kick_01', 'Drums', 'kick')");
    auto q = db.prepare("SELECT rowid FROM fts_files WHERE fts_files MATCH ?");
    q.bind(1, "\"kic\"*");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 7);
}

TEST_CASE("Transaction rolls back unless committed", "[db]")
{
    Db db = Db::openInMemory();
    {
        asma::Transaction tx(db);
        db.exec("INSERT INTO roots(path) VALUES ('/a')");
    }
    {
        asma::Transaction tx(db);
        db.exec("INSERT INTO roots(path) VALUES ('/b')");
        tx.commit();
    }
    auto q = db.prepare("SELECT path FROM roots");
    REQUIRE(q.step());
    CHECK(q.getText(0) == "/b");
    CHECK_FALSE(q.step());
}

TEST_CASE("An empty string binds as text, not NULL", "[db]")
{
    Db db = Db::openInMemory();
    auto q = db.prepare("SELECT ? IS NULL, typeof(?)");
    q.bind(1, std::string_view{}).bind(2, std::string_view{});
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
    CHECK(q.getText(1) == "text");
}

TEST_CASE("bindOptional binds NULL for an empty optional", "[db]")
{
    Db db = Db::openInMemory();
    auto q = db.prepare("SELECT ? IS NULL, ?");
    q.bindOptional(1, std::optional<double>{}).bindOptional(2, std::optional<int>{5});
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 1);
    CHECK(q.getInt(1) == 5);
}

TEST_CASE("Deleting a root cascades to its files", "[db]")
{
    Db db = Db::openInMemory();
    db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
    db.exec("INSERT INTO files(root_id, rel_path, name, size, mtime, format, status) "
            "VALUES (1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok')");
    db.exec("DELETE FROM roots WHERE id = 1");
    auto q = db.prepare("SELECT COUNT(*) FROM files");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
}

TEST_CASE("Bad SQL throws DbError with SQLite's message", "[db]")
{
    Db db = Db::openInMemory();
    CHECK_THROWS_AS(db.exec("SELEKT 1"), asma::DbError);
    CHECK_THROWS_AS(db.prepare("SELECT * FROM nope"), asma::DbError);
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/Db.h' file not found`.

- [ ] **Step 3: Implement Db and the schema**

`core/include/asma/core/Db.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace asma {

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A prepared statement. Bind indices are 1-based, column indices 0-based.
class Statement {
public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bindNull(int index);

    template <typename T>
    Statement& bindOptional(int index, const std::optional<T>& value)
    {
        return value ? bind(index, *value) : bindNull(index);
    }

    bool step();  // true while a row is available
    void run();   // steps to completion, for writes
    void reset(); // resets and clears bindings for reuse

    bool isNull(int column) const;
    std::int64_t getInt(int column) const;
    double getDouble(int column) const;
    std::string getText(int column) const;

private:
    void check(int rc, std::string_view what);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class Db {
public:
    // Opens or creates the file (and its parent directories), enables WAL and
    // migrates to the current schema.
    static Db open(const std::filesystem::path& file);
    // Private in-memory database with the current schema, for tests.
    static Db openInMemory();

    ~Db();
    Db(Db&& other) noexcept;
    Db& operator=(Db&& other) noexcept;
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    void exec(std::string_view sql);
    Statement prepare(std::string_view sql);
    std::int64_t lastInsertId() const;
    int schemaVersion();
    sqlite3* handle() const { return db_; }

private:
    explicit Db(sqlite3* db) : db_(db) {}
    static Db openHandle(const std::string& utf8Name);

    sqlite3* db_ = nullptr;
};

// BEGIN IMMEDIATE on construction; ROLLBACK on destruction unless committed.
class Transaction {
public:
    explicit Transaction(Db& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Db& db_;
    bool finished_ = false;
};

} // namespace asma
```

`core/include/asma/core/Schema.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace asma {

class Db;

int currentSchemaVersion();

// Applies pending migrations, each in its own transaction. Throws DbError when
// the database was written by a newer asma.
void migrate(Db& db);

} // namespace asma
```

`core/src/Db.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Db.h"

#include "asma/core/Fs.h"
#include "asma/core/Schema.h"

#include <sqlite3.h>
#include <utility>

namespace asma {

namespace {

[[noreturn]] void fail(sqlite3* db, std::string_view what)
{
    throw DbError(std::string(what) + ": " + (db ? sqlite3_errmsg(db) : "out of memory"));
}

} // namespace

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db)
{
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK)
        fail(db, "prepare failed for: " + std::string(sql));
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr))
{
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this != &other) {
        sqlite3_finalize(stmt_);
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void Statement::check(int rc, std::string_view what)
{
    if (rc != SQLITE_OK) fail(db_, what);
}

Statement& Statement::bind(int index, std::int64_t value)
{
    check(sqlite3_bind_int64(stmt_, index, value), "bind failed");
    return *this;
}

Statement& Statement::bind(int index, double value)
{
    check(sqlite3_bind_double(stmt_, index, value), "bind failed");
    return *this;
}

Statement& Statement::bind(int index, std::string_view value)
{
    // A default-constructed string_view has a null data pointer, which SQLite
    // would bind as NULL. Bind an empty string instead.
    const char* data = value.data() ? value.data() : "";
    check(sqlite3_bind_text(stmt_, index, data, static_cast<int>(value.size()), SQLITE_TRANSIENT),
          "bind failed");
    return *this;
}

Statement& Statement::bindNull(int index)
{
    check(sqlite3_bind_null(stmt_, index), "bind failed");
    return *this;
}

bool Statement::step()
{
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    fail(db_, "step failed");
}

void Statement::run()
{
    while (step()) {
    }
}

void Statement::reset()
{
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

std::int64_t Statement::getInt(int column) const { return sqlite3_column_int64(stmt_, column); }

double Statement::getDouble(int column) const { return sqlite3_column_double(stmt_, column); }

std::string Statement::getText(int column) const
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
    if (!text) return {};
    return std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)));
}

Db Db::openHandle(const std::string& utf8Name)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(utf8Name.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    Db db(raw); // owns the handle even on failure, so it gets closed
    if (rc != SQLITE_OK) fail(raw, "cannot open database " + utf8Name);
    sqlite3_busy_timeout(raw, 5000);
    db.exec("PRAGMA foreign_keys = ON");
    return db;
}

Db Db::open(const std::filesystem::path& file)
{
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
    Db db = openHandle(toUtf8(file));
    db.exec("PRAGMA journal_mode = WAL");
    db.exec("PRAGMA synchronous = NORMAL");
    migrate(db);
    return db;
}

Db Db::openInMemory()
{
    Db db = openHandle(":memory:");
    migrate(db);
    return db;
}

Db::~Db() { sqlite3_close_v2(db_); }

Db::Db(Db&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

Db& Db::operator=(Db&& other) noexcept
{
    if (this != &other) {
        sqlite3_close_v2(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void Db::exec(std::string_view sql)
{
    const std::string text(sql);
    char* error = nullptr;
    if (sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "unknown error";
        sqlite3_free(error);
        throw DbError("exec failed: " + message);
    }
}

Statement Db::prepare(std::string_view sql) { return Statement(db_, sql); }

std::int64_t Db::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }

int Db::schemaVersion()
{
    auto q = prepare("PRAGMA user_version");
    q.step();
    return static_cast<int>(q.getInt(0));
}

Transaction::Transaction(Db& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction()
{
    if (finished_) return;
    try {
        db_.exec("ROLLBACK");
    } catch (...) {
        // Nothing useful to do in a destructor.
    }
}

void Transaction::commit()
{
    db_.exec("COMMIT");
    finished_ = true;
}

} // namespace asma
```

`core/src/Schema.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Schema.h"

#include "asma/core/Db.h"

#include <array>
#include <string>
#include <string_view>

namespace asma {

namespace {

// Append-only. Never edit a migration that has shipped; add a new one.
constexpr std::array<std::string_view, 1> kMigrations = {
    R"SQL(
CREATE TABLE roots (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    enabled INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    root_id INTEGER NOT NULL REFERENCES roots(id) ON DELETE CASCADE,
    rel_path TEXT NOT NULL,
    name TEXT NOT NULL,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    content_hash TEXT,
    format TEXT NOT NULL,
    sample_rate INTEGER,
    channels INTEGER,
    bit_depth INTEGER,
    duration REAL,
    status TEXT NOT NULL CHECK (status IN ('ok', 'missing', 'failed')),
    failure_reason TEXT,
    analysis_version INTEGER NOT NULL DEFAULT 0,
    UNIQUE (root_id, rel_path)
);
CREATE INDEX files_content_hash ON files(content_hash);
CREATE INDEX files_status ON files(status);

CREATE TABLE features (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    bpm REAL,
    bpm_confidence REAL,
    key TEXT,
    key_confidence REAL,
    is_loop INTEGER,
    root_note INTEGER
);

CREATE TABLE tags (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE file_tags (
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    source TEXT NOT NULL CHECK (source IN ('auto', 'embedded', 'user')),
    PRIMARY KEY (file_id, tag_id)
);

CREATE VIRTUAL TABLE fts_files USING fts5(
    name, folder, tags,
    tokenize = 'unicode61 remove_diacritics 2',
    prefix = '2 3'
);
)SQL",
};

} // namespace

int currentSchemaVersion() { return static_cast<int>(kMigrations.size()); }

void migrate(Db& db)
{
    for (;;) {
        // Read the version inside the write transaction so two processes that
        // open the same new database cannot both apply the same migration.
        Transaction tx(db);
        const int version = db.schemaVersion();
        if (version > currentSchemaVersion())
            throw DbError("database schema version " + std::to_string(version)
                          + " is newer than this asma build supports ("
                          + std::to_string(currentSchemaVersion()) + ")");
        if (version == currentSchemaVersion()) {
            tx.commit();
            return;
        }
        db.exec(kMigrations[static_cast<std::size_t>(version)]);
        db.exec("PRAGMA user_version = " + std::to_string(version + 1));
        tx.commit();
    }
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[db]"`

Expected: all 9 `[db]` test cases pass.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: add SQLite wrapper and schema v1"
```

---

### Task 3: Audio probing for WAV, AIFF, FLAC, MP3 and Ogg

**Files:**

- Create: `core/include/asma/core/AudioProbe.h`, `core/src/AudioProbe.cpp`,
  `core/src/Riff.h`, `core/src/Riff.cpp`, `core/src/vendor_dr.c`,
  `core/src/vendor_stb_vorbis.c`, `tests/test_audio_probe.cpp`,
  `tests/fixtures/tone.flac`, `tests/fixtures/tone.mp3`,
  `tests/fixtures/tone.ogg`, `tests/fixtures/README.md`
- Modify: `tests/TestUtil.h` (append WAV/AIFF writers and `fixture()`)

**Interfaces:**

- Consumes: `openFileRead`, `seekFile`, `FilePtr`, `toUtf8` (Task 1).
- Produces:
  - `enum class asma::AudioFormat { Wav, Aiff, Flac, Mp3, Ogg }`
  - `std::optional<AudioFormat> asma::formatFromExtension(const std::filesystem::path&)`
  - `std::string_view asma::formatName(AudioFormat)` returning
    `"wav" | "aiff" | "flac" | "mp3" | "ogg"`
  - `class asma::ProbeError : std::runtime_error`
  - `struct asma::AcidInfo { bool oneShot; float tempo; }`
  - `struct asma::ProbeResult { AudioFormat format; int sampleRate; int channels; int bitDepth; double durationSeconds; std::uint64_t hashOffset; std::uint64_t hashLength; std::optional<AcidInfo> acid; std::optional<int> smplUnityNote; }`
  - `ProbeResult asma::probeFile(const std::filesystem::path&)` (throws
    `ProbeError`)
  - Test helpers: `asma::test::WavSpec`, `writeWav`, `AiffSpec`, `writeAiff`,
    `fixture(std::string_view)`

- [ ] **Step 1: Generate the compressed fixtures**

ffmpeg's built-in Vorbis encoder only supports stereo, so the Ogg fixture is
stereo.

```sh
mkdir -p tests/fixtures
SRC="sine=frequency=440:sample_rate=44100:duration=0.5"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -ac 1 -c:a flac -sample_fmt s16 tests/fixtures/tone.flac
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -ac 1 -c:a libmp3lame -b:a 64k tests/fixtures/tone.mp3
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -ac 2 -c:a vorbis -strict experimental tests/fixtures/tone.ogg
ls -l tests/fixtures
```

`tests/fixtures/README.md`:

```markdown
# Test fixtures

Half-second 440 Hz sine tones generated with ffmpeg. They contain no third-party
audio.

    SRC="sine=frequency=440:sample_rate=44100:duration=0.5"
    ffmpeg -f lavfi -i "$SRC" -ac 1 -c:a flac -sample_fmt s16 tone.flac
    ffmpeg -f lavfi -i "$SRC" -ac 1 -c:a libmp3lame -b:a 64k tone.mp3
    ffmpeg -f lavfi -i "$SRC" -ac 2 -c:a vorbis -strict experimental tone.ogg
```

- [ ] **Step 2: Add WAV and AIFF writers to the test helpers**

Append to `tests/TestUtil.h`, inside `namespace asma::test` (add
`#include <cmath>`, `#include <cstdint>`, `#include <cstring>`,
`#include <utility>`, `#include <vector>` at the top):

```cpp
inline fs::path fixture(std::string_view name) { return fs::path(ASMA_TEST_FIXTURES) / name; }

namespace detail {

inline void putLe16(std::string& b, std::uint16_t v)
{
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>(v >> 8));
}
inline void putLe32(std::string& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
inline void putBe16(std::string& b, std::uint16_t v)
{
    b.push_back(static_cast<char>(v >> 8));
    b.push_back(static_cast<char>(v & 0xFF));
}
inline void putBe32(std::string& b, std::uint32_t v)
{
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
inline void putChunkLe(std::string& b, std::string_view id, const std::string& payload)
{
    b.append(id.data(), 4);
    putLe32(b, static_cast<std::uint32_t>(payload.size()));
    b += payload;
    if (payload.size() & 1) b.push_back('\0');
}
inline void putChunkBe(std::string& b, std::string_view id, const std::string& payload)
{
    b.append(id.data(), 4);
    putBe32(b, static_cast<std::uint32_t>(payload.size()));
    b += payload;
    if (payload.size() & 1) b.push_back('\0');
}
// Deterministic pseudo-random sample bytes; different seeds give different data.
inline std::string sampleBytes(std::size_t count, std::uint32_t seed)
{
    std::string data;
    data.reserve(count);
    std::uint32_t x = seed;
    for (std::size_t i = 0; i < count; ++i) {
        x = x * 1664525u + 1013904223u;
        data.push_back(static_cast<char>((x >> 24) & 0xFF));
    }
    return data;
}
// 80-bit IEEE extended, as used for the AIFF sample rate.
inline std::string extended(double value)
{
    int exponent = 0;
    const double mantissa = std::frexp(value, &exponent);
    const auto bits = static_cast<std::uint64_t>(std::ldexp(mantissa, 64));
    const int biased = exponent - 1 + 16383;
    std::string out;
    out.push_back(static_cast<char>((biased >> 8) & 0x7F));
    out.push_back(static_cast<char>(biased & 0xFF));
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
    return out;
}

} // namespace detail

struct WavSpec {
    int sampleRate = 44100;
    int channels = 1;
    int bitsPerSample = 16;
    int frames = 4410;
    std::uint32_t seed = 1;
    std::optional<std::pair<bool, float>> acid; // {oneShot, tempo}
    std::optional<int> smplUnityNote;
    std::vector<std::pair<std::string, std::string>> chunksBeforeData; // {4-char id, payload}
};

inline void writeWav(const fs::path& path, const WavSpec& spec)
{
    using namespace detail;
    const int blockAlign = spec.channels * spec.bitsPerSample / 8;
    std::string fmt;
    putLe16(fmt, 1);
    putLe16(fmt, static_cast<std::uint16_t>(spec.channels));
    putLe32(fmt, static_cast<std::uint32_t>(spec.sampleRate));
    putLe32(fmt, static_cast<std::uint32_t>(spec.sampleRate * blockAlign));
    putLe16(fmt, static_cast<std::uint16_t>(blockAlign));
    putLe16(fmt, static_cast<std::uint16_t>(spec.bitsPerSample));

    std::string body = "WAVE";
    putChunkLe(body, "fmt ", fmt);
    if (spec.acid) {
        std::string acid;
        putLe32(acid, spec.acid->first ? 1u : 0u); // flags: bit 0 = one-shot
        putLe16(acid, 0x3C);                      // root note
        putLe16(acid, 0x8000);
        putLe32(acid, 0);                         // unknown float
        putLe32(acid, 8);                         // beats
        putLe16(acid, 4);                         // meter denominator
        putLe16(acid, 4);                         // meter numerator
        std::uint32_t tempoBits = 0;
        std::memcpy(&tempoBits, &spec.acid->second, 4);
        putLe32(acid, tempoBits);
        putChunkLe(body, "acid", acid);
    }
    if (spec.smplUnityNote) {
        std::string smpl;
        putLe32(smpl, 0);     // manufacturer
        putLe32(smpl, 0);     // product
        putLe32(smpl, 22675); // sample period
        putLe32(smpl, static_cast<std::uint32_t>(*spec.smplUnityNote));
        for (int i = 0; i < 5; ++i) putLe32(smpl, 0);
        putChunkLe(body, "smpl", smpl);
    }
    for (const auto& [id, payload] : spec.chunksBeforeData) putChunkLe(body, id, payload);
    putChunkLe(body, "data",
               sampleBytes(static_cast<std::size_t>(spec.frames) * static_cast<std::size_t>(blockAlign), spec.seed));

    std::string file = "RIFF";
    putLe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

struct AiffSpec {
    int sampleRate = 44100;
    int channels = 1;
    int bitsPerSample = 16;
    int frames = 4410;
    std::uint32_t seed = 1;
};

inline void writeAiff(const fs::path& path, const AiffSpec& spec)
{
    using namespace detail;
    std::string comm;
    putBe16(comm, static_cast<std::uint16_t>(spec.channels));
    putBe32(comm, static_cast<std::uint32_t>(spec.frames));
    putBe16(comm, static_cast<std::uint16_t>(spec.bitsPerSample));
    comm += extended(spec.sampleRate);

    std::string ssnd;
    putBe32(ssnd, 0); // offset
    putBe32(ssnd, 0); // block size
    ssnd += sampleBytes(static_cast<std::size_t>(spec.frames) * static_cast<std::size_t>(spec.channels)
                            * static_cast<std::size_t>(spec.bitsPerSample / 8),
                        spec.seed);

    std::string body = "AIFF";
    putChunkBe(body, "COMM", comm);
    putChunkBe(body, "SSND", ssnd);
    std::string file = "FORM";
    putBe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}
```

- [ ] **Step 3: Write the failing tests**

`tests/test_audio_probe.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/AudioProbe.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

TEST_CASE("formatFromExtension is case-insensitive and rejects unknown types", "[probe]")
{
    CHECK(formatFromExtension("KICK.WAV") == AudioFormat::Wav);
    CHECK(formatFromExtension("x.wave") == AudioFormat::Wav);
    CHECK(formatFromExtension("pad.Aif") == AudioFormat::Aiff);
    CHECK(formatFromExtension("pad.aifc") == AudioFormat::Aiff);
    CHECK(formatFromExtension("x.flac") == AudioFormat::Flac);
    CHECK(formatFromExtension("x.MP3") == AudioFormat::Mp3);
    CHECK(formatFromExtension("x.ogg") == AudioFormat::Ogg);
    CHECK_FALSE(formatFromExtension("notes.txt").has_value());
    CHECK_FALSE(formatFromExtension("wav").has_value());
    CHECK(formatName(AudioFormat::Aiff) == "aiff");
}

TEST_CASE("WAV header fields and data range", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "a.wav";
    test::WavSpec spec;
    spec.channels = 2;
    spec.frames = 44100;
    test::writeWav(p, spec);

    const ProbeResult r = probeFile(p);
    CHECK(r.format == AudioFormat::Wav);
    CHECK(r.sampleRate == 44100);
    CHECK(r.channels == 2);
    CHECK(r.bitDepth == 16);
    CHECK(r.durationSeconds == Catch::Approx(1.0));
    CHECK(r.hashOffset == 44);
    CHECK(r.hashLength == 44100u * 4u);
    CHECK_FALSE(r.acid.has_value());
    CHECK_FALSE(r.smplUnityNote.has_value());
}

TEST_CASE("Chunks before data move the offset but not the length", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "b.wav";
    test::WavSpec spec;
    spec.chunksBeforeData = {{"LIST", std::string(10, 'x')}, {"junk", "abc"}}; // odd size is padded
    test::writeWav(p, spec);

    const ProbeResult r = probeFile(p);
    CHECK(r.hashOffset == 44u + (8u + 10u) + (8u + 3u + 1u));
    CHECK(r.hashLength == 4410u * 2u);
}

TEST_CASE("ACID and smpl chunks are read", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "c.wav";
    test::WavSpec spec;
    spec.acid = std::make_pair(false, 128.0f);
    spec.smplUnityNote = 48;
    test::writeWav(p, spec);

    const ProbeResult r = probeFile(p);
    REQUIRE(r.acid.has_value());
    CHECK_FALSE(r.acid->oneShot);
    CHECK(r.acid->tempo == Catch::Approx(128.0));
    CHECK(r.smplUnityNote == 48);
}

TEST_CASE("AIFF header fields and data range", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "d.aiff";
    test::AiffSpec spec;
    spec.sampleRate = 48000;
    spec.bitsPerSample = 24;
    spec.frames = 24000;
    test::writeAiff(p, spec);

    const ProbeResult r = probeFile(p);
    CHECK(r.format == AudioFormat::Aiff);
    CHECK(r.sampleRate == 48000);
    CHECK(r.channels == 1);
    CHECK(r.bitDepth == 24);
    CHECK(r.durationSeconds == Catch::Approx(0.5));
    CHECK(r.hashLength == 24000u * 3u);
}

TEST_CASE("FLAC, MP3 and Ogg fixtures", "[probe]")
{
    const auto flacPath = test::fixture("tone.flac");
    const ProbeResult flac = probeFile(flacPath);
    CHECK(flac.format == AudioFormat::Flac);
    CHECK(flac.sampleRate == 44100);
    CHECK(flac.channels == 1);
    CHECK(flac.bitDepth == 16);
    CHECK(flac.durationSeconds == Catch::Approx(0.5).margin(0.001));
    CHECK(flac.hashOffset == 0);
    CHECK(flac.hashLength == std::filesystem::file_size(flacPath));

    const ProbeResult mp3 = probeFile(test::fixture("tone.mp3"));
    CHECK(mp3.format == AudioFormat::Mp3);
    CHECK(mp3.sampleRate == 44100);
    CHECK(mp3.channels == 1);
    CHECK(mp3.bitDepth == 0);
    CHECK(mp3.durationSeconds == Catch::Approx(0.5).margin(0.06));

    const ProbeResult ogg = probeFile(test::fixture("tone.ogg"));
    CHECK(ogg.format == AudioFormat::Ogg);
    CHECK(ogg.sampleRate == 44100);
    CHECK(ogg.channels == 2);
    CHECK(ogg.durationSeconds == Catch::Approx(0.5).margin(0.01));
}

TEST_CASE("Empty, truncated and mislabelled files throw ProbeError", "[probe]")
{
    TempDir dir;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"empty.wav", ""},
        {"text.wav", "hello, this is not audio at all"},
        {"truncated.wav", std::string("RIFF\x10\0\0\0WAVE", 12)},
        {"nodata.wav", std::string("RIFF\x04\0\0\0WAVE", 12)},
        {"text.aiff", "FORM but not really"},
        {"text.flac", std::string(2000, 'a')},
        {"text.mp3", std::string(2000, 'a')},
        {"text.ogg", std::string(2000, 'a')},
    };
    for (const auto& [name, bytes] : cases) {
        INFO(name);
        const auto p = dir.path() / name;
        test::writeBytes(p, bytes);
        CHECK_THROWS_AS(probeFile(p), ProbeError);
    }
}

TEST_CASE("A missing file throws ProbeError", "[probe]")
{
    TempDir dir;
    CHECK_THROWS_AS(probeFile(dir.path() / "nope.wav"), ProbeError);
}
```

- [ ] **Step 4: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/AudioProbe.h' file not found`.

- [ ] **Step 5: Implement probing**

`core/include/asma/core/AudioProbe.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace asma {

enum class AudioFormat { Wav, Aiff, Flac, Mp3, Ogg };

// Case-insensitive: .wav .wave .aif .aiff .aifc .flac .mp3 .ogg
std::optional<AudioFormat> formatFromExtension(const std::filesystem::path& path);

// "wav", "aiff", "flac", "mp3" or "ogg".
std::string_view formatName(AudioFormat format);

class ProbeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AcidInfo {
    bool oneShot = false;
    float tempo = 0.0f; // 0 when the chunk carries none
};

struct ProbeResult {
    AudioFormat format = AudioFormat::Wav;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0; // 0 for lossy formats
    double durationSeconds = 0.0;
    // Byte range hashed for the content hash: the audio payload for WAV and
    // AIFF (so metadata edits keep the hash), the whole file otherwise.
    std::uint64_t hashOffset = 0;
    std::uint64_t hashLength = 0;
    std::optional<AcidInfo> acid;
    std::optional<int> smplUnityNote; // MIDI note, 60 = C4
};

// Reads headers only (MP3 also needs a frame scan). Throws ProbeError for
// unreadable, unsupported or malformed files.
ProbeResult probeFile(const std::filesystem::path& path);

} // namespace asma
```

`core/src/Riff.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/AudioProbe.h"

#include <cstdint>
#include <cstdio>

namespace asma::detail {

// Chunk walkers for RIFF/WAVE and IFF/AIFF. Throw ProbeError.
ProbeResult probeWav(std::FILE* file, std::uint64_t fileSize);
ProbeResult probeAiff(std::FILE* file, std::uint64_t fileSize);

} // namespace asma::detail
```

`core/src/Riff.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Riff.h"

#include "asma/core/Fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace asma::detail {

namespace {

std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::uint32_t le32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t be16(const unsigned char* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

std::uint32_t be32(const unsigned char* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

float leFloat(const unsigned char* p)
{
    const std::uint32_t bits = le32(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// 80-bit IEEE 754 extended precision, big-endian.
double extendedToDouble(const unsigned char* b)
{
    const int exponent = ((b[0] & 0x7F) << 8) | b[1];
    std::uint64_t mantissa = 0;
    for (int i = 2; i < 10; ++i) mantissa = (mantissa << 8) | b[i];
    if (exponent == 0 && mantissa == 0) return 0.0;
    const double value = std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
    return (b[0] & 0x80) ? -value : value;
}

bool readExact(std::FILE* file, void* buffer, std::size_t size)
{
    return std::fread(buffer, 1, size, file) == size;
}

} // namespace

ProbeResult probeWav(std::FILE* file, std::uint64_t fileSize)
{
    unsigned char header[12];
    if (!readExact(file, header, sizeof header)) throw ProbeError("file too short for a WAV header");
    if (std::memcmp(header, "RF64", 4) == 0) throw ProbeError("RF64 WAV files are not supported");
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
        throw ProbeError("not a RIFF/WAVE file");

    ProbeResult r;
    r.format = AudioFormat::Wav;
    bool haveFmt = false;
    bool haveData = false;
    int blockAlign = 0;

    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize) {
        unsigned char chunk[8];
        if (!seekFile(file, pos) || !readExact(file, chunk, sizeof chunk)) break;
        const std::uint32_t size = le32(chunk + 4);
        const std::uint64_t payload = pos + 8;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            unsigned char fmt[16];
            if (!readExact(file, fmt, sizeof fmt)) throw ProbeError("truncated fmt chunk");
            r.channels = le16(fmt + 2);
            r.sampleRate = static_cast<int>(le32(fmt + 4));
            blockAlign = le16(fmt + 12);
            r.bitDepth = le16(fmt + 14);
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            r.hashOffset = payload;
            // Streamed WAVs may carry a bogus size; never run past the file.
            r.hashLength = std::min<std::uint64_t>(size, fileSize - payload);
            haveData = true;
        } else if (std::memcmp(chunk, "acid", 4) == 0 && size >= 24) {
            unsigned char acid[24];
            if (readExact(file, acid, sizeof acid)) {
                AcidInfo info;
                info.oneShot = (le32(acid) & 0x01u) != 0;
                info.tempo = leFloat(acid + 20);
                r.acid = info;
            }
        } else if (std::memcmp(chunk, "smpl", 4) == 0 && size >= 16) {
            unsigned char smpl[16];
            if (readExact(file, smpl, sizeof smpl)) {
                const std::uint32_t note = le32(smpl + 12);
                if (note <= 127) r.smplUnityNote = static_cast<int>(note);
            }
        }
        pos = payload + size + (size & 1u);
    }

    if (!haveFmt) throw ProbeError("WAV file has no fmt chunk");
    if (!haveData) throw ProbeError("WAV file has no data chunk");
    if (r.sampleRate <= 0 || r.channels <= 0 || blockAlign <= 0) throw ProbeError("WAV fmt chunk is invalid");
    r.durationSeconds = static_cast<double>(r.hashLength / static_cast<std::uint64_t>(blockAlign))
                      / static_cast<double>(r.sampleRate);
    return r;
}

ProbeResult probeAiff(std::FILE* file, std::uint64_t fileSize)
{
    unsigned char header[12];
    if (!readExact(file, header, sizeof header)) throw ProbeError("file too short for an AIFF header");
    if (std::memcmp(header, "FORM", 4) != 0
        || (std::memcmp(header + 8, "AIFF", 4) != 0 && std::memcmp(header + 8, "AIFC", 4) != 0))
        throw ProbeError("not an AIFF file");

    ProbeResult r;
    r.format = AudioFormat::Aiff;
    bool haveComm = false;
    bool haveSsnd = false;
    std::uint32_t frames = 0;
    double rate = 0.0;

    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize) {
        unsigned char chunk[8];
        if (!seekFile(file, pos) || !readExact(file, chunk, sizeof chunk)) break;
        const std::uint32_t size = be32(chunk + 4);
        const std::uint64_t payload = pos + 8;

        if (std::memcmp(chunk, "COMM", 4) == 0 && size >= 18) {
            unsigned char comm[18];
            if (!readExact(file, comm, sizeof comm)) throw ProbeError("truncated COMM chunk");
            r.channels = static_cast<std::int16_t>(be16(comm));
            frames = be32(comm + 2);
            r.bitDepth = static_cast<std::int16_t>(be16(comm + 6));
            rate = extendedToDouble(comm + 8);
            r.sampleRate = static_cast<int>(std::lround(rate));
            haveComm = true;
        } else if (std::memcmp(chunk, "SSND", 4) == 0 && size >= 8) {
            unsigned char ssnd[8];
            if (!readExact(file, ssnd, sizeof ssnd)) throw ProbeError("truncated SSND chunk");
            const std::uint64_t start = payload + 8 + be32(ssnd);
            const std::uint64_t end = std::min<std::uint64_t>(payload + size, fileSize);
            if (start <= end) {
                r.hashOffset = start;
                r.hashLength = end - start;
                haveSsnd = true;
            }
        }
        pos = payload + size + (size & 1u);
    }

    if (!haveComm) throw ProbeError("AIFF file has no COMM chunk");
    if (!haveSsnd) throw ProbeError("AIFF file has no SSND chunk");
    if (rate <= 0.0 || r.channels <= 0) throw ProbeError("AIFF COMM chunk is invalid");
    r.durationSeconds = static_cast<double>(frames) / rate;
    return r;
}

} // namespace asma::detail
```

`core/src/vendor_dr.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Implementation unit for the vendored single-header decoders. */
#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
```

`core/src/vendor_stb_vorbis.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Implementation unit for the vendored Ogg Vorbis decoder. */
#include <stb_vorbis.c>
```

Silence warnings in vendored code: add to `core/CMakeLists.txt` after
`asma_set_warnings(asma_core)`:

```cmake
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor_dr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor_stb_vorbis.c
  PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
```

`core/src/AudioProbe.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/AudioProbe.h"

#include "Riff.h"
#include "asma/core/Fs.h"

#include <cctype>
#include <memory>
#include <string>

#include <dr_flac.h>
#include <dr_mp3.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace fs = std::filesystem;

namespace asma {

std::optional<AudioFormat> formatFromExtension(const fs::path& path)
{
    std::string ext = toUtf8(path.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".wav" || ext == ".wave") return AudioFormat::Wav;
    if (ext == ".aif" || ext == ".aiff" || ext == ".aifc") return AudioFormat::Aiff;
    if (ext == ".flac") return AudioFormat::Flac;
    if (ext == ".mp3") return AudioFormat::Mp3;
    if (ext == ".ogg") return AudioFormat::Ogg;
    return std::nullopt;
}

std::string_view formatName(AudioFormat format)
{
    switch (format) {
    case AudioFormat::Wav: return "wav";
    case AudioFormat::Aiff: return "aiff";
    case AudioFormat::Flac: return "flac";
    case AudioFormat::Mp3: return "mp3";
    case AudioFormat::Ogg: return "ogg";
    }
    return "unknown";
}

namespace {

void requireAudio(const ProbeResult& r, std::uint64_t frames, const char* what)
{
    if (r.sampleRate <= 0 || r.channels <= 0) throw ProbeError(std::string(what) + " stream has no audio format");
    if (frames == 0) throw ProbeError(std::string(what) + " stream has no audio frames");
}

ProbeResult probeFlac(const fs::path& path, std::uint64_t fileSize)
{
#ifdef _WIN32
    drflac* flac = drflac_open_file_w(path.c_str(), nullptr);
#else
    drflac* flac = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!flac) throw ProbeError("cannot decode FLAC header");
    ProbeResult r;
    r.format = AudioFormat::Flac;
    r.sampleRate = static_cast<int>(flac->sampleRate);
    r.channels = flac->channels;
    r.bitDepth = flac->bitsPerSample;
    const std::uint64_t frames = flac->totalPCMFrameCount;
    drflac_close(flac);
    requireAudio(r, frames, "FLAC");
    r.durationSeconds = static_cast<double>(frames) / r.sampleRate;
    r.hashLength = fileSize;
    return r;
}

ProbeResult probeMp3(const fs::path& path, std::uint64_t fileSize)
{
    auto mp3 = std::make_unique<drmp3>(); // large struct: keep it off worker stacks
#ifdef _WIN32
    const bool opened = drmp3_init_file_w(mp3.get(), path.c_str(), nullptr);
#else
    const bool opened = drmp3_init_file(mp3.get(), path.c_str(), nullptr);
#endif
    if (!opened) throw ProbeError("cannot decode MP3 stream");
    ProbeResult r;
    r.format = AudioFormat::Mp3;
    r.sampleRate = static_cast<int>(mp3->sampleRate);
    r.channels = static_cast<int>(mp3->channels);
    const std::uint64_t frames = drmp3_get_pcm_frame_count(mp3.get());
    drmp3_uninit(mp3.get());
    requireAudio(r, frames, "MP3");
    r.durationSeconds = static_cast<double>(frames) / r.sampleRate;
    r.hashLength = fileSize;
    return r;
}

ProbeResult probeOgg(const fs::path& path, std::uint64_t fileSize)
{
    FilePtr file(openFileRead(path));
    if (!file) throw ProbeError("cannot open file");
    int error = 0;
    // close_handle_on_close = 0: the FilePtr owns the handle on every path.
    stb_vorbis* vorbis = stb_vorbis_open_file(file.get(), 0, &error, nullptr);
    if (!vorbis) throw ProbeError("cannot decode Ogg Vorbis header");
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    const std::uint64_t frames = stb_vorbis_stream_length_in_samples(vorbis);
    stb_vorbis_close(vorbis);
    ProbeResult r;
    r.format = AudioFormat::Ogg;
    r.sampleRate = static_cast<int>(info.sample_rate);
    r.channels = info.channels;
    requireAudio(r, frames, "Ogg Vorbis");
    r.durationSeconds = static_cast<double>(frames) / r.sampleRate;
    r.hashLength = fileSize;
    return r;
}

} // namespace

ProbeResult probeFile(const fs::path& path)
{
    const auto format = formatFromExtension(path);
    if (!format) throw ProbeError("unsupported file extension");

    std::error_code ec;
    const std::uint64_t size = fs::file_size(path, ec);
    if (ec) throw ProbeError("cannot read file size: " + ec.message());

    switch (*format) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: {
        FilePtr file(openFileRead(path));
        if (!file) throw ProbeError("cannot open file");
        return *format == AudioFormat::Wav ? detail::probeWav(file.get(), size)
                                           : detail::probeAiff(file.get(), size);
    }
    case AudioFormat::Flac: return probeFlac(path, size);
    case AudioFormat::Mp3: return probeMp3(path, size);
    case AudioFormat::Ogg: return probeOgg(path, size);
    }
    throw ProbeError("unsupported format");
}

} // namespace asma
```

- [ ] **Step 6: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[probe]"`

Expected: all 8 `[probe]` test cases pass. If the MP3 duration assertion fails,
print `mp3.durationSeconds` and check whether dr_mp3 counts the encoder delay;
widen the margin only if the value is within 0.1 s of 0.5, and note why in a
comment.

- [ ] **Step 7: Commit**

```sh
git add -A
git commit -m "core: probe WAV, AIFF, FLAC, MP3 and Ogg headers"
```

---

### Task 4: Content hash

**Files:**

- Create: `core/include/asma/core/ContentHash.h`, `core/src/ContentHash.cpp`,
  `tests/test_content_hash.cpp`

**Interfaces:**

- Consumes: `openFileRead`, `seekFile`, `FilePtr` (Task 1); `ProbeError`,
  `probeFile` (Task 3); test `writeWav` (Task 3).
- Produces:
  `std::string asma::contentHash(const std::filesystem::path&, std::uint64_t offset, std::uint64_t length)`
  returning 16 lowercase hex characters of XXH3-64 (seed 0). Throws `ProbeError`
  when the file cannot be opened or seeked. A file shorter than
  `offset + length` hashes whatever bytes exist.

- [ ] **Step 1: Write the failing tests**

`tests/test_content_hash.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/ContentHash.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

namespace {
std::string hashOf(const std::filesystem::path& p)
{
    const ProbeResult r = probeFile(p);
    return contentHash(p, r.hashOffset, r.hashLength);
}
} // namespace

TEST_CASE("Hash is 16 lowercase hex characters", "[hash]")
{
    TempDir dir;
    const auto p = dir.path() / "a.wav";
    test::writeWav(p, {});
    const std::string h = hashOf(p);
    REQUIRE(h.size() == 16);
    CHECK(h.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE("Hash of zero bytes is the XXH3-64 empty digest", "[hash]")
{
    TempDir dir;
    const auto p = dir.path() / "x.bin";
    test::writeBytes(p, "abc");
    CHECK(contentHash(p, 0, 0) == "2d06800538d394c2");
}

TEST_CASE("Metadata chunks do not change the hash", "[hash]")
{
    TempDir dir;
    test::WavSpec plain;
    test::WavSpec tagged;
    tagged.chunksBeforeData = {{"LIST", "INFOIART some artist"}};
    tagged.acid = std::make_pair(false, 120.0f);
    test::writeWav(dir.path() / "plain.wav", plain);
    test::writeWav(dir.path() / "tagged.wav", tagged);
    CHECK(hashOf(dir.path() / "plain.wav") == hashOf(dir.path() / "tagged.wav"));
}

TEST_CASE("Different audio gives a different hash", "[hash]")
{
    TempDir dir;
    test::WavSpec a;
    test::WavSpec b;
    b.seed = 2;
    test::writeWav(dir.path() / "a.wav", a);
    test::writeWav(dir.path() / "b.wav", b);
    CHECK(hashOf(dir.path() / "a.wav") != hashOf(dir.path() / "b.wav"));
}

TEST_CASE("A missing file throws ProbeError", "[hash]")
{
    TempDir dir;
    CHECK_THROWS_AS(contentHash(dir.path() / "nope.wav", 0, 10), ProbeError);
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/ContentHash.h' file not found`.

- [ ] **Step 3: Implement**

`core/include/asma/core/ContentHash.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace asma {

// XXH3-64 over [offset, offset + length) of the file, as 16 lowercase hex
// characters. Hashes whatever exists if the file is shorter. Throws ProbeError
// when the file cannot be opened or seeked.
std::string contentHash(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length);

} // namespace asma
```

`core/src/ContentHash.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ContentHash.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include <xxhash.h>

namespace asma {

std::string contentHash(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length)
{
    FilePtr file(openFileRead(path));
    if (!file) throw ProbeError("cannot open file for hashing");
    if (!seekFile(file.get(), offset)) throw ProbeError("cannot seek to the audio data");

    std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
    if (!state || XXH3_64bits_reset(state.get()) != XXH_OK) throw ProbeError("cannot initialise the hash");

    std::vector<unsigned char> buffer(1u << 20);
    std::uint64_t remaining = length;
    while (remaining > 0) {
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        const std::size_t got = std::fread(buffer.data(), 1, want, file.get());
        if (got == 0) break;
        XXH3_64bits_update(state.get(), buffer.data(), got);
        remaining -= got;
    }

    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx",
                  static_cast<unsigned long long>(XXH3_64bits_digest(state.get())));
    return hex;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[hash]"`

Expected: all 5 `[hash]` test cases pass.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: hash the audio payload with XXH3"
```

---

### Task 5: Filename metadata (tokens, BPM, key, loop)

**Files:**

- Create: `core/include/asma/core/NameParse.h`, `core/src/NameParse.cpp`,
  `tests/test_name_parse.cpp`

**Interfaces:**

- Consumes: nothing from earlier tasks.
- Produces:
  - `std::vector<std::string> asma::splitTokens(std::string_view)`: splits on
    anything that is not an ASCII letter, digit, `#` or a byte >= 0x80, and at
    letter/digit boundaries; preserves case.
  - `std::optional<std::string> asma::parseKeyToken(std::string_view token, std::string_view nextToken = {})`:
    canonical key, sharps only, minor suffixed `m` (`"C"`, `"F#"`, `"Am"`,
    `"C#m"`).
  - `struct asma::NameInfo { std::optional<double> bpm; std::optional<std::string> key; std::optional<bool> isLoop; std::vector<std::string> tokens; }`
    where `tokens` is lowercase, filename stem first, then folders nearest
    first.
  - `NameInfo asma::parseName(std::string_view relPath)` (`/` separators,
    extension included).

Rules the tests pin:

- Key: the letter must be `A`-`G`. `#` after it is sharp, `b` is flat. Quality
  is `m` (lowercase only), `min`, `minor`, `maj` or `major`, attached or as the
  next token. A bare letter with no accidental and no quality is not a key
  (`Kick_A` is a variant marker). A lowercase letter is only a key when the
  quality is a whole word (`amin`), so `am` and `eb` are not keys.
- BPM: a `bpm` token takes the number just before it, else just after it, if
  that number is 40 to 300. Stem first, then folders nearest first. Failing
  that, if the file is a loop, the last numeric token of the stem counts when it
  has 2 or 3 digits and is 60 to 200.
- Loop: `loop`/`loops` means loop; `oneshot`/`oneshots` or `one` followed by
  `shot`/`shots` means one-shot. The stem decides first, then the nearest folder
  that says anything.

- [ ] **Step 1: Write the failing tests**

`tests/test_name_parse.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/NameParse.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using Tokens = std::vector<std::string>;

TEST_CASE("splitTokens splits on separators and letter/digit boundaries", "[name]")
{
    CHECK(splitTokens("Kick01_hard-808") == Tokens{"Kick", "01", "hard", "808"});
    CHECK(splitTokens("F#min") == Tokens{"F#min"});
    CHECK(splitTokens("120bpm") == Tokens{"120", "bpm"});
    CHECK(splitTokens("Café Loops") == Tokens{"Café", "Loops"});
    CHECK(splitTokens("  __ ").empty());
}

TEST_CASE("parseKeyToken accepts real keys", "[name]")
{
    CHECK(parseKeyToken("Am") == "Am");
    CHECK(parseKeyToken("C#m") == "C#m");
    CHECK(parseKeyToken("Bb") == "A#");
    CHECK(parseKeyToken("Ebmaj") == "D#");
    CHECK(parseKeyToken("F#") == "F#");
    CHECK(parseKeyToken("Dmin") == "Dm");
    CHECK(parseKeyToken("Cb") == "B");
    CHECK(parseKeyToken("amin") == "Am");
    CHECK(parseKeyToken("E", "Minor") == "Em");
    CHECK(parseKeyToken("A", "major") == "A");
}

TEST_CASE("parseKeyToken rejects words and variant markers", "[name]")
{
    CHECK_FALSE(parseKeyToken("A").has_value());
    CHECK_FALSE(parseKeyToken("am").has_value());
    CHECK_FALSE(parseKeyToken("eb").has_value());
    CHECK_FALSE(parseKeyToken("AM").has_value());
    CHECK_FALSE(parseKeyToken("Kick").has_value());
    CHECK_FALSE(parseKeyToken("Abc").has_value());
    CHECK_FALSE(parseKeyToken("").has_value());
}

TEST_CASE("parseName reads BPM, key and loop from a typical loop", "[name]")
{
    const NameInfo n = parseName("Loops/Bass_Loop_Am_128.wav");
    CHECK(n.bpm == 128.0);
    CHECK(n.key == "Am");
    CHECK(n.isLoop == true);
    CHECK(n.tokens == Tokens{"bass", "loop", "am", "128", "loops"});
}

TEST_CASE("parseName BPM rules", "[name]")
{
    CHECK(parseName("Drums/Kick_120bpm.wav").bpm == 120.0);
    CHECK(parseName("Drums/BPM 95 Groove.wav").bpm == 95.0);
    CHECK(parseName("90 BPM Loops/Groove.wav").bpm == 90.0);
    CHECK_FALSE(parseName("Drums/Kick_01.wav").bpm.has_value());
    CHECK_FALSE(parseName("Loops/Drum_Loop_05.wav").bpm.has_value());
    CHECK_FALSE(parseName("Drums/Snare_128.wav").bpm.has_value()); // not a loop
}

TEST_CASE("parseName loop rules, stem before folders", "[name]")
{
    CHECK(parseName("One Shots/Snare.wav").isLoop == false);
    CHECK(parseName("Loops/Kick One Shot.wav").isLoop == false);
    CHECK(parseName("Oneshots/Loop Kit/Snare Loop.wav").isLoop == true);
    CHECK_FALSE(parseName("Drums/Snare.wav").isLoop.has_value());
}

TEST_CASE("parseName key rules", "[name]")
{
    CHECK(parseName("Pads/Pad F# Minor.wav").key == "F#m");
    CHECK_FALSE(parseName("One Shots/Snare_C.wav").key.has_value());
    CHECK(parseName("Cm/Stab.wav").key == "Cm");
}

TEST_CASE("parseName tolerates odd paths", "[name]")
{
    CHECK(parseName("").tokens.empty());
    CHECK(parseName(".hidden").tokens == Tokens{"hidden"});
    CHECK(parseName("no_extension").tokens == Tokens{"no", "extension"});
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/NameParse.h' file not found`.

- [ ] **Step 3: Implement**

`core/include/asma/core/NameParse.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

// Splits on anything that isn't an ASCII letter, digit, '#' or a non-ASCII
// byte, and at letter/digit boundaries. Case is preserved.
std::vector<std::string> splitTokens(std::string_view text);

// Canonical key: sharps only, minor keys suffixed with "m" ("C", "F#", "Am",
// "C#m"). nextToken lets "E Minor" be read as one key.
std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken = {});

struct NameInfo {
    std::optional<double> bpm;
    std::optional<std::string> key;
    std::optional<bool> isLoop;
    std::vector<std::string> tokens; // lowercase; stem first, then folders nearest first
};

// relPath: root-relative, '/' separators, extension included.
NameInfo parseName(std::string_view relPath);

} // namespace asma
```

`core/src/NameParse.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/NameParse.h"

#include <array>
#include <cctype>

namespace asma {

namespace {

using Group = std::vector<std::string>;

std::string toLower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isNumber(std::string_view s)
{
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::optional<double> numberInRange(std::string_view s, double lo, double hi)
{
    if (!isNumber(s) || s.size() > 3) return std::nullopt;
    const double value = std::stod(std::string(s));
    if (value < lo || value > hi) return std::nullopt;
    return value;
}

enum class Quality { None, Minor, Major };

// Returns the quality named by a whole word, if any.
std::optional<Quality> qualityWord(std::string_view s)
{
    const std::string lower = toLower(s);
    if (lower == "min" || lower == "minor") return Quality::Minor;
    if (lower == "maj" || lower == "major") return Quality::Major;
    return std::nullopt;
}

std::optional<bool> loopDecision(const Group& g)
{
    for (std::size_t i = 0; i < g.size(); ++i) {
        const std::string t = toLower(g[i]);
        if (t == "loop" || t == "loops") return true;
        if (t == "oneshot" || t == "oneshots") return false;
        if (t == "one" && i + 1 < g.size()) {
            const std::string next = toLower(g[i + 1]);
            if (next == "shot" || next == "shots") return false;
        }
    }
    return std::nullopt;
}

std::optional<double> bpmFromBpmToken(const Group& g)
{
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (toLower(g[i]) != "bpm") continue;
        if (i > 0)
            if (auto v = numberInRange(g[i - 1], 40, 300)) return v;
        if (i + 1 < g.size())
            if (auto v = numberInRange(g[i + 1], 40, 300)) return v;
    }
    return std::nullopt;
}

std::optional<double> trailingTempo(const Group& stem)
{
    for (auto it = stem.rbegin(); it != stem.rend(); ++it) {
        if (!isNumber(*it)) continue;
        if (it->size() < 2) return std::nullopt;
        return numberInRange(*it, 60, 200);
    }
    return std::nullopt;
}

} // namespace

std::vector<std::string> splitTokens(std::string_view text)
{
    enum class Kind { None, Alpha, Digit };
    std::vector<std::string> out;
    std::string current;
    Kind kind = Kind::None;
    auto flush = [&] {
        if (!current.empty()) out.push_back(current);
        current.clear();
        kind = Kind::None;
    };
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        Kind k;
        if (std::isdigit(u)) k = Kind::Digit;
        else if (std::isalpha(u) || c == '#' || u >= 0x80) k = Kind::Alpha;
        else {
            flush();
            continue;
        }
        if (kind != Kind::None && k != kind) flush();
        current.push_back(c);
        kind = k;
    }
    flush();
    return out;
}

std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken)
{
    if (token.empty()) return std::nullopt;
    static constexpr std::string_view kLetters = "CDEFGAB";
    static constexpr std::array<int, 7> kPitch = {0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<const char*, 12> kNames = {"C", "C#", "D", "D#", "E", "F",
                                                           "F#", "G", "G#", "A", "A#", "B"};

    const char letter = token[0];
    const auto index = kLetters.find(static_cast<char>(std::toupper(static_cast<unsigned char>(letter))));
    if (index == std::string_view::npos) return std::nullopt;
    int pitch = kPitch[index];

    std::string_view rest = token.substr(1);
    bool accidental = false;
    if (!rest.empty() && rest[0] == '#') {
        pitch += 1;
        accidental = true;
        rest.remove_prefix(1);
    } else if (!rest.empty() && rest[0] == 'b') {
        pitch += 11;
        accidental = true;
        rest.remove_prefix(1);
    }

    Quality quality = Quality::None;
    bool wholeWord = false;
    if (rest == "m") {
        quality = Quality::Minor; // lowercase only: "AM" is not A minor
    } else if (!rest.empty()) {
        const auto word = qualityWord(rest);
        if (!word) return std::nullopt;
        quality = *word;
        wholeWord = true;
    } else if (!nextToken.empty()) {
        if (const auto word = qualityWord(nextToken)) {
            quality = *word;
            wholeWord = true;
        }
    }

    if (!accidental && quality == Quality::None) return std::nullopt; // "Kick_A"
    if (!std::isupper(static_cast<unsigned char>(letter)) && !wholeWord) return std::nullopt; // "am", "eb"

    std::string key = kNames[static_cast<std::size_t>(pitch % 12)];
    if (quality == Quality::Minor) key += 'm';
    return key;
}

NameInfo parseName(std::string_view relPath)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= relPath.size()) {
        const auto slash = relPath.find('/', start);
        const auto end = slash == std::string_view::npos ? relPath.size() : slash;
        if (end > start) parts.push_back(relPath.substr(start, end - start));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    NameInfo info;
    if (parts.empty()) return info;

    const std::string_view file = parts.back();
    const auto dot = file.rfind('.');
    const std::string_view stem = (dot == std::string_view::npos || dot == 0) ? file : file.substr(0, dot);

    std::vector<Group> groups;
    groups.push_back(splitTokens(stem));
    for (auto it = parts.rbegin() + 1; it != parts.rend(); ++it) groups.push_back(splitTokens(*it));

    for (const auto& group : groups)
        for (const auto& token : group) info.tokens.push_back(toLower(token));

    for (const auto& group : groups) {
        if (!info.key) {
            for (std::size_t i = 0; i < group.size() && !info.key; ++i)
                info.key = parseKeyToken(group[i], i + 1 < group.size() ? std::string_view(group[i + 1])
                                                                        : std::string_view());
        }
        if (!info.isLoop) info.isLoop = loopDecision(group);
        if (!info.bpm) info.bpm = bpmFromBpmToken(group);
    }
    if (!info.bpm && info.isLoop == true) info.bpm = trailingTempo(groups.front());
    return info;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[name]"`

Expected: all 8 `[name]` test cases pass.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: parse BPM, key and loop hints from file names"
```

---

### Task 6: Instrument tag dictionary

**Files:**

- Create: `data/instrument_tokens.txt`, `core/src/InstrumentTokensData.cpp.in`,
  `core/include/asma/core/InstrumentTags.h`, `core/src/InstrumentTags.cpp`,
  `tests/test_instrument_tags.cpp`
- Modify: `core/CMakeLists.txt` (generate and compile the embedded data file)

**Interfaces:**

- Consumes: nothing.
- Produces:
  - `class asma::InstrumentDictionary` with
    `static InstrumentDictionary parse(std::string_view text)` (throws
    `std::invalid_argument` naming the line on malformed input),
    `static const InstrumentDictionary& builtin()`,
    `std::vector<std::string> tagsFor(const std::vector<std::string>& lowercaseTokens) const`
    (sorted, unique).

- [ ] **Step 1: Write the failing tests**

`tests/test_instrument_tags.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/InstrumentTags.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

using asma::InstrumentDictionary;
using Tags = std::vector<std::string>;

TEST_CASE("parse reads tag lines and ignores comments and blanks", "[tags]")
{
    const auto dict = InstrumentDictionary::parse("# comment\n\nkick: kick, bd\n  snare : Snare,sd  \n");
    CHECK(dict.tagsFor({"bd", "01"}) == Tags{"kick"});
    CHECK(dict.tagsFor({"snare"}) == Tags{"snare"});
    CHECK(dict.tagsFor({"sd", "kick", "bd"}) == Tags{"kick", "snare"});
    CHECK(dict.tagsFor({"pad"}).empty());
}

TEST_CASE("the first tag to claim a token wins", "[tags]")
{
    const auto dict = InstrumentDictionary::parse("drums: kick\nkick: kick\n");
    CHECK(dict.tagsFor({"kick"}) == Tags{"drums"});
}

TEST_CASE("malformed lines are rejected with their line number", "[tags]")
{
    try {
        InstrumentDictionary::parse("kick: kick\nthis line has no colon\n");
        FAIL("expected invalid_argument");
    } catch (const std::invalid_argument& e) {
        CHECK(std::string(e.what()).find("line 2") != std::string::npos);
    }
}

TEST_CASE("the built-in dictionary covers common drum names", "[tags]")
{
    const auto& dict = InstrumentDictionary::builtin();
    CHECK(dict.tagsFor({"bd"}) == Tags{"kick"});
    CHECK(dict.tagsFor({"hh"}) == Tags{"hihat"});
    CHECK(dict.tagsFor({"808"}) == Tags{"808"});
    CHECK(dict.tagsFor({"vox"}) == Tags{"vocal"});
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/InstrumentTags.h' file not found`.

- [ ] **Step 3: Add the data file and its build step**

`data/instrument_tokens.txt`:

```
# Instrument tag dictionary, embedded into asma-core at build time.
# Each line is "tag: token, token, ...". Tokens are matched against the
# lowercase words of a file name and its folders. The first tag to claim a
# token wins. Do not use the at-sign character anywhere in this file.
kick: kick, kicks, kik, bd, bassdrum
snare: snare, snares, snr, sd
clap: clap, claps, clp
hihat: hihat, hihats, hh, hat, hats
cymbal: cymbal, cymbals, crash, ride, splash
tom: tom, toms
perc: perc, percs, percussion, shaker, tambourine, conga, bongo, rim, rimshot, cowbell
808: 808, 808s
bass: bass, basses, sub, reese
synth: synth, synths
lead: lead, leads
pad: pad, pads
pluck: pluck, plucks
keys: keys, piano, rhodes, epiano, organ
guitar: guitar, guitars, gtr
strings: strings, violin, cello, viola
brass: brass, horn, horns, trumpet, trombone, sax
vocal: vocal, vocals, vox, acapella, chant
fx: fx, sfx, riser, risers, impact, impacts, sweep, downlifter, uplifter, noise
drums: drums, drum, beat, beats, breaks, break, top, tops
```

`core/src/InstrumentTokensData.cpp.in`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Generated by CMake from data/instrument_tokens.txt. Do not edit.
namespace asma::detail {
extern const char* const kInstrumentTokensData;
const char* const kInstrumentTokensData = R"ASMA_DATA(@ASMA_INSTRUMENT_TOKENS@)ASMA_DATA";
} // namespace asma::detail
```

In `core/CMakeLists.txt`, before `add_library(asma_core ...)`:

```cmake
set(ASMA_TOKENS_FILE ${PROJECT_SOURCE_DIR}/data/instrument_tokens.txt)
file(READ ${ASMA_TOKENS_FILE} ASMA_INSTRUMENT_TOKENS)
configure_file(src/InstrumentTokensData.cpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/InstrumentTokensData.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ASMA_TOKENS_FILE})
list(APPEND ASMA_CORE_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/InstrumentTokensData.cpp)
```

- [ ] **Step 4: Implement the dictionary**

`core/include/asma/core/InstrumentTags.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace asma {

class InstrumentDictionary {
public:
    // Lines of "tag: token, token". '#' starts a comment line. Throws
    // std::invalid_argument naming the line for malformed input.
    static InstrumentDictionary parse(std::string_view text);

    // Parsed from data/instrument_tokens.txt, embedded at build time.
    static const InstrumentDictionary& builtin();

    // Sorted, unique tags for the given lowercase tokens.
    std::vector<std::string> tagsFor(const std::vector<std::string>& lowercaseTokens) const;

private:
    std::unordered_map<std::string, std::string> tokenToTag_;
};

} // namespace asma
```

`core/src/InstrumentTags.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/InstrumentTags.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace asma {

namespace detail {
extern const char* const kInstrumentTokensData;
}

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

InstrumentDictionary InstrumentDictionary::parse(std::string_view text)
{
    InstrumentDictionary dict;
    int lineNumber = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto newline = text.find('\n', pos);
        const auto end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = trim(text.substr(pos, end - pos));
        ++lineNumber;
        pos = end + 1;

        if (!line.empty() && line.front() != '#') {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos || trim(line.substr(0, colon)).empty())
                throw std::invalid_argument("instrument dictionary line " + std::to_string(lineNumber)
                                            + ": expected 'tag: token, token'");
            const std::string tag = lower(trim(line.substr(0, colon)));
            std::string_view tokens = line.substr(colon + 1);
            while (!tokens.empty()) {
                const auto comma = tokens.find(',');
                const std::string token = lower(trim(tokens.substr(0, comma)));
                if (!token.empty()) dict.tokenToTag_.emplace(token, tag); // first claim wins
                if (comma == std::string_view::npos) break;
                tokens.remove_prefix(comma + 1);
            }
        }
        if (newline == std::string_view::npos) break;
    }
    return dict;
}

const InstrumentDictionary& InstrumentDictionary::builtin()
{
    static const InstrumentDictionary dict = parse(detail::kInstrumentTokensData);
    return dict;
}

std::vector<std::string> InstrumentDictionary::tagsFor(const std::vector<std::string>& lowercaseTokens) const
{
    std::set<std::string> tags;
    for (const auto& token : lowercaseTokens)
        if (auto it = tokenToTag_.find(token); it != tokenToTag_.end()) tags.insert(it->second);
    return {tags.begin(), tags.end()};
}

} // namespace asma
```

- [ ] **Step 5: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[tags]"`

Expected: all 4 `[tags]` test cases pass.

- [ ] **Step 6: Commit**

```sh
git add -A
git commit -m "core: add the instrument tag dictionary"
```

---

### Task 7: Library repository

**Files:**

- Create: `core/include/asma/core/Library.h`, `core/src/Library.cpp`,
  `tests/test_library.cpp`

**Interfaces:**

- Consumes: `Db`, `Statement`, `Transaction` (Task 2); `toUtf8`, `fromUtf8`
  (Task 1).
- Produces:
  - `struct asma::Root { std::int64_t id; std::string path; bool enabled; }`
  - `enum class asma::FileStatus { Ok, Missing, Failed }`
  - `enum class asma::TagSource { Auto, Embedded, User }`
  - `struct asma::FileRecord { std::int64_t id; std::int64_t rootId; std::string relPath; std::int64_t size; std::int64_t mtime; std::string contentHash; std::string format; int sampleRate; int channels; int bitDepth; double duration; FileStatus status; std::string failureReason; }`
  - `struct asma::DerivedInfo { std::optional<double> bpm; double bpmConfidence; std::optional<std::string> key; double keyConfidence; std::optional<bool> isLoop; std::optional<int> rootNote; std::vector<std::pair<std::string, TagSource>> tags; }`
  - `class asma::Library` (`explicit Library(Db&)`) with:
    `std::int64_t addRoot(const std::filesystem::path&)`,
    `std::vector<Root> roots()`, `std::optional<Root> root(std::int64_t)`,
    `std::vector<FileRecord> filesInRoot(std::int64_t)`,
    `std::optional<FileRecord> fileById(std::int64_t)`,
    `std::optional<FileRecord> fileByPath(std::int64_t rootId, std::string_view relPath)`,
    `std::vector<FileRecord> relinkCandidates(std::string_view hash, std::int64_t size)`,
    `std::int64_t insertFile(const FileRecord&)`,
    `void updateFile(const FileRecord&)`,
    `void setStatus(std::int64_t, FileStatus, std::string_view reason = {})`,
    `void resetAnalysis(std::int64_t)`,
    `void setDerived(std::int64_t, const DerivedInfo&)`,
    `std::optional<DerivedInfo> derived(std::int64_t)` (tags left empty),
    `void addUserTag(std::int64_t, std::string_view)`,
    `std::vector<std::pair<std::string, TagSource>> tags(std::int64_t)` (sorted
    by name).
  - Library methods never open their own transaction; callers group writes.

- [ ] **Step 1: Write the failing tests**

`tests/test_library.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

namespace {

FileRecord sampleRecord(std::int64_t rootId, std::string relPath, std::string hash = "00000000000000aa")
{
    FileRecord f;
    f.rootId = rootId;
    f.relPath = std::move(relPath);
    f.size = 100;
    f.mtime = 5;
    f.contentHash = std::move(hash);
    f.format = "wav";
    f.sampleRate = 44100;
    f.channels = 1;
    f.bitDepth = 16;
    f.duration = 0.1;
    return f;
}

std::int64_t ftsMatch(Db& db, const char* expr)
{
    auto q = db.prepare("SELECT rowid FROM fts_files WHERE fts_files MATCH ?");
    q.bind(1, expr);
    return q.step() ? q.getInt(0) : -1;
}

} // namespace

TEST_CASE("addRoot is idempotent for the same directory", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto a = lib.addRoot(dir.path());
    const auto b = lib.addRoot(dir.path() / "");
    CHECK(a == b);
    REQUIRE(lib.roots().size() == 1);
    CHECK(lib.roots()[0].enabled);
    CHECK(lib.root(a)->path == toUtf8(std::filesystem::weakly_canonical(dir.path())));
}

TEST_CASE("insertFile and fileById round-trip", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    FileRecord rec = sampleRecord(root, "Drums/Kick 01.wav", "");
    rec.status = FileStatus::Failed;
    rec.failureReason = "bad header";
    const auto id = lib.insertFile(rec);

    const auto back = lib.fileById(id);
    REQUIRE(back);
    CHECK(back->relPath == "Drums/Kick 01.wav");
    CHECK(back->contentHash.empty());
    CHECK(back->status == FileStatus::Failed);
    CHECK(back->failureReason == "bad header");
    CHECK(lib.fileByPath(root, "Drums/Kick 01.wav")->id == id);
    CHECK_FALSE(lib.fileById(id + 1).has_value());

    auto name = db.prepare("SELECT name FROM files WHERE id = ?");
    name.bind(1, id);
    REQUIRE(name.step());
    CHECK(name.getText(0) == "Kick 01.wav");
}

TEST_CASE("setDerived stores features and keeps user tags", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "Loops/Bass_Loop_Am_128.wav"));

    DerivedInfo d;
    d.bpm = 128.0;
    d.bpmConfidence = 0.9;
    d.key = "Am";
    d.keyConfidence = 0.9;
    d.isLoop = true;
    d.tags = {{"bass", TagSource::Auto}, {"kick", TagSource::Auto}};
    lib.setDerived(id, d);
    lib.addUserTag(id, "Punchy");
    lib.addUserTag(id, "kick"); // user claims an auto tag

    DerivedInfo again;
    again.tags = {{"snare", TagSource::Auto}};
    lib.setDerived(id, again);

    using Tags = std::vector<std::pair<std::string, TagSource>>;
    CHECK(lib.tags(id) == Tags{{"kick", TagSource::User}, {"punchy", TagSource::User}, {"snare", TagSource::Auto}});
    const auto features = lib.derived(id);
    REQUIRE(features);
    CHECK_FALSE(features->bpm.has_value()); // replaced by the second setDerived
    CHECK_FALSE(features->isLoop.has_value());
}

TEST_CASE("derived returns what setDerived stored", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    DerivedInfo d;
    d.bpm = 120.0;
    d.bpmConfidence = 1.0;
    d.isLoop = false;
    d.rootNote = 60;
    lib.setDerived(id, d);
    const auto back = lib.derived(id);
    REQUIRE(back);
    CHECK(back->bpm == 120.0);
    CHECK(back->bpmConfidence == 1.0);
    CHECK(back->isLoop == false);
    CHECK(back->rootNote == 60);
    CHECK_FALSE(back->key.has_value());
}

TEST_CASE("the FTS index follows names, folders and tags", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "Vintage Drums/Kick_01.wav"));
    lib.setDerived(id, {});
    CHECK(ftsMatch(db, "\"kick\"*") == id);
    CHECK(ftsMatch(db, "\"vint\"*") == id);
    CHECK(ftsMatch(db, "\"wav\"*") == -1); // extension is not indexed
    lib.addUserTag(id, "punchy");
    CHECK(ftsMatch(db, "\"punch\"*") == id);
}

TEST_CASE("relinkCandidates returns only missing rows with the same hash and size", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto live = lib.insertFile(sampleRecord(root, "a.wav", "00000000000000aa"));
    const auto gone = lib.insertFile(sampleRecord(root, "b.wav", "00000000000000aa"));
    const auto other = lib.insertFile(sampleRecord(root, "c.wav", "00000000000000bb"));
    lib.setStatus(gone, FileStatus::Missing);
    lib.setStatus(other, FileStatus::Missing);

    const auto candidates = lib.relinkCandidates("00000000000000aa", 100);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].id == gone);
    CHECK(lib.relinkCandidates("00000000000000aa", 101).empty());
    (void)live;
}

TEST_CASE("setStatus to ok clears the failure reason", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    lib.setStatus(id, FileStatus::Failed, "boom");
    CHECK(lib.fileById(id)->failureReason == "boom");
    lib.setStatus(id, FileStatus::Ok);
    CHECK(lib.fileById(id)->failureReason.empty());
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/Library.h' file not found`.

- [ ] **Step 3: Implement**

`core/include/asma/core/Library.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asma {

struct Root {
    std::int64_t id = 0;
    std::string path; // UTF-8, '/' separators, absolute
    bool enabled = true;
};

enum class FileStatus { Ok, Missing, Failed };
enum class TagSource { Auto, Embedded, User };

struct FileRecord {
    std::int64_t id = 0;
    std::int64_t rootId = 0;
    std::string relPath; // UTF-8, '/' separators
    std::int64_t size = 0;
    std::int64_t mtime = 0;
    std::string contentHash; // empty when unknown
    std::string format;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    double duration = 0.0;
    FileStatus status = FileStatus::Ok;
    std::string failureReason;
};

// Metadata derived from headers and file names (Plan 2 adds DSP analysis).
struct DerivedInfo {
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    std::optional<std::string> key;
    double keyConfidence = 0.0;
    std::optional<bool> isLoop;
    std::optional<int> rootNote;
    std::vector<std::pair<std::string, TagSource>> tags;
};

// Typed access to roots, files, features, tags and the FTS index. Methods do
// not open transactions; callers group writes.
class Library {
public:
    explicit Library(Db& db) : db_(db) {}

    // Returns the existing id when the directory is already a root.
    std::int64_t addRoot(const std::filesystem::path& dir);
    std::vector<Root> roots();
    std::optional<Root> root(std::int64_t id);

    std::vector<FileRecord> filesInRoot(std::int64_t rootId);
    std::optional<FileRecord> fileById(std::int64_t id);
    std::optional<FileRecord> fileByPath(std::int64_t rootId, std::string_view relPath);
    // Missing rows in any root whose content matches.
    std::vector<FileRecord> relinkCandidates(std::string_view contentHash, std::int64_t size);

    std::int64_t insertFile(const FileRecord& file);
    void updateFile(const FileRecord& file); // by file.id, every column
    void setStatus(std::int64_t fileId, FileStatus status, std::string_view reason = {});
    void resetAnalysis(std::int64_t fileId);

    // Replaces the features row and the auto/embedded tags; user tags stay.
    void setDerived(std::int64_t fileId, const DerivedInfo& info);
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty

    void addUserTag(std::int64_t fileId, std::string_view tag);
    std::vector<std::pair<std::string, TagSource>> tags(std::int64_t fileId);

private:
    std::int64_t ensureTag(std::string_view name);
    void refreshFts(std::int64_t fileId);

    Db& db_;
};

} // namespace asma
```

`core/src/Library.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Library.h"

#include "asma/core/Fs.h"

#include <cctype>

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr std::string_view kFileColumns =
    "id, root_id, rel_path, size, mtime, content_hash, format, sample_rate, channels, "
    "bit_depth, duration, status, failure_reason";

std::string_view statusText(FileStatus s)
{
    switch (s) {
    case FileStatus::Ok: return "ok";
    case FileStatus::Missing: return "missing";
    case FileStatus::Failed: return "failed";
    }
    return "ok";
}

FileStatus statusFromText(std::string_view s)
{
    if (s == "missing") return FileStatus::Missing;
    if (s == "failed") return FileStatus::Failed;
    return FileStatus::Ok;
}

std::string_view sourceText(TagSource s)
{
    switch (s) {
    case TagSource::Auto: return "auto";
    case TagSource::Embedded: return "embedded";
    case TagSource::User: return "user";
    }
    return "auto";
}

TagSource sourceFromText(std::string_view s)
{
    if (s == "user") return TagSource::User;
    if (s == "embedded") return TagSource::Embedded;
    return TagSource::Auto;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string baseName(std::string_view relPath)
{
    const auto slash = relPath.rfind('/');
    return std::string(slash == std::string_view::npos ? relPath : relPath.substr(slash + 1));
}

FileRecord readFile(const Statement& s)
{
    FileRecord f;
    f.id = s.getInt(0);
    f.rootId = s.getInt(1);
    f.relPath = s.getText(2);
    f.size = s.getInt(3);
    f.mtime = s.getInt(4);
    f.contentHash = s.getText(5);
    f.format = s.getText(6);
    f.sampleRate = static_cast<int>(s.getInt(7));
    f.channels = static_cast<int>(s.getInt(8));
    f.bitDepth = static_cast<int>(s.getInt(9));
    f.duration = s.getDouble(10);
    f.status = statusFromText(s.getText(11));
    f.failureReason = s.getText(12);
    return f;
}

std::vector<FileRecord> readFiles(Statement& s)
{
    std::vector<FileRecord> out;
    while (s.step()) out.push_back(readFile(s));
    return out;
}

// Binds the 12 data columns of a file row starting at parameter `first`.
void bindFileColumns(Statement& s, int first, const FileRecord& f)
{
    s.bind(first, f.rootId);
    s.bind(first + 1, std::string_view(f.relPath));
    s.bind(first + 2, std::string_view(baseName(f.relPath)));
    s.bind(first + 3, f.size);
    s.bind(first + 4, f.mtime);
    if (f.contentHash.empty()) s.bindNull(first + 5);
    else s.bind(first + 5, std::string_view(f.contentHash));
    s.bind(first + 6, std::string_view(f.format));
    s.bind(first + 7, f.sampleRate);
    s.bind(first + 8, f.channels);
    s.bind(first + 9, f.bitDepth);
    s.bind(first + 10, f.duration);
    s.bind(first + 11, statusText(f.status));
    if (f.failureReason.empty()) s.bindNull(first + 12);
    else s.bind(first + 12, std::string_view(f.failureReason));
}

} // namespace

std::int64_t Library::addRoot(const fs::path& dir)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::absolute(dir), ec);
    if (ec) canonical = fs::absolute(dir);
    if (!canonical.has_filename() && canonical != canonical.root_path()) canonical = canonical.parent_path();
    const std::string path = toUtf8(canonical);

    auto select = db_.prepare("SELECT id FROM roots WHERE path = ?");
    select.bind(1, std::string_view(path));
    if (select.step()) return select.getInt(0);

    auto insert = db_.prepare("INSERT INTO roots(path) VALUES (?)");
    insert.bind(1, std::string_view(path));
    insert.run();
    return db_.lastInsertId();
}

std::vector<Root> Library::roots()
{
    std::vector<Root> out;
    auto q = db_.prepare("SELECT id, path, enabled FROM roots ORDER BY id");
    while (q.step()) out.push_back({q.getInt(0), q.getText(1), q.getInt(2) != 0});
    return out;
}

std::optional<Root> Library::root(std::int64_t id)
{
    auto q = db_.prepare("SELECT id, path, enabled FROM roots WHERE id = ?");
    q.bind(1, id);
    if (!q.step()) return std::nullopt;
    return Root{q.getInt(0), q.getText(1), q.getInt(2) != 0};
}

std::vector<FileRecord> Library::filesInRoot(std::int64_t rootId)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE root_id = ?");
    q.bind(1, rootId);
    return readFiles(q);
}

std::optional<FileRecord> Library::fileById(std::int64_t id)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE id = ?");
    q.bind(1, id);
    if (!q.step()) return std::nullopt;
    return readFile(q);
}

std::optional<FileRecord> Library::fileByPath(std::int64_t rootId, std::string_view relPath)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE root_id = ? AND rel_path = ?");
    q.bind(1, rootId).bind(2, relPath);
    if (!q.step()) return std::nullopt;
    return readFile(q);
}

std::vector<FileRecord> Library::relinkCandidates(std::string_view contentHash, std::int64_t size)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns)
                         + " FROM files WHERE status = 'missing' AND content_hash = ? AND size = ? ORDER BY id");
    q.bind(1, contentHash).bind(2, size);
    return readFiles(q);
}

std::int64_t Library::insertFile(const FileRecord& file)
{
    auto q = db_.prepare("INSERT INTO files(root_id, rel_path, name, size, mtime, content_hash, format, "
                         "sample_rate, channels, bit_depth, duration, status, failure_reason) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    bindFileColumns(q, 1, file);
    q.run();
    const auto id = db_.lastInsertId();
    refreshFts(id);
    return id;
}

void Library::updateFile(const FileRecord& file)
{
    auto q = db_.prepare("UPDATE files SET root_id = ?, rel_path = ?, name = ?, size = ?, mtime = ?, "
                         "content_hash = ?, format = ?, sample_rate = ?, channels = ?, bit_depth = ?, "
                         "duration = ?, status = ?, failure_reason = ? WHERE id = ?");
    bindFileColumns(q, 1, file);
    q.bind(14, file.id);
    q.run();
    refreshFts(file.id);
}

void Library::setStatus(std::int64_t fileId, FileStatus status, std::string_view reason)
{
    auto q = db_.prepare("UPDATE files SET status = ?, failure_reason = ? WHERE id = ?");
    q.bind(1, statusText(status));
    if (reason.empty()) q.bindNull(2);
    else q.bind(2, reason);
    q.bind(3, fileId);
    q.run();
}

void Library::resetAnalysis(std::int64_t fileId)
{
    auto q = db_.prepare("UPDATE files SET analysis_version = 0 WHERE id = ?");
    q.bind(1, fileId);
    q.run();
}

void Library::setDerived(std::int64_t fileId, const DerivedInfo& info)
{
    auto features = db_.prepare(
        "INSERT INTO features(file_id, bpm, bpm_confidence, key, key_confidence, is_loop, root_note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(file_id) DO UPDATE SET bpm = excluded.bpm, bpm_confidence = excluded.bpm_confidence, "
        "key = excluded.key, key_confidence = excluded.key_confidence, is_loop = excluded.is_loop, "
        "root_note = excluded.root_note");
    features.bind(1, fileId);
    features.bindOptional(2, info.bpm);
    if (info.bpm) features.bind(3, info.bpmConfidence);
    else features.bindNull(3);
    if (info.key) features.bind(4, std::string_view(*info.key));
    else features.bindNull(4);
    if (info.key) features.bind(5, info.keyConfidence);
    else features.bindNull(5);
    features.bindOptional(6, info.isLoop);
    features.bindOptional(7, info.rootNote);
    features.run();

    auto clear = db_.prepare("DELETE FROM file_tags WHERE file_id = ? AND source IN ('auto', 'embedded')");
    clear.bind(1, fileId);
    clear.run();

    auto insert = db_.prepare("INSERT OR IGNORE INTO file_tags(file_id, tag_id, source) VALUES (?, ?, ?)");
    for (const auto& [name, source] : info.tags) {
        insert.bind(1, fileId).bind(2, ensureTag(name)).bind(3, sourceText(source));
        insert.run();
        insert.reset();
    }
    refreshFts(fileId);
}

std::optional<DerivedInfo> Library::derived(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT bpm, bpm_confidence, key, key_confidence, is_loop, root_note "
                         "FROM features WHERE file_id = ?");
    q.bind(1, fileId);
    if (!q.step()) return std::nullopt;
    DerivedInfo d;
    if (!q.isNull(0)) {
        d.bpm = q.getDouble(0);
        d.bpmConfidence = q.getDouble(1);
    }
    if (!q.isNull(2)) {
        d.key = q.getText(2);
        d.keyConfidence = q.getDouble(3);
    }
    if (!q.isNull(4)) d.isLoop = q.getInt(4) != 0;
    if (!q.isNull(5)) d.rootNote = static_cast<int>(q.getInt(5));
    return d;
}

void Library::addUserTag(std::int64_t fileId, std::string_view tag)
{
    auto q = db_.prepare("INSERT INTO file_tags(file_id, tag_id, source) VALUES (?, ?, 'user') "
                         "ON CONFLICT(file_id, tag_id) DO UPDATE SET source = 'user'");
    q.bind(1, fileId).bind(2, ensureTag(tag));
    q.run();
    refreshFts(fileId);
}

std::vector<std::pair<std::string, TagSource>> Library::tags(std::int64_t fileId)
{
    std::vector<std::pair<std::string, TagSource>> out;
    auto q = db_.prepare("SELECT t.name, ft.source FROM file_tags ft JOIN tags t ON t.id = ft.tag_id "
                         "WHERE ft.file_id = ? ORDER BY t.name");
    q.bind(1, fileId);
    while (q.step()) out.emplace_back(q.getText(0), sourceFromText(q.getText(1)));
    return out;
}

std::int64_t Library::ensureTag(std::string_view name)
{
    const std::string normalised = lower(name);
    auto insert = db_.prepare("INSERT OR IGNORE INTO tags(name) VALUES (?)");
    insert.bind(1, std::string_view(normalised));
    insert.run();
    auto select = db_.prepare("SELECT id FROM tags WHERE name = ?");
    select.bind(1, std::string_view(normalised));
    select.step();
    return select.getInt(0);
}

void Library::refreshFts(std::int64_t fileId)
{
    auto file = db_.prepare("SELECT rel_path FROM files WHERE id = ?");
    file.bind(1, fileId);
    if (!file.step()) return;
    const std::string relPath = file.getText(0);

    const auto slash = relPath.rfind('/');
    std::string folder = slash == std::string::npos ? std::string() : relPath.substr(0, slash);
    for (char& c : folder)
        if (c == '/') c = ' ';
    std::string name = baseName(relPath);
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) name.resize(dot);

    std::string tagText;
    for (const auto& [tag, source] : tags(fileId)) {
        if (!tagText.empty()) tagText += ' ';
        tagText += tag;
    }

    auto remove = db_.prepare("DELETE FROM fts_files WHERE rowid = ?");
    remove.bind(1, fileId);
    remove.run();
    auto insert = db_.prepare("INSERT INTO fts_files(rowid, name, folder, tags) VALUES (?, ?, ?, ?)");
    insert.bind(1, fileId)
        .bind(2, std::string_view(name))
        .bind(3, std::string_view(folder))
        .bind(4, std::string_view(tagText));
    insert.run();
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[library]"`

Expected: all 7 `[library]` test cases pass.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: add the library repository with tags and FTS sync"
```

---

### Task 8: Incremental scanner

**Files:**

- Create: `core/include/asma/core/Scanner.h`, `core/src/Scanner.cpp`,
  `tests/test_scanner.cpp`

**Interfaces:**

- Consumes: `Library`, `FileRecord`, `DerivedInfo`, `FileStatus`, `TagSource`
  (Task 7); `probeFile`, `formatFromExtension`, `formatName`, `ProbeError` (Task
  3); `contentHash` (Task 4); `parseName` (Task 5);
  `InstrumentDictionary::builtin()` (Task 6); `Transaction` (Task 2); `toUtf8`,
  `fromUtf8`, `fileTimeToInt` (Task 1).
- Produces:
  - `struct asma::ScanStats { std::size_t added, updated, unchanged, relinked, missing, failed; }`
  - `struct asma::ScanOptions { unsigned threads = 0; std::size_t batchSize = 200; std::function<void(std::string_view relPath)> onFileStart; std::function<void(std::size_t done, std::size_t total, std::string_view relPath)> onProgress; }`
  - `ScanStats asma::scanRoot(Db&, std::int64_t rootId, const ScanOptions& = {})`
    (throws `std::invalid_argument` for an unknown root, `DbError` on database
    failure)
  - `void asma::markFailedPath(Db&, std::int64_t rootId, std::string_view relPath, std::string_view reason)`

Behaviour the tests pin (from spec section 6):

- Hidden entries (name starts with `.`, including macOS `._` files and hidden
  folders) and non-audio extensions are ignored.
- Unchanged size and mtime: skipped (`unchanged`). This includes `failed` rows,
  which stay failed until the file changes. A `missing` row whose file is back
  unchanged becomes `ok` (`updated`).
- Changed size or mtime: re-probe and re-hash (`updated`); if the hash changed,
  `analysis_version` resets to 0.
- New path whose hash and size match a `missing` row in any root: that row is
  moved to the new path, keeping its id and user tags (`relinked`). Live rows
  are never re-link candidates.
- Known path not on disk: `missing`. Only done when the walk finished without
  errors, or the root directory itself is gone.
- Probe or hash failure: row stored as `failed` with the reason (`failed`).
- Derived info: ACID tempo (20 to 400) with confidence 1.0, else filename BPM at
  0.9; filename key at 0.9; loop from the ACID one-shot flag, else from the
  filename; root note from `smpl`; instrument tags from the builtin dictionary
  as `auto`.
- Callbacks may run on worker threads; calls are serialised.

- [ ] **Step 1: Write the failing tests**

`tests/test_scanner.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    TempDir dir;
    fs::path root = dir.path() / "lib";
    Db db = Db::openInMemory();
    Library lib{db};
    std::int64_t rootId = 0;

    Fixture()
    {
        fs::create_directories(root);
        rootId = lib.addRoot(root);
    }

    void wav(const std::string& rel, std::uint32_t seed = 1)
    {
        test::WavSpec spec;
        spec.seed = seed;
        test::writeWav(root / fromUtf8(rel), spec);
    }

    ScanStats scan(ScanOptions options = {}) { return scanRoot(db, rootId, options); }

    FileRecord file(const std::string& rel) { return lib.fileByPath(rootId, rel).value(); }
};

void touchLater(const fs::path& p)
{
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(2));
}

} // namespace

TEST_CASE("first scan adds audio files and ignores the rest", "[scanner]")
{
    Fixture f;
    f.wav("Drums/Kick_01.wav", 1);
    f.wav("Drums/Snare_01.wav", 2);
    f.wav("Loops/Bass_Loop_Am_128.wav", 3);
    test::writeBytes(f.root / "notes.txt", "hello");
    f.wav("Drums/._Kick_01.wav", 4);
    f.wav(".hidden/Secret.wav", 5);

    const ScanStats s = f.scan();
    CHECK(s.added == 3);
    CHECK(s.failed == 0);
    CHECK(f.lib.filesInRoot(f.rootId).size() == 3);
}

TEST_CASE("a second scan with no changes touches nothing", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.wav("b.wav", 2);
    f.scan();
    const ScanStats s = f.scan();
    CHECK(s.added == 0);
    CHECK(s.updated == 0);
    CHECK(s.unchanged == 2);
}

TEST_CASE("a changed file is re-hashed and its analysis reset", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.scan();
    const std::string before = f.file("a.wav").contentHash;
    f.db.exec("UPDATE files SET analysis_version = 3");

    f.wav("a.wav", 9);
    touchLater(f.root / "a.wav");
    const ScanStats s = f.scan();
    CHECK(s.updated == 1);
    CHECK(f.file("a.wav").contentHash != before);
    auto q = f.db.prepare("SELECT analysis_version FROM files");
    REQUIRE(q.step());
    CHECK(q.getInt(0) == 0);
}

TEST_CASE("a moved file keeps its id and user tags", "[scanner]")
{
    Fixture f;
    f.wav("Kick.wav", 1);
    f.scan();
    const auto id = f.file("Kick.wav").id;
    f.lib.addUserTag(id, "favourite");

    fs::create_directories(f.root / "Sorted");
    fs::rename(f.root / "Kick.wav", f.root / "Sorted" / "Kick.wav");
    const ScanStats s = f.scan();
    CHECK(s.relinked == 1);
    CHECK(s.added == 0);
    CHECK(s.missing == 0);
    const auto moved = f.file("Sorted/Kick.wav");
    CHECK(moved.id == id);
    CHECK(moved.status == FileStatus::Ok);
    const auto tags = f.lib.tags(id);
    CHECK(std::find(tags.begin(), tags.end(), std::make_pair(std::string("favourite"), TagSource::User))
          != tags.end());
}

TEST_CASE("a file moved to another root is re-linked there", "[scanner]")
{
    Fixture f;
    const fs::path other = f.dir.path() / "other";
    fs::create_directories(other);
    const auto otherId = f.lib.addRoot(other);
    f.wav("Kick.wav", 1);
    f.scan();
    const auto id = f.file("Kick.wav").id;

    fs::rename(f.root / "Kick.wav", other / "Kick.wav");
    f.scan();                              // marks it missing in the first root
    const ScanStats s = scanRoot(f.db, otherId);
    CHECK(s.relinked == 1);
    CHECK(f.lib.fileById(id)->rootId == otherId);
}

TEST_CASE("duplicate copies do not steal each other's rows", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 7);
    f.scan();
    fs::copy_file(f.root / "a.wav", f.root / "copy.wav");
    const ScanStats s = f.scan();
    CHECK(s.added == 1);
    CHECK(s.relinked == 0);
    CHECK(f.file("a.wav").status == FileStatus::Ok);
}

TEST_CASE("a deleted file becomes missing and comes back unchanged", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.scan();
    const auto id = f.file("a.wav").id;
    const fs::path stash = f.dir.path() / "a.wav";
    fs::rename(f.root / "a.wav", stash);

    CHECK(f.scan().missing == 1);
    CHECK(f.lib.fileById(id)->status == FileStatus::Missing);

    fs::rename(stash, f.root / "a.wav");
    const ScanStats back = f.scan();
    CHECK(back.updated == 1);
    CHECK(f.lib.fileById(id)->status == FileStatus::Ok);
}

TEST_CASE("an unmounted root marks everything missing without deleting rows", "[scanner]")
{
    Fixture f;
    f.wav("a.wav", 1);
    f.wav("b.wav", 2);
    f.scan();
    const auto id = f.file("a.wav").id;
    f.lib.addUserTag(id, "keeper");
    const fs::path away = f.dir.path() / "unplugged";
    fs::rename(f.root, away);

    CHECK(f.scan().missing == 2);
    CHECK(f.lib.filesInRoot(f.rootId).size() == 2);

    fs::rename(away, f.root);
    f.scan();
    CHECK(f.lib.fileById(id)->status == FileStatus::Ok);
    CHECK(f.lib.tags(id).size() == 1);
}

#ifndef _WIN32
TEST_CASE("an unreadable folder's files come back when readable again", "[scanner]")
{
    Fixture f;
    f.wav("Locked/a.wav", 1);
    f.scan();
    const auto id = f.file("Locked/a.wav").id;
    fs::permissions(f.root / "Locked", fs::perms::none);
    f.scan();
    fs::permissions(f.root / "Locked", fs::perms::owner_all);
    f.scan();
    CHECK(f.lib.fileById(id)->status == FileStatus::Ok);
}
#endif

TEST_CASE("broken files are failed with a reason and skipped until they change", "[scanner]")
{
    Fixture f;
    test::writeBytes(f.root / "broken.wav", "not audio");
    test::writeBytes(f.root / "empty.mp3", "");
    f.wav("good.wav", 1);

    const ScanStats first = f.scan();
    CHECK(first.failed == 2);
    CHECK(first.added == 1);
    CHECK(f.file("broken.wav").status == FileStatus::Failed);
    CHECK_FALSE(f.file("broken.wav").failureReason.empty());

    CHECK(f.scan().unchanged == 3);

    f.wav("broken.wav", 5);
    touchLater(f.root / "broken.wav");
    f.scan();
    CHECK(f.file("broken.wav").status == FileStatus::Ok);
}

TEST_CASE("markFailedPath makes later scans skip the file", "[scanner]")
{
    Fixture f;
    f.wav("crashy.wav", 1);
    markFailedPath(f.db, f.rootId, "crashy.wav", "crashed the scanner");
    CHECK(f.file("crashy.wav").status == FileStatus::Failed);
    const ScanStats s = f.scan();
    CHECK(s.unchanged == 1);
    CHECK(s.added == 0);
    CHECK(f.file("crashy.wav").failureReason == "crashed the scanner");
}

TEST_CASE("derived info comes from ACID, smpl and the file name", "[scanner]")
{
    Fixture f;
    f.wav("Loops/Bass_Loop_Am_128.wav", 1);
    test::WavSpec acid;
    acid.seed = 2;
    acid.acid = std::make_pair(true, 0.0f); // one-shot, no tempo
    acid.smplUnityNote = 36;
    test::writeWav(f.root / "Loops/Kick_Hit.wav", acid);
    f.scan();

    const auto loop = f.lib.derived(f.file("Loops/Bass_Loop_Am_128.wav").id).value();
    CHECK(loop.bpm == 128.0);
    CHECK(loop.bpmConfidence == 0.9);
    CHECK(loop.key == "Am");
    CHECK(loop.isLoop == true);
    const auto loopTags = f.lib.tags(f.file("Loops/Bass_Loop_Am_128.wav").id);
    CHECK(std::find(loopTags.begin(), loopTags.end(), std::make_pair(std::string("bass"), TagSource::Auto))
          != loopTags.end());

    const auto kick = f.lib.derived(f.file("Loops/Kick_Hit.wav").id).value();
    CHECK(kick.isLoop == false); // ACID flag beats the "Loops" folder
    CHECK_FALSE(kick.bpm.has_value());
    CHECK(kick.rootNote == 36);
}

TEST_CASE("progress callbacks cover every file and small batches work", "[scanner]")
{
    Fixture f;
    for (int i = 0; i < 5; ++i) f.wav("f" + std::to_string(i) + ".wav", static_cast<std::uint32_t>(i + 1));
    std::size_t starts = 0;
    std::size_t lastDone = 0;
    std::size_t lastTotal = 0;
    ScanOptions options;
    options.threads = 3;
    options.batchSize = 2;
    options.onFileStart = [&](std::string_view) { ++starts; };
    options.onProgress = [&](std::size_t done, std::size_t total, std::string_view) {
        lastDone = done;
        lastTotal = total;
    };
    const ScanStats s = f.scan(options);
    CHECK(s.added == 5);
    CHECK(starts == 5);
    CHECK(lastDone == 5);
    CHECK(lastTotal == 5);
}

TEST_CASE("non-ASCII paths are scanned and stored as UTF-8", "[scanner]")
{
    Fixture f;
    f.wav("Café Loops/Kick Ü.wav", 1);
    CHECK(f.scan().added == 1);
    CHECK(f.lib.fileByPath(f.rootId, "Café Loops/Kick Ü.wav").has_value());
}

TEST_CASE("an unknown root id throws", "[scanner]")
{
    Db db = Db::openInMemory();
    CHECK_THROWS_AS(scanRoot(db, 42), std::invalid_argument);
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/Scanner.h' file not found`.

- [ ] **Step 3: Implement**

`core/include/asma/core/Scanner.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace asma {

struct ScanStats {
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t unchanged = 0;
    std::size_t relinked = 0;
    std::size_t missing = 0;
    std::size_t failed = 0;
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

// Records that relPath crashed the scanner, so later scans skip it until its
// size or mtime changes. Creates the row if the file is not known yet.
void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

} // namespace asma
```

`core/src/Scanner.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Scanner.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/ContentHash.h"
#include "asma/core/Fs.h"
#include "asma/core/InstrumentTags.h"
#include "asma/core/Library.h"
#include "asma/core/NameParse.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace asma {

namespace {

struct DiskEntry {
    std::string relPath;
    std::int64_t size = 0;
    std::int64_t mtime = 0;
};

struct WalkResult {
    std::vector<DiskEntry> entries;
    bool complete = true; // false when the iterator itself failed part-way
};

WalkResult walk(const fs::path& root)
{
    WalkResult result;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) {
        result.complete = false;
        return result;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            result.complete = false;
            break;
        }
        const fs::directory_entry& entry = *it;
        const std::string name = toUtf8(entry.path().filename());
        std::error_code entryEc;
        if (!name.empty() && name.front() == '.') { // hidden files, "._" resource forks, hidden folders
            if (entry.is_directory(entryEc)) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(entryEc) || !formatFromExtension(entry.path())) continue;
        const auto size = entry.file_size(entryEc);
        if (entryEc) continue;
        const auto mtime = entry.last_write_time(entryEc);
        if (entryEc) continue;
        result.entries.push_back({toUtf8(entry.path().lexically_relative(root)), static_cast<std::int64_t>(size),
                                  fileTimeToInt(mtime)});
    }
    return result;
}

enum class JobKind { New, Changed };

struct Job {
    JobKind kind = JobKind::New;
    DiskEntry disk;
    std::optional<FileRecord> existing;
};

struct JobResult {
    std::optional<ProbeResult> probe;
    std::string hash;
    NameInfo name;
    std::string error;
};

JobResult process(const fs::path& root, const Job& job)
{
    JobResult r;
    r.name = parseName(job.disk.relPath);
    try {
        const fs::path full = root / fromUtf8(job.disk.relPath);
        r.probe = probeFile(full);
        r.hash = contentHash(full, r.probe->hashOffset, r.probe->hashLength);
    } catch (const std::exception& e) {
        r.probe.reset();
        r.error = e.what();
    }
    return r;
}

DerivedInfo derive(const JobResult& r)
{
    DerivedInfo d;
    const auto& probe = *r.probe;
    if (probe.acid && probe.acid->tempo >= 20.0f && probe.acid->tempo <= 400.0f) {
        d.bpm = probe.acid->tempo;
        d.bpmConfidence = 1.0;
    } else if (r.name.bpm) {
        d.bpm = r.name.bpm;
        d.bpmConfidence = 0.9;
    }
    if (r.name.key) {
        d.key = r.name.key;
        d.keyConfidence = 0.9;
    }
    d.isLoop = probe.acid ? std::optional<bool>(!probe.acid->oneShot) : r.name.isLoop;
    d.rootNote = probe.smplUnityNote;
    for (auto& tag : InstrumentDictionary::builtin().tagsFor(r.name.tokens)) d.tags.emplace_back(tag, TagSource::Auto);
    return d;
}

std::string formatOf(std::string_view relPath)
{
    const auto format = formatFromExtension(fromUtf8(relPath));
    return format ? std::string(formatName(*format)) : std::string("unknown");
}

FileRecord recordFrom(std::int64_t rootId, const DiskEntry& disk, const JobResult& r)
{
    FileRecord f;
    f.rootId = rootId;
    f.relPath = disk.relPath;
    f.size = disk.size;
    f.mtime = disk.mtime;
    if (r.probe) {
        f.contentHash = r.hash;
        f.format = std::string(formatName(r.probe->format));
        f.sampleRate = r.probe->sampleRate;
        f.channels = r.probe->channels;
        f.bitDepth = r.probe->bitDepth;
        f.duration = r.probe->durationSeconds;
        f.status = FileStatus::Ok;
    } else {
        f.format = formatOf(disk.relPath);
        f.status = FileStatus::Failed;
        f.failureReason = r.error;
    }
    return f;
}

void apply(Library& lib, std::int64_t rootId, const Job& job, const JobResult& r, ScanStats& stats,
           std::unordered_set<std::int64_t>& gone)
{
    FileRecord rec = recordFrom(rootId, job.disk, r);

    if (!r.probe) {
        if (job.existing) {
            rec.id = job.existing->id;
            rec.contentHash = job.existing->contentHash;
            lib.updateFile(rec);
        } else {
            lib.insertFile(rec);
        }
        ++stats.failed;
        return;
    }

    const DerivedInfo derived = derive(r);

    if (job.kind == JobKind::Changed) {
        rec.id = job.existing->id;
        lib.updateFile(rec);
        if (rec.contentHash != job.existing->contentHash) lib.resetAnalysis(rec.id);
        lib.setDerived(rec.id, derived);
        ++stats.updated;
        return;
    }

    const auto candidates = lib.relinkCandidates(rec.contentHash, rec.size);
    if (!candidates.empty()) {
        rec.id = candidates.front().id;
        lib.updateFile(rec);
        lib.setDerived(rec.id, derived);
        gone.erase(rec.id);
        ++stats.relinked;
        return;
    }

    rec.id = lib.insertFile(rec);
    lib.setDerived(rec.id, derived);
    ++stats.added;
}

} // namespace

ScanStats scanRoot(Db& db, std::int64_t rootId, const ScanOptions& options)
{
    Library lib(db);
    const auto root = lib.root(rootId);
    if (!root) throw std::invalid_argument("unknown root id " + std::to_string(rootId));
    const fs::path rootPath = fromUtf8(root->path);

    std::unordered_map<std::string, FileRecord> known;
    for (auto& file : lib.filesInRoot(rootId)) {
        std::string key = file.relPath;
        known.emplace(std::move(key), std::move(file));
    }

    std::error_code ec;
    const bool rootPresent = fs::is_directory(rootPath, ec);
    const WalkResult walked = rootPresent ? walk(rootPath) : WalkResult{{}, false};

    ScanStats stats;
    std::vector<Job> jobs;
    std::unordered_set<std::int64_t> gone;
    {
        Transaction tx(db);
        std::unordered_set<std::string> seen;
        for (const auto& disk : walked.entries) {
            seen.insert(disk.relPath);
            const auto it = known.find(disk.relPath);
            if (it == known.end()) {
                jobs.push_back({JobKind::New, disk, std::nullopt});
                continue;
            }
            const FileRecord& file = it->second;
            if (file.size == disk.size && file.mtime == disk.mtime) {
                if (file.status == FileStatus::Missing) {
                    lib.setStatus(file.id, FileStatus::Ok);
                    ++stats.updated;
                } else {
                    ++stats.unchanged; // failed files stay failed until they change
                }
                continue;
            }
            jobs.push_back({JobKind::Changed, disk, file});
        }
        // A partial walk must not mark the unwalked part of the library
        // missing. A root that is gone entirely (unplugged drive) is fine to
        // mark: rows are kept and come back on remount.
        if (!rootPresent || walked.complete) {
            for (const auto& [relPath, file] : known) {
                if (seen.count(relPath) || file.status == FileStatus::Missing) continue;
                lib.setStatus(file.id, FileStatus::Missing);
                gone.insert(file.id);
            }
        }
        tx.commit();
    }

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const unsigned threads = options.threads ? options.threads : hardware;
    const std::size_t batchSize = std::max<std::size_t>(1, options.batchSize);
    const std::size_t total = jobs.size();
    std::mutex callbackMutex;
    std::size_t done = 0;

    for (std::size_t start = 0; start < total; start += batchSize) {
        const std::size_t end = std::min(total, start + batchSize);
        std::vector<JobResult> results(end - start);
        std::atomic<std::size_t> next{start};

        auto worker = [&] {
            for (;;) {
                const std::size_t i = next.fetch_add(1);
                if (i >= end) return;
                if (options.onFileStart) {
                    std::lock_guard lock(callbackMutex);
                    options.onFileStart(jobs[i].disk.relPath);
                }
                results[i - start] = process(rootPath, jobs[i]);
                std::lock_guard lock(callbackMutex);
                ++done;
                if (options.onProgress) options.onProgress(done, total, jobs[i].disk.relPath);
            }
        };

        const auto poolSize = static_cast<unsigned>(std::min<std::size_t>(threads, end - start));
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < poolSize; ++t) pool.emplace_back(worker);
        worker();
        for (auto& thread : pool) thread.join();

        Transaction tx(db);
        for (std::size_t i = start; i < end; ++i) apply(lib, rootId, jobs[i], results[i - start], stats, gone);
        tx.commit();
    }

    stats.missing = gone.size();
    return stats;
}

void markFailedPath(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason)
{
    Library lib(db);
    const auto root = lib.root(rootId);
    if (!root) throw std::invalid_argument("unknown root id " + std::to_string(rootId));
    const fs::path full = fromUtf8(root->path) / fromUtf8(relPath);

    FileRecord rec;
    rec.rootId = rootId;
    rec.relPath = std::string(relPath);
    std::error_code ec;
    const auto size = fs::file_size(full, ec);
    rec.size = ec ? 0 : static_cast<std::int64_t>(size);
    const auto mtime = fs::last_write_time(full, ec);
    rec.mtime = ec ? 0 : fileTimeToInt(mtime);
    rec.format = formatOf(relPath);
    rec.status = FileStatus::Failed;
    rec.failureReason = std::string(reason);

    Transaction tx(db);
    if (const auto existing = lib.fileByPath(rootId, relPath)) {
        rec.id = existing->id;
        rec.contentHash = existing->contentHash;
        lib.updateFile(rec);
    } else {
        lib.insertFile(rec);
    }
    tx.commit();
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[scanner]"`

Expected: all `[scanner]` test cases pass (15 on macOS/Linux, 14 on Windows). If
"an unreadable folder" fails because the process runs as root (some Linux
containers ignore permissions), confirm with `id -u`; CI runners are not root.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: add the incremental scanner with re-linking"
```

---

### Task 9: Single-writer lock

**Files:**

- Create: `core/include/asma/core/WriterLock.h`, `core/src/WriterLock.cpp`,
  `tests/test_writer_lock.cpp`

**Interfaces:**

- Consumes: nothing beyond the standard library and OS APIs.
- Produces:
  - `class asma::WriterLock` (move-only) with
    `static std::optional<WriterLock> tryAcquire(const std::filesystem::path& dir)`,
    `static std::optional<std::int64_t> holder(const std::filesystem::path& dir)`,
    `static bool processAlive(std::int64_t pid)`,
    `static std::int64_t currentProcessId()`. The lock file is
    `<dir>/writer.lock` holding the owner's PID in decimal; the destructor
    removes it if it still names this process.

Rules: a lock naming a dead PID is taken over. A lock file with no readable PID
younger than 5 seconds counts as held (its owner may still be writing it); older
than that, it is stale.

- [ ] **Step 1: Write the failing tests**

`tests/test_writer_lock.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/WriterLock.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using asma::WriterLock;
using asma::test::TempDir;
namespace fs = std::filesystem;

TEST_CASE("acquire writes our pid and blocks a second acquire", "[lock]")
{
    TempDir dir;
    auto lock = WriterLock::tryAcquire(dir.path());
    REQUIRE(lock.has_value());
    CHECK(WriterLock::holder(dir.path()) == WriterLock::currentProcessId());
    CHECK_FALSE(WriterLock::tryAcquire(dir.path()).has_value());
}

TEST_CASE("the lock is released on destruction", "[lock]")
{
    TempDir dir;
    {
        auto lock = WriterLock::tryAcquire(dir.path());
        REQUIRE(lock.has_value());
    }
    CHECK_FALSE(fs::exists(dir.path() / "writer.lock"));
    CHECK(WriterLock::tryAcquire(dir.path()).has_value());
}

TEST_CASE("a lock left by a dead process is taken over", "[lock]")
{
    TempDir dir;
    asma::test::writeBytes(dir.path() / "writer.lock", "999999999");
    auto lock = WriterLock::tryAcquire(dir.path());
    REQUIRE(lock.has_value());
    CHECK(WriterLock::holder(dir.path()) == WriterLock::currentProcessId());
}

TEST_CASE("an unreadable lock is respected while fresh and taken when old", "[lock]")
{
    TempDir dir;
    const fs::path file = dir.path() / "writer.lock";
    asma::test::writeBytes(file, "");
    CHECK_FALSE(WriterLock::tryAcquire(dir.path()).has_value());
    fs::last_write_time(file, fs::last_write_time(file) - std::chrono::seconds(10));
    CHECK(WriterLock::tryAcquire(dir.path()).has_value());
}

TEST_CASE("processAlive", "[lock]")
{
    CHECK(WriterLock::processAlive(WriterLock::currentProcessId()));
    CHECK_FALSE(WriterLock::processAlive(0));
    CHECK_FALSE(WriterLock::processAlive(999999999));
}

TEST_CASE("a moved lock releases once", "[lock]")
{
    TempDir dir;
    auto first = WriterLock::tryAcquire(dir.path());
    REQUIRE(first.has_value());
    WriterLock moved = std::move(*first);
    first.reset(); // the moved-from lock must not remove the file
    CHECK(fs::exists(dir.path() / "writer.lock"));
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/WriterLock.h' file not found`.

- [ ] **Step 3: Implement**

`core/include/asma/core/WriterLock.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace asma {

// Only one process writes to a library at a time: the scanner or the
// standalone app's file operations. The lock is <dir>/writer.lock holding the
// owner's PID.
class WriterLock {
public:
    // Returns nullopt while another live process holds the lock. A lock left
    // by a dead process is taken over.
    static std::optional<WriterLock> tryAcquire(const std::filesystem::path& dir);
    static std::optional<std::int64_t> holder(const std::filesystem::path& dir);
    static bool processAlive(std::int64_t pid);
    static std::int64_t currentProcessId();

    ~WriterLock();
    WriterLock(WriterLock&& other) noexcept;
    WriterLock& operator=(WriterLock&& other) noexcept;
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;

private:
    explicit WriterLock(std::filesystem::path file) : file_(std::move(file)) {}
    void release() noexcept;

    std::filesystem::path file_;
};

} // namespace asma
```

`core/src/WriterLock.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/WriterLock.h"

#include <chrono>
#include <fstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr const char* kLockName = "writer.lock";

bool createExclusive(const fs::path& file, const std::string& content)
{
#ifdef _WIN32
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(handle);
    return true;
#else
    const int fd = ::open(file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) return false;
    const ssize_t written = ::write(fd, content.data(), content.size());
    (void)written;
    ::close(fd);
    return true;
#endif
}

} // namespace

std::int64_t WriterLock::currentProcessId()
{
#ifdef _WIN32
    return static_cast<std::int64_t>(GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(::getpid());
#endif
}

bool WriterLock::processAlive(std::int64_t pid)
{
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return ok && code == STILL_ACTIVE;
#else
    if (pid > std::int64_t{0x7fffffff}) return false;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

std::optional<std::int64_t> WriterLock::holder(const fs::path& dir)
{
    std::ifstream in(dir / kLockName);
    std::int64_t pid = 0;
    if (in >> pid) return pid;
    return std::nullopt;
}

std::optional<WriterLock> WriterLock::tryAcquire(const fs::path& dir)
{
    fs::create_directories(dir);
    const fs::path file = dir / kLockName;
    const std::string content = std::to_string(currentProcessId());

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (createExclusive(file, content)) return WriterLock(file);

        if (const auto pid = holder(dir)) {
            if (processAlive(*pid)) return std::nullopt;
        } else {
            // No readable PID: the owner may be between create and write.
            std::error_code ec;
            const auto written = fs::last_write_time(file, ec);
            if (!ec && fs::file_time_type::clock::now() - written < std::chrono::seconds(5)) return std::nullopt;
        }
        std::error_code ec;
        fs::remove(file, ec); // stale: take it over on the next attempt
    }
    return std::nullopt;
}

void WriterLock::release() noexcept
{
    if (file_.empty()) return;
    try {
        if (holder(file_.parent_path()) == currentProcessId()) {
            std::error_code ec;
            fs::remove(file_, ec);
        }
    } catch (...) {
    }
    file_.clear();
}

WriterLock::~WriterLock() { release(); }

WriterLock::WriterLock(WriterLock&& other) noexcept : file_(std::exchange(other.file_, {})) {}

WriterLock& WriterLock::operator=(WriterLock&& other) noexcept
{
    if (this != &other) {
        release();
        file_ = std::exchange(other.file_, {});
    }
    return *this;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[lock]"`

Expected: all 6 `[lock]` test cases pass.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: add the single-writer lock file"
```

---

### Task 10: Search

**Files:**

- Create: `core/include/asma/core/Query.h`, `core/src/Query.cpp`,
  `tests/test_query.cpp`

**Interfaces:**

- Consumes: `Db`, `Statement` (Task 2); `Library`, `FileRecord`, `DerivedInfo`,
  `TagSource` (Task 7, for seeding tests).
- Produces:
  - `enum class asma::SampleType { Any, Loop, OneShot }`
  - `enum class asma::SortField { Name, Bpm, Duration, Key }`
  - `struct asma::SearchModel { std::string text; SampleType type = SampleType::Any; std::optional<double> bpmMin, bpmMax; std::vector<std::string> keys; std::vector<std::string> tags; std::optional<double> durationMin, durationMax; std::vector<std::string> formats; std::optional<std::int64_t> rootId; SortField sort = SortField::Name; bool descending = false; int limit = 500; int offset = 0; }`
  - `struct asma::SearchRow { std::int64_t id; std::string rootPath; std::string relPath; std::string name; std::string format; double duration; std::optional<double> bpm; std::optional<std::string> key; std::optional<bool> isLoop; }`
  - `using asma::SqlParam = std::variant<std::int64_t, double, std::string>`
  - `struct asma::SqlQuery { std::string sql; std::vector<SqlParam> params; }`
  - `std::string asma::ftsMatchExpression(std::string_view text)` (empty when
    the text has no word characters)
  - `SqlQuery asma::buildSearchSql(const SearchModel&)`
  - `std::vector<SearchRow> asma::search(Db&, const SearchModel&)`

Semantics: only `ok` files in enabled roots. Text: every word must prefix-match
the file name, folder path or tags. `keys` and `formats` are OR within the list;
`tags` are AND. BPM and key sorts put unknown values last in both directions;
ties break on file id.

- [ ] **Step 1: Write the failing tests**

`tests/test_query.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using asma::test::TempDir;

namespace {

struct Seeded {
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib{db};
    std::int64_t root = lib.addRoot(dir.path() / "a");
    std::int64_t other = lib.addRoot(dir.path() / "b");

    std::int64_t add(std::int64_t rootId, const std::string& rel, double duration, std::optional<double> bpm,
                     std::optional<std::string> key, std::optional<bool> loop, std::vector<std::string> tags,
                     std::string format = "wav")
    {
        FileRecord f;
        f.rootId = rootId;
        f.relPath = rel;
        f.size = 1;
        f.mtime = 1;
        f.contentHash = "00000000000000aa";
        f.format = std::move(format);
        f.duration = duration;
        const auto id = lib.insertFile(f);
        DerivedInfo d;
        d.bpm = bpm;
        d.bpmConfidence = 0.9;
        d.key = std::move(key);
        d.keyConfidence = 0.9;
        d.isLoop = loop;
        for (auto& t : tags) d.tags.emplace_back(t, TagSource::Auto);
        lib.setDerived(id, d);
        return id;
    }

    std::int64_t kick, snare, bassLoop, padLoop, flacHit;

    Seeded()
    {
        kick = add(root, "Drums/Kick_01.wav", 0.4, std::nullopt, std::nullopt, false, {"kick"});
        snare = add(root, "Drums/snare_02.wav", 0.3, std::nullopt, std::nullopt, false, {"snare"});
        bassLoop = add(root, "Loops/Bass_Loop_Am_128.wav", 7.5, 128.0, "Am", true, {"bass"});
        padLoop = add(root, "Loops/Pad_Loop_C_90.wav", 10.6, 90.0, "C", true, {"pad", "synth"});
        flacHit = add(other, "Hits/Kick_Deep.flac", 0.8, std::nullopt, std::nullopt, false, {"kick"}, "flac");
    }
};

std::vector<std::int64_t> ids(const std::vector<SearchRow>& rows)
{
    std::vector<std::int64_t> out;
    for (const auto& r : rows) out.push_back(r.id);
    return out;
}

using Ids = std::vector<std::int64_t>;

} // namespace

TEST_CASE("ftsMatchExpression quotes each word as a prefix", "[query]")
{
    CHECK(ftsMatchExpression("kick 808") == "\"kick\"* \"808\"*");
    CHECK(ftsMatchExpression("  ") == "");
    CHECK(ftsMatchExpression("kick\" OR * -(x)") == "\"kick\"* \"OR\"* \"x\"*");
    CHECK(ftsMatchExpression("café") == "\"café\"*");
}

TEST_CASE("an empty model lists every ok file by name, case-insensitively", "[query]")
{
    Seeded s;
    CHECK(ids(search(s.db, {})) == Ids{s.bassLoop, s.kick, s.flacHit, s.padLoop, s.snare});
}

TEST_CASE("text search prefix-matches names, folders and tags", "[query]")
{
    Seeded s;
    SearchModel m;
    m.text = "kic";
    CHECK(ids(search(s.db, m)) == Ids{s.kick, s.flacHit});
    m.text = "loop bass";
    CHECK(ids(search(s.db, m)) == Ids{s.bassLoop});
    m.text = "synth";
    CHECK(ids(search(s.db, m)) == Ids{s.padLoop});
}

TEST_CASE("FTS syntax in the search text never throws", "[query]")
{
    Seeded s;
    for (const char* text : {"\"", "*", "-", "OR", "(", "kick\" OR *", "NEAR(", "   ", "\t\n"}) {
        INFO(text);
        SearchModel m;
        m.text = text;
        CHECK_NOTHROW(search(s.db, m));
    }
    SearchModel blank;
    blank.text = "   ";
    CHECK(search(s.db, blank).size() == 5);
}

TEST_CASE("facet filters", "[query]")
{
    Seeded s;
    SearchModel loops;
    loops.type = SampleType::Loop;
    CHECK(ids(search(s.db, loops)) == Ids{s.bassLoop, s.padLoop});

    SearchModel shots;
    shots.type = SampleType::OneShot;
    CHECK(search(s.db, shots).size() == 3);

    SearchModel bpm;
    bpm.bpmMin = 100.0;
    bpm.bpmMax = 130.0;
    CHECK(ids(search(s.db, bpm)) == Ids{s.bassLoop});

    SearchModel keys;
    keys.keys = {"Am", "C"};
    CHECK(ids(search(s.db, keys)) == Ids{s.bassLoop, s.padLoop});

    SearchModel tags;
    tags.tags = {"pad", "SYNTH"};
    CHECK(ids(search(s.db, tags)) == Ids{s.padLoop});

    SearchModel duration;
    duration.durationMax = 0.5;
    CHECK(ids(search(s.db, duration)) == Ids{s.kick, s.snare});

    SearchModel format;
    format.formats = {"flac"};
    CHECK(ids(search(s.db, format)) == Ids{s.flacHit});

    SearchModel root;
    root.rootId = s.other;
    CHECK(ids(search(s.db, root)) == Ids{s.flacHit});
}

TEST_CASE("missing, failed and disabled-root files are excluded", "[query]")
{
    Seeded s;
    s.lib.setStatus(s.kick, FileStatus::Missing);
    s.lib.setStatus(s.snare, FileStatus::Failed, "bad");
    auto disable = s.db.prepare("UPDATE roots SET enabled = 0 WHERE id = ?");
    disable.bind(1, s.other);
    disable.run();
    CHECK(ids(search(s.db, {})) == Ids{s.bassLoop, s.padLoop});
}

TEST_CASE("sorting and paging", "[query]")
{
    Seeded s;
    SearchModel byBpm;
    byBpm.sort = SortField::Bpm;
    byBpm.descending = true;
    const auto rows = search(s.db, byBpm);
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].id == s.bassLoop);
    CHECK(rows[1].id == s.padLoop);
    CHECK_FALSE(rows[4].bpm.has_value()); // unknown BPM sorts last

    SearchModel page;
    page.limit = 2;
    page.offset = 2;
    CHECK(ids(search(s.db, page)) == Ids{s.flacHit, s.padLoop});
}

TEST_CASE("rows carry what the CLI prints", "[query]")
{
    Seeded s;
    SearchModel m;
    m.text = "bass";
    const auto rows = search(s.db, m);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].relPath == "Loops/Bass_Loop_Am_128.wav");
    CHECK(rows[0].name == "Bass_Loop_Am_128.wav");
    CHECK(rows[0].format == "wav");
    CHECK(rows[0].bpm == 128.0);
    CHECK(rows[0].key == "Am");
    CHECK(rows[0].isLoop == true);
    CHECK(rows[0].rootPath == s.lib.root(s.root)->path);
}

// Hidden: run with ./build/tests/asma_tests "[.perf]" on a Release build.
TEST_CASE("search over 200k files", "[.perf]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const char* words[] = {"Kick", "Snare", "Hat", "Bass", "Pad", "Lead", "Vox", "FX"};
    {
        Transaction tx(db);
        for (int i = 0; i < 200000; ++i) {
            FileRecord f;
            f.rootId = root;
            f.relPath = std::string("Pack ") + std::to_string(i % 500) + "/" + words[i % 8] + "_"
                      + std::to_string(i) + ".wav";
            f.size = i;
            f.mtime = i;
            f.format = "wav";
            f.duration = (i % 100) / 10.0;
            lib.setDerived(lib.insertFile(f), {});
        }
        tx.commit();
    }
    SearchModel m;
    m.text = "kick";
    const auto start = std::chrono::steady_clock::now();
    const auto rows = search(db, m);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WARN("search took " << ms << " ms for " << rows.size() << " rows");
    CHECK(ms < 50.0);
}
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL, `'asma/core/Query.h' file not found`.

- [ ] **Step 3: Implement**

`core/include/asma/core/Query.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace asma {

enum class SampleType { Any, Loop, OneShot };
enum class SortField { Name, Bpm, Duration, Key };

struct SearchModel {
    std::string text;                  // every word must prefix-match name, folder or tags
    SampleType type = SampleType::Any;
    std::optional<double> bpmMin;
    std::optional<double> bpmMax;
    std::vector<std::string> keys;     // canonical keys, any of
    std::vector<std::string> tags;     // all of
    std::optional<double> durationMin; // seconds
    std::optional<double> durationMax;
    std::vector<std::string> formats;  // any of
    std::optional<std::int64_t> rootId;
    SortField sort = SortField::Name;
    bool descending = false;
    int limit = 500;
    int offset = 0;
};

struct SearchRow {
    std::int64_t id = 0;
    std::string rootPath;
    std::string relPath;
    std::string name;
    std::string format;
    double duration = 0.0;
    std::optional<double> bpm;
    std::optional<std::string> key;
    std::optional<bool> isLoop;
};

using SqlParam = std::variant<std::int64_t, double, std::string>;

struct SqlQuery {
    std::string sql;
    std::vector<SqlParam> params;
};

// Each word of the text as a quoted FTS5 prefix term. Empty when the text has
// no word characters, which means "no text filter".
std::string ftsMatchExpression(std::string_view text);

SqlQuery buildSearchSql(const SearchModel& model);
std::vector<SearchRow> search(Db& db, const SearchModel& model);

} // namespace asma
```

`core/src/Query.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"

#include <cctype>

namespace asma {

namespace {

bool isWordByte(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || u >= 0x80;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string placeholders(std::size_t count)
{
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += i ? ", ?" : "?";
    return out;
}

} // namespace

std::string ftsMatchExpression(std::string_view text)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !isWordByte(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && isWordByte(text[i])) ++i;
        if (i > start) {
            if (!out.empty()) out += ' ';
            out += '"';
            out.append(text.substr(start, i - start));
            out += "\"*";
        }
    }
    return out;
}

SqlQuery buildSearchSql(const SearchModel& m)
{
    SqlQuery q;
    q.sql = "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop "
            "FROM files f JOIN roots r ON r.id = f.root_id "
            "LEFT JOIN features ft ON ft.file_id = f.id "
            "WHERE f.status = 'ok' AND r.enabled = 1";

    if (const std::string match = ftsMatchExpression(m.text); !match.empty()) {
        q.sql += " AND f.id IN (SELECT rowid FROM fts_files WHERE fts_files MATCH ?)";
        q.params.emplace_back(match);
    }
    if (m.type == SampleType::Loop) q.sql += " AND ft.is_loop = 1";
    if (m.type == SampleType::OneShot) q.sql += " AND ft.is_loop = 0";
    if (m.bpmMin) {
        q.sql += " AND ft.bpm >= ?";
        q.params.emplace_back(*m.bpmMin);
    }
    if (m.bpmMax) {
        q.sql += " AND ft.bpm <= ?";
        q.params.emplace_back(*m.bpmMax);
    }
    if (!m.keys.empty()) {
        q.sql += " AND ft.key IN (" + placeholders(m.keys.size()) + ")";
        for (const auto& key : m.keys) q.params.emplace_back(key);
    }
    for (const auto& tag : m.tags) {
        q.sql += " AND f.id IN (SELECT x.file_id FROM file_tags x JOIN tags t ON t.id = x.tag_id WHERE t.name = ?)";
        q.params.emplace_back(lower(tag));
    }
    if (m.durationMin) {
        q.sql += " AND f.duration >= ?";
        q.params.emplace_back(*m.durationMin);
    }
    if (m.durationMax) {
        q.sql += " AND f.duration <= ?";
        q.params.emplace_back(*m.durationMax);
    }
    if (!m.formats.empty()) {
        q.sql += " AND f.format IN (" + placeholders(m.formats.size()) + ")";
        for (const auto& format : m.formats) q.params.emplace_back(lower(format));
    }
    if (m.rootId) {
        q.sql += " AND f.root_id = ?";
        q.params.emplace_back(*m.rootId);
    }

    const std::string direction = m.descending ? " DESC" : " ASC";
    std::string order;
    switch (m.sort) {
    case SortField::Name: order = "f.name COLLATE NOCASE" + direction; break;
    case SortField::Bpm: order = "ft.bpm IS NULL, ft.bpm" + direction; break;
    case SortField::Duration: order = "f.duration" + direction; break;
    case SortField::Key: order = "ft.key IS NULL, ft.key" + direction; break;
    }
    q.sql += " ORDER BY " + order + ", f.id LIMIT ? OFFSET ?";
    q.params.emplace_back(static_cast<std::int64_t>(m.limit));
    q.params.emplace_back(static_cast<std::int64_t>(m.offset));
    return q;
}

std::vector<SearchRow> search(Db& db, const SearchModel& model)
{
    const SqlQuery q = buildSearchSql(model);
    Statement s = db.prepare(q.sql);
    for (std::size_t i = 0; i < q.params.size(); ++i) {
        const int index = static_cast<int>(i) + 1;
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string>) s.bind(index, std::string_view(value));
            else s.bind(index, value);
        }, q.params[i]);
    }

    std::vector<SearchRow> rows;
    while (s.step()) {
        SearchRow r;
        r.id = s.getInt(0);
        r.rootPath = s.getText(1);
        r.relPath = s.getText(2);
        r.name = s.getText(3);
        r.format = s.getText(4);
        r.duration = s.getDouble(5);
        if (!s.isNull(6)) r.bpm = s.getDouble(6);
        if (!s.isNull(7)) r.key = s.getText(7);
        if (!s.isNull(8)) r.isLoop = s.getInt(8) != 0;
        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace asma
```

Add `#include <type_traits>` to `Query.cpp`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build && ./build/tests/asma_tests "[query]"`

Expected: all 8 `[query]` test cases pass. Then check the performance target on
a Release build:

```sh
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/tests/asma_tests "[.perf]"
```

Expected: PASS with a warning line like `search took 4.2 ms`. If it exceeds 50
ms, check that the FTS subquery is used (`EXPLAIN QUERY PLAN`) before changing
anything else.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: add search with FTS prefix matching and facets"
```

---

### Task 11: JSON lines, the `asma` CLI and the `asma-scan` worker

**Files:**

- Create: `core/include/asma/core/Json.h`, `core/src/Json.cpp`, `apps/Args.h`,
  `apps/Args.cpp`, `apps/CliCommon.h`, `apps/CliCommon.cpp`,
  `apps/asma_scan_main.cpp`, `docs/scan-protocol.md`, `tests/test_json.cpp`,
  `tests/test_args.cpp`, `tests/test_cli_e2e.cpp`
- Modify: `apps/CMakeLists.txt`, `apps/asma_main.cpp` (replace),
  `tests/CMakeLists.txt`, `README.md`

**Interfaces:**

- Consumes: everything above: `Db`, `Library`, `scanRoot`, `markFailedPath`,
  `ScanStats`, `ScanOptions`, `WriterLock`, `SearchModel`, `search`,
  `parseKeyToken`, `defaultDataDir`, `toUtf8`, `fromUtf8`.
- Produces:
  - `class asma::JsonLine` with `str(key, std::string_view)`,
    `num(key, std::int64_t)`, `real(key, double)` (non-finite becomes `null`),
    `boolean(key, bool)`, `null(key)`, `std::string build() const`;
    `std::string asma::jsonEscape(std::string_view)`.
  - `class asma::cli::Args` with `static Args fromMain(int, char**)` (UTF-8 on
    Windows via `GetCommandLineW`), `explicit Args(std::vector<std::string>)`,
    `bool flag(std::string_view)`,
    `std::optional<std::string> option(std::string_view)`,
    `std::vector<std::string> options(std::string_view)`,
    `std::optional<std::string> positional()`,
    `std::vector<std::string> rest() const`; `class asma::cli::UsageError`.
  - `std::filesystem::path asma::cli::resolveDbPath(Args&)` (`--db` or
    `defaultDataDir() / "library.db"`), `void asma::cli::setupConsole()`.
  - Executables `asma` and `asma-scan` with exit codes 0 ok, 1 error, 2 usage, 3
    library locked.
  - `docs/scan-protocol.md`: the contract Plan 3's supervisor builds on.

- [ ] **Step 1: Write the failing JSON and Args tests**

`tests/test_json.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Json.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using asma::JsonLine;

TEST_CASE("jsonEscape handles quotes, backslashes and control characters", "[json]")
{
    CHECK(asma::jsonEscape("a\"b\\c") == "a\\\"b\\\\c");
    CHECK(asma::jsonEscape("line\nnext\ttab") == "line\\nnext\\ttab");
    CHECK(asma::jsonEscape(std::string("\x01", 1)) == "\\u0001");
    CHECK(asma::jsonEscape("Café") == "Café");
}

TEST_CASE("JsonLine builds an object in insertion order", "[json]")
{
    const std::string line = JsonLine()
                                 .str("event", "progress")
                                 .num("done", 3)
                                 .real("bpm", 128.5)
                                 .boolean("loop", true)
                                 .null("key")
                                 .build();
    CHECK(line == R"({"event":"progress","done":3,"bpm":128.5,"loop":true,"key":null})");
}

TEST_CASE("non-finite numbers become null", "[json]")
{
    CHECK(JsonLine().real("x", std::nan("")).build() == R"({"x":null})");
    CHECK(JsonLine().build() == "{}");
}
```

`tests/test_args.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"

#include <catch2/catch_test_macros.hpp>

using asma::cli::Args;
using asma::cli::UsageError;
using Strings = std::vector<std::string>;

TEST_CASE("options, flags and positionals", "[args]")
{
    Args args({"--db", "/x/lib.db", "query", "kick", "--key=Am", "--key", "C", "--json", "808"});
    CHECK(args.option("db") == "/x/lib.db");
    CHECK(args.positional() == "query");
    CHECK(args.options("key") == Strings{"Am", "C"});
    CHECK(args.flag("json"));
    CHECK_FALSE(args.flag("json"));
    CHECK(args.rest() == Strings{"kick", "808"});
}

TEST_CASE("an option without a value is a usage error", "[args]")
{
    Args args({"scan", "--root"});
    CHECK_THROWS_AS(args.option("root"), UsageError);
}

TEST_CASE("unknown options stay in rest for the caller to reject", "[args]")
{
    Args args({"--bogus", "x"});
    CHECK(args.rest() == Strings{"--bogus", "x"});
}
```

Update `tests/CMakeLists.txt`: link the CLI support library and point the e2e
test at the executables. Replace the `target_link_libraries` and
`target_compile_definitions` lines with:

```cmake
target_link_libraries(asma_tests PRIVATE asma::core asma_cli_support Catch2::Catch2WithMain)
target_compile_definitions(asma_tests PRIVATE
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  ASMA_VERSION="${PROJECT_VERSION}"
  ASMA_CLI_PATH="$<TARGET_FILE:asma>"
  ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>")
add_dependencies(asma_tests asma asma-scan)
```

- [ ] **Step 2: Build to see the failure**

Run: `cmake --build build`

Expected: FAIL. CMake reports that `asma_cli_support` and `asma-scan` do not
exist yet, or the compiler reports `'asma/core/Json.h' file not found`.

- [ ] **Step 3: Implement Json, Args and CliCommon**

`core/include/asma/core/Json.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace asma {

// Escapes for use inside a JSON string. UTF-8 passes through unchanged.
std::string jsonEscape(std::string_view text);

// Builds one flat JSON object, keys in insertion order. Distinct method names
// avoid overload surprises (a string literal would otherwise pick bool).
class JsonLine {
public:
    JsonLine& str(std::string_view key, std::string_view value);
    JsonLine& num(std::string_view key, std::int64_t value);
    JsonLine& real(std::string_view key, double value); // NaN/inf become null
    JsonLine& boolean(std::string_view key, bool value);
    JsonLine& null(std::string_view key);
    std::string build() const;

private:
    void key(std::string_view name);
    std::string body_;
};

} // namespace asma
```

`core/src/Json.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Json.h"

#include <cmath>
#include <cstdio>

namespace asma {

std::string jsonEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void JsonLine::key(std::string_view name)
{
    if (!body_.empty()) body_ += ',';
    body_ += '"';
    body_ += jsonEscape(name);
    body_ += "\":";
}

JsonLine& JsonLine::str(std::string_view k, std::string_view value)
{
    key(k);
    body_ += '"';
    body_ += jsonEscape(value);
    body_ += '"';
    return *this;
}

JsonLine& JsonLine::num(std::string_view k, std::int64_t value)
{
    key(k);
    body_ += std::to_string(value);
    return *this;
}

JsonLine& JsonLine::real(std::string_view k, double value)
{
    key(k);
    if (!std::isfinite(value)) {
        body_ += "null";
        return *this;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", value);
    body_ += buf;
    return *this;
}

JsonLine& JsonLine::boolean(std::string_view k, bool value)
{
    key(k);
    body_ += value ? "true" : "false";
    return *this;
}

JsonLine& JsonLine::null(std::string_view k)
{
    key(k);
    body_ += "null";
    return *this;
}

std::string JsonLine::build() const { return "{" + body_ + "}"; }

} // namespace asma
```

`apps/Args.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace asma::cli {

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Minimal argv parser. Options are "--name value" or "--name=value", flags are
// "--name". Each accessor removes what it consumed, so take global options
// first, then the command, then command options, then rest().
class Args {
public:
    // UTF-8 arguments on every platform (Windows reads GetCommandLineW).
    static Args fromMain(int argc, char** argv);
    explicit Args(std::vector<std::string> args) : args_(std::move(args)) {}

    bool flag(std::string_view name);
    std::optional<std::string> option(std::string_view name);   // last occurrence
    std::vector<std::string> options(std::string_view name);    // every occurrence
    std::optional<std::string> positional();                    // first non-option
    std::vector<std::string> rest() const { return args_; }

private:
    std::vector<std::string> args_;
};

} // namespace asma::cli
```

`apps/Args.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace asma::cli {

namespace {

#ifdef _WIN32
std::string narrow(const wchar_t* wide)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    return out;
}
#endif

} // namespace

Args Args::fromMain(int argc, char** argv)
{
    std::vector<std::string> args;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) args.push_back(narrow(wide[i]));
    LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return Args(std::move(args));
}

bool Args::flag(std::string_view name)
{
    const std::string wanted = "--" + std::string(name);
    bool found = false;
    for (auto it = args_.begin(); it != args_.end();) {
        if (*it == wanted) {
            it = args_.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    return found;
}

std::vector<std::string> Args::options(std::string_view name)
{
    const std::string wanted = "--" + std::string(name);
    const std::string prefix = wanted + "=";
    std::vector<std::string> values;
    for (auto it = args_.begin(); it != args_.end();) {
        if (*it == wanted) {
            if (it + 1 == args_.end()) throw UsageError("option " + wanted + " needs a value");
            values.push_back(*(it + 1));
            it = args_.erase(it, it + 2);
        } else if (it->rfind(prefix, 0) == 0) {
            values.push_back(it->substr(prefix.size()));
            it = args_.erase(it);
        } else {
            ++it;
        }
    }
    return values;
}

std::optional<std::string> Args::option(std::string_view name)
{
    auto values = options(name);
    if (values.empty()) return std::nullopt;
    return values.back();
}

std::optional<std::string> Args::positional()
{
    for (auto it = args_.begin(); it != args_.end(); ++it) {
        if (it->rfind("--", 0) == 0) continue;
        std::string value = *it;
        args_.erase(it);
        return value;
    }
    return std::nullopt;
}

} // namespace asma::cli
```

`apps/CliCommon.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include <filesystem>

namespace asma::cli {

enum ExitCode { kOk = 0, kError = 1, kUsage = 2, kLocked = 3 };

// --db when given, else defaultDataDir() / "library.db".
std::filesystem::path resolveDbPath(Args& args);

// UTF-8 console output on Windows; no-op elsewhere.
void setupConsole();

} // namespace asma::cli
```

`apps/CliCommon.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "CliCommon.h"

#include "asma/core/Fs.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace asma::cli {

std::filesystem::path resolveDbPath(Args& args)
{
    if (auto db = args.option("db")) return fromUtf8(*db);
    return defaultDataDir() / "library.db";
}

void setupConsole()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

} // namespace asma::cli
```

`apps/CMakeLists.txt` (replace):

```cmake
# SPDX-License-Identifier: GPL-3.0-only
add_library(asma_cli_support STATIC Args.cpp CliCommon.cpp)
target_include_directories(asma_cli_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asma_cli_support PUBLIC asma::core)
if(WIN32)
  target_link_libraries(asma_cli_support PUBLIC shell32)
endif()
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp)
target_link_libraries(asma PRIVATE asma_cli_support)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)

add_executable(asma-scan asma_scan_main.cpp)
target_link_libraries(asma-scan PRIVATE asma_cli_support)
asma_set_warnings(asma-scan)
```

Create a placeholder `apps/asma_scan_main.cpp` with `int main() { return 0; }`
(plus the SPDX line) so the build links; Step 6 replaces it.

- [ ] **Step 4: Run the JSON and Args tests**

Run: `cmake --build build && ./build/tests/asma_tests "[json],[args]"`

Expected: all 6 test cases pass.

- [ ] **Step 5: Write the failing end-to-end tests**

`tests/test_cli_e2e.cpp`:

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
#else
#include <sys/wait.h>
#endif

using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct RunResult {
    int exitCode = -1;
    std::string out;
};

// UTF-8 text of a path with native separators, in double quotes.
std::string quote(const fs::path& p)
{
    const std::u8string u8 = fs::path(p).make_preferred().u8string();
    return "\"" + std::string(reinterpret_cast<const char*>(u8.data()), u8.size()) + "\"";
}

int systemUtf8(const std::string& command)
{
#ifdef _WIN32
    // cmd.exe strips one pair of outer quotes, and the narrow system() would
    // mangle non-ASCII text, so wrap the command and go through UTF-16.
    const std::string wrapped = "\"" + command + "\"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, wrapped.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, wrapped.c_str(), -1, wide.data(), size);
    return _wsystem(wide.c_str());
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// Runs an executable with arguments, capturing stdout.
RunResult run(const char* exe, const std::string& arguments, const fs::path& scratch)
{
    const fs::path outFile = scratch / "stdout.txt";
    RunResult r;
    r.exitCode = systemUtf8(quote(asma::fromUtf8(exe)) + " " + arguments + " > " + quote(outFile));
    std::ifstream in(outFile, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    r.out = ss.str();
    return r;
}

struct Cli {
    TempDir dir;
    fs::path lib = dir.path() / asma::fromUtf8("Café Samples");
    fs::path db = dir.path() / "data" / "library.db";

    Cli()
    {
        asma::test::WavSpec loop;
        loop.seed = 1;
        asma::test::writeWav(lib / "Loops" / "Bass_Loop_Am_128.wav", loop);
        asma::test::WavSpec kick;
        kick.seed = 2;
        asma::test::writeWav(lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav"), kick);
    }

    RunResult runAsma(const std::string& args) { return run(ASMA_CLI_PATH, "--db " + quote(db) + " " + args, dir.path()); }
    RunResult runScan(const std::string& args)
    {
        return run(ASMA_SCAN_PATH, "--db " + quote(db) + " " + args, dir.path());
    }
};

} // namespace

TEST_CASE("asma --version", "[e2e]")
{
    TempDir dir;
    const RunResult r = run(ASMA_CLI_PATH, "--version", dir.path());
    CHECK(r.exitCode == 0);
    CHECK(r.out.find(std::string("asma ") + ASMA_VERSION) != std::string::npos);
}

TEST_CASE("root add, scan and query from the CLI, with non-ASCII paths", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult list = cli.runAsma("root list");
    CHECK(list.out.find("Café Samples") != std::string::npos);

    REQUIRE(cli.runAsma("scan").exitCode == 0);

    const RunResult kick = cli.runAsma("query kick");
    CHECK(kick.exitCode == 0);
    CHECK(kick.out.find("Kick Ü_01.wav") != std::string::npos);
    CHECK(kick.out.find("Bass_Loop") == std::string::npos);

    const RunResult loops = cli.runAsma("query --type loop --key Am --json");
    CHECK(loops.out.find("\"bpm\":128") != std::string::npos);
    CHECK(loops.out.find("\"key\":\"Am\"") != std::string::npos);
}

TEST_CASE("asma-scan speaks JSON lines and honours --fail", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult r = cli.runScan("--root 1 --fail " + quote(asma::fromUtf8("Drums/Kick Ü_01.wav")));
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("{\"event\":\"marked_failed\"") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"start\",\"path\":\"Loops/Bass_Loop_Am_128.wav\"}") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"progress\",\"done\":1,\"total\":1") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"done\",\"added\":1") != std::string::npos);

    CHECK(cli.runAsma("query kick").out.find("Kick") == std::string::npos);
}

TEST_CASE("a second writer is refused with exit code 3", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    auto lock = asma::WriterLock::tryAcquire(cli.db.parent_path());
    REQUIRE(lock.has_value());
    CHECK(cli.runAsma("scan").exitCode == 3);
    const RunResult worker = cli.runScan("--root 1");
    CHECK(worker.exitCode == 3);
    CHECK(worker.out.find("\"code\":\"locked\"") != std::string::npos);
    lock.reset();
    CHECK(cli.runAsma("query").out.empty()); // nothing was scanned while locked
}

TEST_CASE("bad usage exits with 2", "[e2e]")
{
    Cli cli;
    CHECK(cli.runAsma("frobnicate").exitCode == 2);
    CHECK(cli.runAsma("query --bogus").exitCode == 2);
    CHECK(cli.runAsma("query --key H").exitCode == 2);
    CHECK(cli.runScan("").exitCode == 2);
}
```

Note `quote(asma::fromUtf8("Drums/Kick Ü_01.wav"))` passes the relative path
with native separators; `asma-scan` must normalise `--fail` values through
`toUtf8(fromUtf8(value))` so `\` becomes `/` on Windows.

- [ ] **Step 6: Implement `asma-scan`**

`apps/asma_scan_main.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// asma-scan: out-of-process scanner. Protocol: docs/scan-protocol.md.
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Scanner.h"
#include "asma/core/WriterLock.h"

#include <iostream>
#include <string>

using namespace asma;
using namespace asma::cli;

namespace {

void emit(const JsonLine& line) { std::cout << line.build() << '\n' << std::flush; }

} // namespace

int main(int argc, char** argv)
{
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
        const auto db = args.option("db");
        const auto root = args.option("root");
        const auto threads = args.option("threads");
        const auto failed = args.options("fail");
        if (!db || !root || !args.rest().empty())
            throw UsageError("usage: asma-scan --db PATH --root ID [--threads N] [--fail RELPATH]...");

        const std::filesystem::path dbPath = fromUtf8(*db);
        auto lock = WriterLock::tryAcquire(dbPath.parent_path());
        if (!lock) {
            JsonLine line;
            line.str("event", "error").str("code", "locked");
            if (const auto pid = WriterLock::holder(dbPath.parent_path())) line.num("pid", *pid);
            emit(line);
            return kLocked;
        }

        Db database = Db::open(dbPath);
        const std::int64_t rootId = std::stoll(*root);
        for (const auto& path : failed) {
            const std::string rel = toUtf8(fromUtf8(path));
            markFailedPath(database, rootId, rel, "crashed the scanner");
            emit(JsonLine().str("event", "marked_failed").str("path", rel));
        }

        ScanOptions options;
        if (threads) options.threads = static_cast<unsigned>(std::stoul(*threads));
        options.onFileStart = [](std::string_view rel) { emit(JsonLine().str("event", "start").str("path", rel)); };
        options.onProgress = [](std::size_t done, std::size_t total, std::string_view rel) {
            emit(JsonLine()
                     .str("event", "progress")
                     .num("done", static_cast<std::int64_t>(done))
                     .num("total", static_cast<std::int64_t>(total))
                     .str("path", rel));
        };
        const ScanStats s = scanRoot(database, rootId, options);
        emit(JsonLine()
                 .str("event", "done")
                 .num("added", static_cast<std::int64_t>(s.added))
                 .num("updated", static_cast<std::int64_t>(s.updated))
                 .num("unchanged", static_cast<std::int64_t>(s.unchanged))
                 .num("relinked", static_cast<std::int64_t>(s.relinked))
                 .num("missing", static_cast<std::int64_t>(s.missing))
                 .num("failed", static_cast<std::int64_t>(s.failed)));
        return kOk;
    } catch (const UsageError& e) {
        std::cerr << e.what() << "\n";
        return kUsage;
    } catch (const std::exception& e) {
        emit(JsonLine().str("event", "error").str("code", "failed").str("message", e.what()));
        return kError;
    }
}
```

- [ ] **Step 7: Implement the `asma` CLI**

`apps/asma_main.cpp` (replace):

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/NameParse.h"
#include "asma/core/Query.h"
#include "asma/core/Scanner.h"
#include "asma/core/WriterLock.h"

#include <cstdio>
#include <iostream>
#include <string>

using namespace asma;
using namespace asma::cli;

namespace {

constexpr const char* kUsageText =
    "usage: asma [--db PATH] <command>\n"
    "  root add <dir>          add a sample folder\n"
    "  root list               list sample folders\n"
    "  scan [--root ID] [--threads N]\n"
    "  query [words...] [--type loop|oneshot] [--bpm N|MIN-MAX] [--key K]...\n"
    "        [--tag T]... [--format F]... [--min-duration S] [--max-duration S]\n"
    "        [--sort name|bpm|duration|key] [--desc] [--limit N] [--json]\n"
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

int cmdRoot(Args& args, Db& db)
{
    Library lib(db);
    const auto sub = args.positional();
    if (sub == "add") {
        const auto dir = args.positional();
        if (!dir) throw UsageError("root add needs a directory");
        rejectLeftovers(args);
        const auto id = lib.addRoot(fromUtf8(*dir));
        std::cout << "root " << id << " " << lib.root(id)->path << "\n";
        return kOk;
    }
    if (sub == "list") {
        rejectLeftovers(args);
        for (const auto& r : lib.roots()) std::cout << r.id << "\t" << r.path << "\t" << (r.enabled ? "on" : "off") << "\n";
        return kOk;
    }
    throw UsageError("root needs 'add' or 'list'");
}

int cmdScan(Args& args, Db& db, const std::filesystem::path& dbPath)
{
    const auto root = args.option("root");
    const auto threads = args.option("threads");
    rejectLeftovers(args);

    auto lock = WriterLock::tryAcquire(dbPath.parent_path());
    if (!lock) {
        std::cerr << "another asma process is writing to this library";
        if (const auto pid = WriterLock::holder(dbPath.parent_path())) std::cerr << " (pid " << *pid << ")";
        std::cerr << "\n";
        return kLocked;
    }

    Library lib(db);
    ScanOptions options;
    if (threads) options.threads = static_cast<unsigned>(toDouble(*threads, "--threads"));
    options.onProgress = [](std::size_t done, std::size_t total, std::string_view) {
        if (done % 500 == 0 || done == total) std::cerr << "\r" << done << "/" << total << std::flush;
    };
    for (const auto& r : lib.roots()) {
        if (root && std::to_string(r.id) != *root) continue;
        if (!r.enabled) continue;
        const ScanStats s = scanRoot(db, r.id, options);
        std::cerr << "\r";
        std::cout << "root " << r.id << ": added " << s.added << ", updated " << s.updated << ", unchanged "
                  << s.unchanged << ", relinked " << s.relinked << ", missing " << s.missing << ", failed "
                  << s.failed << "\n";
    }
    return kOk;
}

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

    for (const auto& row : search(db, m)) {
        const std::string path = row.rootPath + "/" + row.relPath;
        if (json) {
            JsonLine line;
            line.num("id", row.id).str("path", path).str("format", row.format).real("duration", row.duration);
            if (row.bpm) line.real("bpm", *row.bpm);
            else line.null("bpm");
            if (row.key) line.str("key", *row.key);
            else line.null("key");
            if (row.isLoop) line.boolean("is_loop", *row.isLoop);
            else line.null("is_loop");
            std::cout << line.build() << "\n";
        } else {
            char duration[32];
            std::snprintf(duration, sizeof duration, "%.2f", row.duration);
            char bpm[32] = "-";
            if (row.bpm) std::snprintf(bpm, sizeof bpm, "%g", *row.bpm);
            const char* type = !row.isLoop ? "-" : (*row.isLoop ? "loop" : "oneshot");
            std::cout << path << "\t" << bpm << "\t" << row.key.value_or("-") << "\t" << type << "\t" << duration
                      << "\n";
        }
    }
    return kOk;
}

} // namespace

int main(int argc, char** argv)
{
    setupConsole();
    try {
        Args args = Args::fromMain(argc, argv);
        if (args.flag("version")) {
            std::cout << "asma " << ASMA_VERSION << "\n";
            return kOk;
        }
        const std::filesystem::path dbPath = resolveDbPath(args);
        const auto command = args.positional();
        if (!command) throw UsageError("missing command");
        if (*command != "root" && *command != "scan" && *command != "query")
            throw UsageError("unknown command: " + *command);

        Db db = Db::open(dbPath);
        if (*command == "root") return cmdRoot(args, db);
        if (*command == "scan") return cmdScan(args, db, dbPath);
        return cmdQuery(args, db);
    } catch (const UsageError& e) {
        std::cerr << "asma: " << e.what() << "\n" << kUsageText;
        return kUsage;
    } catch (const std::exception& e) {
        std::cerr << "asma: " << e.what() << "\n";
        return kError;
    }
}
```

- [ ] **Step 8: Run the whole suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: every test passes, including the `[e2e]` group.

- [ ] **Step 9: Document the scan protocol and the CLI**

`docs/scan-protocol.md`:

```markdown
# asma-scan protocol

`asma-scan` is the out-of-process scanner. The UI starts it, reads its stdout
line by line, and restarts it if it dies. Every line is one JSON object with an
`event` field.

## Invocation

    asma-scan --db PATH --root ID [--threads N] [--fail RELPATH]...

`--fail` marks a root-relative path as failed with the reason "crashed the
scanner" before scanning starts, so the scan skips it until the file changes.

## Events

| event           | fields                                                           | meaning                             |
| --------------- | ---------------------------------------------------------------- | ----------------------------------- |
| `marked_failed` | `path`                                                           | a `--fail` path was recorded        |
| `start`         | `path`                                                           | a worker began probing this file    |
| `progress`      | `done`, `total`, `path`                                          | this file finished                  |
| `done`          | `added`, `updated`, `unchanged`, `relinked`, `missing`, `failed` | the scan completed                  |
| `error`         | `code` (`locked` or `failed`), optional `pid`, `message`         | the scan did not run or did not end |

Paths are root-relative, UTF-8, with `/` separators.

## Exit codes

0 success, 1 error, 2 usage, 3 another process holds the writer lock.

## Crash recovery (supervisor contract)

Several files are probed in parallel, so a crash cannot be pinned on one file
from the last line alone. The supervisor keeps the set of paths that have a
`start` but no `progress`. When the worker exits without a `done` event, the
supervisor restarts it with `--threads 1`, and when a single-threaded worker
dies, the path it started last is passed back with `--fail`.
```

Extend `README.md` with a usage section:

```markdown
## Command line

    asma root add ~/Samples
    asma scan
    asma query kick
    asma query --type loop --bpm 120-130 --key Am
    asma query dusty --tag drums --json

The library lives in the platform data directory (on macOS
`~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
`ASMA_DATA_DIR` environment variable override it.
```

Run `prettier -w README.md docs/scan-protocol.md`.

- [ ] **Step 10: Commit**

```sh
git add -A
git commit -m "apps: add the asma CLI and the asma-scan worker"
```

---

### Task 12: Verify on all platforms and merge

**Files:** none new.

**Interfaces:** consumes the whole branch; produces a merged `main`.

- [ ] **Step 1: Full local run, Debug and Release**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-release && ctest --test-dir build-release --output-on-failure
./build-release/tests/asma_tests "[.perf]"
```

Expected: all green, perf warning under 50 ms.

- [ ] **Step 2: Smoke test on a real library**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
./build-release/apps/asma root add ~/Music   # or any folder with samples
time ./build-release/apps/asma scan
./build-release/apps/asma query kick | head
```

Expected: the scan finishes without errors; note files/second in the merge
commit message body. Failed files, if any, are listed by
`sqlite3 $ASMA_DATA_DIR/library.db "select rel_path, failure_reason from files where status='failed'"`.

- [ ] **Step 3: Check the text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git -C . log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: Merge**

CI on all three platforms runs once the repo is on GitHub (creating
`anode-audio/asma` is a separate, user-approved step). Until then, merge
locally:

```sh
git -C product/asma merge --ff-only plan-1-core-cli
git -C product/asma worktree remove .worktrees/plan-1
```
