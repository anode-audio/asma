# asma Plan 3b: Audition Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything the plugin and the standalone will play samples through,
built and tested without JUCE: streaming previews, trim, reverse and ping-pong,
tempo and key sync with Signalsmith Stretch, loudness matching, eight MIDI
voices, and drag-out renders in a capped cache, with `asma render` on the CLI.

**Architecture:** `AudioReader` in `asma-core` reads any supported file in
stereo, in chunks and from any frame; `decodeFile` is rebuilt on it. A new
static library, `asma-audio` (`audio/`), holds the audition engine. A `Loader`
thread turns selections into previews (whole files up to 10 s from an LRU
`PreviewCache`, longer ones as a `StreamSource` read ahead in blocks) and hands
them to the audio thread over a lock-free queue. On the audio thread a
`PlayHead` (trim, direction, looping, resampling, anti-click fades) feeds a
`Stretcher` (Signalsmith Stretch); `planSync` decides tempo ratio and
transposition, `matchGain` the level, and `VoicePool` plays MIDI notes.
`AuditionEngine` ties these together behind one control API and one `process()`
call, which never allocates. `renderToFile` and `RenderCache` bake edits into
WAV files for drag-out.

**Tech Stack:** C++20, dr_libs and stb_vorbis (already vendored), Signalsmith
Stretch 1.4.0 with Signalsmith Linear 0.6.4 (new, MIT, header-only), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (sections 4, 8 and
9, Drag out). Task 12 amends it where this plan decides something the spec left
open.

**How this plan was checked:** every task was built on a clone of `main`
(87decea), then replayed commit by commit on a Release build: with only the
task's test changes applied, each red step failed as stated; with the whole
task, each green step built without warnings and passed. The `Expected:` lines
are the recorded outputs. The audio tests also ran clean under ThreadSanitizer
(74 test cases). The code blocks below were checked by applying them in order to
a fresh `main` and comparing the result with the prototype: they match file for
file. The replay ran on macOS 26 (Apple clang). Linux (GCC 11) and Windows
(MSVC) were not compiled locally: CI on all three platforms must be green before
merging (Task 13).

## Where this sits

Plan 3 is split in three, each shippable and testable on its own:

- **3a (merged):** organise data, the CLI writer commands, the scan supervisor,
  read-only access and change notification.
- **3b (this plan):** the audition engine and drag-out renders, tested headless.
- **3c:** JUCE 8, `asma-ui`, the standalone and the VST3/AU/CLAP/LV2 shells,
  Ableton Link, pluginval and clap-validator. The plugin's `processBlock` calls
  `AuditionEngine::process`; the UI calls the control API and `status()`.

Plan 4 (file manager) and plan 5 (packaging) are unchanged.

## Global Constraints

- License: GPLv3. Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-only` (CMake files: `# SPDX-...`).
- C++20, `CMAKE_CXX_EXTENSIONS OFF`, no `std::format` (GCC 11 on Ubuntu 22.04),
  no floating-point `std::to_chars` or `std::from_chars` (macOS 12). Use
  `snprintf` to format numbers, a classic-locale stream to parse them, and
  integers in anything a locale must not change (render file names).
- Platforms: macOS 12+, Windows x64 with MSVC, Linux x64 on Ubuntu 22.04.
- `asma-core` and `asma-audio` never link JUCE or any GUI library.
- Code that runs on the audio thread (`SampleSource::read`/`hint`, `PlayHead`,
  `Stretcher::process`, `VoicePool`, `Loader::takeReady`/`release`,
  `AuditionEngine::process` and the audio calls) never allocates, locks, blocks
  or throws. Allocation happens in `prepare()` and on the loader thread.
- Paths in the database are UTF-8 with `/` separators, via `toUtf8`/`fromUtf8`.
- No schema change in this plan.
- No em-dashes in code, comments, docs or commit messages. No AI references or
  co-author trailers. Worktree, then a direct merge; no PR.

## Decisions made while prototyping

- **`asma-audio` is its own JUCE-free library, the root note is MIDI 60, and 3b
  is one plan:** the user's calls (2026-09-29). Task 12 writes the first two
  into the spec, which had the engine inside `asma-ui` and said "C3".
- **`AudioReader` lives in core and `decodeFile` is rebuilt on it.** One set of
  decoder bindings for analysis and playback. Playback takes the first two
  channels of a multichannel file; analysis still averages all of them, so
  analysis output is unchanged (the analysis tests pass as before). MP3 gets a
  seek table at open: without one every seek decodes from the start, and reverse
  playback of a long MP3 seeks once per block. Decoders disagree about seeking
  exactly to the end, so the reader handles the end itself.
- **Streaming keeps the first 10 s and the last block, plus 16 rolling blocks of
  32768 frames** (about 12 s at 44.1 kHz) read ahead in the direction of travel
  and one behind for ping-pong turns. The first 10 s make a forward start
  instant, the last block a reverse one. A block not loaded yet plays as
  silence; the audio thread never waits.
- **Slots are shared through a reader count, not a seqlock.** The audio thread
  bumps a slot's reader count before checking which block it holds; the loader
  clears the block and waits for readers to leave before writing. A seqlock
  would copy data while it is being written, which is a data race.
- **A preview is freed only when the audio thread no longer uses it.** Previews
  are numbered by selection. The audio thread reports the oldest generation it
  still uses (its selection, a sound fading out, any ringing MIDI voice), and
  `Loader::release` caps that report at one past the newest preview it has
  taken. Without the cap, a report of "nothing plays" made before a new preview
  is taken would let the loader free that preview in the gap; the test "Loader
  keeps a preview taken but not yet reported" fails without the cap.
- **Fades only where a sound would click:** 5 ms at trim points, at a reverse
  start, at the end of a reversed or trimmed pass, at the wrap of a trimmed or
  reversed loop, and on stop. None at a forward start from frame 0 (the attack
  is the sound), at a one-shot's own end, at the wrap of an untrimmed forward
  loop (made to be seamless), or at a ping-pong turn (continuous).
- **The stretch is pre-rolled** with Signalsmith's `outputSeek`, so the first
  frame of a sound comes out first rather than about 120 ms late (measured: a
  tone after 50 ms of silence starts at 50 ms). Its tail after the sound ends is
  one seek length plus two analysis blocks: one block cut the sound off at 0.35
  of full scale, two let it die out to silence. Sounds without sync bypass the
  stretch and play bit for bit; synced sounds always run through it, so a loop
  can follow the host tempo as it changes.
- **Tempo sync picks half or double time when closer to 1**, so a loop at 70
  plays unchanged at 140 and a tempo detected an octave off (a quarter of
  labelled loops) still lands on the grid.
- **The confidence thresholds were measured** on labelled libraries with the
  opt-in real-library evaluation, split by confidence:

  | Material (labelled files)      | Confidence | Right, or octave / relative key |
  | ------------------------------ | ---------- | ------------------------------- |
  | Breaks and loops, tempo (87)   | 0.15..0.3  | 76%                             |
  |                                | 0.3 and up | 52 of 53                        |
  | Apple Loops, key (918)         | 0.6 and up | 100%                            |
  | Synth one-shots and loops, key | 0.5..0.7   | 72%                             |
  | (Triaz, 498)                   | 0.7 and up | 89%                             |
  | Guitar recordings, key (438)   | any        | 15% to 42%                      |

  Tempo syncs at 0.3, key at 0.7. A relative-key mistake is harmless because
  transposition aims at the relative key across modes. Key sync starts off.

- **`SyncSettings::projectKey` is a fixed four-byte `KeyName`**, so settings
  reach the audio thread through the command queue without a heap string.
- **Loudness:** to -16 LUFS, at most +12 dB, never boosting the peak past -1
  dBFS; cuts are not limited. A short file the scan has not analysed yet is
  measured when it loads; a streamed one plays at unity.
- **MIDI voices pitch by speed**, like a classic sampler (and eight stretchers
  would cost too much), and play the trimmed region once. A ninth note takes a
  releasing voice first, then the oldest.
- **An edit while a sample plays restarts it; switching samples fades the old
  one out over 5 ms first.** Quantised start waits only while the transport
  plays.
- **Renders are one pass at an exact length:** the trimmed pass (there and back
  for ping-pong) divided by the tempo ratio, so a synced loop lands on the DAW's
  grid. 32-bit float WAV with the file's channels, written to a temporary name
  and renamed. A sample-rate change alone is not an edit: the original is
  dragged and the DAW converts it. The cache counts a hit as a use by touching
  the file's mtime; the file being dragged is never evicted.
- **Signalsmith's headers are marked `SYSTEM`** (they trip our `-Wshadow`), and
  `asma_audio` needs `/bigobj` on MSVC, which Signalsmith sets only for its own
  directory. On macOS Signalsmith Linear uses Accelerate.
- **A full command queue (64 control calls inside one audio block) drops the
  command.** Controls come from user actions; 64 in one block does not happen.

## Review Focus

1. **Arrowing through a list with auto-play.** Selections arrive faster than
   files load. Only the newest loads, the old sound fades rather than clicks,
   and nothing the audio thread holds is freed. Pinned in Task 4 ("Loader skips
   selections replaced before they loaded", "Loader keeps a preview taken but
   not yet reported") and Task 10 ("AuditionEngine fades the old sample out
   before the new one starts"); the audio tests also run clean under
   ThreadSanitizer.
2. **A file that changes or disappears while it is previewed.** An edited file
   is decoded again, not served stale; a stream whose drive goes away plays
   silence instead of crashing. Pinned in Task 2 ("PreviewCache returns the same
   buffer until the file changes") and Task 3 ("StreamSource stops streaming
   when the file goes away").
3. **A sample at another rate than the session** (44.1 kHz files in a 48 kHz
   project). Pitch and length stay right in audition and in renders. Pinned in
   Task 5 ("a 440 Hz tone at 44.1 kHz stays 440 Hz at 48 kHz") and Task 11
   ("renderToFile stretches to an exact length at the same pitch").
4. **Analysis that guessed.** A wrong tempo or key must not mangle the sample:
   below the threshold it plays as it is and says "?". Pinned in Task 7
   ("planSync plays an uncertain tempo unmodified and says so", "planSync
   transposes to the project key only when sure of the sample's key") and
   Task 10.
5. **The host's audio thread.** Any allocation there can glitch every track in
   the session. Pinned in Task 10 ("AuditionEngine never allocates on the audio
   thread"), which counts allocations through selections, streaming, sync,
   quantised starts, edits, MIDI and stop; the count was checked to catch a
   planted allocation.

---

## File Structure

```
core/include/asma/core/AudioReader.h   chunked, seekable, stereo decoding
core/src/AudioReader.cpp
audio/                                 asma-audio (new library)
  CMakeLists.txt
  include/asma/audio/
    SampleSource.h   AudioBuffer, loadAudio, SampleSource, MemorySource
    PreviewCache.h   LRU of decoded files
    StreamSource.h   long files in blocks; openSource
    SpscQueue.h      lock-free single-producer single-consumer queue
    SampleInfo.h     what the library knows about a sample
    Loader.h         Preview, Loader
    PlayHead.h       Direction, PlayOptions, PlayHead
    Stretcher.h      Signalsmith Stretch wrapper
    Sync.h           KeyName, SyncSettings, SyncPlan, planSync, sampleInfo
    Gain.h           matchGain
    Voices.h         VoicePool
    Edits.h          LoopMode, Edits, toPlayOptions
    AuditionEngine.h Transport, EngineStatus, AuditionEngine
    Render.h         RenderSettings, renderToFile, RenderCache
  src/               one .cpp per header except SpscQueue.h and SampleInfo.h
tests/
  FakeReader.h, AllocCounter.h, alloc_counter.cpp
  test_audio_reader.cpp, test_sample_source.cpp, test_preview_cache.cpp,
  test_stream_source.cpp, test_loader.cpp, test_play_head.cpp,
  test_stretcher.cpp, test_sync.cpp, test_gain.cpp, test_voices.cpp,
  test_audition_engine.cpp, test_render.cpp
```

Modified: `CMakeLists.txt`, `cmake/Dependencies.cmake`, `core/src/Decode.cpp`,
`core/include/asma/core/Library.h`, `core/src/Library.cpp` (`loudness`),
`core/include/asma/core/Fs.h`, `core/src/Fs.cpp` (`defaultCacheDir`),
`apps/asma_main.cpp`, `apps/CMakeLists.txt` (`asma render`),
`tests/CMakeLists.txt`, `tests/TestUtil.h`, `tests/test_library.cpp`,
`tests/test_fs.cpp`, `tests/test_cli_e2e.cpp`, `README.md`, the spec.

Commands as in plans 1 to 3a (`cmake --build build`,
`./build/tests/asma_tests "[tag]"`,
`ctest --test-dir build --output-on-failure`). Configure again
(`cmake -B build`) whenever a task adds files: the source lists are globs.

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-3b -b plan-3b-audition
```

All paths below are relative to `product/asma/.worktrees/plan-3b`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` (the analysis
accuracy tests are slow in Debug).

---

### Task 1: Chunked, seekable, stereo decoding

`decodeFile` reads a whole file as mono from the start, which is what analysis
wants and not what playback needs. `AudioReader` reads up to two channels, in
chunks, from any frame, and knows the exact length. `decodeFile` becomes a thin
loop over `readMono`, so analysis output does not change.

**Files:**

- Create: `core/include/asma/core/AudioReader.h`, `core/src/AudioReader.cpp`,
  `tests/test_audio_reader.cpp`
- Modify: `core/src/Decode.cpp` (rebuilt on the reader), `tests/TestUtil.h`
  (`writeWavFloat`, `ramp`)

**Interfaces:**

- Consumes: `detectFormat`, `ProbeError`, `FileAccessError`, `openFileRead`,
  `FilePtr` (plan 1); the dr_libs and stb_vorbis bindings.
- Produces: `class asma::AudioReader` with
  `static std::unique_ptr<AudioReader> open(const std::filesystem::path&)`,
  `int sampleRate() const`, `int sourceChannels() const`, `int channels() const`
  (1 or 2), `std::uint64_t frames() const`,
  `std::uint64_t read(float* const* out, std::uint64_t n)` (planar),
  `std::uint64_t readMono(float* out, std::uint64_t n)`,
  `void seek(std::uint64_t frame)`; protected for test doubles: `AudioReader()`,
  `void init()`, `virtual std::uint64_t readInterleaved(float*, std::uint64_t)`,
  `virtual bool seekTo(std::uint64_t)`, fields `sampleRate_`, `sourceChannels_`,
  `frames_`. Test helpers
  `asma::test::writeWavFloat(path, rate, std::vector<std::vector<float>>)` and
  `asma::test::ramp(n, step = 1/65536, offset = 0)`.

Behaviour the tests pin:

- Stereo files read as two planar channels, sample for sample; seeking lands on
  the exact frame in WAV, FLAC, MP3 and Ogg, and reading the same frames twice
  gives the same values.
- A read that reaches the end is short; seeking at or past the end reads
  nothing.
- A file with four channels reads its first two; `readMono` averages all four.
- NaN and inf read as 0, absurd values clamp to +-16.
- Sample rates outside 1 kHz..768 kHz are refused (`ProbeError`); a missing file
  is a `FileAccessError`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_audio_reader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace asma;
using asma::test::TempDir;

TEST_CASE("AudioReader reads stereo planar and seeks to any frame", "[reader]")
{
    TempDir dir;
    const auto p = dir.path() / "stereo.wav";
    const auto left = test::ramp(10000);
    const auto right = test::ramp(10000, -1.0f / 65536.0f);
    test::writeWavFloat(p, 48000, {left, right});

    const auto r = AudioReader::open(p);
    CHECK(r->sampleRate() == 48000);
    CHECK(r->channels() == 2);
    CHECK(r->frames() == 10000);

    std::vector<float> l(100), rr(100);
    float* out[] = {l.data(), rr.data()};
    r->seek(5000);
    REQUIRE(r->read(out, 100) == 100);
    CHECK(l[0] == left[5000]);
    CHECK(rr[99] == right[5099]);

    r->seek(9950);
    CHECK(r->read(out, 100) == 50); // short read at the end
    CHECK(l[49] == left[9999]);
    r->seek(20000); // past the end clamps
    CHECK(r->read(out, 100) == 0);
    r->seek(0);
    REQUIRE(r->read(out, 1) == 1);
    CHECK(l[0] == left[0]);
}

TEST_CASE("AudioReader mixes to mono and keeps the first two of many channels", "[reader]")
{
    TempDir dir;
    const auto p = dir.path() / "quad.wav";
    test::writeWavFloat(p, 44100, {{0.1f, 0.1f}, {0.2f, 0.2f}, {0.3f, 0.3f}, {0.4f, 0.4f}});
    auto r = AudioReader::open(p);
    CHECK(r->sourceChannels() == 4);
    CHECK(r->channels() == 2);
    std::vector<float> a(2), b(2);
    float* out[] = {a.data(), b.data()};
    REQUIRE(r->read(out, 2) == 2);
    CHECK(a[1] == 0.1f);
    CHECK(b[1] == 0.2f);
    r->seek(0);
    std::vector<float> mono(2);
    REQUIRE(r->readMono(mono.data(), 2) == 2);
    CHECK(mono[0] == Catch::Approx(0.25f));

    const auto m = dir.path() / "mono.wav";
    test::writeWavFloat(m, 44100, {{0.5f}});
    CHECK(AudioReader::open(m)->channels() == 1);
}

TEST_CASE("AudioReader replaces NaN and inf and clamps absurd values", "[reader]")
{
    TempDir dir;
    const auto p = dir.path() / "broken.wav";
    const float inf = std::numeric_limits<float>::infinity();
    test::writeWavFloat(p, 44100, {{std::nanf(""), inf, 1e9f, -0.5f}});
    const auto r = AudioReader::open(p);
    std::vector<float> x(4);
    float* out[] = {x.data()};
    REQUIRE(r->read(out, 4) == 4);
    CHECK(x[0] == 0.0f);
    CHECK(x[1] == 0.0f);
    CHECK(x[2] == 16.0f);
    CHECK(x[3] == -0.5f);
}

TEST_CASE("AudioReader seeks in FLAC, MP3 and Ogg", "[reader]")
{
    for (const char* name : {"tone.flac", "tone.mp3", "tone.ogg"}) {
        INFO(name);
        const auto r = AudioReader::open(test::fixture(name));
        CHECK(r->sampleRate() == 44100);
        CHECK(static_cast<double>(r->frames()) / 44100.0 == Catch::Approx(0.5).margin(0.06));
        std::vector<float> a(64), b(64), a2(64), b2(64);
        float* first[] = {a.data(), b.data()};
        float* second[] = {a2.data(), b2.data()};
        r->seek(8000);
        REQUIRE(r->read(first, 64) == 64);
        r->seek(0);
        r->seek(8000);
        REQUIRE(r->read(second, 64) == 64);
        CHECK(a == a2); // the same frames, however we got there
        r->seek(r->frames());
        CHECK(r->read(first, 64) == 0);
    }
}

TEST_CASE("AudioReader refuses what it cannot play", "[reader]")
{
    TempDir dir;
    const auto odd = dir.path() / "odd.wav";
    test::WavSpec spec;
    spec.sampleRate = 500;
    test::writeWav(odd, spec);
    CHECK_THROWS_AS(AudioReader::open(odd), ProbeError);
    CHECK_THROWS_AS(AudioReader::open(dir.path() / "nope.wav"), FileAccessError);
}
```

In `tests/TestUtil.h`, replace:

```cpp
    writeBytes(path, file);
}

struct AiffSpec {
    int sampleRate = 44100;
    int channels = 1;
```

with:

```cpp
    writeBytes(path, file);
}

// 32-bit float WAV, one vector per channel (all the same length). Float keeps
// every value exact, so playback tests can compare samples one for one.
inline void writeWavFloat(const fs::path& path, int sampleRate, const std::vector<std::vector<float>>& channels)
{
    using namespace detail;
    const auto count = static_cast<std::uint16_t>(channels.size());
    std::string fmt;
    putLe16(fmt, 3); // IEEE float
    putLe16(fmt, count);
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate));
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate * 4 * count));
    putLe16(fmt, static_cast<std::uint16_t>(4 * count));
    putLe16(fmt, 32);
    std::string data;
    const std::size_t frames = channels.empty() ? 0 : channels[0].size();
    data.reserve(frames * 4 * count);
    for (std::size_t i = 0; i < frames; ++i)
        for (const auto& channel : channels) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &channel[i], 4);
            putLe32(data, bits);
        }
    std::string body = "WAVE";
    putChunkLe(body, "fmt ", fmt);
    putChunkLe(body, "data", data);
    std::string file = "RIFF";
    putLe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

// 0, 1, 2, ... n-1 scaled by `step`: every frame is recognisable by value.
inline std::vector<float> ramp(std::size_t n, float step = 1.0f / 65536.0f, float offset = 0.0f)
{
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = offset + static_cast<float>(i) * step;
    return out;
}

struct AiffSpec {
    int sampleRate = 44100;
    int channels = 1;
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_audio_reader.cpp:4:10: fatal error: 'asma/core/AudioReader.h' file not found`

- [ ] **Step 3: Implement**

Create `core/include/asma/core/AudioReader.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma {

// Reads a file as float frames, in chunks and from any position. One reader
// belongs to one thread at a time. Samples are sanitised as they are read:
// NaN and inf become 0 and the rest is clamped to -16..16, because a broken
// float render would otherwise poison everything downstream.
class AudioReader {
public:
    // The header decides the format, as in probeFile. Throws FileAccessError
    // when the file cannot be read and ProbeError when it cannot be decoded,
    // has no channels or has a sample rate outside 1 kHz..768 kHz.
    static std::unique_ptr<AudioReader> open(const std::filesystem::path& path);

    virtual ~AudioReader() = default;
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    int sampleRate() const { return sampleRate_; }
    // Channels in the file.
    int sourceChannels() const { return sourceChannels_; }
    // Channels read(): 1 or 2. Files with more play their first two.
    int channels() const { return sourceChannels_ < 2 ? 1 : 2; }
    // Exact length in frames, counted at open when the header does not say.
    std::uint64_t frames() const { return frames_; }

    // Reads up to n frames from the current position into channels() planar
    // buffers. Returns the frames read; fewer than n only at the end.
    std::uint64_t read(float* const* out, std::uint64_t n);
    // As read, but the average of every source channel.
    std::uint64_t readMono(float* out, std::uint64_t n);
    // Moves to a frame; past the end clamps to the end. Throws ProbeError.
    void seek(std::uint64_t frame);

protected:
    AudioReader() = default;
    // Called by open() once the subclass has set the fields below.
    void init();

    virtual std::uint64_t readInterleaved(float* out, std::uint64_t n) = 0;
    virtual bool seekTo(std::uint64_t frame) = 0;

    int sampleRate_ = 0;
    int sourceChannels_ = 0;
    std::uint64_t frames_ = 0;

private:
    template <typename Emit>
    std::uint64_t readChunks(std::uint64_t n, Emit emit);

    std::vector<float> scratch_;
    bool atEnd_ = false;
};

} // namespace asma
```

Create `core/src/AudioReader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/AudioReader.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <cmath>

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr std::uint64_t kChunkFrames = 4096;

float clean(float x) { return std::isfinite(x) ? std::clamp(x, -16.0f, 16.0f) : 0.0f; }

class WavReader final : public AudioReader {
public:
    explicit WavReader(const fs::path& path)
    {
#ifdef _WIN32
        const bool opened = drwav_init_file_w(&wav_, path.c_str(), nullptr);
#else
        const bool opened = drwav_init_file(&wav_, path.c_str(), nullptr);
#endif
        if (!opened) throw ProbeError("cannot decode WAV/AIFF audio");
        sampleRate_ = static_cast<int>(wav_.sampleRate);
        sourceChannels_ = wav_.channels;
        frames_ = wav_.totalPCMFrameCount;
    }
    ~WavReader() override { drwav_uninit(&wav_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return drwav_read_pcm_frames_f32(&wav_, n, out);
    }
    bool seekTo(std::uint64_t frame) override { return drwav_seek_to_pcm_frame(&wav_, frame); }

private:
    drwav wav_{};
};

class FlacReader final : public AudioReader {
public:
    explicit FlacReader(const fs::path& path)
    {
#ifdef _WIN32
        flac_ = drflac_open_file_w(path.c_str(), nullptr);
#else
        flac_ = drflac_open_file(path.c_str(), nullptr);
#endif
        if (!flac_) throw ProbeError("cannot decode FLAC audio");
        sampleRate_ = static_cast<int>(flac_->sampleRate);
        sourceChannels_ = flac_->channels;
        frames_ = flac_->totalPCMFrameCount; // 0 when the stream info does not say
    }
    ~FlacReader() override { drflac_close(flac_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return drflac_read_pcm_frames_f32(flac_, n, out);
    }
    bool seekTo(std::uint64_t frame) override { return drflac_seek_to_pcm_frame(flac_, frame); }

private:
    drflac* flac_ = nullptr;
};

class Mp3Reader final : public AudioReader {
public:
    explicit Mp3Reader(const fs::path& path)
    {
#ifdef _WIN32
        const bool opened = drmp3_init_file_w(&mp3_, path.c_str(), nullptr);
#else
        const bool opened = drmp3_init_file(&mp3_, path.c_str(), nullptr);
#endif
        if (!opened) throw ProbeError("cannot decode MP3 audio");
        sampleRate_ = static_cast<int>(mp3_.sampleRate);
        sourceChannels_ = static_cast<int>(mp3_.channels);
        // Without a seek table every seek decodes from the start, and reverse
        // playback of a long MP3 seeks once per block.
        drmp3_uint32 count = 256;
        seekPoints_.resize(count);
        if (drmp3_calculate_seek_points(&mp3_, &count, seekPoints_.data()) && count > 0) {
            seekPoints_.resize(count);
            drmp3_bind_seek_table(&mp3_, count, seekPoints_.data());
        } else {
            seekPoints_.clear();
        }
        frames_ = drmp3_get_pcm_frame_count(&mp3_);
    }
    ~Mp3Reader() override { drmp3_uninit(&mp3_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return drmp3_read_pcm_frames_f32(&mp3_, n, out);
    }
    bool seekTo(std::uint64_t frame) override { return drmp3_seek_to_pcm_frame(&mp3_, frame); }

private:
    drmp3 mp3_{};
    std::vector<drmp3_seek_point> seekPoints_;
};

class OggReader final : public AudioReader {
public:
    explicit OggReader(const fs::path& path) : file_(openFileRead(path))
    {
        if (!file_) throw FileAccessError("cannot open file");
        int error = 0;
        vorbis_ = stb_vorbis_open_file(file_.get(), 0, &error, nullptr);
        if (!vorbis_) throw ProbeError("cannot decode Ogg Vorbis audio");
        const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        sampleRate_ = static_cast<int>(info.sample_rate);
        sourceChannels_ = info.channels;
        frames_ = stb_vorbis_stream_length_in_samples(vorbis_);
    }
    ~OggReader() override { stb_vorbis_close(vorbis_); }

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        return static_cast<std::uint64_t>(stb_vorbis_get_samples_float_interleaved(
            vorbis_, sourceChannels_, out, static_cast<int>(n) * sourceChannels_));
    }
    bool seekTo(std::uint64_t frame) override { return stb_vorbis_seek(vorbis_, static_cast<unsigned>(frame)) != 0; }

private:
    FilePtr file_;
    stb_vorbis* vorbis_ = nullptr;
};

} // namespace

std::unique_ptr<AudioReader> AudioReader::open(const fs::path& path)
{
    std::unique_ptr<AudioReader> reader;
    switch (detectFormat(path)) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: reader = std::make_unique<WavReader>(path); break;
    case AudioFormat::Flac: reader = std::make_unique<FlacReader>(path); break;
    case AudioFormat::Mp3: reader = std::make_unique<Mp3Reader>(path); break;
    case AudioFormat::Ogg: reader = std::make_unique<OggReader>(path); break;
    }
    reader->init();
    return reader;
}

void AudioReader::init()
{
    if (sourceChannels_ <= 0) throw ProbeError("stream has no channels");
    // Rates outside what audio hardware uses come from corrupt headers; below
    // ~90 Hz the analysis frames would have no hop at all.
    if (sampleRate_ < 1000 || sampleRate_ > 768000) throw ProbeError("unsupported sample rate");
    scratch_.resize(static_cast<std::size_t>(kChunkFrames) * static_cast<std::size_t>(sourceChannels_));
    if (frames_ == 0) {
        // Some FLAC encoders leave the length out; count it once.
        std::uint64_t got = 0;
        while ((got = readInterleaved(scratch_.data(), kChunkFrames)) > 0) frames_ += got;
        seek(0);
    }
}

template <typename Emit>
std::uint64_t AudioReader::readChunks(std::uint64_t n, Emit emit)
{
    std::uint64_t done = 0;
    while (done < n && !atEnd_) {
        const std::uint64_t got = readInterleaved(scratch_.data(), std::min(kChunkFrames, n - done));
        if (got == 0) break;
        emit(done, got);
        done += got;
    }
    return done;
}

std::uint64_t AudioReader::read(float* const* out, std::uint64_t n)
{
    const auto source = static_cast<std::size_t>(sourceChannels_);
    const int outChannels = channels();
    return readChunks(n, [&](std::uint64_t at, std::uint64_t got) {
        for (std::uint64_t i = 0; i < got; ++i)
            for (int c = 0; c < outChannels; ++c)
                out[c][at + i] = clean(scratch_[static_cast<std::size_t>(i) * source + static_cast<std::size_t>(c)]);
    });
}

std::uint64_t AudioReader::readMono(float* out, std::uint64_t n)
{
    const auto source = static_cast<std::size_t>(sourceChannels_);
    return readChunks(n, [&](std::uint64_t at, std::uint64_t got) {
        for (std::uint64_t i = 0; i < got; ++i) {
            float sum = 0.0f;
            for (std::size_t c = 0; c < source; ++c) sum += clean(scratch_[static_cast<std::size_t>(i) * source + c]);
            out[at + i] = sum / static_cast<float>(source);
        }
    });
}

void AudioReader::seek(std::uint64_t frame)
{
    // Decoders disagree about seeking to the very end, so the end is ours.
    atEnd_ = frame >= frames_;
    if (!atEnd_ && !seekTo(frame)) throw ProbeError("cannot seek in audio stream");
}

} // namespace asma
```

In `core/src/Decode.cpp`, replace:

```cpp
#include "asma/core/Decode.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr std::uint64_t kChunkFrames = 4096;

// Reads interleaved float frames through `read` until it returns 0 or
// maxFrames is reached, appending the channel average to audio.mono.
template <typename Read>
void readFrames(int channels, std::uint64_t maxFrames, DecodedAudio& audio, Read read)
{
    if (channels <= 0) throw ProbeError("stream has no channels");
    std::vector<float> buffer(static_cast<std::size_t>(kChunkFrames) * static_cast<std::size_t>(channels));
    std::uint64_t remaining = maxFrames;
    while (remaining > 0) {
        const std::uint64_t got = read(buffer.data(), std::min(kChunkFrames, remaining));
        if (got == 0) return;
        for (std::uint64_t i = 0; i < got; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) {
                // Broken float renders carry NaN, inf and absurd values; one
                // such sample would poison every descriptor of the file.
                const float x = buffer[static_cast<std::size_t>(i * channels + c)];
                sum += std::isfinite(x) ? std::clamp(x, -16.0f, 16.0f) : 0.0f;
            }
            audio.mono.push_back(sum / static_cast<float>(channels));
        }
        remaining -= got;
    }
    // Hit the limit: the file is longer if one more frame can be read.
    audio.truncated = read(buffer.data(), 1) > 0;
}

std::uint64_t framesFor(double seconds, int rate)
{
    return static_cast<std::uint64_t>(std::ceil(seconds * rate));
}

DecodedAudio decodeWavOrAiff(const fs::path& path, double maxSeconds)
{
    auto wav = std::make_unique<drwav>();
#ifdef _WIN32
    const bool opened = drwav_init_file_w(wav.get(), path.c_str(), nullptr);
#else
    const bool opened = drwav_init_file(wav.get(), path.c_str(), nullptr);
#endif
    if (!opened) throw ProbeError("cannot decode WAV/AIFF audio");
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(wav->sampleRate);
    try {
        readFrames(wav->channels, framesFor(maxSeconds, audio.sampleRate), audio,
                   [&](float* out, std::uint64_t n) { return drwav_read_pcm_frames_f32(wav.get(), n, out); });
    } catch (...) {
        drwav_uninit(wav.get());
        throw;
    }
    drwav_uninit(wav.get());
    return audio;
}

DecodedAudio decodeFlac(const fs::path& path, double maxSeconds)
{
#ifdef _WIN32
    drflac* flac = drflac_open_file_w(path.c_str(), nullptr);
#else
    drflac* flac = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!flac) throw ProbeError("cannot decode FLAC audio");
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(flac->sampleRate);
    try {
        readFrames(flac->channels, framesFor(maxSeconds, audio.sampleRate), audio,
                   [&](float* out, std::uint64_t n) { return drflac_read_pcm_frames_f32(flac, n, out); });
    } catch (...) {
        drflac_close(flac);
        throw;
    }
    drflac_close(flac);
    return audio;
}

DecodedAudio decodeMp3(const fs::path& path, double maxSeconds)
{
    auto mp3 = std::make_unique<drmp3>();
#ifdef _WIN32
    const bool opened = drmp3_init_file_w(mp3.get(), path.c_str(), nullptr);
#else
    const bool opened = drmp3_init_file(mp3.get(), path.c_str(), nullptr);
#endif
    if (!opened) throw ProbeError("cannot decode MP3 audio");
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(mp3->sampleRate);
    try {
        readFrames(static_cast<int>(mp3->channels), framesFor(maxSeconds, audio.sampleRate), audio,
                   [&](float* out, std::uint64_t n) { return drmp3_read_pcm_frames_f32(mp3.get(), n, out); });
    } catch (...) {
        drmp3_uninit(mp3.get());
        throw;
    }
    drmp3_uninit(mp3.get());
    return audio;
}

DecodedAudio decodeOgg(const fs::path& path, double maxSeconds)
{
    FilePtr file(openFileRead(path));
    if (!file) throw FileAccessError("cannot open file");
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_file(file.get(), 0, &error, nullptr);
    if (!vorbis) throw ProbeError("cannot decode Ogg Vorbis audio");
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    DecodedAudio audio;
    audio.sampleRate = static_cast<int>(info.sample_rate);
    const int channels = info.channels;
    try {
        readFrames(channels, framesFor(maxSeconds, audio.sampleRate), audio, [&](float* out, std::uint64_t n) {
            return static_cast<std::uint64_t>(
                stb_vorbis_get_samples_float_interleaved(vorbis, channels, out, static_cast<int>(n) * channels));
        });
    } catch (...) {
        stb_vorbis_close(vorbis);
        throw;
    }
    stb_vorbis_close(vorbis);
    return audio;
}

} // namespace

DecodedAudio decodeFile(const fs::path& path, double maxSeconds)
{
    DecodedAudio audio;
    switch (detectFormat(path)) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: audio = decodeWavOrAiff(path, maxSeconds); break;
    case AudioFormat::Flac: audio = decodeFlac(path, maxSeconds); break;
    case AudioFormat::Mp3: audio = decodeMp3(path, maxSeconds); break;
    case AudioFormat::Ogg: audio = decodeOgg(path, maxSeconds); break;
    }
    // Rates outside what audio hardware uses come from corrupt headers; below
    // ~90 Hz the analysis frames would have no hop at all.
    if (audio.sampleRate < 1000 || audio.sampleRate > 768000) throw ProbeError("unsupported sample rate");
    if (audio.mono.empty()) throw ProbeError("stream has no audio frames");
    return audio;
}
```

with:

```cpp
#include "asma/core/Decode.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace asma {

DecodedAudio decodeFile(const std::filesystem::path& path, double maxSeconds)
{
    const auto reader = AudioReader::open(path);
    DecodedAudio audio;
    audio.sampleRate = reader->sampleRate();
    const auto limit = static_cast<std::uint64_t>(std::ceil(maxSeconds * audio.sampleRate));
    // Read until the decoder stops rather than trusting the header's length.
    std::uint64_t have = 0;
    audio.mono.resize(static_cast<std::size_t>(std::min(limit, reader->frames())));
    while (have < limit) {
        if (have == audio.mono.size())
            audio.mono.resize(static_cast<std::size_t>(std::min<std::uint64_t>(limit, have + 65536)));
        const std::uint64_t want = audio.mono.size() - have;
        const std::uint64_t got = reader->readMono(audio.mono.data() + have, want);
        have += got;
        if (got < want) break;
    }
    audio.mono.resize(static_cast<std::size_t>(have));
    // Hit the limit: the file is longer if one more frame can be read.
    float extra = 0.0f;
    audio.truncated = audio.mono.size() == limit && reader->readMono(&extra, 1) > 0;
    if (audio.mono.empty()) throw ProbeError("stream has no audio frames");
    return audio;
}
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[reader]"`

Expected: no compiler warnings;
`All tests passed (44 assertions in 5 test cases)`. The decode and analysis
tests are unchanged and still pass. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 223`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: read audio in chunks, in stereo and from any frame"
```

---

### Task 2: The asma-audio library, decoded buffers and the preview cache

The new library starts with the whole-file case: an `AudioBuffer` in memory, a
`SampleSource` interface that playback reads through, and an LRU cache of
decoded files keyed by path and invalidated by size and mtime.

**Files:**

- Create: `audio/CMakeLists.txt`, `audio/include/asma/audio/SampleSource.h`,
  `audio/src/SampleSource.cpp`, `audio/include/asma/audio/PreviewCache.h`,
  `audio/src/PreviewCache.cpp`, `tests/test_sample_source.cpp`,
  `tests/test_preview_cache.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `AudioReader` (Task 1), `toUtf8`, `fileTimeToInt`.
- Produces: target `asma_audio` (alias `asma::audio`); namespace `asma::audio`;
  `struct AudioBuffer { int sampleRate; std::vector<std::vector<float>> channels; int channelCount() const; std::int64_t frames() const; std::size_t bytes() const; }`;
  `AudioBuffer loadAudio(const std::filesystem::path&)`,
  `AudioBuffer loadAudio(AudioReader&)`; `class SampleSource` with virtual
  `sampleRate()`, `channels()`, `frames()`,
  `bool read(std::int64_t start, int n, float* const* out)`,
  `void hint(std::int64_t frame, int direction)`;
  `class MemorySource(std::shared_ptr<const AudioBuffer>)` with
  `const AudioBuffer& buffer() const`;
  `class PreviewCache(std::size_t capacityBytes = 256 MB)` with `find(path)`,
  `load(path, AudioReader&)`, `get(path)` (all return
  `std::shared_ptr<const AudioBuffer>`), `bytes()`, `size()`.

Behaviour the tests pin:

- `MemorySource::read` zero-fills anything before frame 0 or past the end,
  including a read entirely outside the file.
- The cache shares one buffer between hits, decodes again when the file's size
  or mtime changes, evicts the least recently used first, returns without
  keeping a file larger than the whole cache, and reports a vanished file as
  `FileAccessError`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_preview_cache.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/PreviewCache.h"
#include "asma/core/AudioProbe.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

// n frames of mono float: n * 4 bytes in the cache.
fs::path writeMono(const TempDir& dir, const char* name, std::size_t n, float offset = 0.0f)
{
    const auto p = dir.path() / name;
    test::writeWavFloat(p, 44100, {test::ramp(n, 1.0f / 65536.0f, offset)});
    return p;
}

} // namespace

TEST_CASE("PreviewCache returns the same buffer until the file changes", "[cache]")
{
    TempDir dir;
    const auto p = writeMono(dir, "a.wav", 1000);
    PreviewCache cache;
    const auto first = cache.get(p);
    CHECK(cache.get(p) == first); // a hit shares the buffer
    CHECK(cache.size() == 1);
    CHECK(cache.bytes() == 4000);

    writeMono(dir, "a.wav", 1200); // other size
    const auto second = cache.get(p);
    CHECK(second != first);
    CHECK(second->frames() == 1200);
    CHECK(cache.bytes() == 4800);

    writeMono(dir, "a.wav", 1200, 0.5f); // same size, new mtime
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(5));
    const auto third = cache.get(p);
    CHECK(third != second);
    CHECK(third->channels[0][0] == 0.5f);
    CHECK(cache.size() == 1);
}

TEST_CASE("PreviewCache evicts the least recently used", "[cache]")
{
    TempDir dir;
    const auto a = writeMono(dir, "a.wav", 1000);
    const auto b = writeMono(dir, "b.wav", 1000);
    const auto c = writeMono(dir, "c.wav", 1000);
    const auto huge = writeMono(dir, "huge.wav", 5000);
    PreviewCache cache(10000); // room for two
    const auto bufA = cache.get(a);
    cache.get(b);
    CHECK(cache.get(a) == bufA); // a is now the most recent
    cache.get(c);                // evicts b
    CHECK(cache.size() == 2);
    CHECK(cache.bytes() == 8000);
    CHECK(cache.get(a) == bufA);

    const auto big = cache.get(huge); // larger than the cache: returned, not kept
    CHECK(big->frames() == 5000);
    CHECK(cache.size() == 2);
    CHECK(cache.get(a) == bufA);
}

TEST_CASE("PreviewCache reports a vanished file", "[cache]")
{
    TempDir dir;
    const auto p = writeMono(dir, "a.wav", 100);
    PreviewCache cache;
    cache.get(p);
    fs::remove(p);
    CHECK_THROWS_AS(cache.get(p), FileAccessError);
}
```

Create `tests/test_sample_source.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/SampleSource.h"
#include "asma/core/AudioProbe.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;

TEST_CASE("loadAudio decodes every frame of every channel", "[source]")
{
    TempDir dir;
    const auto p = dir.path() / "s.wav";
    const auto left = test::ramp(3000);
    const auto right = test::ramp(3000, 0.0f, 0.5f);
    test::writeWavFloat(p, 22050, {left, right});
    const AudioBuffer b = loadAudio(p);
    CHECK(b.sampleRate == 22050);
    CHECK(b.channelCount() == 2);
    CHECK(b.frames() == 3000);
    CHECK(b.bytes() == 2 * 3000 * sizeof(float));
    CHECK(b.channels[0] == left);
    CHECK(b.channels[1] == right);
    CHECK_THROWS_AS(loadAudio(dir.path() / "gone.wav"), FileAccessError);
}

TEST_CASE("MemorySource zero-fills outside the file", "[source]")
{
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->sampleRate = 44100;
    buffer->channels = {{1, 2, 3, 4}};
    MemorySource source(buffer);
    CHECK(source.frames() == 4);
    CHECK(source.channels() == 1);

    std::vector<float> out(6, -1.0f);
    float* dst[] = {out.data()};
    CHECK(source.read(-2, 6, dst));
    CHECK(out == std::vector<float>{0, 0, 1, 2, 3, 4});
    CHECK(source.read(2, 6, dst));
    CHECK(out == std::vector<float>{3, 4, 0, 0, 0, 0});
    CHECK(source.read(10, 6, dst)); // entirely past the end
    CHECK(out == std::vector<float>(6, 0.0f));
    CHECK(source.read(-10, 6, dst)); // entirely before the start
    CHECK(out == std::vector<float>(6, 0.0f));
}
```

In `tests/CMakeLists.txt`, replace:

```cmake

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma_cli_support Catch2::Catch2WithMain)
target_compile_definitions(asma_tests PRIVATE
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  ASMA_VERSION="${PROJECT_VERSION}"
```

with:

```cmake

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma_cli_support asma::audio Catch2::Catch2WithMain)
target_compile_definitions(asma_tests PRIVATE
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  ASMA_VERSION="${PROJECT_VERSION}"
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL at configure:

```
CMake Error at tests/CMakeLists.txt:14 (target_link_libraries):
  Target "asma_tests" links to:

    asma::audio
```

- [ ] **Step 3: Implement**

Create `audio/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0-only
file(GLOB ASMA_AUDIO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(asma_audio STATIC ${ASMA_AUDIO_SOURCES})
add_library(asma::audio ALIAS asma_audio)
target_include_directories(asma_audio
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_audio PUBLIC asma::core Threads::Threads)
asma_set_warnings(asma_audio)
```

Create `audio/include/asma/audio/PreviewCache.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleSource.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace asma {
class AudioReader;
}

namespace asma::audio {

// Recently decoded files, least recently used out first. A file edited on
// disk (other size or mtime) is decoded again. Thread-safe; decoding happens
// outside the lock.
class PreviewCache {
public:
    static constexpr std::size_t kDefaultCapacity = 256u << 20; // bytes of samples

    explicit PreviewCache(std::size_t capacityBytes = kDefaultCapacity) : capacity_(capacityBytes) {}

    // The cached buffer when the file has not changed since, else nullptr.
    // Throws FileAccessError when the file is gone.
    std::shared_ptr<const AudioBuffer> find(const std::filesystem::path& path);
    // Decodes through `reader`, freshly opened on `path`, and keeps the result.
    // Throws ProbeError.
    std::shared_ptr<const AudioBuffer> load(const std::filesystem::path& path, AudioReader& reader);
    // find, or open and load. Throws ProbeError (FileAccessError when the
    // file is gone).
    std::shared_ptr<const AudioBuffer> get(const std::filesystem::path& path);

    std::size_t bytes() const;
    std::size_t size() const;

private:
    struct Stamp {
        std::int64_t size = 0;
        std::int64_t mtime = 0;
        bool operator==(const Stamp&) const = default;
    };
    struct Entry {
        std::string key;
        Stamp stamp;
        std::shared_ptr<const AudioBuffer> buffer;
    };

    static Stamp stampOf(const std::filesystem::path& path);

    mutable std::mutex mutex_;
    std::size_t capacity_;
    std::size_t bytes_ = 0;
    std::list<Entry> entries_; // most recent first
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

} // namespace asma::audio
```

Create `audio/include/asma/audio/SampleSource.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma {
class AudioReader;
}

namespace asma::audio {

// A whole file decoded to planar float, one or two channels.
struct AudioBuffer {
    int sampleRate = 0;
    std::vector<std::vector<float>> channels;

    int channelCount() const { return static_cast<int>(channels.size()); }
    std::int64_t frames() const { return channels.empty() ? 0 : static_cast<std::int64_t>(channels[0].size()); }
    std::size_t bytes() const
    {
        return channels.size() * static_cast<std::size_t>(frames()) * sizeof(float);
    }
};

// Decodes every frame of a file, first two channels. Throws ProbeError.
AudioBuffer loadAudio(const std::filesystem::path& path);
// The same, through a reader that has not read anything yet.
AudioBuffer loadAudio(AudioReader& reader);

// What playback reads from. read() and hint() run on the audio thread: they
// never block, allocate or throw.
class SampleSource {
public:
    virtual ~SampleSource() = default;
    virtual int sampleRate() const = 0;
    virtual int channels() const = 0; // 1 or 2
    virtual std::int64_t frames() const = 0;
    // Copies frames [start, start + n) into channels() planar buffers. Frames
    // outside the file, or not loaded yet, come out as zeros; returns false
    // when any frame inside the file was not loaded (an underrun).
    virtual bool read(std::int64_t start, int n, float* const* out) = 0;
    // Where playback is and which way it is moving (+1 or -1), so a
    // streaming source can read ahead.
    virtual void hint(std::int64_t /*frame*/, int /*direction*/) {}
};

// A decoded file held in memory.
class MemorySource final : public SampleSource {
public:
    explicit MemorySource(std::shared_ptr<const AudioBuffer> buffer) : buffer_(std::move(buffer)) {}
    int sampleRate() const override { return buffer_->sampleRate; }
    int channels() const override { return buffer_->channelCount(); }
    std::int64_t frames() const override { return buffer_->frames(); }
    bool read(std::int64_t start, int n, float* const* out) override;
    const AudioBuffer& buffer() const { return *buffer_; }

private:
    std::shared_ptr<const AudioBuffer> buffer_;
};

} // namespace asma::audio
```

Create `audio/src/PreviewCache.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/PreviewCache.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"
#include "asma/core/Fs.h"

namespace fs = std::filesystem;

namespace asma::audio {

PreviewCache::Stamp PreviewCache::stampOf(const fs::path& path)
{
    std::error_code ec;
    Stamp stamp;
    stamp.size = static_cast<std::int64_t>(fs::file_size(path, ec));
    if (ec) throw FileAccessError("cannot open file");
    stamp.mtime = fileTimeToInt(fs::last_write_time(path, ec));
    if (ec) throw FileAccessError("cannot open file");
    return stamp;
}

std::shared_ptr<const AudioBuffer> PreviewCache::find(const fs::path& path)
{
    const Stamp stamp = stampOf(path);
    const std::string key = toUtf8(path);
    const std::lock_guard lock(mutex_);
    const auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    if (it->second->stamp == stamp) {
        entries_.splice(entries_.begin(), entries_, it->second);
        return it->second->buffer;
    }
    bytes_ -= it->second->buffer->bytes();
    entries_.erase(it->second);
    index_.erase(it);
    return nullptr;
}

std::shared_ptr<const AudioBuffer> PreviewCache::load(const fs::path& path, AudioReader& reader)
{
    // Stamp first: a file edited while it decodes is decoded again next time.
    const Stamp stamp = stampOf(path);
    auto buffer = std::make_shared<const AudioBuffer>(loadAudio(reader));
    const std::string key = toUtf8(path);
    const std::lock_guard lock(mutex_);
    if (buffer->bytes() > capacity_) return buffer; // too big to keep
    if (const auto it = index_.find(key); it != index_.end()) {
        bytes_ -= it->second->buffer->bytes();
        entries_.erase(it->second);
        index_.erase(it);
    }
    entries_.push_front({key, stamp, buffer});
    index_[key] = entries_.begin();
    bytes_ += buffer->bytes();
    while (bytes_ > capacity_) {
        bytes_ -= entries_.back().buffer->bytes();
        index_.erase(entries_.back().key);
        entries_.pop_back();
    }
    return buffer;
}

std::shared_ptr<const AudioBuffer> PreviewCache::get(const fs::path& path)
{
    if (auto hit = find(path)) return hit;
    return load(path, *AudioReader::open(path));
}

std::size_t PreviewCache::bytes() const
{
    const std::lock_guard lock(mutex_);
    return bytes_;
}

std::size_t PreviewCache::size() const
{
    const std::lock_guard lock(mutex_);
    return entries_.size();
}

} // namespace asma::audio
```

Create `audio/src/SampleSource.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/SampleSource.h"

#include "asma/core/AudioReader.h"

#include <algorithm>

namespace asma::audio {

AudioBuffer loadAudio(const std::filesystem::path& path) { return loadAudio(*AudioReader::open(path)); }

AudioBuffer loadAudio(AudioReader& reader)
{
    AudioBuffer buffer;
    buffer.sampleRate = reader.sampleRate();
    buffer.channels.assign(static_cast<std::size_t>(reader.channels()),
                           std::vector<float>(static_cast<std::size_t>(reader.frames())));
    float* out[2] = {buffer.channels[0].data(), buffer.channels.back().data()};
    const std::uint64_t got = reader.read(out, reader.frames());
    for (auto& channel : buffer.channels) channel.resize(static_cast<std::size_t>(got));
    return buffer;
}

bool MemorySource::read(std::int64_t start, int n, float* const* out)
{
    const std::int64_t from = std::max<std::int64_t>(start, 0);
    const std::int64_t to = std::min<std::int64_t>(start + n, buffer_->frames());
    for (int c = 0; c < channels(); ++c) {
        float* dst = out[c];
        std::fill(dst, dst + n, 0.0f);
        const float* src = buffer_->channels[static_cast<std::size_t>(c)].data();
        if (to > from) std::copy(src + from, src + to, dst + (from - start));
    }
    return true;
}

} // namespace asma::audio
```

In `CMakeLists.txt`, replace:

```cmake
include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(apps)

if(ASMA_BUILD_TESTS)
```

with:

```cmake
include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(apps)

if(ASMA_BUILD_TESTS)
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[source],[cache]"`

Expected: no compiler warnings;
`All tests passed (34 assertions in 5 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 228`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: add asma_audio with decoded buffers and a preview cache"
```

---

### Task 3: Streaming long files

Files over 10 s stream. The first 10 s and the last block are loaded at open;
the rest is read in blocks by `fill()` on the loader thread, ahead of the
playhead in the direction of travel. The audio thread and the loader share each
rolling slot through a reader count: the audio thread announces itself before
checking which block a slot holds, and `fill()` clears the block and waits for
readers to leave before overwriting it.

**Files:**

- Create: `audio/include/asma/audio/StreamSource.h`,
  `audio/src/StreamSource.cpp`, `tests/FakeReader.h`,
  `tests/test_stream_source.cpp`

**Interfaces:**

- Consumes: `AudioReader`, `SampleSource`, `MemorySource`, `PreviewCache` (Tasks
  1 and 2).
- Produces: `constexpr double kMaxMemorySeconds = 10.0`;
  `class StreamSource(std::unique_ptr<AudioReader>, double headSeconds = 10.0)`
  with `kBlockFrames = 32768`, `kSlots = 16`, `int fill(int maxBlocks = 4)`,
  `bool failed() const`, `std::int64_t underruns() const`;
  `std::shared_ptr<SampleSource> openSource(const std::filesystem::path&, PreviewCache&)`.
  Test double `asma::test::FakeReader(frames, channels = 1, sampleRate = 1000)`
  with `static float value(frame, channel)` and a `broken` flag.

Behaviour the tests pin:

- The head and tail play before any `fill()`; a block that is not loaded reads
  as silence and counts an underrun.
- `fill()` loads the playhead's block first, then ahead in the direction of
  travel (backwards in reverse), plus one behind; after a jump it reuses the
  slots left behind.
- When the reader fails mid-stream, streaming stops (`failed()`) and the head
  still plays.
- With a loader thread filling while reads jump around, a read never returns a
  half-written block.
- `openSource` keeps files of exactly 10 s in memory (and in the cache) and
  streams anything longer.

- [ ] **Step 1: Write the failing tests**

Create `tests/FakeReader.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// An AudioReader over computed audio: any length without a file on disk, and
// every frame recognisable by value.
#pragma once

#include "asma/core/AudioReader.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace asma::test {

class FakeReader final : public AudioReader {
public:
    FakeReader(std::uint64_t frames, int channels = 1, int sampleRate = 1000)
    {
        sampleRate_ = sampleRate;
        sourceChannels_ = channels;
        frames_ = frames;
        init();
    }

    // Exact in float for frames below 2^24.
    static float value(std::int64_t frame, int channel)
    {
        return static_cast<float>(frame) / 16777216.0f + static_cast<float>(channel);
    }

    // Seeks from now on fail, as when a drive goes away mid-stream.
    std::shared_ptr<std::atomic<bool>> broken = std::make_shared<std::atomic<bool>>(false);

protected:
    std::uint64_t readInterleaved(float* out, std::uint64_t n) override
    {
        std::uint64_t i = 0;
        for (; i < n && pos_ < frames_; ++i, ++pos_)
            for (int c = 0; c < sourceChannels_; ++c)
                out[i * static_cast<std::uint64_t>(sourceChannels_) + static_cast<std::uint64_t>(c)] =
                    value(static_cast<std::int64_t>(pos_), c);
        return i;
    }
    bool seekTo(std::uint64_t frame) override
    {
        if (broken->load()) return false;
        pos_ = frame;
        return true;
    }

private:
    std::uint64_t pos_ = 0;
};

} // namespace asma::test
```

Create `tests/test_stream_source.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "FakeReader.h"
#include "TestUtil.h"
#include "asma/audio/StreamSource.h"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

using namespace asma;
using namespace asma::audio;
using asma::test::FakeReader;
using asma::test::TempDir;

namespace {

constexpr std::int64_t B = StreamSource::kBlockFrames;

// Reads n frames at `start` and checks each against FakeReader::value.
// Returns whether read() said the frames were all there.
bool readAndCheck(StreamSource& s, std::int64_t start, int n)
{
    std::vector<float> l(static_cast<std::size_t>(n)), r(static_cast<std::size_t>(n));
    float* out[] = {l.data(), r.data()};
    const bool complete = s.read(start, n, out);
    for (int i = 0; i < n; ++i) {
        const std::int64_t frame = start + i;
        const bool inside = frame >= 0 && frame < s.frames();
        const float expect = complete && inside ? FakeReader::value(frame, 0) : 0.0f;
        if (complete || !inside) REQUIRE(l[static_cast<std::size_t>(i)] == expect);
        if (s.channels() == 2 && complete && inside) REQUIRE(r[static_cast<std::size_t>(i)] == FakeReader::value(frame, 1));
    }
    return complete;
}

std::unique_ptr<StreamSource> stream(std::int64_t blocks, int channels = 1, double headSeconds = 10.0)
{
    // 1 kHz: a 10 s head is one block.
    return std::make_unique<StreamSource>(
        std::make_unique<FakeReader>(static_cast<std::uint64_t>(blocks * B + 100), channels), headSeconds);
}

} // namespace

TEST_CASE("StreamSource plays its head and tail at once and the rest after fill", "[stream]")
{
    auto s = stream(7, 2); // 8 blocks, the last one short
    CHECK(s->frames() == 7 * B + 100);
    CHECK(s->channels() == 2);
    CHECK(readAndCheck(*s, 0, 4096));         // head
    CHECK(readAndCheck(*s, 7 * B, 100));      // tail
    CHECK(readAndCheck(*s, 7 * B + 50, 4096)); // runs past the end: zeros, still complete
    CHECK_FALSE(readAndCheck(*s, 3 * B, 64));  // not loaded yet
    CHECK(s->underruns() == 1);

    s->hint(3 * B + 10, 1);
    CHECK(s->fill(16) == 5); // 3, 4, 5, 6 ahead (7 is the tail) and 2 behind
    CHECK(readAndCheck(*s, 3 * B - 32, 64)); // across a block boundary
    CHECK(readAndCheck(*s, 2 * B, 4096));
    CHECK(readAndCheck(*s, 6 * B, 4096));
    CHECK_FALSE(readAndCheck(*s, B, 64)); // block 1 is two behind: not wanted
    CHECK(s->fill(16) == 0);             // nothing left to do here
}

TEST_CASE("StreamSource reads ahead backwards in reverse", "[stream]")
{
    auto s = stream(40);
    s->hint(30 * B, -1);
    CHECK(s->fill(1) == 1); // nearest first: the playhead's block
    CHECK(readAndCheck(*s, 30 * B, 64));
    CHECK_FALSE(readAndCheck(*s, 29 * B, 64));
    s->fill(100);
    CHECK(readAndCheck(*s, 16 * B, 64));  // 30 - 14
    CHECK(readAndCheck(*s, 31 * B, 64));  // one behind
    CHECK_FALSE(readAndCheck(*s, 15 * B, 64));
}

TEST_CASE("StreamSource reuses the slots the playhead left behind", "[stream]")
{
    auto s = stream(60);
    s->hint(2 * B, 1);
    s->fill(100);
    CHECK(readAndCheck(*s, 10 * B, 64));
    s->hint(40 * B, 1); // a jump: everything loaded is now useless
    CHECK(s->fill(100) == 16);
    CHECK_FALSE(readAndCheck(*s, 10 * B, 64));
    CHECK(readAndCheck(*s, 54 * B, 64));
    CHECK(readAndCheck(*s, 39 * B, 64));
}

TEST_CASE("StreamSource stops streaming when the file goes away", "[stream]")
{
    auto reader = std::make_unique<FakeReader>(static_cast<std::uint64_t>(20 * B));
    const auto broken = reader->broken;
    StreamSource s(std::move(reader));
    broken->store(true);
    s.hint(5 * B, 1);
    CHECK(s.fill() == 0);
    CHECK(s.failed());
    CHECK_FALSE(readAndCheck(s, 5 * B, 64));
    CHECK(readAndCheck(s, 0, 64)); // the head is still there
}

TEST_CASE("StreamSource never hands out a block while fill overwrites it", "[stream]")
{
    auto s = stream(200);
    std::atomic<bool> stop{false};
    std::thread loader([&] {
        while (!stop.load()) s->fill();
    });
    // Jump around so fill keeps recycling slots under the reads.
    std::int64_t complete = 0;
    for (int i = 0; i < 20000; ++i) {
        const std::int64_t at = (i * 7919 % 200) * B + i % 1000;
        s->hint(at, i % 2 ? 1 : -1);
        if (readAndCheck(*s, at, 256)) ++complete;
    }
    stop.store(true);
    loader.join();
    CHECK(complete > 0);
}

TEST_CASE("openSource keeps short files in memory and streams long ones", "[stream]")
{
    TempDir dir;
    const auto shortFile = dir.path() / "short.wav";
    const auto longFile = dir.path() / "long.wav";
    test::writeWavFloat(shortFile, 1000, {test::ramp(10000)}); // exactly 10 s
    test::writeWavFloat(longFile, 1000, {test::ramp(10001)});
    PreviewCache cache;
    const auto a = openSource(shortFile, cache);
    CHECK(dynamic_cast<MemorySource*>(a.get()));
    CHECK(cache.size() == 1);
    const auto again = openSource(shortFile, cache);
    CHECK(&dynamic_cast<MemorySource&>(*again).buffer() == &dynamic_cast<MemorySource&>(*a).buffer());
    const auto b = openSource(longFile, cache);
    CHECK(dynamic_cast<StreamSource*>(b.get()));
    CHECK(b->frames() == 10001);
    CHECK(cache.size() == 1);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_stream_source.cpp:4:10: fatal error: 'asma/audio/StreamSource.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/StreamSource.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PreviewCache.h"
#include "asma/audio/SampleSource.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma {
class AudioReader;
}

namespace asma::audio {

// Files up to this long are decoded whole; longer ones stream.
constexpr double kMaxMemorySeconds = 10.0;

// A long file read in blocks. The first kMaxMemorySeconds and the last block
// are loaded at open and stay, so playback starts at once in either direction;
// the rest is read ahead of the playhead, in the direction of travel, by
// fill() on a loader thread. read() on the audio thread never waits: a block
// that is not loaded yet plays as silence.
class StreamSource final : public SampleSource {
public:
    static constexpr std::int64_t kBlockFrames = 32768;
    static constexpr int kSlots = 16; // rolling blocks: about 12 s at 44.1 kHz

    // Loads the head and the tail block. Throws ProbeError.
    explicit StreamSource(std::unique_ptr<AudioReader> reader, double headSeconds = kMaxMemorySeconds);
    ~StreamSource() override;

    int sampleRate() const override { return sampleRate_; }
    int channels() const override { return channels_; }
    std::int64_t frames() const override { return frames_; }
    bool read(std::int64_t start, int n, float* const* out) override;
    void hint(std::int64_t frame, int direction) override;

    // Loader thread: loads up to maxBlocks missing blocks around the playhead,
    // nearest first. Returns how many it loaded. A read error ends streaming:
    // failed() turns true and the missing blocks stay silent.
    int fill(int maxBlocks = 4);
    bool failed() const { return failed_.load(); }
    // Blocks read() found missing, for tests and diagnostics.
    std::int64_t underruns() const { return underruns_.load(); }

private:
    struct Slot {
        std::atomic<std::int64_t> block{-1};
        std::atomic<int> readers{0};
        std::vector<float> data; // channels_ * kBlockFrames, planar
    };

    std::int64_t blockCount() const { return (frames_ + kBlockFrames - 1) / kBlockFrames; }
    bool pinned(std::int64_t block) const { return block < headBlocks_ || block == blockCount() - 1; }
    // Reads one block into planar channels `stride` floats apart.
    void loadBlock(std::int64_t block, float* base, std::int64_t stride);
    // Copies part of one block into out; false when the block is not loaded.
    bool copyFrom(std::int64_t block, std::int64_t offset, int n, float* const* out, int at);

    std::unique_ptr<AudioReader> reader_; // loader thread only after construction
    int sampleRate_ = 0;
    int channels_ = 0;
    std::int64_t frames_ = 0;
    std::int64_t headBlocks_ = 0;
    std::vector<float> head_; // planar, headBlocks_ * kBlockFrames per channel
    std::vector<float> tail_; // planar, kBlockFrames per channel
    std::array<Slot, kSlots> slots_;
    std::atomic<std::int64_t> playhead_{0};
    std::atomic<int> direction_{1};
    std::atomic<bool> failed_{false};
    std::atomic<std::int64_t> underruns_{0};
};

// A file ready to play: from the cache or decoded whole when it is at most
// kMaxMemorySeconds long, streamed otherwise. Throws ProbeError.
std::shared_ptr<SampleSource> openSource(const std::filesystem::path& path, PreviewCache& cache);

} // namespace asma::audio
```

Create `audio/src/StreamSource.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/StreamSource.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace asma::audio {

StreamSource::StreamSource(std::unique_ptr<AudioReader> reader, double headSeconds)
    : reader_(std::move(reader)),
      sampleRate_(reader_->sampleRate()),
      channels_(reader_->channels()),
      frames_(static_cast<std::int64_t>(reader_->frames()))
{
    const auto headFrames = static_cast<std::int64_t>(std::ceil(headSeconds * sampleRate_));
    headBlocks_ = std::min(blockCount(), (headFrames + kBlockFrames - 1) / kBlockFrames);
    const std::int64_t stride = headBlocks_ * kBlockFrames;
    head_.resize(static_cast<std::size_t>(channels_ * stride));
    for (std::int64_t b = 0; b < headBlocks_; ++b) loadBlock(b, head_.data() + b * kBlockFrames, stride);
    if (blockCount() > headBlocks_) {
        tail_.resize(static_cast<std::size_t>(channels_ * kBlockFrames));
        loadBlock(blockCount() - 1, tail_.data(), kBlockFrames);
    }
    for (auto& slot : slots_) slot.data.resize(static_cast<std::size_t>(channels_ * kBlockFrames));
}

StreamSource::~StreamSource() = default;

void StreamSource::loadBlock(std::int64_t block, float* base, std::int64_t stride)
{
    const std::int64_t start = block * kBlockFrames;
    const std::int64_t n = std::min(kBlockFrames, frames_ - start);
    float* out[2] = {base, base + (channels_ - 1) * stride};
    reader_->seek(static_cast<std::uint64_t>(start));
    const auto got = static_cast<std::int64_t>(reader_->read(out, static_cast<std::uint64_t>(n)));
    for (int c = 0; c < channels_; ++c) std::fill(out[c] + got, out[c] + kBlockFrames, 0.0f);
}

bool StreamSource::copyFrom(std::int64_t block, std::int64_t offset, int n, float* const* out, int at)
{
    const auto copy = [&](const float* base, std::int64_t stride) {
        for (int c = 0; c < channels_; ++c) {
            const float* src = base + c * stride + offset;
            std::copy(src, src + n, out[c] + at);
        }
    };
    if (block < headBlocks_) {
        copy(head_.data() + block * kBlockFrames, headBlocks_ * kBlockFrames);
        return true;
    }
    if (block == blockCount() - 1) {
        copy(tail_.data(), kBlockFrames);
        return true;
    }
    for (auto& slot : slots_) {
        // Announce the read before checking the block: fill() clears the block
        // and then waits for readers to leave before it overwrites the data.
        slot.readers.fetch_add(1);
        const bool here = slot.block.load() == block;
        if (here) copy(slot.data.data(), kBlockFrames);
        slot.readers.fetch_sub(1);
        if (here) return true;
    }
    return false;
}

bool StreamSource::read(std::int64_t start, int n, float* const* out)
{
    bool complete = true;
    int at = 0;
    while (at < n) {
        const std::int64_t pos = start + at;
        if (pos < 0 || pos >= frames_) {
            const int k = pos < 0 ? static_cast<int>(std::min<std::int64_t>(n - at, -pos)) : n - at;
            for (int c = 0; c < channels_; ++c) std::fill(out[c] + at, out[c] + at + k, 0.0f);
            at += k;
            continue;
        }
        const std::int64_t block = pos / kBlockFrames;
        const std::int64_t offset = pos % kBlockFrames;
        const int k = static_cast<int>(std::min({static_cast<std::int64_t>(n - at), kBlockFrames - offset, frames_ - pos}));
        if (!copyFrom(block, offset, k, out, at)) {
            for (int c = 0; c < channels_; ++c) std::fill(out[c] + at, out[c] + at + k, 0.0f);
            complete = false;
        }
        at += k;
    }
    if (!complete) underruns_.fetch_add(1, std::memory_order_relaxed);
    return complete;
}

void StreamSource::hint(std::int64_t frame, int direction)
{
    playhead_.store(frame, std::memory_order_relaxed);
    direction_.store(direction < 0 ? -1 : 1, std::memory_order_relaxed);
}

int StreamSource::fill(int maxBlocks)
{
    const std::int64_t count = blockCount();
    if (failed_.load() || count == 0) return 0;
    const std::int64_t here = std::clamp<std::int64_t>(playhead_.load(std::memory_order_relaxed) / kBlockFrames, 0, count - 1);
    const int dir = direction_.load(std::memory_order_relaxed);

    // The playhead's block, kSlots - 2 more in the direction of travel and one
    // behind (ping-pong turns around), nearest first; pinned blocks need no slot.
    std::vector<std::int64_t> wanted;
    for (std::int64_t i = 0; i < kSlots - 1; ++i) wanted.push_back(here + dir * i);
    wanted.push_back(here - dir);
    std::erase_if(wanted, [&](std::int64_t b) { return b < 0 || b >= count || pinned(b); });

    const auto isWanted = [&](std::int64_t b) { return std::find(wanted.begin(), wanted.end(), b) != wanted.end(); };
    const auto resident = [&](std::int64_t b) {
        return std::any_of(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.block.load() == b; });
    };

    int loaded = 0;
    for (const std::int64_t b : wanted) {
        if (loaded >= maxBlocks) break;
        if (resident(b)) continue;
        const auto slot = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) {
            const std::int64_t held = s.block.load();
            return held < 0 || !isWanted(held);
        });
        if (slot == slots_.end()) break;
        slot->block.store(-1);
        while (slot->readers.load() != 0) std::this_thread::yield();
        try {
            loadBlock(b, slot->data.data(), kBlockFrames);
        } catch (const ProbeError&) {
            failed_.store(true);
            return loaded;
        }
        slot->block.store(b);
        ++loaded;
    }
    return loaded;
}

std::shared_ptr<SampleSource> openSource(const std::filesystem::path& path, PreviewCache& cache)
{
    if (auto hit = cache.find(path)) return std::make_shared<MemorySource>(std::move(hit));
    auto reader = AudioReader::open(path);
    if (static_cast<double>(reader->frames()) <= kMaxMemorySeconds * reader->sampleRate())
        return std::make_shared<MemorySource>(cache.load(path, *reader));
    return std::make_shared<StreamSource>(std::move(reader));
}

} // namespace asma::audio
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[stream]"`

Expected: no compiler warnings;
`All tests passed (... assertions in 6 test cases)`. The assertion count varies
from run to run: the concurrency test checks every frame it managed to read.
Then `ctest --test-dir build --output-on-failure`:
`100% tests passed out of 234`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: stream long files in blocks read ahead of the playhead"
```

---

### Task 4: Loading previews off the audio thread

`Loader` owns the preview lifecycle. `select()` (control thread) queues the
newest request; `pump()` (loader thread, or a test) opens it, hands the result
to the audio thread through an `SpscQueue`, feeds streams and frees previews.
The audio thread calls `takeReady()` for the newest preview and `release()` with
the oldest generation it still plays. See the decision on the release cap: it is
what keeps a freshly taken preview alive.

**Files:**

- Create: `audio/include/asma/audio/SpscQueue.h`,
  `audio/include/asma/audio/SampleInfo.h`, `audio/include/asma/audio/Loader.h`,
  `audio/src/Loader.cpp`, `tests/test_loader.cpp`

**Interfaces:**

- Consumes: `openSource`, `StreamSource::fill`, `PreviewCache` (Tasks 2, 3).
- Produces: `template <typename T, std::size_t Capacity> class SpscQueue` with
  `bool push(const T&)`, `bool pop(T&)`;
  `struct SampleInfo { std::optional<double> bpm; double bpmConfidence; std::optional<std::string> key; double keyConfidence; std::optional<bool> isLoop; std::optional<double> lufs, peak; std::optional<int> rootNote; }`;
  `struct Preview { std::uint64_t generation; std::shared_ptr<SampleSource> source; SampleInfo info; std::string error; }`;
  `class Loader(PreviewCache&)` with `kNone`, `start()`,
  `std::uint64_t select(path, SampleInfo = {})`, `Preview* takeReady()`,
  `void release(std::uint64_t oldestInUse)`, `bool pump()`,
  `std::size_t liveCount() const`.

Behaviour the tests pin:

- A selection loads on `pump()` and is taken once; generations count from 1.
- A selection replaced before it loaded is never decoded.
- A file that cannot be opened arrives with a null source and a reason.
- A preview stays alive while the audio thread reports it (or anything older) in
  use, including one only a MIDI voice still plays, and is freed after.
- A preview taken after the audio thread's last report stays alive: the report
  is capped at one past the newest preview taken. This test fails without the
  cap.
- Previews the audio thread skipped (two arrived in one block) are freed.
- Streaming previews are fed by `pump()`; the loader also runs on its own
  thread.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_loader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/Loader.h"
#include "asma/audio/SpscQueue.h"
#include "asma/audio/StreamSource.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

struct Files {
    TempDir dir;
    fs::path a = dir.path() / "a.wav";
    fs::path b = dir.path() / "b.wav";
    fs::path longFile = dir.path() / "long.wav";
    Files()
    {
        test::writeWavFloat(a, 1000, {test::ramp(500)});
        test::writeWavFloat(b, 1000, {test::ramp(700)});
        test::writeWavFloat(longFile, 1000, {test::ramp(200000)}); // 200 s: streams
    }
};

} // namespace

TEST_CASE("SpscQueue holds its capacity and keeps order", "[loader]")
{
    SpscQueue<int, 3> q;
    CHECK(q.push(1));
    CHECK(q.push(2));
    CHECK(q.push(3));
    CHECK_FALSE(q.push(4));
    int x = 0;
    CHECK(q.pop(x));
    CHECK(x == 1);
    CHECK(q.push(4));
    for (int expect : {2, 3, 4}) {
        REQUIRE(q.pop(x));
        CHECK(x == expect);
    }
    CHECK_FALSE(q.pop(x));
}

TEST_CASE("Loader turns a selection into a preview", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    CHECK(loader.takeReady() == nullptr);
    SampleInfo info;
    info.bpm = 120.0;
    const auto g = loader.select(f.a, info);
    CHECK(g == 1);
    CHECK(loader.takeReady() == nullptr); // nothing loads until pump
    CHECK(loader.pump());
    Preview* p = loader.takeReady();
    REQUIRE(p);
    CHECK(p->generation == 1);
    REQUIRE(p->source);
    CHECK(p->source->frames() == 500);
    CHECK(p->info.bpm == 120.0);
    CHECK(loader.takeReady() == nullptr); // taken once
}

TEST_CASE("Loader skips selections replaced before they loaded", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.select(f.a);
    const auto g = loader.select(f.b);
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    CHECK(p->generation == g);
    CHECK(p->source->frames() == 700);
    CHECK(cache.size() == 1); // a was never decoded
}

TEST_CASE("Loader reports a file it cannot open", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.select(f.dir.path() / "gone.wav");
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    CHECK_FALSE(p->source);
    CHECK_FALSE(p->error.empty());
}

TEST_CASE("Loader frees a preview only once the audio thread is done with it", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.select(f.a);
    loader.pump();
    Preview* a = loader.takeReady();
    const std::weak_ptr<SampleSource> aSource = a->source;
    loader.release(a->generation);

    loader.select(f.b);
    loader.pump();
    Preview* b = loader.takeReady();
    REQUIRE(b);
    loader.pump();
    CHECK_FALSE(aSource.expired()); // the audio thread has not said it let go of a

    loader.release(a->generation); // a MIDI voice still rings on a
    loader.pump();
    CHECK_FALSE(aSource.expired());

    loader.release(b->generation);
    loader.pump();
    CHECK(aSource.expired());
    CHECK(loader.liveCount() == 1);
    CHECK(b->source->frames() == 700); // b stays while it is in use

    loader.release(Loader::kNone); // nothing plays
    loader.pump();
    CHECK(loader.liveCount() == 0);
}

TEST_CASE("Loader keeps a preview taken but not yet reported", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.select(f.a);
    loader.pump();
    REQUIRE(loader.takeReady());
    loader.release(Loader::kNone); // a stopped: nothing plays
    loader.select(f.b);
    loader.pump();
    Preview* b = loader.takeReady();
    REQUIRE(b);
    // The loader runs before the audio thread reports b: its last word is
    // still "nothing plays", said before b was taken.
    loader.pump();
    CHECK(loader.liveCount() == 1); // a is gone, b is not
    CHECK(b->source->frames() == 700);
}

TEST_CASE("Loader frees previews the audio thread skipped", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.select(f.a);
    loader.pump();
    loader.select(f.b);
    loader.pump();
    CHECK(loader.liveCount() == 2);
    Preview* b = loader.takeReady(); // pops a and b, keeps the newest
    REQUIRE(b);
    CHECK(b->source->frames() == 700);
    loader.release(b->generation);
    loader.pump();
    CHECK(loader.liveCount() == 1);
}

TEST_CASE("Loader feeds a streaming preview", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.select(f.longFile);
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    loader.release(p->generation);
    REQUIRE(dynamic_cast<StreamSource*>(p->source.get()));
    const std::int64_t at = 5 * StreamSource::kBlockFrames;
    std::vector<float> x(64);
    float* out[] = {x.data()};
    CHECK_FALSE(p->source->read(at, 64, out));
    p->source->hint(at, 1);
    while (loader.pump()) {
    }
    CHECK(p->source->read(at, 64, out));
    CHECK(x[0] == static_cast<float>(at) / 65536.0f);
}

TEST_CASE("Loader runs on its own thread", "[loader]")
{
    Files f;
    PreviewCache cache;
    Loader loader(cache);
    loader.start();
    loader.select(f.b);
    Preview* p = nullptr;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!p && std::chrono::steady_clock::now() < deadline) {
        p = loader.takeReady();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(p);
    CHECK(p->source->frames() == 700);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_loader.cpp:3:10: fatal error: 'asma/audio/Loader.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/Loader.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PreviewCache.h"
#include "asma/audio/SampleInfo.h"
#include "asma/audio/SampleSource.h"
#include "asma/audio/SpscQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace asma::audio {

// A selected file, ready to play. Generations count selections from 1.
struct Preview {
    std::uint64_t generation = 0;
    std::shared_ptr<SampleSource> source; // null when the file could not be opened
    SampleInfo info;
    std::string error; // why source is null
};

// Turns selections into previews off the audio thread, keeps streaming
// sources fed, and frees previews once the audio thread is done with them.
//
// Threads: select() from one control thread; takeReady() and release() from
// the audio thread; pump() from the loader thread that start() runs, or from
// a test that never calls start().
class Loader {
public:
    static constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

    explicit Loader(PreviewCache& cache) : cache_(cache) {}
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    void start();

    // Asks for a file; a newer selection replaces one not loaded yet.
    // Returns the selection's generation.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info = {});

    // Audio thread: the newest preview that arrived since the last call, or
    // null. It stays valid while its generation is at or above what the audio
    // thread passes to release().
    Preview* takeReady();
    // Audio thread: the oldest generation it still plays (kNone for none).
    void release(std::uint64_t oldestInUse);

    // Loads the pending selection, hands finished previews to the audio
    // thread, feeds streams and frees what the audio thread let go. Returns
    // whether it did anything.
    bool pump();

    // Previews alive, for tests.
    std::size_t liveCount() const;

private:
    struct Request {
        std::uint64_t generation = 0;
        std::filesystem::path path;
        SampleInfo info;
    };
    struct Live {
        std::shared_ptr<Preview> preview;
        bool delivered = false;
    };

    PreviewCache& cache_;
    std::mutex requestMutex_;
    std::condition_variable wake_;
    std::optional<Request> request_;
    std::uint64_t nextGeneration_ = 1;

    std::vector<Live> live_; // pump() only, oldest first
    mutable std::mutex liveMutex_; // guards live_ against liveCount()
    SpscQueue<Preview*, 16> ready_;
    std::atomic<std::uint64_t> seen_{0};         // newest generation takeReady popped
    std::atomic<std::uint64_t> inUseFrom_{1};    // nothing older is in use

    std::thread thread_;
    std::atomic<bool> stop_{false};
};

} // namespace asma::audio
```

Create `audio/include/asma/audio/SampleInfo.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>

namespace asma::audio {

// What the library knows about a sample, as audition needs it. Everything is
// optional: an unscanned file plays unsynced at unity gain from note 60.
struct SampleInfo {
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    std::optional<std::string> key; // canonical, as parseKeyToken: "C", "F#m"
    double keyConfidence = 0.0;
    std::optional<bool> isLoop;
    std::optional<double> lufs; // integrated loudness
    std::optional<double> peak; // linear sample peak
    std::optional<int> rootNote; // MIDI note from a smpl chunk
};

} // namespace asma::audio
```

Create `audio/include/asma/audio/SpscQueue.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace asma::audio {

// Fixed-capacity queue for one producer thread and one consumer thread.
// push and pop never block or allocate, so either side can be the audio
// thread.
template <typename T, std::size_t Capacity>
class SpscQueue {
public:
    // Producer. False when full.
    bool push(const T& value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) % kSlots;
        if (next == head_.load(std::memory_order_acquire)) return false;
        slots_[tail] = value;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer. False when empty.
    bool pop(T& out)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        out = slots_[head];
        head_.store((head + 1) % kSlots, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kSlots = Capacity + 1; // one slot tells full from empty
    std::array<T, kSlots> slots_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

} // namespace asma::audio
```

Create `audio/src/Loader.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Loader.h"

#include "asma/audio/StreamSource.h"
#include "asma/core/AudioProbe.h"

#include <algorithm>
#include <chrono>

namespace asma::audio {

Loader::~Loader()
{
    if (thread_.joinable()) {
        {
            const std::lock_guard lock(requestMutex_);
            stop_.store(true);
        }
        wake_.notify_all();
        thread_.join();
    }
}

void Loader::start()
{
    thread_ = std::thread([this] {
        while (!stop_.load()) {
            if (pump()) continue;
            // Idle: wake for a selection, or after a few ms to feed streams.
            std::unique_lock lock(requestMutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(5), [this] { return stop_.load() || request_.has_value(); });
        }
    });
}

std::uint64_t Loader::select(const std::filesystem::path& path, SampleInfo info)
{
    std::uint64_t generation = 0;
    {
        const std::lock_guard lock(requestMutex_);
        generation = nextGeneration_++;
        request_ = Request{generation, path, std::move(info)};
    }
    wake_.notify_one();
    return generation;
}

Preview* Loader::takeReady()
{
    Preview* newest = nullptr;
    Preview* next = nullptr;
    while (ready_.pop(next)) newest = next;
    if (newest) seen_.store(newest->generation);
    return newest;
}

void Loader::release(std::uint64_t oldestInUse)
{
    // Never above one past the newest preview taken: a preview still in the
    // queue, or taken but not yet reported, must look in use.
    inUseFrom_.store(std::min(oldestInUse, seen_.load() + 1));
}

bool Loader::pump()
{
    bool did = false;

    std::optional<Request> request;
    {
        const std::lock_guard lock(requestMutex_);
        request.swap(request_);
    }
    if (request) {
        auto preview = std::make_shared<Preview>();
        preview->generation = request->generation;
        preview->info = std::move(request->info);
        try {
            preview->source = openSource(request->path, cache_);
        } catch (const ProbeError& e) {
            preview->error = e.what();
        }
        const std::lock_guard lock(liveMutex_);
        live_.push_back({std::move(preview), false});
        did = true;
    }

    const std::lock_guard lock(liveMutex_);
    if (!live_.empty() && !live_.back().delivered && ready_.push(live_.back().preview.get())) {
        live_.back().delivered = true;
        did = true;
    }

    const std::uint64_t from = inUseFrom_.load();
    const std::uint64_t seen = seen_.load();
    for (const Live& l : live_)
        if (!l.delivered || l.preview->generation >= from)
            if (auto* stream = dynamic_cast<StreamSource*>(l.preview->source.get())) did |= stream->fill() > 0;

    const Preview* newest = live_.empty() ? nullptr : live_.back().preview.get();
    const auto before = live_.size();
    std::erase_if(live_, [&](const Live& l) {
        if (!l.delivered) return l.preview.get() != newest; // superseded before the audio thread saw it
        return l.preview->generation <= seen && l.preview->generation < from;
    });
    return did || live_.size() != before;
}

std::size_t Loader::liveCount() const
{
    const std::lock_guard lock(liveMutex_);
    return live_.size();
}

} // namespace asma::audio
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[loader]"`

Expected: no compiler warnings;
`All tests passed (53 assertions in 9 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 243`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: load previews off the audio thread and free them when it lets go"
```

---

### Task 5: Playing a region

`PlayHead` is the one reader both the audition chain and the MIDI voices use:
trim, direction, looping, cubic resampling to the output rate and a speed factor
on top, with 5 ms fades only at edges that would click. Equal rates at speed 1
copy samples exactly, because Catmull-Rom interpolation passes through its
points.

**Files:**

- Create: `audio/include/asma/audio/PlayHead.h`, `audio/src/PlayHead.cpp`,
  `tests/test_play_head.cpp`

**Interfaces:**

- Consumes: `SampleSource`, `MemorySource` (Task 2).
- Produces: `enum class Direction { Forward, Reverse, PingPong }`;
  `struct PlayOptions { std::int64_t trimStart = 0; std::int64_t trimEnd = -1; Direction direction; bool loop; double speed = 1.0; }`;
  `class PlayHead` with `kFadeSeconds = 0.005`, `kWindowFrames = 8192`,
  `void prepare(int outputRate)`,
  `void start(SampleSource&, const PlayOptions&)`, `void stop()`,
  `bool active() const`, `int render(float* const* out, int n)` (stereo; returns
  frames that carried sound), `double position() const`,
  `int direction() const`.

Behaviour the tests pin:

- A forward one-shot plays untouched and stops; mono comes out on both sides;
  stereo stays stereo.
- Reverse plays backwards with a fade-in at the file's end and a fade-out at its
  start.
- Trim points fade; a forward start at frame 0 and a one-shot's own end do not.
- An untrimmed forward loop wraps seamlessly; a trimmed loop fades at the wrap.
- Ping-pong turns without repeating the turning sample; without looping it goes
  there and back once.
- Upsampling interpolates (exactly, on a straight line); a 440 Hz tone at 44.1
  kHz stays 440 Hz at the same level at 48 kHz; speed 2 is twice as fast.
- An empty region plays nothing; a one-frame region too short to turn plays
  forward once.
- The source is told where playback is and which way it is going.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_play_head.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/audio/PlayHead.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

using namespace asma::audio;

namespace {

// 1, 2, 3, ... so a zero always means silence or a fade.
std::shared_ptr<AudioBuffer> counting(int frames, int rate = 1000, int channels = 1)
{
    auto b = std::make_shared<AudioBuffer>();
    b->sampleRate = rate;
    for (int c = 0; c < channels; ++c) {
        std::vector<float> ch(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i) ch[static_cast<std::size_t>(i)] = static_cast<float>(i + 1) * (c == 0 ? 1.0f : -1.0f);
        b->channels.push_back(std::move(ch));
    }
    return b;
}

struct Rendered {
    std::vector<float> left, right;
    int sounding = 0;
};

Rendered render(PlayHead& head, int n)
{
    Rendered r;
    r.left.assign(static_cast<std::size_t>(n), 99.0f);
    r.right.assign(static_cast<std::size_t>(n), 99.0f);
    float* out[] = {r.left.data(), r.right.data()};
    r.sounding = head.render(out, n);
    return r;
}

std::vector<float> slice(const std::vector<float>& v, int from, int to)
{
    return {v.begin() + from, v.begin() + to};
}

std::vector<float> values(std::initializer_list<int> frames)
{
    std::vector<float> out;
    for (int f : frames) out.push_back(static_cast<float>(f + 1)); // counting() value of a frame
    return out;
}

// Fades are 5 ms: 5 frames at 1 kHz.
constexpr int kFade = 5;

} // namespace

TEST_CASE("PlayHead plays a one-shot forward untouched, then stops", "[playhead]")
{
    MemorySource src(counting(20));
    PlayHead head;
    head.prepare(1000);
    head.start(src, {});
    const Rendered r = render(head, 30);
    CHECK(r.sounding == 20);
    for (int i = 0; i < 20; ++i) CHECK(r.left[static_cast<std::size_t>(i)] == static_cast<float>(i + 1)); // no fades
    CHECK(slice(r.left, 20, 30) == std::vector<float>(10, 0.0f));
    CHECK(r.right == r.left); // mono on both sides
    CHECK_FALSE(head.active());
}

TEST_CASE("PlayHead plays stereo as stereo", "[playhead]")
{
    MemorySource src(counting(10, 1000, 2));
    PlayHead head;
    head.prepare(1000);
    head.start(src, {});
    const Rendered r = render(head, 10);
    CHECK(r.left[3] == 4.0f);
    CHECK(r.right[3] == -4.0f);
}

TEST_CASE("PlayHead plays reversed, fading in where the file ends", "[playhead]")
{
    MemorySource src(counting(20));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.direction = Direction::Reverse;
    head.start(src, o);
    const Rendered r = render(head, 20);
    CHECK(r.sounding == 20);
    for (int i = kFade; i < 20 - kFade; ++i) CHECK(r.left[static_cast<std::size_t>(i)] == static_cast<float>(20 - i));
    CHECK(r.left[0] < r.left[kFade]);                      // faded in
    CHECK(r.left[19] < 1.0f);                              // and out: a reversed attack would click
    CHECK(r.left[0] == Catch::Approx(20.0f / kFade));      // 1/5 of frame 19's value
}

TEST_CASE("PlayHead fades at trim points but not at the file's edges", "[playhead]")
{
    MemorySource src(counting(100));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.trimStart = 10;
    o.trimEnd = 40;
    head.start(src, o);
    const Rendered r = render(head, 40);
    CHECK(r.sounding == 30);
    CHECK(r.left[0] == Catch::Approx(11.0f / kFade)); // frame 10 at 1/5
    CHECK(r.left[kFade] == 16.0f);                    // frame 15, full
    CHECK(r.left[29] == Catch::Approx(40.0f / kFade)); // frame 39, fading out
    CHECK(slice(r.left, 30, 40) == std::vector<float>(10, 0.0f));

    o.trimStart = 0;
    o.trimEnd = -1;
    head.start(src, o);
    const Rendered whole = render(head, 100);
    CHECK(whole.left[0] == 1.0f);
    CHECK(whole.left[99] == 100.0f);
}

TEST_CASE("PlayHead loops an untrimmed forward loop seamlessly", "[playhead]")
{
    MemorySource src(counting(8));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.loop = true;
    head.start(src, o);
    const Rendered r = render(head, 20);
    CHECK(r.sounding == 20);
    for (int i = 0; i < 20; ++i) CHECK(r.left[static_cast<std::size_t>(i)] == static_cast<float>(i % 8 + 1));
    CHECK(head.active());
}

TEST_CASE("PlayHead fades a trimmed loop at its wrap", "[playhead]")
{
    MemorySource src(counting(100));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.loop = true;
    o.trimStart = 20;
    o.trimEnd = 50;
    head.start(src, o);
    const Rendered r = render(head, 90);
    CHECK(r.left[29] == Catch::Approx(50.0f / kFade)); // last frame of a pass, faded
    CHECK(r.left[30] == Catch::Approx(21.0f / kFade)); // first frame of the next
    CHECK(r.left[45] == 36.0f);
}

TEST_CASE("PlayHead ping-pongs without repeating the turning sample", "[playhead]")
{
    MemorySource src(counting(6));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.direction = Direction::PingPong;
    o.loop = true;
    head.start(src, o);
    const Rendered r = render(head, 16);
    // 0 1 2 3 4 5 4 3 2 1 0 1 2 3 4 5: turns are continuous, so no fades.
    CHECK(r.left == values({0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5}));

    o.loop = false;
    head.start(src, o);
    const Rendered once = render(head, 16);
    CHECK(once.sounding == 11); // forward, back, stop
    CHECK(slice(once.left, 0, 6) == values({0, 1, 2, 3, 4, 5}));
    CHECK(slice(once.left, 11, 16) == std::vector<float>(5, 0.0f));
}

TEST_CASE("PlayHead converts sample rates", "[playhead]")
{
    SECTION("upsampling interpolates between frames")
    {
        MemorySource src(counting(100));
        PlayHead head;
        head.prepare(2000);
        head.start(src, {});
        const Rendered r = render(head, 200);
        CHECK(r.sounding == 200);
        CHECK(r.left[20] == Catch::Approx(11.0f));  // frame 10
        CHECK(r.left[21] == Catch::Approx(11.5f));  // halfway to frame 11: exact on a straight line
    }
    SECTION("a 440 Hz tone at 44.1 kHz stays 440 Hz at 48 kHz")
    {
        auto b = std::make_shared<AudioBuffer>();
        b->sampleRate = 44100;
        b->channels.push_back(asma::test::sine(440.0, 1.0, 0.5, 44100));
        MemorySource src(b);
        PlayHead head;
        head.prepare(48000);
        head.start(src, {});
        const Rendered r = render(head, 48000);
        CHECK(r.sounding == Catch::Approx(48000).margin(1));
        int crossings = 0;
        float peak = 0.0f;
        for (std::size_t i = 1; i < 47000; ++i) {
            if ((r.left[i - 1] < 0.0f) != (r.left[i] < 0.0f)) ++crossings;
            peak = std::max(peak, std::abs(r.left[i]));
        }
        CHECK(crossings / 2.0 / (47000.0 / 48000.0) == Catch::Approx(440.0).margin(2.0));
        CHECK(peak == Catch::Approx(0.5).margin(0.01));
    }
    SECTION("speed doubles the pitch and halves the length")
    {
        MemorySource src(counting(100));
        PlayHead head;
        head.prepare(1000);
        PlayOptions o;
        o.speed = 2.0;
        head.start(src, o);
        const Rendered r = render(head, 100);
        CHECK(r.sounding == 50);
        CHECK(r.left[10] == Catch::Approx(21.0f)); // frame 20
    }
}

TEST_CASE("PlayHead ignores an empty region and plays a single frame", "[playhead]")
{
    MemorySource src(counting(10));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.trimStart = 5;
    o.trimEnd = 5;
    head.start(src, o);
    CHECK_FALSE(head.active());
    CHECK(render(head, 4).sounding == 0);

    o.trimEnd = 6;
    o.direction = Direction::PingPong; // too short to turn: plays forward once
    head.start(src, o);
    CHECK(render(head, 4).sounding == 1);
}

namespace {

struct HintSpy final : SampleSource {
    MemorySource inner{counting(100)};
    std::int64_t frame = -1;
    int direction = 0;
    int sampleRate() const override { return inner.sampleRate(); }
    int channels() const override { return inner.channels(); }
    std::int64_t frames() const override { return inner.frames(); }
    bool read(std::int64_t start, int n, float* const* out) override { return inner.read(start, n, out); }
    void hint(std::int64_t f, int d) override
    {
        frame = f;
        direction = d;
    }
};

} // namespace

TEST_CASE("PlayHead tells the source where it is going", "[playhead]")
{
    HintSpy spy;
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.direction = Direction::Reverse;
    head.start(spy, o);
    render(head, 10);
    CHECK(spy.frame == 99);
    CHECK(spy.direction == -1);
    render(head, 10);
    CHECK(spy.frame == 89);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_play_head.cpp:3:10: fatal error: 'asma/audio/PlayHead.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/PlayHead.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleSource.h"

#include <array>
#include <cstdint>
#include <vector>

namespace asma::audio {

enum class Direction { Forward, Reverse, PingPong };

struct PlayOptions {
    std::int64_t trimStart = 0; // source frames
    std::int64_t trimEnd = -1;  // exclusive; -1 or past the end means the end of the file
    Direction direction = Direction::Forward;
    bool loop = false;
    double speed = 1.0; // on top of the sample-rate conversion; 2 is an octave up and twice as fast
};

// Reads a region of a source at the output sample rate: trim, direction,
// looping and resampling (cubic, so equal rates at speed 1 copy samples
// exactly). Edges that would click get a 5 ms fade; edges a sound was made to
// have do not: a forward start at frame 0, a one-shot's own end, and the wrap
// of an untrimmed forward loop.
//
// prepare() allocates; everything else is safe on the audio thread.
class PlayHead {
public:
    static constexpr double kFadeSeconds = 0.005;
    static constexpr int kWindowFrames = 8192; // source frames read per step

    void prepare(int outputRate);
    // Does nothing when the region is empty.
    void start(SampleSource& source, const PlayOptions& options);
    void stop() { active_ = false; }
    bool active() const { return active_; }

    // Writes n frames of stereo (mono sources on both sides) and returns how
    // many carried sound; the rest are zeros once the region has played out.
    int render(float* const* out, int n);

    double position() const { return pos_; } // source frames
    int direction() const { return dir_; }    // +1 or -1

private:
    void beginPass(bool first);
    void endPass();
    float fade(double p) const;

    int outputRate_ = 44100;
    std::array<std::vector<float>, 2> window_;

    SampleSource* source_ = nullptr;
    bool active_ = false;
    double pos_ = 0.0;
    double step_ = 1.0;
    int dir_ = 1;
    std::int64_t a_ = 0, b_ = 0; // region [a_, b_)
    bool loop_ = false, pingPong_ = false, seamless_ = false;
    int passes_ = 0;
    bool fadeStart_ = false, fadeEnd_ = false;
    double fadeSource_ = 1.0; // fade length in source frames
};

} // namespace asma::audio
```

Create `audio/src/PlayHead.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/PlayHead.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

namespace {

// Catmull-Rom through y1 (f = 0) and y2 (f = 1).
float cubic(float y0, float y1, float y2, float y3, float f)
{
    return y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0)));
}

} // namespace

void PlayHead::prepare(int outputRate)
{
    outputRate_ = outputRate;
    for (auto& w : window_) w.assign(kWindowFrames, 0.0f);
}

void PlayHead::start(SampleSource& source, const PlayOptions& options)
{
    const std::int64_t frames = source.frames();
    source_ = &source;
    a_ = std::clamp<std::int64_t>(options.trimStart, 0, frames);
    b_ = options.trimEnd < 0 || options.trimEnd > frames ? frames : std::max(options.trimEnd, a_);
    active_ = b_ > a_;
    if (!active_) return;
    step_ = static_cast<double>(source.sampleRate()) / outputRate_ * std::max(options.speed, 1e-3);
    loop_ = options.loop;
    pingPong_ = options.direction == Direction::PingPong && b_ - a_ >= 2;
    seamless_ = loop_ && options.direction == Direction::Forward && a_ == 0 && b_ == frames;
    dir_ = options.direction == Direction::Reverse ? -1 : 1;
    pos_ = dir_ > 0 ? static_cast<double>(a_) : static_cast<double>(b_ - 1);
    passes_ = 0;
    fadeSource_ = std::max(1.0, kFadeSeconds * outputRate_ * step_);
    beginPass(true);
}

void PlayHead::beginPass(bool first)
{
    const bool forward = dir_ > 0;
    fadeStart_ = first ? !(forward && a_ == 0) : !(pingPong_ || seamless_);
    if (pingPong_) fadeEnd_ = !loop_ && passes_ == 1; // the backward pass that ends playback
    else if (loop_) fadeEnd_ = !seamless_;
    else fadeEnd_ = !(forward && b_ == source_->frames());
}

void PlayHead::endPass()
{
    ++passes_;
    const auto a = static_cast<double>(a_);
    const auto last = static_cast<double>(b_ - 1);
    if (pingPong_) {
        if (!loop_ && passes_ >= 2) {
            active_ = false;
            return;
        }
        // Mirror around the end sample, so the turn neither repeats nor skips it.
        pos_ = dir_ > 0 ? 2.0 * last - pos_ : 2.0 * a - pos_;
        pos_ = std::clamp(pos_, a, last);
        dir_ = -dir_;
    } else if (loop_) {
        const auto length = static_cast<double>(b_ - a_);
        pos_ = dir_ > 0 ? a + std::fmod(pos_ - a, length) : last - std::fmod(last - pos_, length);
    } else {
        active_ = false;
        return;
    }
    beginPass(false);
}

float PlayHead::fade(double p) const
{
    double gain = 1.0;
    const auto a = static_cast<double>(a_);
    const auto last = static_cast<double>(b_ - 1);
    if (fadeStart_) gain = std::min(gain, ((dir_ > 0 ? p - a : last - p) + 1.0) / fadeSource_);
    if (fadeEnd_) gain = std::min(gain, ((dir_ > 0 ? last - p : p - a) + 1.0) / fadeSource_);
    return static_cast<float>(std::clamp(gain, 0.0, 1.0));
}

int PlayHead::render(float* const* out, int n)
{
    int done = 0;
    const int channels = source_ ? source_->channels() : 1;
    while (done < n && active_) {
        // Output frames left in this pass: positions stay below b_ going
        // forward and above a_ - 1 going backward.
        const double remain = dir_ > 0 ? static_cast<double>(b_) - pos_ : pos_ - static_cast<double>(a_ - 1);
        const int left = std::max(1, static_cast<int>(std::ceil(remain / step_)));
        const int fits = std::max(1, static_cast<int>((kWindowFrames - 4) / step_));
        const int seg = std::min({n - done, left, fits});

        const double end = pos_ + dir_ * step_ * (seg - 1);
        const auto lo = static_cast<std::int64_t>(std::floor(std::min(pos_, end))) - 1;
        const auto len = static_cast<int>(static_cast<std::int64_t>(std::floor(std::max(pos_, end))) + 3 - lo);
        float* window[2] = {window_[0].data(), window_[1].data()};
        source_->hint(static_cast<std::int64_t>(pos_), dir_);
        source_->read(lo, len, window);

        for (int i = 0; i < seg; ++i) {
            const double p = pos_ + dir_ * step_ * i;
            const double whole = std::floor(p);
            const auto f = static_cast<float>(p - whole);
            const auto at = static_cast<std::size_t>(static_cast<std::int64_t>(whole) - lo);
            const float gain = fade(p);
            for (int c = 0; c < channels; ++c) {
                const float* w = window[c];
                out[c][done + i] = gain * cubic(w[at - 1], w[at], w[at + 1], w[at + 2], f);
            }
            if (channels == 1) out[1][done + i] = out[0][done + i];
        }
        pos_ += dir_ * step_ * seg;
        done += seg;
        if (seg == left) endPass();
    }
    for (int c = 0; c < 2; ++c) std::fill(out[c] + done, out[c] + n, 0.0f);
    return done;
}

} // namespace asma::audio
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[playhead]"`

Expected: no compiler warnings;
`All tests passed (90 assertions in 10 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 253`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: play a region forward, reversed or ping-pong at any sample rate"
```

---

### Task 6: Stretch and transpose

`Stretcher` puts Signalsmith Stretch after a `PlayHead`. A sound with neutral
timing and bypass allowed passes through untouched. A stretched sound is
pre-rolled with `outputSeek`, and keeps running after the `PlayHead` finishes
until its tail is out. Signalsmith Stretch arrives through FetchContent pinned
to the 1.4.0 commit; it fetches Signalsmith Linear 0.6.4 itself.

**Files:**

- Create: `audio/include/asma/audio/Stretcher.h`, `audio/src/Stretcher.cpp`,
  `tests/test_stretcher.cpp`
- Modify: `cmake/Dependencies.cmake`, `audio/CMakeLists.txt`

**Interfaces:**

- Consumes: `PlayHead` (Task 5).
- Produces: `class Stretcher` with `kMinRatio = 0.25`, `kMaxRatio = 4.0`,
  `void prepare(int sampleRate, int maxBlock)`,
  `void setTiming(double ratio, double semitones)` (ratio: input frames per
  output frame), `double ratio() const`, `double semitones() const`,
  `void start(PlayHead&, bool allowBypass)`,
  `int process(PlayHead&, float* const* out, int n)`,
  `bool active(const PlayHead&) const`, `bool bypassed() const`.

Behaviour the tests pin:

- Neutral timing with bypass allowed copies the `PlayHead`'s output exactly.
- Ratio 2 plays a 2 s tone in 1 s at the same pitch (measured: 1.017 s, 440.0
  Hz); +12 semitones doubles the pitch at the same length (1.002 s, 880.0 Hz).
- A tone after 50 ms of silence starts at 50 ms, not one stretch latency later
  (measured 0.0500 s).
- At half speed a 0.5 s sound lasts about 1 s and the stretch then stops, its
  last 512 frames silent rather than cut off.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_stretcher.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/audio/Stretcher.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

using namespace asma::audio;

namespace {

constexpr int kRate = 44100;
constexpr int kBlock = 512;

std::shared_ptr<AudioBuffer> mono(std::vector<float> samples)
{
    auto b = std::make_shared<AudioBuffer>();
    b->sampleRate = kRate;
    b->channels.push_back(std::move(samples));
    return b;
}

// Plays the whole sound through the stretcher in blocks; returns the left channel.
std::vector<float> play(Stretcher& st, SampleSource& src, double ratio, double semitones, bool allowBypass,
                        int maxFrames)
{
    PlayHead head;
    head.prepare(kRate);
    head.start(src, {});
    st.setTiming(ratio, semitones);
    st.start(head, allowBypass);
    std::vector<float> left, l(kBlock), r(kBlock);
    float* out[] = {l.data(), r.data()};
    while (st.active(head) && static_cast<int>(left.size()) < maxFrames) {
        st.process(head, out, kBlock);
        left.insert(left.end(), l.begin(), l.end());
    }
    return left;
}

// Frequency from zero crossings over [from, to).
double frequency(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    int crossings = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
    return crossings / 2.0 / (static_cast<double>(to - from) / kRate);
}

// Seconds until the sound falls below 0.05 (-26 dB against 0.5) for good:
// where it ends, not where the stretch's decay dies out.
double soundingSeconds(const std::vector<float>& x)
{
    std::size_t last = 0;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs(x[i]) > 0.05f) last = i;
    return static_cast<double>(last) / kRate;
}

} // namespace

TEST_CASE("Stretcher at neutral timing passes the sound through untouched", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 0.3, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 1.0, 0.0, true, kRate);
    CHECK(st.bypassed());
    const auto& in = src.buffer().channels[0];
    REQUIRE(out.size() >= in.size());
    CHECK(std::equal(in.begin(), in.end(), out.begin()));
}

TEST_CASE("Stretcher plays twice as fast at the same pitch", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 2.0, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 2.0, 0.0, true, 4 * kRate);
    CHECK_FALSE(st.bypassed());
    CHECK(soundingSeconds(out) == Catch::Approx(1.0).margin(0.08));
    CHECK(frequency(out, kRate / 4, 3 * kRate / 4) == Catch::Approx(440.0).margin(4.0));
}

TEST_CASE("Stretcher transposes without changing the length", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 1.0, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 1.0, 12.0, true, 3 * kRate);
    CHECK(soundingSeconds(out) == Catch::Approx(1.0).margin(0.08));
    CHECK(frequency(out, kRate / 4, 3 * kRate / 4) == Catch::Approx(880.0).margin(8.0));
}

TEST_CASE("Stretcher starts a stretched sound on its first frame", "[stretch]")
{
    // 50 ms of silence, then a tone: the tone must start near 50 ms, not one
    // stretch latency (about 120 ms) later.
    std::vector<float> x(static_cast<std::size_t>(kRate / 20), 0.0f);
    const auto tone = asma::test::sine(440.0, 0.5, 0.5, kRate);
    x.insert(x.end(), tone.begin(), tone.end());
    MemorySource src(mono(x));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 1.0, 0.01, false, 2 * kRate); // tiny shift: stretch on
    std::size_t first = 0;
    while (first < out.size() && std::abs(out[first]) < 0.05f) ++first;
    CHECK(static_cast<double>(first) / kRate == Catch::Approx(0.05).margin(0.02));
}

TEST_CASE("Stretcher ends once the tail is out", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 0.5, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 0.5, 0.0, true, 10 * kRate); // half speed: about 1 s
    CHECK(out.size() < static_cast<std::size_t>(2 * kRate));  // did not run on to the limit
    CHECK(soundingSeconds(out) == Catch::Approx(1.0).margin(0.1));
    float lastPeak = 0.0f; // the tail died out rather than being cut off
    for (std::size_t i = out.size() - 512; i < out.size(); ++i) lastPeak = std::max(lastPeak, std::abs(out[i]));
    CHECK(lastPeak < 0.005f);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_stretcher.cpp:3:10: fatal error: 'asma/audio/Stretcher.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/Stretcher.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PlayHead.h"

#include <array>
#include <memory>
#include <vector>

namespace asma::audio {

// Time-stretch and pitch-shift after a PlayHead, with Signalsmith Stretch.
// Bypassed (the PlayHead's output untouched) when a sound starts at ratio 1
// and 0 semitones with bypass allowed. A stretched sound is pre-rolled, so
// its first frame still comes out first rather than one latency late.
//
// prepare() allocates; everything else is safe on the audio thread.
class Stretcher {
public:
    static constexpr double kMinRatio = 0.25;
    static constexpr double kMaxRatio = 4.0;

    Stretcher();
    ~Stretcher();
    Stretcher(const Stretcher&) = delete;
    Stretcher& operator=(const Stretcher&) = delete;

    void prepare(int sampleRate, int maxBlock);

    // ratio: input frames per output frame (2 plays twice as fast), clamped
    // to kMinRatio..kMaxRatio. Takes effect at once, even mid-sound.
    void setTiming(double ratio, double semitones);
    double ratio() const { return ratio_; }
    double semitones() const { return semitones_; }

    // Call right after head.start(). With allowBypass and neutral timing the
    // sound plays unprocessed until the next start.
    void start(PlayHead& head, bool allowBypass);
    // Writes n stereo frames. Returns how many carried sound: n until the
    // head has finished and the stretch has let out its tail.
    int process(PlayHead& head, float* const* out, int n);
    bool active(const PlayHead& head) const { return bypass_ ? head.active() : head.active() || tail_ > 0; }
    bool bypassed() const { return bypass_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int maxBlock_ = 512;
    double ratio_ = 1.0;
    double semitones_ = 0.0;
    bool bypass_ = true;
    double carry_ = 0.0; // fraction of an input frame owed
    int tail_ = 0;       // output frames left after the head finished; -1 while it plays
    std::array<std::vector<float>, 2> input_;
};

} // namespace asma::audio
```

Create `audio/src/Stretcher.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Stretcher.h"

#include <algorithm>
#include <cmath>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace asma::audio {

struct Stretcher::Impl {
    // A fixed seed: the same input always gives the same output.
    signalsmith::stretch::SignalsmithStretch<float> stretch{12345};
};

Stretcher::Stretcher() : impl_(std::make_unique<Impl>()) {}
Stretcher::~Stretcher() = default;

void Stretcher::prepare(int sampleRate, int maxBlock)
{
    maxBlock_ = maxBlock;
    auto& s = impl_->stretch;
    s.presetDefault(2, static_cast<float>(sampleRate));
    const int seek = s.outputSeekLength(static_cast<float>(kMaxRatio));
    const int frames = std::max(seek, static_cast<int>(std::ceil(maxBlock * kMaxRatio)) + 1);
    for (auto& channel : input_) channel.assign(static_cast<std::size_t>(frames), 0.0f);
    // Run a seek once so its scratch buffers are sized before the audio thread needs them.
    float* in[2] = {input_[0].data(), input_[1].data()};
    s.outputSeek(in, seek);
    s.reset();
}

void Stretcher::setTiming(double ratio, double semitones)
{
    ratio_ = std::clamp(ratio, kMinRatio, kMaxRatio);
    semitones_ = semitones;
    impl_->stretch.setTransposeSemitones(static_cast<float>(semitones));
}

void Stretcher::start(PlayHead& head, bool allowBypass)
{
    bypass_ = allowBypass && ratio_ == 1.0 && semitones_ == 0.0;
    carry_ = 0.0;
    tail_ = -1;
    if (bypass_) return;
    auto& s = impl_->stretch;
    const int seek = s.outputSeekLength(static_cast<float>(ratio_));
    float* in[2] = {input_[0].data(), input_[1].data()};
    head.render(in, seek);
    s.outputSeek(in, seek);
}

int Stretcher::process(PlayHead& head, float* const* out, int n)
{
    if (bypass_) return head.render(out, n);
    auto& s = impl_->stretch;
    int done = 0;
    int sounding = 0;
    float* in[2] = {input_[0].data(), input_[1].data()};
    while (done < n) {
        if (tail_ == 0) {
            for (int c = 0; c < 2; ++c) std::fill(out[c] + done, out[c] + n, 0.0f);
            break;
        }
        const int m = std::min(n - done, maxBlock_);
        const double want = m * ratio_ + carry_;
        const int frames = static_cast<int>(want);
        carry_ = want - frames;
        head.render(in, frames);
        if (!head.active() && tail_ < 0) {
            // What is still inside the stretch comes out over one seek length,
            // and the last analysis block needs one block more to ring out.
            tail_ = static_cast<int>(std::ceil(s.outputSeekLength(static_cast<float>(ratio_)) / ratio_))
                  + 2 * s.blockSamples();
        }
        float* o[2] = {out[0] + done, out[1] + done};
        s.process(in, frames, o, m);
        done += m;
        sounding = done;
        if (tail_ > 0) tail_ = std::max(0, tail_ - m);
    }
    return sounding;
}

} // namespace asma::audio
```

In `audio/CMakeLists.txt`, replace:

```cmake
target_include_directories(asma_audio
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_audio PUBLIC asma::core Threads::Threads)
asma_set_warnings(asma_audio)
```

with:

```cmake
target_include_directories(asma_audio
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_audio PUBLIC asma::core Threads::Threads PRIVATE signalsmith-stretch)
asma_set_warnings(asma_audio)
if(MSVC)
  # Signalsmith Stretch instantiates more templates than one object file holds by default.
  target_compile_options(asma_audio PRIVATE /bigobj)
endif()
```

In `cmake/Dependencies.cmake`, replace:

```cmake
  GIT_TAG v1.2.6
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128)

add_library(asma_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(asma_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
```

with:

```cmake
  GIT_TAG v1.2.6
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
# Its own CMakeLists fetches signalsmith-linear 0.6.4.
FetchContent_Declare(signalsmith_stretch
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df # 1.4.0
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128 signalsmith_stretch)
# Header-only; SYSTEM keeps their warnings out of ours.
set_target_properties(signalsmith-stretch signalsmith-linear PROPERTIES SYSTEM TRUE)

add_library(asma_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(asma_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[stretch]"`

Expected: no compiler warnings;
`All tests passed (12 assertions in 5 test cases)`. The first configure
downloads Signalsmith Stretch and Signalsmith Linear. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 258`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: stretch and transpose with Signalsmith Stretch, starting on the first frame"
```

---

### Task 7: Tempo and key sync

`planSync` is a pure function from what the library knows about a sample and the
sync settings to a stretch ratio, a transposition and the flags the UI shows.
`framesToNextBoundary` places a quantised start. `sampleInfo` reads a file's
values from the library, with `Library::loudness` added for the next task.

**Files:**

- Create: `audio/include/asma/audio/Sync.h`, `audio/src/Sync.cpp`,
  `tests/test_sync.cpp`
- Modify: `core/include/asma/core/Library.h`, `core/src/Library.cpp`,
  `tests/test_library.cpp`

**Interfaces:**

- Consumes: `SampleInfo` (Task 4); `Library::derived`, `Loudness` (plans 1, 2).
- Produces: `kMinTempoConfidence = 0.3`, `kMinKeyConfidence = 0.7`;
  `class KeyName` (implicit from `std::string_view` and `const char*`,
  `std::string_view view() const`, `bool empty() const`);
  `struct SyncSettings { bool tempo = true; bool key = false; double hostBpm = 0; KeyName projectKey; }`;
  `struct SyncPlan { double ratio = 1; double semitones = 0; bool tempoSynced, keySynced, tempoUnsure, keyUnsure; }`;
  `SyncPlan planSync(const SampleInfo&, const SyncSettings&)`;
  `std::optional<int> keyInterval(std::string_view from, std::string_view to)`;
  `std::int64_t framesToNextBoundary(double ppq, double bpm, int sampleRate, double beats)`;
  `SampleInfo sampleInfo(Library&, std::int64_t fileId)`;
  `std::optional<Loudness> Library::loudness(std::int64_t fileId)`.

Behaviour the tests pin:

- Loops stretch to the host tempo, halved or doubled toward 1: 90 into 120 is
  4/3, 70 into 140 is 1, 180 into 120 is 4/3.
- One-shots, an unknown host tempo and sync switched off change nothing; a
  one-shot never shows "?".
- A loop whose tempo is below 0.3 confidence, or unknown, plays unmodified and
  is flagged unsure; 0.3 exactly syncs.
- Key intervals are the shortest way, -6..+5 (a tritone goes down), and aim at
  the relative key across modes: Am into C is 0, Em into D is -5.
- A key below 0.7 confidence is flagged unsure and not transposed; a sample with
  no key is neither.
- The next beat or bar: half a beat at 120 is 12000 frames at 48 kHz; on a
  boundary (or a hair past it) is 0; a count-in before bar 1 works.
- `sampleInfo` returns what `setDerived` and `setAnalysis` stored, loudness
  included; `loudness` is empty until analysis ran and again after
  `resetAnalysis`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_sync.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/Sync.h"
#include "asma/core/Library.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;

namespace {

SampleInfo loop(double bpm, double confidence = 0.9)
{
    SampleInfo s;
    s.bpm = bpm;
    s.bpmConfidence = confidence;
    s.isLoop = true;
    return s;
}

SyncSettings host(double bpm)
{
    SyncSettings s;
    s.hostBpm = bpm;
    return s;
}

} // namespace

TEST_CASE("planSync stretches loops to the host tempo, half or double time toward 1", "[sync]")
{
    CHECK(planSync(loop(120), host(120)).ratio == 1.0);
    CHECK(planSync(loop(90), host(120)).ratio == Catch::Approx(4.0 / 3.0));
    CHECK(planSync(loop(70), host(140)).ratio == 1.0);                     // plays at half time
    CHECK(planSync(loop(180), host(120)).ratio == Catch::Approx(4.0 / 3.0)); // a 90 detected as 180
    CHECK(planSync(loop(128), host(85)).ratio == Catch::Approx(85.0 * 2 / 128));
    const SyncPlan p = planSync(loop(100), host(120));
    CHECK(p.tempoSynced);
    CHECK_FALSE(p.tempoUnsure);
}

TEST_CASE("planSync leaves one-shots, unknown tempos and a switched-off sync alone", "[sync]")
{
    SampleInfo hit = loop(120);
    hit.isLoop = false;
    CHECK_FALSE(planSync(hit, host(100)).tempoSynced);
    CHECK_FALSE(planSync(hit, host(100)).tempoUnsure); // no "?" on a kick

    CHECK_FALSE(planSync(loop(120), host(0)).tempoSynced); // host tempo unknown

    SyncSettings off = host(100);
    off.tempo = false;
    CHECK(planSync(loop(120), off).ratio == 1.0);
}

TEST_CASE("planSync plays an uncertain tempo unmodified and says so", "[sync]")
{
    const SyncPlan guess = planSync(loop(120, kMinTempoConfidence - 0.01), host(100));
    CHECK(guess.ratio == 1.0);
    CHECK_FALSE(guess.tempoSynced);
    CHECK(guess.tempoUnsure);
    CHECK(planSync(loop(120, kMinTempoConfidence), host(100)).tempoSynced);

    SampleInfo noTempo;
    noTempo.isLoop = true;
    CHECK(planSync(noTempo, host(100)).tempoUnsure);
}

TEST_CASE("keyInterval takes the shortest way, to the relative key across modes", "[sync]")
{
    CHECK(keyInterval("C", "D") == 2);
    CHECK(keyInterval("C", "A#") == -2);
    CHECK(keyInterval("C", "F#") == -6); // a tritone goes down
    CHECK(keyInterval("F#", "C") == -6);
    CHECK(keyInterval("Am", "Dm") == 5);
    CHECK(keyInterval("Am", "C") == 0); // relative minor
    CHECK(keyInterval("C", "Am") == 0);
    CHECK(keyInterval("Em", "D") == -5); // D's relative minor is Bm
    CHECK(keyInterval("A", "Cm") == -6); // Cm's relative major is D#
    CHECK_FALSE(keyInterval("H", "C"));
    CHECK_FALSE(keyInterval("C", ""));
}

TEST_CASE("planSync transposes to the project key only when sure of the sample's key", "[sync]")
{
    SampleInfo s;
    s.key = "G";
    s.keyConfidence = 0.9;
    SyncSettings settings;
    settings.key = true;
    settings.projectKey = "A";
    SyncPlan p = planSync(s, settings);
    CHECK(p.keySynced);
    CHECK(p.semitones == 2.0);

    s.keyConfidence = kMinKeyConfidence - 0.01;
    p = planSync(s, settings);
    CHECK_FALSE(p.keySynced);
    CHECK(p.keyUnsure);
    CHECK(p.semitones == 0.0);

    SampleInfo drum; // no key at all: nothing to be unsure about
    CHECK_FALSE(planSync(drum, settings).keyUnsure);

    settings.key = false;
    s.keyConfidence = 0.9;
    CHECK(planSync(s, settings).semitones == 0.0);
}

TEST_CASE("framesToNextBoundary counts to the next beat or bar", "[sync]")
{
    CHECK(framesToNextBoundary(0.0, 120, 48000, 1) == 0);
    CHECK(framesToNextBoundary(0.5, 120, 48000, 1) == 12000); // half a beat at 120 is 0.25 s
    CHECK(framesToNextBoundary(3.5, 120, 48000, 4) == 12000);
    CHECK(framesToNextBoundary(4.0, 120, 48000, 4) == 0);
    CHECK(framesToNextBoundary(4.0000000001, 120, 48000, 4) == 0); // host rounding
    CHECK(framesToNextBoundary(1.0, 120, 48000, 4) == 72000);
    CHECK(framesToNextBoundary(-0.25, 120, 48000, 1) == 6000); // count-in before bar 1
}

TEST_CASE("sampleInfo reads what the library knows", "[sync]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    FileRecord f;
    f.rootId = lib.addRoot(dir.path());
    f.relPath = "Bass_Loop_Am_128.wav";
    f.contentHash = "00000000000000aa";
    f.format = "wav";
    const auto id = lib.insertFile(f);
    CHECK_FALSE(sampleInfo(lib, id).bpm);

    DerivedInfo d;
    d.bpm = 128.0;
    d.bpmConfidence = 0.9;
    d.key = "Am";
    d.keyConfidence = 0.9;
    d.isLoop = true;
    d.rootNote = 57;
    lib.setDerived(id, d);
    AnalysisResult r;
    r.loudness = {0.8, -12.0};
    r.featureVector.assign(kFeatureVectorSize, 0.0f);
    lib.setAnalysis(id, r);

    const SampleInfo s = sampleInfo(lib, id);
    CHECK(s.bpm == 128.0);
    CHECK(s.bpmConfidence == 0.9);
    CHECK(s.key == "Am");
    CHECK(s.isLoop == true);
    CHECK(s.rootNote == 57);
    CHECK(s.lufs == -12.0);
    CHECK(s.peak == 0.8);
}
```

In `tests/test_library.cpp`, replace:

```cpp
    CHECK_FALSE(lib.fileByAbsolutePath(dir.path() / "Drums" / "Nope.wav").has_value());
    CHECK_FALSE(lib.fileByAbsolutePath(dir.path().parent_path() / "elsewhere.wav").has_value());
}
```

with:

```cpp
    CHECK_FALSE(lib.fileByAbsolutePath(dir.path() / "Drums" / "Nope.wav").has_value());
    CHECK_FALSE(lib.fileByAbsolutePath(dir.path().parent_path() / "elsewhere.wav").has_value());
}

TEST_CASE("loudness is there once the file is analysed", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    CHECK_FALSE(lib.loudness(id));
    lib.setDerived(id, {});
    CHECK_FALSE(lib.loudness(id)); // a features row without analysis
    AnalysisResult r = analysed(120.0, "C", true);
    r.loudness = {0.5, -14.5};
    lib.setAnalysis(id, r);
    const auto l = lib.loudness(id).value();
    CHECK(l.peak == 0.5);
    CHECK(l.lufs == -14.5);
    lib.resetAnalysis(id);
    CHECK_FALSE(lib.loudness(id));
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_sync.cpp:3:10: fatal error: 'asma/audio/Sync.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/Sync.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace asma {
class Library;
}

namespace asma::audio {

// Below these, a value is a guess: the sample plays unmodified and the UI
// shows "?". File-name (0.9) and embedded (1.0) values always pass. Measured
// on labelled libraries: loop tempos at 0.3 and up were right or an octave
// off (which sync absorbs) 52 times in 53; keys at 0.7 and up were right or
// the relative key about 9 times in 10 on loops and synths. Guitar recordings
// scored far lower at every confidence, which is why key sync starts off.
constexpr double kMinTempoConfidence = 0.3;
constexpr double kMinKeyConfidence = 0.7;

// A canonical key ("C#m" at most) held in place, so settings that carry one
// reach the audio thread without allocating.
class KeyName {
public:
    KeyName() = default;
    KeyName(std::string_view key) // NOLINT: implicit, so settings.projectKey = "Am" reads naturally
    {
        const std::size_t n = key.size() < sizeof(text_) ? key.size() : sizeof(text_) - 1;
        for (std::size_t i = 0; i < n; ++i) text_[i] = key[i];
    }
    KeyName(const char* key) : KeyName(std::string_view(key)) {}
    std::string_view view() const { return text_; }
    bool empty() const { return text_[0] == '\0'; }

private:
    char text_[4] = {};
};

struct SyncSettings {
    bool tempo = true;     // stretch loops to the host tempo
    bool key = false;      // transpose to the project key
    double hostBpm = 0.0;  // 0 when unknown: no tempo sync
    KeyName projectKey;    // empty: no key sync
};

struct SyncPlan {
    double ratio = 1.0;     // input frames per output frame, for Stretcher::setTiming
    double semitones = 0.0;
    bool tempoSynced = false;
    bool keySynced = false;
    // Sync was wanted but the sample's own value is too uncertain.
    bool tempoUnsure = false;
    bool keyUnsure = false;
};

// Tempo sync applies to loops only. The ratio is hostBpm / bpm, halved or
// doubled toward 1, so a loop at 70 plays unchanged at 140 (and a tempo
// detected an octave off still lands on the grid). The transposition is the
// shortest interval, -6..+5 semitones, to the project key or, when the modes
// differ, to its relative key: Am into C stays put.
SyncPlan planSync(const SampleInfo& info, const SyncSettings& settings);

// Semitones from one canonical key to another, as planSync uses them;
// nullopt when either key is not canonical.
std::optional<int> keyInterval(std::string_view from, std::string_view to);

// Output frames from `ppq` (host position in quarter notes) to the next
// multiple of `beats`; 0 when exactly on one.
std::int64_t framesToNextBoundary(double ppq, double bpm, int sampleRate, double beats);

// What the library knows about a file, for audition.
SampleInfo sampleInfo(Library& library, std::int64_t fileId);

} // namespace asma::audio
```

Create `audio/src/Sync.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Sync.h"

#include "asma/core/Library.h"

#include <array>
#include <cmath>

namespace asma::audio {

namespace {

struct Key {
    int pitchClass = 0; // 0 = C
    bool minor = false;
};

std::optional<Key> parseKey(std::string_view key)
{
    static constexpr std::array<std::string_view, 12> kNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    Key k;
    if (!key.empty() && key.back() == 'm') {
        k.minor = true;
        key.remove_suffix(1);
    }
    for (int i = 0; i < 12; ++i)
        if (kNames[static_cast<std::size_t>(i)] == key) {
            k.pitchClass = i;
            return k;
        }
    return std::nullopt;
}

} // namespace

std::optional<int> keyInterval(std::string_view from, std::string_view to)
{
    const auto a = parseKey(from);
    const auto b = parseKey(to);
    if (!a || !b) return std::nullopt;
    int target = b->pitchClass;
    // Different modes: aim for the project's relative key, which shares its notes.
    if (a->minor != b->minor) target = b->minor ? (target + 3) % 12 : (target + 9) % 12;
    int d = ((target - a->pitchClass) % 12 + 12) % 12;
    if (d > 5) d -= 12;
    return d;
}

SyncPlan planSync(const SampleInfo& info, const SyncSettings& settings)
{
    SyncPlan plan;
    if (settings.tempo && settings.hostBpm > 0.0 && info.isLoop.value_or(false)) {
        if (info.bpm && *info.bpm > 0.0 && info.bpmConfidence >= kMinTempoConfidence) {
            double ratio = settings.hostBpm / *info.bpm;
            // Half or double time toward 1: the loop stays on the grid either way.
            while (ratio > std::sqrt(2.0)) ratio /= 2.0;
            while (ratio < 1.0 / std::sqrt(2.0)) ratio *= 2.0;
            plan.ratio = ratio;
            plan.tempoSynced = true;
        } else {
            plan.tempoUnsure = true;
        }
    }
    if (settings.key && !settings.projectKey.empty() && info.key) {
        if (info.keyConfidence >= kMinKeyConfidence) {
            if (const auto interval = keyInterval(*info.key, settings.projectKey.view())) {
                plan.semitones = *interval;
                plan.keySynced = true;
            }
        } else {
            plan.keyUnsure = true;
        }
    }
    return plan;
}

std::int64_t framesToNextBoundary(double ppq, double bpm, int sampleRate, double beats)
{
    if (bpm <= 0.0 || beats <= 0.0) return 0;
    const double at = ppq / beats;
    const double next = std::ceil(at - 1e-9); // a host a hair past a boundary is on it
    const double beatsLeft = std::max(0.0, (next - at) * beats);
    return std::llround(beatsLeft * 60.0 / bpm * sampleRate);
}

SampleInfo sampleInfo(Library& library, std::int64_t fileId)
{
    SampleInfo info;
    if (const auto d = library.derived(fileId)) {
        info.bpm = d->bpm;
        info.bpmConfidence = d->bpmConfidence;
        info.key = d->key;
        info.keyConfidence = d->keyConfidence;
        info.isLoop = d->isLoop;
        info.rootNote = d->rootNote;
    }
    if (const auto l = library.loudness(fileId)) {
        info.lufs = l->lufs;
        info.peak = l->peak;
    }
    return info;
}

} // namespace asma::audio
```

In `core/include/asma/core/Library.h`, replace:

```cpp
    // not retried until its content changes.
    void setAnalysisError(std::int64_t fileId, std::string_view reason);
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty

    void addUserTag(std::int64_t fileId, std::string_view tag);
    // Removes the tag only where the user added it; auto and embedded tags
```

with:

```cpp
    // not retried until its content changes.
    void setAnalysisError(std::int64_t fileId, std::string_view reason);
    std::optional<DerivedInfo> derived(std::int64_t fileId); // tags left empty
    // Peak and LUFS from analysis; nullopt until the file has been analysed.
    std::optional<Loudness> loudness(std::int64_t fileId);

    void addUserTag(std::int64_t fileId, std::string_view tag);
    // Removes the tag only where the user added it; auto and embedded tags
```

In `core/src/Library.cpp`, replace:

```cpp
    return d;
}

void Library::setAnalysis(std::int64_t fileId, const AnalysisResult& r)
{
    auto ensure = db_.prepare("INSERT INTO features(file_id) VALUES (?) ON CONFLICT(file_id) DO NOTHING");
```

with:

```cpp
    return d;
}

std::optional<Loudness> Library::loudness(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT peak, lufs FROM features WHERE file_id = ? AND peak IS NOT NULL AND lufs IS NOT NULL");
    q.bind(1, fileId);
    if (!q.step()) return std::nullopt;
    return Loudness{q.getDouble(0), q.getDouble(1)};
}

void Library::setAnalysis(std::int64_t fileId, const AnalysisResult& r)
{
    auto ensure = db_.prepare("INSERT INTO features(file_id) VALUES (?) ON CONFLICT(file_id) DO NOTHING");
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[sync],[library]"`

Expected: no compiler warnings;
`All tests passed (113 assertions in 20 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 266`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: sync loops to the host tempo and samples to the project key when sure"
```

---

### Task 8: Loudness matching

`matchGain` turns a sample's loudness into the gain that evens out browsing. The
loader measures short files the scan has not reached yet, so gain matching works
on a fresh library too.

**Files:**

- Create: `audio/include/asma/audio/Gain.h`, `audio/src/Gain.cpp`,
  `tests/test_gain.cpp`
- Modify: `audio/src/Loader.cpp`

**Interfaces:**

- Consumes: `SampleInfo`, `Loader`, `MemorySource` (Tasks 2, 4);
  `measureLoudness` (plan 2).
- Produces: `kTargetLufs = -16.0`, `kMaxBoostDb = 12.0`, `kPeakCeiling = 0.891`;
  `float matchGain(const SampleInfo&)`. `Loader` fills `info.lufs` and
  `info.peak` for in-memory previews that arrive without them.

Behaviour the tests pin:

- -10 LUFS is cut 6 dB, -22 boosted 6 dB, -4 cut 12 dB (cuts are not limited).
- Near silence is boosted 12 dB at most; a quiet but peaky sample only up to -1
  dBFS; a sample already at full scale is neither boosted nor cut; without a
  peak only the 12 dB limit applies.
- Unknown loudness is unity gain.
- A short file with no loudness is measured on load (matching
  `measureLoudness`); a value the library supplied stands; a streamed file stays
  unmeasured.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_gain.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/audio/Gain.h"
#include "asma/audio/Loader.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;

namespace {

SampleInfo loudness(double lufs, double peak)
{
    SampleInfo s;
    s.lufs = lufs;
    s.peak = peak;
    return s;
}

double db(float gain) { return 20.0 * std::log10(gain); }

} // namespace

TEST_CASE("matchGain brings samples to the target loudness", "[gain]")
{
    CHECK(db(matchGain(loudness(-16.0, 0.3))) == Catch::Approx(0.0).margin(1e-6));
    CHECK(db(matchGain(loudness(-10.0, 0.9))) == Catch::Approx(-6.0));
    CHECK(db(matchGain(loudness(-4.0, 1.0))) == Catch::Approx(-12.0)); // cuts are not limited
    CHECK(db(matchGain(loudness(-22.0, 0.1))) == Catch::Approx(6.0));
}

TEST_CASE("matchGain boosts at most 12 dB and never past -1 dBFS", "[gain]")
{
    CHECK(db(matchGain(loudness(-70.0, 0.001))) == Catch::Approx(kMaxBoostDb)); // near silence
    // Quiet but peaky: +10 dB wanted, the peak allows only 0.891 / 0.5.
    CHECK(matchGain(loudness(-26.0, 0.5)) == Catch::Approx(kPeakCeiling / 0.5));
    CHECK(matchGain(loudness(-26.0, 1.0)) == 1.0f); // already at full scale: no boost, no cut
    SampleInfo noPeak;
    noPeak.lufs = -22.0;
    CHECK(db(matchGain(noPeak)) == Catch::Approx(6.0));
}

TEST_CASE("matchGain leaves unmeasured samples alone", "[gain]")
{
    CHECK(matchGain({}) == 1.0f);
}

TEST_CASE("Loader measures a short file the library has not analysed", "[gain]")
{
    TempDir dir;
    const auto shortFile = dir.path() / "tone.wav";
    const auto longFile = dir.path() / "long.wav";
    const auto tone = test::sine(440.0, 1.0, 0.25, 44100);
    test::writeWavFloat(shortFile, 44100, {tone, tone});
    test::writeWavFloat(longFile, 1000, {test::ramp(20000)});
    PreviewCache cache;
    Loader loader(cache);

    loader.select(shortFile);
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    REQUIRE(p->info.lufs);
    CHECK(*p->info.lufs == Catch::Approx(measureLoudness(tone, 44100).lufs));
    CHECK(*p->info.peak == Catch::Approx(0.25).margin(1e-3));

    SampleInfo known;
    known.lufs = -3.0;
    loader.select(shortFile, known);
    loader.pump();
    CHECK(loader.takeReady()->info.lufs == -3.0); // the library's value stands

    loader.select(longFile);
    loader.pump();
    CHECK_FALSE(loader.takeReady()->info.lufs); // streamed: plays at unity
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_gain.cpp:4:10: fatal error: 'asma/audio/Gain.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/Gain.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"

namespace asma::audio {

constexpr double kTargetLufs = -16.0;
constexpr double kMaxBoostDb = 12.0;
constexpr double kPeakCeiling = 0.891; // -1 dBFS

// The linear gain that brings a sample to kTargetLufs, so browsing does not
// jump in level from file to file. A boost is at most kMaxBoostDb and never
// takes the sample peak past kPeakCeiling; cuts are not limited. 1 when
// loudness is unknown.
float matchGain(const SampleInfo& info);

} // namespace asma::audio
```

Create `audio/src/Gain.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Gain.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

float matchGain(const SampleInfo& info)
{
    if (!info.lufs) return 1.0f;
    double gain = std::pow(10.0, std::min(kTargetLufs - *info.lufs, kMaxBoostDb) / 20.0);
    if (info.peak && *info.peak > 0.0) gain = std::min(gain, std::max(1.0, kPeakCeiling / *info.peak));
    return static_cast<float>(gain);
}

} // namespace asma::audio
```

In `audio/src/Loader.cpp`, replace:

```cpp
#include "asma/audio/Loader.h"

#include "asma/audio/StreamSource.h"
#include "asma/core/AudioProbe.h"

#include <algorithm>
```

with:

```cpp
#include "asma/audio/Loader.h"

#include "asma/audio/StreamSource.h"
#include "asma/core/Analysis.h"
#include "asma/core/AudioProbe.h"

#include <algorithm>
```

In `audio/src/Loader.cpp`, replace:

```cpp
        } catch (const ProbeError& e) {
            preview->error = e.what();
        }
        const std::lock_guard lock(liveMutex_);
        live_.push_back({std::move(preview), false});
        did = true;
```

with:

```cpp
        } catch (const ProbeError& e) {
            preview->error = e.what();
        }
        // Not analysed yet: a short file is cheap to measure for gain matching.
        if (const auto* memory = dynamic_cast<const MemorySource*>(preview->source.get()); memory && !preview->info.lufs) {
            const AudioBuffer& b = memory->buffer();
            std::vector<float> mono(b.channels[0]);
            if (b.channelCount() == 2)
                for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + b.channels[1][i]);
            const Loudness l = measureLoudness(mono, b.sampleRate);
            preview->info.lufs = l.lufs;
            preview->info.peak = l.peak;
        }
        const std::lock_guard lock(liveMutex_);
        live_.push_back({std::move(preview), false});
        did = true;
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[gain],[loader]"`

Expected: no compiler warnings;
`All tests passed (68 assertions in 13 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 270`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: match loudness across samples, measuring short files the scan has not reached"
```

---

### Task 9: MIDI voices

Eight `PlayHead`s pitched by speed from a root note, each with a linear attack
and release. The caller passes the preview's generation with each note so it can
keep that preview alive while the voice rings.

**Files:**

- Create: `audio/include/asma/audio/Voices.h`, `audio/src/Voices.cpp`,
  `tests/test_voices.cpp`

**Interfaces:**

- Consumes: `PlayHead`, `PlayOptions` (Task 5).
- Produces: `class VoicePool` with `kVoices = 8`, `kDefaultRootNote = 60`,
  `kAttackSeconds = 0.002`, `kReleaseSeconds = 0.08`, `kNone`,
  `void prepare(int outputRate, int maxBlock)`,
  `void noteOn(int note, float velocity, SampleSource&, std::uint64_t generation, PlayOptions, int rootNote)`,
  `void noteOff(int note)`, `void releaseAll()`, `void kill()`,
  `void render(float* const* out, int n)` (adds), `int active() const`,
  `std::uint64_t oldestGeneration() const`. Task 10 adds a trailing
  `float gain = 1.0f` to `noteOn`.

Behaviour the tests pin:

- The root note plays the sample at its own pitch after a 20-frame attack (at 10
  kHz); an octave up plays twice as fast and ends in half the time; a `smpl`
  root note of 57 makes 69 an octave up.
- Velocity scales the voice, and voices add into what is already in the output.
- Note-off releases linearly over 80 ms.
- A ninth note takes a releasing voice first, then the oldest; the oldest
  generation in use follows.
- Retriggering a sounding note releases the old voice and starts a new one;
  `kill()` silences everything.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_voices.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Voices.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace asma::audio;

namespace {

constexpr int kRate = 10000; // attack 20 frames, release 800

// 1, 2, 3, ... scaled small; linear, so pitched playback is exact.
std::shared_ptr<AudioBuffer> counting(int frames)
{
    auto b = std::make_shared<AudioBuffer>();
    b->sampleRate = kRate;
    std::vector<float> ch(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) ch[static_cast<std::size_t>(i)] = static_cast<float>(i + 1) / 1000.0f;
    b->channels.push_back(std::move(ch));
    return b;
}

struct Pool {
    VoicePool pool;
    MemorySource src{counting(5000)};
    Pool() { pool.prepare(kRate, 256); }
    void on(int note, float velocity = 1.0f, std::uint64_t generation = 1, int root = VoicePool::kDefaultRootNote)
    {
        pool.noteOn(note, velocity, src, generation, {}, root);
    }
    std::vector<float> render(int n, float prefill = 0.0f)
    {
        std::vector<float> l(static_cast<std::size_t>(n), prefill), r(static_cast<std::size_t>(n), prefill);
        float* out[] = {l.data(), r.data()};
        pool.render(out, n);
        return l;
    }
};

} // namespace

TEST_CASE("VoicePool plays the root note at the sample's own pitch", "[voices]")
{
    Pool p;
    p.on(60);
    const auto out = p.render(600);
    CHECK(out[0] == Catch::Approx(0.001f * 0.05f)); // attack: 1/20 of the way up
    CHECK(out[100] == Catch::Approx(0.101f));        // held: the sample itself
    CHECK(out[500] == Catch::Approx(0.501f));
}

TEST_CASE("VoicePool pitches by speed, from the sample's root note", "[voices]")
{
    Pool p;
    p.on(72); // an octave up: twice as fast
    auto out = p.render(3000);
    CHECK(out[100] == Catch::Approx(0.201f));
    CHECK(out[2499] != 0.0f);
    CHECK(out[2500] == 0.0f); // 5000 frames played in 2500
    CHECK(p.pool.active() == 0);

    p.on(69, 1.0f, 1, 57); // smpl chunk says A3: A4 is an octave up
    out = p.render(200);
    CHECK(out[100] == Catch::Approx(0.201f));
}

TEST_CASE("VoicePool scales by velocity and mixes into the output", "[voices]")
{
    Pool p;
    p.on(60, 0.5f);
    const auto out = p.render(200, 1.0f);
    CHECK(out[100] == Catch::Approx(1.0f + 0.5f * 0.101f));
}

TEST_CASE("VoicePool releases on note-off", "[voices]")
{
    Pool p;
    p.on(60);
    p.render(100);
    p.pool.noteOff(60);
    CHECK(p.pool.active() == 1); // still releasing
    const auto out = p.render(1000);
    CHECK(out[0] == Catch::Approx(0.101f * (1.0f - 1.0f / 800)));
    CHECK(out[400] == Catch::Approx(0.501f * (1.0f - 401.0f / 800)).margin(1e-4)); // 401 float steps
    CHECK(out[800] == 0.0f);
    CHECK(p.pool.active() == 0);
}

TEST_CASE("VoicePool steals a releasing voice first, then the oldest", "[voices]")
{
    Pool p;
    for (int n = 0; n < VoicePool::kVoices; ++n) p.on(60 + n, 1.0f, static_cast<std::uint64_t>(n + 1));
    CHECK(p.pool.active() == 8);
    CHECK(p.pool.oldestGeneration() == 1);

    p.on(80, 1.0f, 9); // takes the oldest: note 60, generation 1
    CHECK(p.pool.active() == 8);
    CHECK(p.pool.oldestGeneration() == 2);

    p.pool.noteOff(65); // generation 6, releasing
    p.on(81, 1.0f, 10); // takes 65 rather than the oldest (61)
    CHECK(p.pool.oldestGeneration() == 2);
    p.pool.noteOff(61);
    p.render(1000); // 61 fades out
    CHECK(p.pool.oldestGeneration() == 3);
}

TEST_CASE("VoicePool restarts a note that is already sounding", "[voices]")
{
    Pool p;
    p.on(60);
    p.render(50);
    p.on(60);
    CHECK(p.pool.active() == 2); // the first one is on its way out
    p.render(1000);
    CHECK(p.pool.active() == 1);
    p.pool.kill();
    CHECK(p.pool.active() == 0);
    CHECK(p.pool.oldestGeneration() == VoicePool::kNone);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_voices.cpp:2:10: fatal error: 'asma/audio/Voices.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/Voices.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PlayHead.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace asma::audio {

// Plays the selected sample from MIDI notes, pitched like a classic sampler:
// speed changes with pitch, so higher notes are shorter. Eight voices with a
// linear attack and release; each plays the trimmed region once in the chosen
// direction (ping-pong: there and back).
//
// prepare() allocates; everything else is safe on the audio thread.
class VoicePool {
public:
    static constexpr int kVoices = 8;
    static constexpr int kDefaultRootNote = 60;
    static constexpr double kAttackSeconds = 0.002;
    static constexpr double kReleaseSeconds = 0.08;
    static constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

    void prepare(int outputRate, int maxBlock);

    // velocity 0..1. A note already sounding is released and started again
    // on a fresh voice. With all voices busy, a releasing voice is taken
    // first, then the oldest. `generation` is the preview's, so the caller
    // can keep the source alive while the voice uses it.
    void noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                int rootNote);
    void noteOff(int note);
    void releaseAll();
    void kill(); // silence at once

    // Adds every sounding voice into out (two channels).
    void render(float* const* out, int n);

    int active() const;
    // The oldest preview generation a voice still plays, or kNone.
    std::uint64_t oldestGeneration() const;

private:
    enum class Stage { Off, Attack, Hold, Release };
    struct Voice {
        PlayHead head;
        Stage stage = Stage::Off;
        int note = -1;
        float velocity = 1.0f;
        float level = 0.0f;
        std::uint64_t started = 0; // order of note-ons, for stealing
        std::uint64_t generation = 0;
    };

    std::array<Voice, kVoices> voices_;
    std::array<std::vector<float>, 2> scratch_;
    float attackStep_ = 1.0f;
    float releaseStep_ = 1.0f;
    std::uint64_t counter_ = 0;
};

} // namespace asma::audio
```

Create `audio/src/Voices.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Voices.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

void VoicePool::prepare(int outputRate, int maxBlock)
{
    for (auto& v : voices_) v.head.prepare(outputRate);
    for (auto& s : scratch_) s.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    attackStep_ = static_cast<float>(1.0 / std::max(1.0, kAttackSeconds * outputRate));
    releaseStep_ = static_cast<float>(1.0 / std::max(1.0, kReleaseSeconds * outputRate));
}

void VoicePool::noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                       int rootNote)
{
    noteOff(note);
    auto pick = std::find_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage == Stage::Off; });
    if (pick == voices_.end()) {
        // Steal: a releasing voice is already on its way out; else the oldest.
        pick = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            const bool ar = a.stage == Stage::Release, br = b.stage == Stage::Release;
            return ar != br ? ar : a.started < b.started;
        });
    }
    Voice& v = *pick;
    options.loop = false;
    options.speed = std::pow(2.0, (note - rootNote) / 12.0);
    v.head.start(source, options);
    v.stage = v.head.active() ? Stage::Attack : Stage::Off;
    v.note = note;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.level = 0.0f;
    v.started = ++counter_;
    v.generation = generation;
}

void VoicePool::noteOff(int note)
{
    for (auto& v : voices_)
        if (v.note == note && (v.stage == Stage::Attack || v.stage == Stage::Hold)) v.stage = Stage::Release;
}

void VoicePool::releaseAll()
{
    for (auto& v : voices_)
        if (v.stage == Stage::Attack || v.stage == Stage::Hold) v.stage = Stage::Release;
}

void VoicePool::kill()
{
    for (auto& v : voices_) {
        v.stage = Stage::Off;
        v.head.stop();
    }
}

void VoicePool::render(float* const* out, int n)
{
    float* tmp[2] = {scratch_[0].data(), scratch_[1].data()};
    for (auto& v : voices_) {
        int done = 0;
        while (v.stage != Stage::Off && done < n) {
            const int m = std::min(n - done, static_cast<int>(scratch_[0].size()));
            const int sounding = v.head.render(tmp, m);
            for (int i = 0; i < m; ++i) {
                switch (v.stage) {
                case Stage::Attack:
                    v.level += attackStep_;
                    if (v.level >= 1.0f) {
                        v.level = 1.0f;
                        v.stage = Stage::Hold;
                    }
                    break;
                case Stage::Release:
                    v.level = std::max(0.0f, v.level - releaseStep_);
                    if (v.level == 0.0f) v.stage = Stage::Off;
                    break;
                default: break;
                }
                const float g = v.level * v.velocity;
                out[0][done + i] += g * tmp[0][i];
                out[1][done + i] += g * tmp[1][i];
            }
            if (sounding < m) v.stage = Stage::Off; // played to the end
            done += m;
        }
    }
}

int VoicePool::active() const
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage != Stage::Off; }));
}

std::uint64_t VoicePool::oldestGeneration() const
{
    std::uint64_t oldest = kNone;
    for (const auto& v : voices_)
        if (v.stage != Stage::Off) oldest = std::min(oldest, v.generation);
    return oldest;
}

} // namespace asma::audio
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[voices]"`

Expected: no compiler warnings;
`All tests passed (24 assertions in 6 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 276`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: play the selected sample from MIDI notes on eight voices"
```

---

### Task 10: The audition engine

`AuditionEngine` is what plan 3c calls: control calls on one thread, `process()`
and MIDI on the audio thread, `status()` from anywhere. Control calls travel as
plain commands through an `SpscQueue`; previews arrive from the `Loader` with an
autoplay flag. The audio thread runs a small state machine (idle, waiting for a
start, playing, stopping) so that a switch or an edit fades the old sound out
before the new one starts, and a quantised start begins on the right frame
inside a block.

`tests/alloc_counter.cpp` replaces the global `operator new` for the whole test
binary. It behaves like `malloc` and counts only on a thread inside a
`CountAllocations` scope.

**Files:**

- Create: `audio/include/asma/audio/Edits.h`, `audio/src/Edits.cpp`,
  `audio/include/asma/audio/AuditionEngine.h`, `audio/src/AuditionEngine.cpp`,
  `tests/AllocCounter.h`, `tests/alloc_counter.cpp`,
  `tests/test_audition_engine.cpp`
- Modify: `audio/include/asma/audio/Loader.h`, `audio/src/Loader.cpp`
  (autoplay), `audio/include/asma/audio/Voices.h`, `audio/src/Voices.cpp` (gain)

**Interfaces:**

- Consumes: everything from Tasks 2 to 9.
- Produces: `enum class LoopMode { Auto, On, Off }`;
  `struct Edits { double trimStart = 0; double trimEnd = -1; Direction direction; LoopMode loop; bool changesAudio() const; }`;
  `PlayOptions toPlayOptions(const Edits&, int sourceRate, bool isLoop)`;
  `struct Transport { double bpm; double ppq; bool playing; }`;
  `struct EngineStatus { generation; playing; position; ratio; semitones; tempoSynced; keySynced; tempoUnsure; keyUnsure; voices; }`;
  `class AuditionEngine(PreviewCache&)` with `Loader& loader()`,
  `void prepare(int sampleRate, int maxBlock)`,
  `std::uint64_t select(path, SampleInfo, bool autoplay)`, `play()`, `stop()`,
  `setEdits(const Edits&)`, `setSync(const SyncSettings&)`,
  `setGainMatch(bool)`, `setQuantise(double beats)`,
  `setTransport(const Transport&)`, `noteOn(int, float)`, `noteOff(int)`,
  `process(float* const* out, int n)`, `EngineStatus status() const`.
  `Preview::autoplay` and `Loader::select(path, info, bool autoplay = false)`;
  `VoicePool::noteOn(..., float gain = 1.0f)`. Test helper
  `asma::test::CountAllocations` with `std::size_t count() const`.

Behaviour the tests pin:

- A selection with autoplay plays as soon as it arrives, exactly, and a one-shot
  plays once.
- Without autoplay nothing sounds until `play()`, which also works when called
  before the file has loaded.
- Stop fades out over 5 ms (240 frames at 48 kHz).
- Switching samples fades the old one out, then starts the new one from its
  first frame; a selection without autoplay silences the old one.
- An edit restarts the sample: the old position fades out, the trim point fades
  in.
- Gain matching lifts a -22 LUFS sample by 6 dB.
- A synced loop reports its ratio and follows host tempo changes; a guessed
  tempo plays at ratio 1 and reports "unsure".
- A quantised start waits for the next bar (12000 frames from 3.5 beats at 120
  and 48 kHz) while the host plays, and starts at once when it does not.
- MIDI notes play the selection at its own pitch and keep the old preview alive
  after a switch until they have rung out.
- `process()` makes no allocation through selections (in memory and streamed),
  key and tempo sync, a quantised start, MIDI, a ping-pong edit, a tempo change
  and stop. Checked by planting one: the count went to 1800.

- [ ] **Step 1: Write the failing tests**

Create `tests/AllocCounter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Counts heap allocations made on the current thread while one of these is
// alive (see alloc_counter.cpp, which replaces the global operator new).
#pragma once

#include <cstddef>

namespace asma::test {

class CountAllocations {
public:
    CountAllocations();
    ~CountAllocations();
    CountAllocations(const CountAllocations&) = delete;
    CountAllocations& operator=(const CountAllocations&) = delete;
    std::size_t count() const;
};

} // namespace asma::test
```

Create `tests/alloc_counter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Replaces the global operator new and delete for the whole test binary, so
// CountAllocations can see allocations on the audio path. Behaviour is
// otherwise that of malloc and free.
#include "AllocCounter.h"

#include <cstdlib>
#include <new>

namespace {

thread_local bool counting = false;
thread_local std::size_t allocations = 0;

void* allocate(std::size_t size)
{
    if (counting) ++allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace asma::test {

CountAllocations::CountAllocations()
{
    allocations = 0;
    counting = true;
}
CountAllocations::~CountAllocations() { counting = false; }
std::size_t CountAllocations::count() const { return allocations; }

} // namespace asma::test
```

Create `tests/test_audition_engine.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "AllocCounter.h"
#include "Signals.h"
#include "TestUtil.h"
#include "asma/audio/AuditionEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 256;
constexpr int kFade = 240; // 5 ms

// (i + 1) / 100000: every frame recognisable, never silent.
std::vector<float> counting(int frames, float offset = 0.0f)
{
    std::vector<float> x(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) x[static_cast<std::size_t>(i)] = offset + static_cast<float>(i + 1) / 100000.0f;
    return x;
}

struct Rig {
    TempDir dir;
    PreviewCache cache;
    AuditionEngine engine{cache};
    Transport transport;

    Rig()
    {
        engine.prepare(kRate, kBlock);
        engine.setGainMatch(false);
    }
    fs::path file(const char* name, const std::vector<float>& samples)
    {
        const auto p = dir.path() / name;
        test::writeWavFloat(p, kRate, {samples});
        return p;
    }
    // Renders whole blocks, loading between them as the loader thread would.
    std::vector<float> run(int frames)
    {
        std::vector<float> left;
        std::vector<float> l(kBlock), r(kBlock);
        float* out[] = {l.data(), r.data()};
        while (static_cast<int>(left.size()) < frames) {
            engine.loader().pump();
            engine.setTransport(transport);
            engine.process(out, kBlock);
            left.insert(left.end(), l.begin(), l.end());
            if (transport.playing) transport.ppq += kBlock * transport.bpm / 60.0 / kRate;
        }
        return left;
    }
};

std::size_t firstSound(const std::vector<float>& x)
{
    std::size_t i = 0;
    while (i < x.size() && x[i] == 0.0f) ++i;
    return i;
}

} // namespace

TEST_CASE("AuditionEngine plays a selection as it arrives", "[engine]")
{
    Rig rig;
    const auto samples = counting(2000);
    rig.engine.select(rig.file("a.wav", samples), {}, true);
    const auto out = rig.run(2560);
    CHECK(out[0] == samples[0]);
    CHECK(out[1999] == samples[1999]);
    CHECK(out[2000] == 0.0f); // a one-shot plays once
    CHECK_FALSE(rig.engine.status().playing);
}

TEST_CASE("AuditionEngine waits for play without autoplay", "[engine]")
{
    Rig rig;
    const auto samples = counting(1000);
    const auto g = rig.engine.select(rig.file("a.wav", samples), {}, false);
    CHECK(firstSound(rig.run(1024)) == 1024);
    CHECK(rig.engine.status().generation == g);
    rig.engine.play();
    const auto out = rig.run(1024);
    CHECK(out[0] == samples[0]);

    // play() straight after select(), before the file has loaded.
    rig.engine.select(rig.file("b.wav", counting(1000, 0.5f)), {}, false);
    rig.engine.play();
    CHECK(rig.run(256)[0] == Catch::Approx(0.50001f));
}

TEST_CASE("AuditionEngine fades out on stop", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
    rig.run(1024);
    CHECK(rig.engine.status().playing);
    rig.engine.stop();
    const auto out = rig.run(512);
    CHECK(out[0] > 0.0f);
    CHECK(std::abs(out[kFade - 1]) < out[0] / 100.0f);
    CHECK(out[kFade] == 0.0f);
    CHECK_FALSE(rig.engine.status().playing);
}

TEST_CASE("AuditionEngine fades the old sample out before the new one starts", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
    rig.run(1024);
    const auto b = counting(4800, 0.5f);
    rig.engine.select(rig.file("b.wav", b), {}, true);
    const auto out = rig.run(1024);
    CHECK(out[0] < 0.1f);         // still a, fading
    CHECK(out[kFade] == b[0]);    // then b from its first frame
    CHECK(out[kFade + 100] == b[100]);

    rig.engine.select(rig.file("c.wav", counting(4800)), {}, false); // no autoplay: b stops
    const auto quiet = rig.run(512);
    CHECK(quiet[kFade] == 0.0f);
}

TEST_CASE("AuditionEngine restarts on an edit", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    const auto samples = counting(48000);
    rig.engine.select(rig.file("a.wav", samples), loop, true);
    rig.run(2048);
    Edits e;
    e.trimStart = 0.5; // frame 24000
    rig.engine.setEdits(e);
    const auto out = rig.run(1024);
    // The old position fades out, then the trim point fades in: a trim is
    // not an edge the sound was made with.
    CHECK(out[kFade] == Catch::Approx(samples[24000] / kFade));
    CHECK(out[2 * kFade + 10] == samples[24000 + kFade + 10]);
}

TEST_CASE("AuditionEngine matches loudness when asked", "[engine]")
{
    Rig rig;
    rig.engine.setGainMatch(true);
    SampleInfo info;
    info.lufs = -22.0; // 6 dB under the target
    const auto samples = counting(1000);
    rig.engine.select(rig.file("a.wav", samples), info, true);
    const auto out = rig.run(512);
    CHECK(out[100] == Catch::Approx(samples[100] * std::pow(10.0f, 6.0f / 20.0f)));
}

TEST_CASE("AuditionEngine syncs a loop to the host tempo and follows it", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 120.0;
    loop.bpmConfidence = 0.9;
    rig.transport.bpm = 90.0;
    rig.engine.select(rig.file("a.wav", test::sine(220.0, 2.0, 0.5, kRate)), loop, true);
    rig.run(1024);
    EngineStatus s = rig.engine.status();
    CHECK(s.tempoSynced);
    CHECK(s.ratio == Catch::Approx(0.75));
    rig.transport.bpm = 150.0;
    rig.run(512);
    CHECK(rig.engine.status().ratio == Catch::Approx(1.25));

    loop.bpmConfidence = 0.1; // a guess: plays as it is, with a "?"
    rig.engine.select(rig.file("b.wav", test::sine(220.0, 2.0, 0.5, kRate)), loop, true);
    rig.run(1024);
    s = rig.engine.status();
    CHECK_FALSE(s.tempoSynced);
    CHECK(s.tempoUnsure);
    CHECK(s.ratio == 1.0);
}

TEST_CASE("AuditionEngine starts on the next bar while the host plays", "[engine]")
{
    Rig rig;
    rig.engine.setQuantise(4.0);
    rig.transport = {120.0, 3.5, true}; // half a beat before bar 2: 0.25 s
    rig.engine.select(rig.file("a.wav", counting(48000)), {}, true);
    const auto out = rig.run(24000);
    CHECK(firstSound(out) == 12000);

    Rig stopped; // the host is not playing: no waiting
    stopped.engine.setQuantise(4.0);
    stopped.transport = {120.0, 3.5, false};
    stopped.engine.select(stopped.file("a.wav", counting(4800)), {}, true);
    CHECK(firstSound(stopped.run(1024)) == 0);
}

TEST_CASE("AuditionEngine plays MIDI notes on the selection and keeps it alive while they ring", "[engine]")
{
    Rig rig;
    const auto samples = counting(48000);
    rig.engine.select(rig.file("a.wav", samples), {}, false);
    rig.run(256);
    rig.engine.noteOn(60, 1.0f);
    auto out = rig.run(512);
    CHECK(out[200] == Catch::Approx(samples[200])); // past the 2 ms attack, at its own pitch
    CHECK(rig.engine.status().voices == 1);

    rig.engine.select(rig.file("b.wav", counting(4800)), {}, false);
    rig.run(512);
    CHECK(rig.engine.loader().liveCount() == 2); // a still rings
    rig.engine.noteOff(60);
    rig.run(8192); // release
    rig.engine.loader().pump();
    CHECK(rig.engine.loader().liveCount() == 1);
    CHECK(rig.engine.status().voices == 0);
}

TEST_CASE("AuditionEngine never allocates on the audio thread", "[engine]")
{
    Rig rig;
    rig.engine.setGainMatch(true);
    SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 100.0;
    loop.bpmConfidence = 0.9;
    loop.key = "Am";
    loop.keyConfidence = 0.9;
    SyncSettings sync;
    sync.key = true;
    sync.projectKey = "C#m";
    rig.engine.setSync(sync);
    rig.engine.setQuantise(1.0);
    rig.transport = {120.0, 0.25, true};
    const auto a = rig.file("a.wav", test::sine(220.0, 3.0, 0.5, kRate));
    const auto longFile = rig.file("long.wav", test::sine(330.0, 12.0, 0.5, kRate)); // streams

    std::vector<float> l(kBlock), r(kBlock);
    float* out[] = {l.data(), r.data()};
    std::size_t allocations = 0;
    const auto block = [&] {
        rig.engine.loader().pump(); // the loader thread's work, not counted
        rig.engine.setTransport(rig.transport);
        {
            const test::CountAllocations counter;
            rig.engine.process(out, kBlock);
            allocations += counter.count();
        }
        rig.transport.ppq += kBlock * rig.transport.bpm / 60.0 / kRate;
    };

    rig.engine.select(a, loop, true);
    for (int i = 0; i < 200; ++i) block();
    rig.engine.noteOn(64, 0.8f);
    rig.engine.noteOn(67, 0.8f);
    for (int i = 0; i < 50; ++i) block();
    Edits e;
    e.direction = Direction::PingPong;
    e.trimStart = 0.1;
    rig.engine.setEdits(e);
    for (int i = 0; i < 100; ++i) block();
    rig.transport.bpm = 97.0;
    for (int i = 0; i < 50; ++i) block();
    rig.engine.select(longFile, {}, true);
    for (int i = 0; i < 400; ++i) block();
    rig.engine.stop();
    rig.engine.noteOff(64);
    rig.engine.noteOff(67);
    for (int i = 0; i < 100; ++i) block();
    CHECK(allocations == 0);
    CHECK(rig.engine.status().voices == 0);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_audition_engine.cpp:5:10: fatal error: 'asma/audio/AuditionEngine.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/AuditionEngine.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"
#include "asma/audio/Loader.h"
#include "asma/audio/PlayHead.h"
#include "asma/audio/SpscQueue.h"
#include "asma/audio/Stretcher.h"
#include "asma/audio/Sync.h"
#include "asma/audio/Voices.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace asma::audio {

// The host's (or the standalone's) timeline, once per block.
struct Transport {
    double bpm = 0.0; // 0 when unknown
    double ppq = 0.0; // position in quarter notes at the start of the block
    bool playing = false;
};

// A snapshot for the UI.
struct EngineStatus {
    std::uint64_t generation = 0; // the preview the audio thread holds
    bool playing = false;
    double position = 0.0; // seconds into the file
    double ratio = 1.0;
    double semitones = 0.0;
    bool tempoSynced = false, keySynced = false;
    bool tempoUnsure = false, keyUnsure = false; // show "?"
    int voices = 0;
};

// Audition as one object: the loader, one previewing chain (PlayHead,
// Stretcher, gain, stop fade) and the MIDI voices.
//
// Threads: prepare() and the control calls from one control thread; the
// audio calls from the audio thread; status() from anywhere. Control calls
// reach the audio thread through a queue and take effect at its next block.
// An edit while playing restarts the preview; switching samples fades the
// old one out over 5 ms first.
class AuditionEngine {
public:
    static constexpr double kStopFadeSeconds = 0.005;

    explicit AuditionEngine(PreviewCache& cache) : loader_(cache) {}

    Loader& loader() { return loader_; }

    // Control thread, while the audio thread is not running. Allocates.
    void prepare(int sampleRate, int maxBlock);

    // Control thread.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info, bool autoplay);
    void play();
    void stop();
    void setEdits(const Edits& edits);
    // hostBpm here is the standalone's manual tempo; a transport tempo wins.
    void setSync(const SyncSettings& sync);
    void setGainMatch(bool on);
    // Start on the next multiple of `beats` while the transport plays; 0 is off.
    void setQuantise(double beats);

    // Audio thread.
    void setTransport(const Transport& transport) { transport_ = transport; }
    void noteOn(int note, float velocity);
    void noteOff(int note);
    // Writes n <= maxBlock stereo frames.
    void process(float* const* out, int n);

    EngineStatus status() const;

private:
    enum class State { Idle, Waiting, Playing, Stopping };
    struct Command {
        enum class Type { Play, Stop, Edits, Sync, GainMatch, Quantise } type = Type::Play;
        std::uint64_t generation = 0;
        Edits edits;
        SyncSettings sync;
        bool flag = false;
        double value = 0.0;
    };

    void push(const Command& command);
    void handle(const Command& command);
    void adopt(Preview* preview);
    // Starts the current preview now, on the next beat or bar, or after the
    // audible one has faded out. `offset`: frames into this block, where
    // rendering stands when the request is made.
    void requestStart(int offset);
    void startNow();
    void beginStop();
    double bpm() const { return transport_.bpm > 0.0 ? transport_.bpm : sync_.hostBpm; }
    SyncPlan plan(const Preview& preview) const;
    void publish();

    Loader loader_;
    SpscQueue<Command, 64> commands_;
    std::atomic<std::uint64_t> selected_{0}; // control thread's latest selection

    // Audio thread state.
    int sampleRate_ = 44100;
    int maxBlock_ = 512;
    PlayHead head_;
    Stretcher stretcher_;
    VoicePool voices_;
    std::array<std::vector<float>, 2> play_, mix_;
    Transport transport_;
    Edits edits_;
    SyncSettings sync_;
    bool gainMatch_ = true;
    double quantise_ = 0.0;
    Preview* current_ = nullptr; // the selection: what play() plays and MIDI notes use
    Preview* playing_ = nullptr; // what the previewing chain renders (the old one during a switch)
    std::uint64_t playWanted_ = 0; // generation play() asked for before it arrived
    State state_ = State::Idle;
    bool restartAfterFade_ = false;
    std::int64_t wait_ = 0; // frames until a quantised start
    int fade_ = 0;          // frames left in the stop fade
    int fadeFrames_ = 1;
    float gain_ = 1.0f;
    SyncPlan plan_;

    // Published for status().
    std::atomic<std::uint64_t> statusGeneration_{0};
    std::atomic<bool> statusPlaying_{false};
    std::atomic<double> statusPosition_{0.0};
    std::atomic<double> statusRatio_{1.0};
    std::atomic<double> statusSemitones_{0.0};
    std::atomic<int> statusFlags_{0};
    std::atomic<int> statusVoices_{0};
};

} // namespace asma::audio
```

Create `audio/include/asma/audio/Edits.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PlayHead.h"

namespace asma::audio {

enum class LoopMode { Auto, On, Off }; // Auto: loops loop, one-shots play once

// What the user did to a sample, in the UI's units.
struct Edits {
    double trimStart = 0.0; // seconds
    double trimEnd = -1.0;  // seconds; negative means the end of the file
    Direction direction = Direction::Forward;
    LoopMode loop = LoopMode::Auto;

    // Trim and direction are what a render has to bake in; looping is not.
    bool changesAudio() const { return trimStart > 0.0 || trimEnd >= 0.0 || direction != Direction::Forward; }
};

PlayOptions toPlayOptions(const Edits& edits, int sourceRate, bool isLoop);

} // namespace asma::audio
```

Create `audio/src/AuditionEngine.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/AuditionEngine.h"

#include "asma/audio/Gain.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

namespace {

enum Flags { kTempoSynced = 1, kKeySynced = 2, kTempoUnsure = 4, kKeyUnsure = 8 };

} // namespace

void AuditionEngine::prepare(int sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    head_.prepare(sampleRate);
    stretcher_.prepare(sampleRate, maxBlock);
    voices_.prepare(sampleRate, maxBlock);
    for (auto* buffers : {&play_, &mix_})
        for (auto& b : *buffers) b.assign(static_cast<std::size_t>(maxBlock), 0.0f);
    fadeFrames_ = std::max(1, static_cast<int>(std::lround(kStopFadeSeconds * sampleRate)));
}

std::uint64_t AuditionEngine::select(const std::filesystem::path& path, SampleInfo info, bool autoplay)
{
    const std::uint64_t generation = loader_.select(path, std::move(info), autoplay);
    selected_.store(generation);
    return generation;
}

void AuditionEngine::push(const Command& command)
{
    commands_.push(command); // a full queue drops the command: 64 control calls inside one block
}

void AuditionEngine::play()
{
    Command c;
    c.type = Command::Type::Play;
    c.generation = selected_.load();
    push(c);
}

void AuditionEngine::stop()
{
    Command c;
    c.type = Command::Type::Stop;
    push(c);
}

void AuditionEngine::setEdits(const Edits& edits)
{
    Command c;
    c.type = Command::Type::Edits;
    c.edits = edits;
    push(c);
}

void AuditionEngine::setSync(const SyncSettings& sync)
{
    Command c;
    c.type = Command::Type::Sync;
    c.sync = sync;
    push(c);
}

void AuditionEngine::setGainMatch(bool on)
{
    Command c;
    c.type = Command::Type::GainMatch;
    c.flag = on;
    push(c);
}

void AuditionEngine::setQuantise(double beats)
{
    Command c;
    c.type = Command::Type::Quantise;
    c.value = beats;
    push(c);
}

void AuditionEngine::handle(const Command& c)
{
    const bool sounding = state_ != State::Idle;
    switch (c.type) {
    case Command::Type::Play:
        if (current_ && current_->generation == c.generation) requestStart(0);
        else playWanted_ = c.generation; // not here yet: start when it arrives
        break;
    case Command::Type::Stop:
        playWanted_ = 0;
        restartAfterFade_ = false;
        if (state_ == State::Playing) beginStop();
        else if (state_ == State::Waiting) state_ = State::Idle;
        break;
    case Command::Type::Edits:
        edits_ = c.edits;
        if (sounding && !(state_ == State::Stopping && !restartAfterFade_)) requestStart(0);
        break;
    case Command::Type::Sync:
        sync_ = c.sync;
        if (sounding && !(state_ == State::Stopping && !restartAfterFade_)) requestStart(0);
        break;
    case Command::Type::GainMatch:
        gainMatch_ = c.flag;
        if (playing_) gain_ = gainMatch_ ? matchGain(playing_->info) : 1.0f;
        break;
    case Command::Type::Quantise: quantise_ = c.value; break;
    }
}

void AuditionEngine::adopt(Preview* preview)
{
    const bool start = preview->autoplay || playWanted_ == preview->generation;
    if (playWanted_ <= preview->generation) playWanted_ = 0;
    current_ = preview;
    if (state_ == State::Waiting) state_ = State::Idle;
    if (start) {
        requestStart(0);
    } else if (state_ == State::Playing) {
        restartAfterFade_ = false;
        beginStop(); // a new selection silences the old one
    } else if (state_ == State::Stopping) {
        restartAfterFade_ = false;
    }
}

void AuditionEngine::requestStart(int offset)
{
    if (!current_ || !current_->source) return;
    if (state_ == State::Playing || state_ == State::Stopping) {
        restartAfterFade_ = true;
        if (state_ == State::Playing) beginStop();
        return;
    }
    // Waiting counts from where rendering is now, `offset` frames into the block.
    wait_ = 0;
    if (quantise_ > 0.0 && transport_.playing && bpm() > 0.0) {
        const double ppq = transport_.ppq + offset * bpm() / (60.0 * sampleRate_);
        wait_ = framesToNextBoundary(ppq, bpm(), sampleRate_, quantise_);
    }
    state_ = State::Waiting;
}

void AuditionEngine::startNow()
{
    playing_ = current_;
    SampleSource& source = *current_->source;
    plan_ = plan(*current_);
    head_.start(source, toPlayOptions(edits_, source.sampleRate(), current_->info.isLoop.value_or(false)));
    stretcher_.setTiming(plan_.ratio, plan_.semitones);
    // Synced sounds always run through the stretch, so a tempo change mid-loop can follow.
    stretcher_.start(head_, !(plan_.tempoSynced || plan_.keySynced));
    gain_ = gainMatch_ ? matchGain(current_->info) : 1.0f;
    restartAfterFade_ = false;
    state_ = head_.active() ? State::Playing : State::Idle;
    if (state_ == State::Idle) playing_ = nullptr;
}

void AuditionEngine::beginStop()
{
    state_ = State::Stopping;
    fade_ = fadeFrames_;
}

SyncPlan AuditionEngine::plan(const Preview& preview) const
{
    SyncSettings s = sync_;
    s.hostBpm = bpm();
    return planSync(preview.info, s);
}

void AuditionEngine::noteOn(int note, float velocity)
{
    if (!current_ || !current_->source) return;
    const float gain = gainMatch_ ? matchGain(current_->info) : 1.0f;
    voices_.noteOn(note, velocity, *current_->source, current_->generation,
                   toPlayOptions(edits_, current_->source->sampleRate(), false),
                   current_->info.rootNote.value_or(VoicePool::kDefaultRootNote), gain);
}

void AuditionEngine::noteOff(int note) { voices_.noteOff(note); }

void AuditionEngine::process(float* const* out, int n)
{
    Command command;
    while (commands_.pop(command)) handle(command);
    if (Preview* preview = loader_.takeReady()) adopt(preview);

    // A synced loop follows the host tempo as it changes.
    if (state_ == State::Playing && plan_.tempoSynced) {
        const SyncPlan now = plan(*playing_);
        if (now.tempoSynced && now.ratio != plan_.ratio) {
            plan_.ratio = now.ratio;
            stretcher_.setTiming(plan_.ratio, plan_.semitones);
        }
    }

    for (int c = 0; c < 2; ++c) std::fill(out[c], out[c] + n, 0.0f);
    float* tmp[2] = {play_[0].data(), play_[1].data()};
    int done = 0;
    while (done < n && state_ != State::Idle) {
        if (state_ == State::Waiting) {
            if (wait_ >= n - done) {
                wait_ -= n - done;
                break;
            }
            done += static_cast<int>(wait_);
            wait_ = 0;
            startNow();
            continue;
        }
        int m = n - done;
        if (state_ == State::Stopping) m = std::min(m, fade_);
        stretcher_.process(head_, tmp, m);
        for (int i = 0; i < m; ++i) {
            float g = gain_;
            if (state_ == State::Stopping) g *= static_cast<float>(fade_ - i) / static_cast<float>(fadeFrames_);
            out[0][done + i] = g * tmp[0][i];
            out[1][done + i] = g * tmp[1][i];
        }
        done += m;
        if (state_ == State::Stopping) {
            fade_ -= m;
            if (fade_ == 0) {
                head_.stop();
                playing_ = nullptr;
                state_ = State::Idle;
                if (restartAfterFade_) requestStart(done);
            }
        } else if (!stretcher_.active(head_)) {
            playing_ = nullptr;
            state_ = State::Idle; // played out
        }
    }

    if (voices_.active() > 0) {
        float* mix[2] = {mix_[0].data(), mix_[1].data()};
        for (auto& b : mix_) std::fill(b.begin(), b.begin() + n, 0.0f);
        voices_.render(mix, n);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) out[c][i] += mix[c][i];
    }

    std::uint64_t oldest = voices_.oldestGeneration();
    for (const Preview* p : {current_, playing_})
        if (p) oldest = std::min(oldest, p->generation);
    loader_.release(oldest);
    publish();
}

void AuditionEngine::publish()
{
    statusGeneration_.store(current_ ? current_->generation : 0, std::memory_order_relaxed);
    statusPlaying_.store(state_ != State::Idle, std::memory_order_relaxed);
    const double rate = playing_ && playing_->source ? playing_->source->sampleRate() : 0.0;
    statusPosition_.store(rate > 0.0 ? head_.position() / rate : 0.0, std::memory_order_relaxed);
    statusRatio_.store(plan_.ratio, std::memory_order_relaxed);
    statusSemitones_.store(plan_.semitones, std::memory_order_relaxed);
    statusFlags_.store((plan_.tempoSynced ? kTempoSynced : 0) | (plan_.keySynced ? kKeySynced : 0)
                           | (plan_.tempoUnsure ? kTempoUnsure : 0) | (plan_.keyUnsure ? kKeyUnsure : 0),
                       std::memory_order_relaxed);
    statusVoices_.store(voices_.active(), std::memory_order_relaxed);
}

EngineStatus AuditionEngine::status() const
{
    EngineStatus s;
    s.generation = statusGeneration_.load(std::memory_order_relaxed);
    s.playing = statusPlaying_.load(std::memory_order_relaxed);
    s.position = statusPosition_.load(std::memory_order_relaxed);
    s.ratio = statusRatio_.load(std::memory_order_relaxed);
    s.semitones = statusSemitones_.load(std::memory_order_relaxed);
    const int flags = statusFlags_.load(std::memory_order_relaxed);
    s.tempoSynced = flags & kTempoSynced;
    s.keySynced = flags & kKeySynced;
    s.tempoUnsure = flags & kTempoUnsure;
    s.keyUnsure = flags & kKeyUnsure;
    s.voices = statusVoices_.load(std::memory_order_relaxed);
    return s;
}

} // namespace asma::audio
```

Create `audio/src/Edits.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Edits.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

PlayOptions toPlayOptions(const Edits& edits, int sourceRate, bool isLoop)
{
    PlayOptions o;
    o.trimStart = std::llround(std::max(0.0, edits.trimStart) * sourceRate);
    o.trimEnd = edits.trimEnd < 0.0 ? -1 : std::llround(edits.trimEnd * sourceRate);
    o.direction = edits.direction;
    o.loop = edits.loop == LoopMode::On || (edits.loop == LoopMode::Auto && isLoop);
    return o;
}

} // namespace asma::audio
```

In `audio/include/asma/audio/Loader.h`, replace:

```cpp
    std::uint64_t generation = 0;
    std::shared_ptr<SampleSource> source; // null when the file could not be opened
    SampleInfo info;
    std::string error; // why source is null
};

// Turns selections into previews off the audio thread, keeps streaming
```

with:

```cpp
    std::uint64_t generation = 0;
    std::shared_ptr<SampleSource> source; // null when the file could not be opened
    SampleInfo info;
    std::string error;     // why source is null
    bool autoplay = false; // start as soon as it arrives
};

// Turns selections into previews off the audio thread, keeps streaming
```

In `audio/include/asma/audio/Loader.h`, replace:

```cpp

    // Asks for a file; a newer selection replaces one not loaded yet.
    // Returns the selection's generation.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info = {});

    // Audio thread: the newest preview that arrived since the last call, or
    // null. It stays valid while its generation is at or above what the audio
```

with:

```cpp

    // Asks for a file; a newer selection replaces one not loaded yet.
    // Returns the selection's generation.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info = {}, bool autoplay = false);

    // Audio thread: the newest preview that arrived since the last call, or
    // null. It stays valid while its generation is at or above what the audio
```

In `audio/include/asma/audio/Loader.h`, replace:

```cpp
        std::uint64_t generation = 0;
        std::filesystem::path path;
        SampleInfo info;
    };
    struct Live {
        std::shared_ptr<Preview> preview;
```

with:

```cpp
        std::uint64_t generation = 0;
        std::filesystem::path path;
        SampleInfo info;
        bool autoplay = false;
    };
    struct Live {
        std::shared_ptr<Preview> preview;
```

In `audio/include/asma/audio/Voices.h`, replace:

```cpp

    void prepare(int outputRate, int maxBlock);

    // velocity 0..1. A note already sounding is released and started again
    // on a fresh voice. With all voices busy, a releasing voice is taken
    // first, then the oldest. `generation` is the preview's, so the caller
    // can keep the source alive while the voice uses it.
    void noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                int rootNote);
    void noteOff(int note);
    void releaseAll();
    void kill(); // silence at once
```

with:

```cpp

    void prepare(int outputRate, int maxBlock);

    // velocity 0..1, times `gain` (loudness matching). A note already
    // sounding is released and started again on a fresh voice. With all
    // voices busy, a releasing voice is taken first, then the oldest.
    // `generation` is the preview's, so the caller can keep the source alive
    // while the voice uses it.
    void noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                int rootNote, float gain = 1.0f);
    void noteOff(int note);
    void releaseAll();
    void kill(); // silence at once
```

In `audio/src/Loader.cpp`, replace:

```cpp
    });
}

std::uint64_t Loader::select(const std::filesystem::path& path, SampleInfo info)
{
    std::uint64_t generation = 0;
    {
        const std::lock_guard lock(requestMutex_);
        generation = nextGeneration_++;
        request_ = Request{generation, path, std::move(info)};
    }
    wake_.notify_one();
    return generation;
```

with:

```cpp
    });
}

std::uint64_t Loader::select(const std::filesystem::path& path, SampleInfo info, bool autoplay)
{
    std::uint64_t generation = 0;
    {
        const std::lock_guard lock(requestMutex_);
        generation = nextGeneration_++;
        request_ = Request{generation, path, std::move(info), autoplay};
    }
    wake_.notify_one();
    return generation;
```

In `audio/src/Loader.cpp`, replace:

```cpp
        auto preview = std::make_shared<Preview>();
        preview->generation = request->generation;
        preview->info = std::move(request->info);
        try {
            preview->source = openSource(request->path, cache_);
        } catch (const ProbeError& e) {
```

with:

```cpp
        auto preview = std::make_shared<Preview>();
        preview->generation = request->generation;
        preview->info = std::move(request->info);
        preview->autoplay = request->autoplay;
        try {
            preview->source = openSource(request->path, cache_);
        } catch (const ProbeError& e) {
```

In `audio/src/Voices.cpp`, replace:

```cpp
}

void VoicePool::noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                       int rootNote)
{
    noteOff(note);
    auto pick = std::find_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage == Stage::Off; });
```

with:

```cpp
}

void VoicePool::noteOn(int note, float velocity, SampleSource& source, std::uint64_t generation, PlayOptions options,
                       int rootNote, float gain)
{
    noteOff(note);
    auto pick = std::find_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.stage == Stage::Off; });
```

In `audio/src/Voices.cpp`, replace:

```cpp
    v.head.start(source, options);
    v.stage = v.head.active() ? Stage::Attack : Stage::Off;
    v.note = note;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.level = 0.0f;
    v.started = ++counter_;
    v.generation = generation;
```

with:

```cpp
    v.head.start(source, options);
    v.stage = v.head.active() ? Stage::Attack : Stage::Off;
    v.note = note;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f) * gain;
    v.level = 0.0f;
    v.started = ++counter_;
    v.generation = generation;
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[engine]"`

Expected: no compiler warnings;
`All tests passed (35 assertions in 10 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 286`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: audition through one engine that never allocates on the audio thread"
```

---

### Task 11: Drag-out renders and asma render

`renderToFile` runs a `PlayHead` and a `Stretcher` offline and writes one pass
at an exact length. `RenderCache` returns the original when nothing changes the
audio, and otherwise a render named by content hash and settings. `asma render`
brings it to the command line: it looks the file up in the library for its
tempo, key and hash, plans sync as the engine would, and prints the file to
drag.

**Files:**

- Create: `audio/include/asma/audio/Render.h`, `audio/src/Render.cpp`,
  `tests/test_render.cpp`
- Modify: `core/include/asma/core/Fs.h`, `core/src/Fs.cpp` (`defaultCacheDir`),
  `audio/CMakeLists.txt` (dr_wav for writing), `apps/asma_main.cpp`,
  `apps/CMakeLists.txt`, `tests/test_fs.cpp`, `tests/test_cli_e2e.cpp`

**Interfaces:**

- Consumes: `PlayHead`, `Stretcher`, `Edits`, `planSync`, `sampleInfo`,
  `loadAudio` (Tasks 2 to 10); `probeFile`, `contentHash`,
  `Library::fileByAbsolutePath`, `parseKeyToken` (plans 1, 2).
- Produces: `std::filesystem::path asma::defaultCacheDir()` (`ASMA_CACHE_DIR`
  wins);
  `struct RenderSettings { Edits edits; double ratio = 1; double semitones = 0; int sampleRate = 0; bool changesAudio() const; }`;
  `void renderToFile(const path& source, const RenderSettings&, const path& out)`;
  `class RenderCache(path dir, std::uintmax_t capacity = 2 GB)` with
  `path fileFor(const path& source, const RenderSettings&, std::string contentHash = {})`,
  `static std::string fileName(std::string_view hash, const RenderSettings&, int sampleRate)`,
  `const path& dir() const`. CLI:
  `asma render <file> [--trim-start S] [--trim-end S] [--reverse | --ping-pong] [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--cache DIR]`.

Behaviour the tests pin:

- A trimmed reverse render is exactly the trimmed length, the frames reversed,
  faded at the trim edges, with no temporary file left behind.
- A ratio of 0.8 at 48 kHz from a 1 s file at 44.1 kHz is exactly 60000 frames
  at 440 Hz; stereo stays stereo.
- Ping-pong renders there and back once: 199 frames from 100.
- No edits (a rate change or looping alone included) gives back the original and
  creates no cache directory.
- A hit does not render again; other settings make another file; names look like
  `00000000000000aa-s0-eend-r-x1000000-c-250-48000.wav` and do not change under
  a decimal-comma locale; without a hash the file is hashed.
- Past the capacity the least recently used render goes (a hit counts as a use);
  the render being handed out stays even when it alone is over.
- `defaultCacheDir` honours `ASMA_CACHE_DIR` and differs from the data dir.
- `asma render` stretches a loop named `..._128.wav` to `--tempo 100` (5645
  frames from 4410), prints a one-shot's own path for `--tempo`, renders
  `--reverse`, and exits 2 on `--reverse --ping-pong` or `--key Q` and 1 on a
  missing file.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_render.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/audio/Render.h"
#include "asma/audio/SampleSource.h"
#include "asma/core/AudioProbe.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cmath>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

std::vector<float> counting(int frames)
{
    std::vector<float> x(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) x[static_cast<std::size_t>(i)] = static_cast<float>(i + 1) / 100000.0f;
    return x;
}

double frequency(const std::vector<float>& x, int rate)
{
    int crossings = 0;
    for (std::size_t i = x.size() / 4 + 1; i < 3 * x.size() / 4; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
    return crossings / 2.0 / (static_cast<double>(x.size()) / 2 / rate);
}

} // namespace

TEST_CASE("renderToFile bakes in trim and reverse", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    const auto samples = counting(48000);
    test::writeWavFloat(src, 48000, {samples});
    RenderSettings s;
    s.edits.trimStart = 0.25; // frame 12000
    s.edits.trimEnd = 0.75;   // frame 36000
    s.edits.direction = Direction::Reverse;
    const auto out = dir.path() / "out.wav";
    renderToFile(src, s, out);
    const AudioBuffer b = loadAudio(out);
    CHECK(b.sampleRate == 48000);
    CHECK(b.channelCount() == 1);
    REQUIRE(b.frames() == 24000);
    CHECK(b.channels[0][1000] == samples[35999 - 1000]);
    CHECK(b.channels[0][0] < samples[35999] / 10.0f); // trim edges fade: 5 ms
    CHECK(b.channels[0][23999] < samples[12000] / 10.0f);
    int files = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir.path())) ++files;
    CHECK(files == 2); // the source and the render: no temporary file left
}

TEST_CASE("renderToFile stretches to an exact length at the same pitch", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "tone.wav";
    const auto tone = test::sine(440.0, 1.0, 0.5, 44100);
    test::writeWavFloat(src, 44100, {tone, tone});
    RenderSettings s;
    s.ratio = 0.8; // e.g. a 120 loop into a 96 project
    s.sampleRate = 48000;
    const auto out = dir.path() / "out.wav";
    renderToFile(src, s, out);
    const AudioBuffer b = loadAudio(out);
    CHECK(b.sampleRate == 48000);
    CHECK(b.channelCount() == 2);
    CHECK(b.frames() == 60000); // 1 s / 0.8 at 48 kHz
    CHECK(frequency(b.channels[0], 48000) == Catch::Approx(440.0).margin(4.0));
}

TEST_CASE("renderToFile plays ping-pong there and back once", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 1000, {counting(100)});
    RenderSettings s;
    s.edits.direction = Direction::PingPong;
    const auto out = dir.path() / "out.wav";
    renderToFile(src, s, out);
    const AudioBuffer b = loadAudio(out);
    REQUIRE(b.frames() == 199);
    CHECK(b.channels[0][99] == Catch::Approx(100.0f / 100000.0f));
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
    CHECK(cache.fileFor(src, s) == src);
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
    CHECK(first.parent_path() == dir.path() / "renders");
    CHECK(first.filename() == "00000000000000aa-s0-eend-r-x1000000-c0-48000.wav");
    test::writeBytes(first, "cached"); // a hit must not render again
    CHECK(cache.fileFor(src, s, "00000000000000aa") == first);
    std::string body;
    {
        std::ifstream in(first, std::ios::binary);
        std::getline(in, body);
    }
    CHECK(body == "cached");

    s.semitones = -2.5;
    const auto other = cache.fileFor(src, s, "00000000000000aa");
    CHECK(other != first);
    CHECK(other.filename() == "00000000000000aa-s0-eend-r-x1000000-c-250-48000.wav");

    const auto hashed = cache.fileFor(src, s); // no hash given: computed from the file
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
```

In `tests/test_cli_e2e.cpp`, replace:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"
```

with:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/SampleSource.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/WriterLock.h"
```

In `tests/test_cli_e2e.cpp`, replace:

```cpp
    CHECK(cli.runAsma("collection delete Keepers").exitCode == 0);
    CHECK(cli.runAsma("query").out.find("Bass_Loop") != std::string::npos); // files stay
}
```

with:

```cpp
    CHECK(cli.runAsma("collection delete Keepers").exitCode == 0);
    CHECK(cli.runAsma("query").out.find("Bass_Loop") != std::string::npos); // files stay
}

TEST_CASE("asma render prints the file to drag", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan --no-analysis").exitCode == 0);
    const auto loop = cli.lib / "Loops" / "Bass_Loop_Am_128.wav";
    const auto kick = cli.lib / "Drums" / asma::fromUtf8("Kick Ü_01.wav");
    const std::string cache = " --cache " + quote(cli.dir.path() / "renders");
    const auto printed = [](const RunResult& r) { return asma::fromUtf8(r.out.substr(0, r.out.find('\n'))); };

    // The name says 128 and loop: at 100 it stretches by 100/128.
    const RunResult synced = cli.runAsma("render " + quote(loop) + " --tempo 100" + cache);
    REQUIRE(synced.exitCode == 0);
    const auto rendered = printed(synced);
    CHECK(rendered.parent_path() == cli.dir.path() / "renders");
    CHECK(asma::audio::loadAudio(rendered).frames() == 5645); // 4410 * 128 / 100

    // A one-shot keeps its tempo: nothing to render, the original is dragged.
    const RunResult oneShot = cli.runAsma("render " + quote(kick) + " --tempo 100" + cache);
    CHECK(oneShot.exitCode == 0);
    CHECK(printed(oneShot) == kick);

    const RunResult reversed = cli.runAsma("render " + quote(kick) + " --reverse" + cache);
    CHECK(reversed.exitCode == 0);
    CHECK(printed(reversed) != kick);

    CHECK(cli.runAsma("render " + quote(kick) + " --reverse --ping-pong").exitCode == 2);
    CHECK(cli.runAsma("render " + quote(kick) + " --key Q").exitCode == 2);
    CHECK(cli.runAsma("render " + quote(cli.lib / "gone.wav") + " --reverse" + cache).exitCode == 1);
}
```

In `tests/test_fs.cpp`, replace:

```cpp
    CHECK(asma::defaultDataDir() == dir.path());
}

TEST_CASE("fileTimeToInt orders later times after earlier ones", "[fs]")
{
    const auto now = fs::file_time_type::clock::now();
```

with:

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

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake -B build && cmake --build build`

Expected: FAIL to compile, first error
`tests/test_render.cpp:4:10: fatal error: 'asma/audio/Render.h' file not found`

- [ ] **Step 3: Implement**

Create `audio/include/asma/audio/Render.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace asma::audio {

// What a drag-out bakes into the file.
struct RenderSettings {
    Edits edits;           // trim and direction; looping is ignored: one pass
    double ratio = 1.0;    // tempo, as SyncPlan::ratio
    double semitones = 0.0;
    int sampleRate = 0;    // the host's; 0 keeps the file's own

    // Without these the original file is what gets dragged. A sample-rate
    // change alone is not an edit: the DAW converts on import.
    bool changesAudio() const { return edits.changesAudio() || ratio != 1.0 || semitones != 0.0; }
};

// Renders one pass of `source` with the edits baked in, as a 32-bit float WAV
// with the source's channel count (up to two). The length is exact: the
// trimmed pass divided by the ratio, so a synced loop lands on the grid.
// Writes to a temporary name and renames, so `out` never holds half a file.
// Throws ProbeError when the source cannot be read and std::runtime_error
// when the output cannot be written.
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
    // means hash the file here.
    std::filesystem::path fileFor(const std::filesystem::path& source, const RenderSettings& settings,
                                  std::string contentHash = {});

    // The name a render gets. Integers only, so no locale can change it.
    static std::string fileName(std::string_view contentHash, const RenderSettings& settings, int sampleRate);

    const std::filesystem::path& dir() const { return dir_; }

private:
    void evict(const std::filesystem::path& keep);

    std::filesystem::path dir_;
    std::uintmax_t capacity_;
};

} // namespace asma::audio
```

Create `audio/src/Render.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Render.h"

#include "asma/audio/PlayHead.h"
#include "asma/audio/Stretcher.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/ContentHash.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include <dr_wav.h>

namespace fs = std::filesystem;

namespace asma::audio {

namespace {

constexpr int kBlock = 4096;

void writeWav(const fs::path& path, int sampleRate, int channels, const std::vector<float>& interleaved)
{
    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = static_cast<drwav_uint32>(channels);
    format.sampleRate = static_cast<drwav_uint32>(sampleRate);
    format.bitsPerSample = 32;
    drwav wav;
#ifdef _WIN32
    const bool opened = drwav_init_file_write_w(&wav, path.c_str(), &format, nullptr);
#else
    const bool opened = drwav_init_file_write(&wav, path.c_str(), &format, nullptr);
#endif
    if (!opened) throw std::runtime_error("cannot write " + toUtf8(path));
    const auto frames = static_cast<drwav_uint64>(interleaved.size() / static_cast<std::size_t>(channels));
    const drwav_uint64 written = drwav_write_pcm_frames(&wav, frames, interleaved.data());
    drwav_uninit(&wav);
    if (written != frames) throw std::runtime_error("cannot write " + toUtf8(path));
}

} // namespace

void renderToFile(const fs::path& source, const RenderSettings& settings, const fs::path& out)
{
    auto buffer = std::make_shared<const AudioBuffer>(loadAudio(source));
    MemorySource src(buffer);
    const int rate = settings.sampleRate > 0 ? settings.sampleRate : buffer->sampleRate;
    PlayOptions options = toPlayOptions(settings.edits, buffer->sampleRate, false);

    // One pass, exactly: what the trim leaves, there and back for ping-pong,
    // at the output rate, divided by the tempo ratio.
    const std::int64_t a = std::clamp<std::int64_t>(options.trimStart, 0, buffer->frames());
    const std::int64_t b = options.trimEnd < 0 || options.trimEnd > buffer->frames() ? buffer->frames()
                                                                                       : std::max(options.trimEnd, a);
    std::int64_t pass = b - a;
    if (options.direction == Direction::PingPong && pass >= 2) pass = 2 * pass - 1;
    const double ratio = std::clamp(settings.ratio, Stretcher::kMinRatio, Stretcher::kMaxRatio);
    const auto frames = static_cast<std::size_t>(
        std::llround(static_cast<double>(pass) * rate / buffer->sampleRate / ratio));

    PlayHead head;
    head.prepare(rate);
    Stretcher stretcher;
    stretcher.prepare(rate, kBlock);
    stretcher.setTiming(ratio, settings.semitones);
    head.start(src, options);
    stretcher.start(head, true);

    const int channels = buffer->channelCount();
    std::vector<float> interleaved(frames * static_cast<std::size_t>(channels), 0.0f);
    std::vector<float> l(kBlock), r(kBlock);
    float* block[2] = {l.data(), r.data()};
    for (std::size_t done = 0; done < frames && stretcher.active(head); done += kBlock) {
        stretcher.process(head, block, kBlock);
        const std::size_t n = std::min<std::size_t>(kBlock, frames - done);
        for (std::size_t i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c)
                interleaved[(done + i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] = block[c][i];
    }

    std::random_device random;
    const fs::path temp = out.parent_path() / (toUtf8(out.filename()) + ".tmp" + std::to_string(random()));
    try {
        writeWav(temp, rate, channels, interleaved);
        fs::rename(temp, out);
    } catch (...) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw;
    }
}

std::string RenderCache::fileName(std::string_view contentHash, const RenderSettings& s, int sampleRate)
{
    const auto micros = [](double seconds) { return std::to_string(std::llround(seconds * 1e6)); };
    const char direction = s.edits.direction == Direction::Forward ? 'f' : s.edits.direction == Direction::Reverse ? 'r' : 'p';
    return std::string(contentHash) + "-s" + micros(std::max(0.0, s.edits.trimStart)) + "-e"
         + (s.edits.trimEnd < 0.0 ? std::string("end") : micros(s.edits.trimEnd)) + "-" + direction + "-x"
         + std::to_string(std::llround(s.ratio * 1e6)) + "-c" + std::to_string(std::llround(s.semitones * 100.0))
         + "-" + std::to_string(sampleRate) + ".wav";
}

fs::path RenderCache::fileFor(const fs::path& source, const RenderSettings& settings, std::string contentHash)
{
    if (!settings.changesAudio()) return source;
    const ProbeResult probe = probeFile(source);
    if (contentHash.empty()) contentHash = asma::contentHash(source, probe.hashOffset, probe.hashLength);
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

In `apps/CMakeLists.txt`, replace:

```cmake
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp OrganiseCommands.cpp)
target_link_libraries(asma PRIVATE asma_cli_support)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)
```

with:

```cmake
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp OrganiseCommands.cpp)
target_link_libraries(asma PRIVATE asma_cli_support asma::audio)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)
```

In `apps/asma_main.cpp`, replace:

```cpp
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

with:

```cpp
#include "OrganiseCommands.h"
#include "SearchArgs.h"

#include "asma/audio/Render.h"
#include "asma/audio/Sync.h"
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

In `apps/asma_main.cpp`, replace:

```cpp
    "  collection list | create <name> | rename <name> <new> | delete <name>\n"
    "  collection add|remove <name> <file>... | --id N...\n"
    "  search list | save <name> [query options] [words...] | delete <name>\n"
    "  --version\n";

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
```

with:

```cpp
    "  collection list | create <name> | rename <name> <new> | delete <name>\n"
    "  collection add|remove <name> <file>... | --id N...\n"
    "  search list | save <name> [query options] [words...] | delete <name>\n"
    "  render <file> [--trim-start S] [--trim-end S] [--reverse | --ping-pong]\n"
    "         [--tempo BPM] [--key K] [--transpose N] [--rate HZ] [--cache DIR]\n"
    "                          print the file to drag, rendering edits if any\n"
    "  --version\n";

// One line per row: TSV (path, bpm, key, type, duration[, similarity]) or JSON,
```

In `apps/asma_main.cpp`, replace:

```cpp
    return kOk;
}

int cmdQuery(Args& args, Db& db)
{
    const bool json = args.flag("json");
```

with:

```cpp
    return kOk;
}

int cmdRender(Args& args, Db& db)
{
    audio::RenderSettings settings;
    if (const auto v = args.option("trim-start")) settings.edits.trimStart = toDouble(*v, "--trim-start");
    if (const auto v = args.option("trim-end")) settings.edits.trimEnd = toDouble(*v, "--trim-end");
    const bool reverse = args.flag("reverse");
    const bool pingPong = args.flag("ping-pong");
    if (reverse && pingPong) throw UsageError("--reverse and --ping-pong do not go together");
    if (reverse) settings.edits.direction = audio::Direction::Reverse;
    if (pingPong) settings.edits.direction = audio::Direction::PingPong;
    audio::SyncSettings sync;
    sync.tempo = false;
    if (const auto v = args.option("tempo")) {
        sync.tempo = true;
        sync.hostBpm = toDouble(*v, "--tempo");
    }
    if (const auto v = args.option("key")) {
        const auto key = parseKeyToken(*v);
        if (!key) throw UsageError("not a key: " + *v);
        sync.key = true;
        sync.projectKey = audio::KeyName(*key);
    }
    double transpose = 0.0;
    if (const auto v = args.option("transpose")) transpose = toDouble(*v, "--transpose");
    if (const auto v = args.option("rate")) settings.sampleRate = static_cast<int>(toDouble(*v, "--rate"));
    const auto cacheOption = args.option("cache");
    const std::filesystem::path cacheDir = cacheOption ? fromUtf8(*cacheOption) : defaultCacheDir() / "renders";
    const auto target = args.positional();
    rejectLeftovers(args);
    if (!target) throw UsageError("render needs a file");

    const std::filesystem::path source = std::filesystem::absolute(fromUtf8(*target));
    Library lib(db);
    audio::SampleInfo info;
    std::string hash;
    if (const auto file = lib.fileByAbsolutePath(source)) {
        info = audio::sampleInfo(lib, file->id);
        hash = file->contentHash;
    }
    const audio::SyncPlan plan = audio::planSync(info, sync);
    if (plan.tempoUnsure) std::cerr << "asma: the tempo is unknown or a guess; left as it is\n";
    else if (sync.tempo && !plan.tempoSynced) std::cerr << "asma: not a loop; tempo left as it is\n";
    if (plan.keyUnsure) std::cerr << "asma: the key is a guess; not transposed\n";
    else if (sync.key && !plan.keySynced) std::cerr << "asma: no key known; not transposed\n";
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones + transpose;

    audio::RenderCache cache(cacheDir);
    std::cout << toUtf8(cache.fileFor(source, settings, hash)) << "\n";
    return kOk;
}

int cmdQuery(Args& args, Db& db)
{
    const bool json = args.flag("json");
```

In `apps/asma_main.cpp`, replace:

```cpp
            {"tag", cmdTag},
            {"collection", cmdCollection},
            {"search", cmdSearch},
        };
        if (*command == "scan") {
            Db db = Db::open(dbPath);
```

with:

```cpp
            {"tag", cmdTag},
            {"collection", cmdCollection},
            {"search", cmdSearch},
            {"render", cmdRender},
        };
        if (*command == "scan") {
            Db db = Db::open(dbPath);
```

In `audio/CMakeLists.txt`, replace:

```cmake
target_include_directories(asma_audio
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_audio PUBLIC asma::core Threads::Threads PRIVATE signalsmith-stretch)
asma_set_warnings(asma_audio)
if(MSVC)
  # Signalsmith Stretch instantiates more templates than one object file holds by default.
```

with:

```cmake
target_include_directories(asma_audio
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_audio PUBLIC asma::core Threads::Threads PRIVATE signalsmith-stretch asma_dr_libs)
asma_set_warnings(asma_audio)
if(MSVC)
  # Signalsmith Stretch instantiates more templates than one object file holds by default.
```

In `core/include/asma/core/Fs.h`, replace:

```cpp
// The directory is not created.
std::filesystem::path defaultDataDir();

// Last-write time as an opaque integer in the file clock's native ticks. Only
// meaningful for equality and ordering on the same machine.
std::int64_t fileTimeToInt(std::filesystem::file_time_type time);
```

with:

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

In `core/src/Fs.cpp`, replace:

```cpp
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

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[render],[fs]"`

Expected: no compiler warnings;
`All tests passed (41 assertions in 14 test cases)`.
`./build/tests/asma_tests "asma render prints the file to drag"`:
`All tests passed (12 assertions in 1 test case)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 295`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "audio: render edits for drag-out into a capped cache; add asma render"
```

---

### Task 12: Docs

The spec gets the decisions this plan made: `asma-audio` as a component, section
8 in detail (loading, the chain and its fades, the measured thresholds, the root
note), and the render cache's exact length, format and location. The README gets
a section on audition and `asma render`.

**Files:**

- Modify: `docs/superpowers/specs/2026-09-25-asma-design.md`, `README.md`

- [ ] **Step 1: Amend the spec and the README**

In `README.md`, replace:

```text

    ASMA_EVAL_DIR=~/Samples ./build/tests/asma_tests "[.real]"

## License

GPLv3. See `LICENSE`.
```

with:

```text

    ASMA_EVAL_DIR=~/Samples ./build/tests/asma_tests "[.real]"

## Audition and drag-out

`asma_audio` is the audition engine the app and plugin play through, built
without JUCE so it runs and tests headless: trim, reverse and ping-pong, loops
stretched to the host tempo, transpose to the project key, loudness matching and
eight MIDI voices. `asma render` prints the file a drag should carry: the
original when nothing changes the audio, otherwise a render with the edits baked
in.

    asma render ~/Samples/Loops/Funk_96.wav --tempo 120   # stretched, same pitch
    asma render ~/Samples/Keys/Rhodes_Am.wav --key C#m --reverse
    asma render ~/Samples/Drums/Kick_01.wav               # no edits: the file itself

Sync only happens when asma is sure of the sample's tempo or key; otherwise it
says so and leaves the sample alone. Renders live in the platform cache
directory (on macOS `~/Library/Caches/Anode Labs/asma/renders`), capped at 2 GB;
`--cache DIR` or the `ASMA_CACHE_DIR` environment variable override it.

## License

GPLv3. See `LICENSE`.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
   - `fileops`: planning, preflight, journal, execute, undo.
2. **`asma-scan`**: worker executable. Wraps `asma-core` `index` and `analysis`,
   reports progress as JSON lines on stdout.
3. **`asma` CLI**: `scan`, `query`, `similar`, `dedupe`, `undo`. Used by CI and
   power users.
4. **`asma-ui`** (JUCE component library): browser table, sidebar, filter bar,
   waveform, audition engine, theme. Shared by both shells.
5. **Standalone and plugin shells**: thin wrappers around `asma-ui`. Only the
   standalone enables file operations and spawns the scanner. If the plugin is
   the only asma instance running, it can request a scan by launching
   `asma-scan`, which still runs outside the host process.
```

with:

```text
   - `fileops`: planning, preflight, journal, execute, undo.
2. **`asma-scan`**: worker executable. Wraps `asma-core` `index` and `analysis`,
   reports progress as JSON lines on stdout.
3. **`asma` CLI**: `scan`, `query`, `similar`, `render`, `dedupe`, `undo`. Used
   by CI and power users.
4. **`asma-audio`** (static library, no JUCE): the audition engine of section 8
   and the drag-out renders. Built on `asma-core`'s decoders and Signalsmith
   Stretch, so it runs and tests headless; the plugin's audio callback calls it
   directly.
5. **`asma-ui`** (JUCE component library): browser table, sidebar, filter bar,
   waveform, theme. Shared by both shells.
6. **Standalone and plugin shells**: thin wrappers around `asma-ui`. Only the
   standalone enables file operations and spawns the scanner. If the plugin is
   the only asma instance running, it can request a scan by launching
   `asma-scan`, which still runs outside the host process.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text

## 8. Audition engine

- **Loading:** files under ~10 s are decoded fully; longer files stream with
  read-ahead. An LRU cache keeps recent previews warm. Decoding runs off the
  audio thread; the audio thread only swaps in ready buffers, lock-free.
- **Chain:** trim, direction (forward, reverse, ping-pong), time-stretch and
  pitch-shift (Signalsmith Stretch), LUFS-based gain matching, short anti-click
  fades.
- **Sync:** loops stretch to the current tempo and start on the next beat or bar
  (optional quantised start). Transpose-to-key takes the shortest interval. Sync
  and transpose only apply above a confidence threshold; below it the sample
  plays unmodified and shows a "?" badge.
- **MIDI:** incoming notes play the selected sample pitched from a root note
  (detected pitch if available, else C3). 8-voice polyphony, simple AR envelope.
- **Plugin output:** the preview is rendered into the plugin's audio output, so
  it's heard through the channel's inserts. By default the plugin stays silent
  while the host transport is stopped unless the user is auditioning.
```

with:

```text

## 8. Audition engine

- **Loading:** files up to 10 s are decoded fully; longer files stream in
  blocks, with the first 10 s and the last block loaded up front so playback
  starts at once in either direction, and the rest read ahead in the direction
  of travel. An LRU cache (256 MB of samples) keeps recent previews warm.
  Decoding runs on a loader thread; the audio thread only takes ready previews
  from a lock-free queue, and a preview is freed only once the audio thread
  reports it no longer plays it (as the selection, the sound fading out, or a
  ringing MIDI voice). The audio thread never allocates.
- **Chain:** trim, direction (forward, reverse, ping-pong), resampling to the
  output rate, time-stretch and pitch-shift (Signalsmith Stretch, pre-rolled so
  the first frame still comes out first), LUFS-based gain matching (to -16 LUFS,
  at most +12 dB, never boosting the peak past -1 dBFS; files not analysed yet
  are measured on load when short, and play at unity when streamed), and 5 ms
  anti-click fades. Fades go only where a sound would click: not at a forward
  start from frame 0, a one-shot's own end, or the wrap of an untrimmed forward
  loop. An edit while a sample plays restarts it; switching samples fades the
  old one out first.
- **Sync:** loops stretch to the current tempo, choosing half or double time
  when that is closer to the original, and can start on the next beat or bar
  while the transport plays (optional quantised start). A synced loop follows
  tempo changes as it plays. Transpose-to-key takes the shortest interval
  (-6..+5 semitones), to the project's relative key when the modes differ. Sync
  and transpose only apply at a tempo confidence of 0.3 or a key confidence of
  0.7 and above (file names and embedded chunks always qualify); below it the
  sample plays unmodified and shows a "?" badge. The thresholds were measured on
  labelled libraries. Key sync is off by default: on guitar recordings key
  detection was unreliable at every confidence.
- **MIDI:** incoming notes play the selected sample pitched from a root note
  (the `smpl` chunk's, else MIDI note 60, which most DAWs call C3), like a
  classic sampler: speed changes with pitch. Each note plays the trimmed region
  once in the chosen direction. 8-voice polyphony, linear AR envelope (2 ms,
  80 ms); a new note beyond eight takes a releasing voice first, then the
  oldest.
- **Plugin output:** the preview is rendered into the plugin's audio output, so
  it's heard through the channel's inserts. By default the plugin stays silent
  while the host transport is stopped unless the user is auditioning.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text

### Drag out

- No edits active: drag the original file path.
- Edits active (trim, reverse, stretch, pitch): render to
  `<cache>/renders/<hash>-<params>.wav` at the host sample rate if known, then
  drag that file. The render cache is capped (default 2 GB, LRU eviction).

### Plugin state
```

with:

```text

### Drag out

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
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
| clap-juce-extensions              | CLAP target                      | MIT                   |
| SQLite (with FTS5)                | database                         | public domain         |
| xxHash                            | content hashing                  | BSD-2                 |
| Signalsmith Stretch               | time-stretch, pitch-shift        | MIT                   |
| dr_libs (dr_flac, dr_mp3, dr_wav) | decoding in asma-core            | MIT-0 / public domain |
| stb_vorbis                        | Ogg Vorbis decoding in asma-core | MIT / public domain   |
| libebur128                        | LUFS                             | MIT                   |
```

with:

```text
| clap-juce-extensions              | CLAP target                      | MIT                   |
| SQLite (with FTS5)                | database                         | public domain         |
| xxHash                            | content hashing                  | BSD-2                 |
| Signalsmith Stretch (and Linear)  | time-stretch, pitch-shift        | MIT                   |
| dr_libs (dr_flac, dr_mp3, dr_wav) | decoding in asma-core            | MIT-0 / public domain |
| stb_vorbis                        | Ogg Vorbis decoding in asma-core | MIT / public domain   |
| libebur128                        | LUFS                             | MIT                   |
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```text
| Catch2                            | tests                            | BSL-1.0               |

asma-core decodes audio with dr_libs and stb_vorbis rather than JUCE, so the
core, the CLI and the scanner build without JUCE.

## 12. Testing
```

with:

```text
| Catch2                            | tests                            | BSL-1.0               |

asma-core decodes audio with dr_libs and stb_vorbis rather than JUCE, so the
core, the audio engine, the CLI and the scanner build without JUCE.

## 12. Testing
```

- [ ] **Step 2: Format and check**

Run: `prettier -w README.md docs/superpowers/specs/2026-09-25-asma-design.md`

Expected: no change beyond the blocks above (they are already formatted).

- [ ] **Step 3: Commit**

```sh
git add -A
git commit -m "docs: asma-audio, the audition decisions, and asma render"
```

---

### Task 13: Verify and merge

- [ ] **Step 1: Full suite, timings and ThreadSanitizer**

```sh
ctest --test-dir build --output-on-failure
./build/tests/asma_tests "[.perf]"
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_C_FLAGS=-fsanitize=thread \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --target asma_tests
./build-tsan/tests/asma_tests "[stream],[loader],[engine],[playhead],[stretch],[voices],[render],[gain],[sync],[cache],[source],[reader]"
```

Expected: `100% tests passed out of 295`; `search took` under 50 ms; the
sanitized run passes with no `WARNING: ThreadSanitizer`.

- [ ] **Step 2: Smoke test on a real library**

Pick a loop from a scanned library whose name states its tempo:

```sh
export ASMA_DATA_DIR=$(mktemp -d) ASMA_CACHE_DIR=$(mktemp -d)
./build/apps/asma root add ~/Music
./build/apps/asma scan --no-analysis
f="$(./build/apps/asma query --type loop --bpm 90-130 --limit 1 | cut -f1)"
./build/apps/asma render "$f"
./build/apps/asma render "$f" --tempo 100 --reverse
afplay "$(./build/apps/asma render "$f" --tempo 100 --reverse)"   # macOS
```

Expected: the first `render` prints `$f` itself; the second prints a file under
`$ASMA_CACHE_DIR/renders` and a second call prints the same path at once; the
render plays the loop backwards at 100 BPM without clicks at its edges.

- [ ] **Step 3: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 4: CI on all three platforms (ask first)**

Pushing the branch publishes it to the public repo, so ask the user before:

```sh
git push -u origin plan-3b-audition
gh run watch --repo anode-audio/asma
```

Expected: macOS, Windows and Ubuntu green. This is the first time GCC and MSVC
see `asma-audio` and Signalsmith Stretch: expect to fix warnings or template
errors there. Fix on the branch until all three pass.

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-3b-audition
git -C product/asma worktree remove .worktrees/plan-3b
git -C product/asma branch -d plan-3b-audition
```

Push `main` once the user agrees, and delete the remote branch.
