# asma Plan 2: Audio Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every indexed sample gets loudness, tempo, key, loop or one-shot, and
a timbre fingerprint, computed once in the background, never overriding what the
file or its name already says; `asma similar` finds sounds like a given one.

**Architecture:** Decoding (dr_libs, stb_vorbis) and pure DSP (`analyse`) live
in `asma-core` with no JUCE. Schema version 2 records where each BPM, key and
loop flag came from. `analysePending` works through files whose
`analysis_version` is behind, in parallel batches, after each scan; `asma-scan`
reports it with new events so the plan 3 supervisor can recover from decoder
crashes.

**Tech Stack:** C++20, libebur128 1.2.6 (new), dr_libs (adds dr_wav),
stb_vorbis, SQLite, Catch2.

**Spec:** `docs/superpowers/specs/2026-09-25-asma-design.md` (section 7,
Analysis; section 12, Testing).

**How this plan was checked:** every code block below was replayed task by task
on a clone of `main` (f560bd7): each red step failed as stated, each green step
built without warnings and passed, and the final tree matched the prototype file
for file. The `Expected:` lines are the recorded outputs.

## Where this sits

Plan 2 of 5. Plan 1 (core and CLI) is merged. Plan 3 (UI, audition, plugin)
builds on `analyse`, the LUFS column (gain matching), `isLoop`/`bpm` (tempo
sync) and the `asma-scan` events.

## Global Constraints

- Everything from plan 1 still holds: GPLv3 SPDX headers, C++20 without
  `std::format` or floating `std::to_chars`, macOS 12+ / Windows x64 / Linux x64
  (Ubuntu 22.04), paths as UTF-8 via `toUtf8`/`fromUtf8`, no em-dashes, no AI
  references or co-author trailers, worktree then direct merge.
- `asma-core` stays JUCE-free. New third-party code only via
  `cmake/Dependencies.cmake`, pinned: libebur128 `v1.2.6`.
- Analysis is pure: the same audio always gives the same result (`analyse` has
  no I/O and no global state).
- Embedded (ACID) and file-name values always beat analysis. Analysis never
  deletes or rewrites sample files.
- Analyse at most the first 30 seconds of a file (`AnalyseOptions::maxSeconds`).
- Bump `kAnalysisVersion` whenever analysis output changes, so stored results
  are recomputed.

## Measured on a real library

While prototyping, the analyser was tuned and checked against a 70,539-file
library (Logic, Alchemy and commercial packs), comparing against the BPM and key
that file names and ACID chunks already state. Nothing from that library is in
the repo; `[.real]` (Task 10) reproduces the measurement on any folder.

| measure                                           | result                                        |
| ------------------------------------------------- | --------------------------------------------- |
| loop tempo, all 287 labelled loops                | 52% exact, 23% octave off, 4% wrong, 22% none |
| loop or one-shot, 451 labelled one-shots          | 7% called loops                               |
| key, 6,776 labelled files                         | reported for 76%; 69% of those right          |
| full scan + analysis, 70,539 files (M-series Mac) | 3 min 32 s; 12 decode failures                |

The 12 failures are AIFC files with the `twos` compression tag (16-bit
big-endian PCM) that dr_wav does not accept. They stay searchable; only their
analysis is missing. Left for a later plan.

## Decisions made while prototyping

- Tempo for complete files comes from their length: loops are cut to whole bars,
  so only `240 * bars / seconds` is scored. This moved labelled loops from 61%
  to 75% exact before the one-shot guard (below) traded some back.
- Onsets for beat decisions use only flux below 1.5 kHz; with all bins, eighth
  note hats looked like beats (20 of 32 generated loops right; 32 of 32 after).
- The autocorrelation is mean-centred; uncentred, 42% of labelled one-shots were
  called loops. The loop thresholds (correlation 0.15, front-quarter energy
  under 60%) come from a sweep over labelled loops and one-shots.
- Temperley key profiles instead of Krumhansl (24 of 24 generated keys against
  21 of 24).
- libebur128's max window is left at its default; raising it to the file length
  cost 740 ms per 30 s file.
- The FFT spells out the complex multiply; `std::complex` operator* checks for
  NaN and infinity and was 2.5 times slower.
- The spec's "small CC0 set" becomes generated audio in CI plus the opt-in
  real-library test; Task 10 updates the spec wording.
- CI timings use a generated library of 1,700 files (1,500 hits, 200 loops), not
  the spec's 50,000: that many WAVs would cost gigabytes of runner disk and
  minutes per job for an informational number. The real-library smoke test in
  Task 11 covers scale.

## Review Focus

1. **Files longer than 30 seconds** (songs, stems): only the start is decoded,
   so they must never be called loops, and they get a tempo only when it is
   clear. Pinned in Task 5 ("Audio cut short by maxSeconds is not called a
   loop").
2. **A file name that disagrees with the audio** (`Groove_90bpm.wav` that is
   really 120): the name wins, through rescans and moves. Pinned in Tasks 6
   and 7.
3. **Audio that probes but will not decode** (MPEG data inside a RIFF
   container): recorded once, never retried in a loop, still searchable. Pinned
   in Tasks 1 and 7.
4. **Silence, a handful of samples, 96 kHz files**: no NaN in feature vectors,
   no crash, sensible defaults. Pinned in Task 5.
5. **A decoder that crashes the process**: the supervisor contract in
   `docs/scan-protocol.md` must be able to fence the file off without hiding it
   from search. Pinned in Task 9 (`--fail-analysis`).

---

## File Structure

```
core/include/asma/core/
  Decode.h        decodeFile: any supported file to mono float
  Analysis.h      pure analysis: loudness, tempo, key, descriptors, analyse()
  Analyser.h      analysePending / markAnalysisFailed over the database
  Similar.h       findSimilar over feature vectors
core/src/
  Decode.cpp, Analysis.cpp, Analyser.cpp, Similar.cpp
  Fft.h, Fft.cpp  radix-2 FFT and STFT (internal)
  Parallel.h      parallelFor / threadCount (internal; scanner and analyser)
tests/
  Signals.h       generated drums, chords, tones, noise
  test_decode.cpp, test_loudness.cpp, test_tempo.cpp, test_key.cpp,
  test_analysis.cpp, test_analysis_accuracy.cpp, test_analyser.cpp,
  test_similar.cpp, test_perf.cpp (hidden), test_real_eval.cpp (hidden)
```

Modified: `AudioProbe.h/.cpp` (detectFormat), `Db.h/.cpp` (blobs, versioned
in-memory databases), `Schema.h/.cpp` (migration 2), `Library.h/.cpp` (feature
sources, analysis storage), `Scanner.cpp` (sources, parallelFor), `Query.h/.cpp`
(rowsForIds), `apps/asma_main.cpp`, `apps/asma_scan_main.cpp`,
`docs/scan-protocol.md`, `README.md`, CI, spec.

Commands as in plan 1 (`cmake --build build`,
`./build/tests/asma_tests "[tag]"`,
`ctest --test-dir build --output-on-failure`).

---

### Task 0: Worktree

- [ ] **Step 1: Create the worktree**

```sh
git -C product/asma worktree add .worktrees/plan-2 -b plan-2-analysis
```

All paths below are relative to `product/asma/.worktrees/plan-2`. Configure
once: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`.

---

### Task 1: Decoding to mono float

The analyser needs PCM, not headers. `detectFormat` moves out of `probeFile` so
the decoder uses the same header-over-extension rule (Logic ships AIFF data in
`.wav` files).

**Files:**

- Create: `tests/test_decode.cpp`, `core/include/asma/core/Decode.h`,
  `core/src/Decode.cpp`
- Modify: `core/include/asma/core/AudioProbe.h`, `core/src/AudioProbe.cpp`,
  `core/src/vendor_dr.c`, `tests/TestUtil.h`

**Interfaces:**

- Consumes: `probeFile`, `ProbeError`, `FileAccessError`, `FilePtr`,
  `openFileRead` (plan 1).
- Produces:
  `asma::AudioFormat asma::detectFormat(const std::filesystem::path&)`;
  `struct asma::DecodedAudio { int sampleRate; std::vector<float> mono; bool truncated; double seconds() const; }`;
  `asma::DecodedAudio asma::decodeFile(const std::filesystem::path&, double maxSeconds = 30.0)`
  (throws `FileAccessError` / `ProbeError`); test helpers `WavSpec::formatTag`,
  `writeWavSamples(path, rate, samples)`.

Behaviour the tests pin:

- Mono is the plain average of the channels.
- `truncated` is true only when more audio follows `maxSeconds`.
- A file that probes fine but cannot be decoded (MPEG inside RIFF, format tag
  0x55) throws `ProbeError`; a missing file throws `FileAccessError`.

- [ ] **Step 1: Write the failing tests**

In `tests/TestUtil.h`, replace:

```cpp
    std::vector<std::pair<std::string, std::string>> chunksBeforeData; // {4-char id, payload}
};
```

with:

```cpp
    std::vector<std::pair<std::string, std::string>> chunksBeforeData; // {4-char id, payload}
    std::uint16_t formatTag = 1; // 1 = PCM; anything else makes an undecodable file
};
```

In `tests/TestUtil.h`, replace:

```cpp
    std::string fmt;
    putLe16(fmt, 1);
    putLe16(fmt, static_cast<std::uint16_t>(spec.channels));
```

with:

```cpp
    std::string fmt;
    putLe16(fmt, spec.formatTag);
    putLe16(fmt, static_cast<std::uint16_t>(spec.channels));
```

In `tests/TestUtil.h`, replace:

```cpp
struct AiffSpec {
```

with:

```cpp
// 16-bit mono PCM WAV of the given samples (clipped to -1..1).
inline void writeWavSamples(const fs::path& path, int sampleRate, const std::vector<float>& samples)
{
    using namespace detail;
    std::string fmt;
    putLe16(fmt, 1);
    putLe16(fmt, 1);
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate));
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate * 2));
    putLe16(fmt, 2);
    putLe16(fmt, 16);
    std::string data;
    data.reserve(samples.size() * 2);
    for (float s : samples) {
        const float clipped = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
        putLe16(data, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clipped * 32767.0f))));
    }
    std::string body = "WAVE";
    putChunkLe(body, "fmt ", fmt);
    putChunkLe(body, "data", data);
    std::string file = "RIFF";
    putLe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

struct AiffSpec {
```

Create `tests/test_decode.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/Decode.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

TEST_CASE("WAV decodes to a mono channel average", "[decode]")
{
    TempDir dir;
    const auto p = dir.path() / "stereo.wav";
    test::WavSpec spec;
    spec.channels = 2;
    spec.frames = 1000;
    test::writeWav(p, spec);
    const DecodedAudio a = decodeFile(p);
    CHECK(a.sampleRate == 44100);
    CHECK(a.mono.size() == 1000);
    CHECK_FALSE(a.truncated);
}

TEST_CASE("Decoded samples match what was written", "[decode]")
{
    TempDir dir;
    const auto p = dir.path() / "ramp.wav";
    test::writeWavSamples(p, 48000, {0.0f, 0.5f, -0.5f, 0.25f});
    const DecodedAudio a = decodeFile(p);
    REQUIRE(a.mono.size() == 4);
    CHECK(a.sampleRate == 48000);
    CHECK(a.mono[1] == Catch::Approx(0.5).margin(1e-3));
    CHECK(a.mono[2] == Catch::Approx(-0.5).margin(1e-3));
}

TEST_CASE("AIFF, FLAC, MP3 and Ogg decode", "[decode]")
{
    TempDir dir;
    const auto aiff = dir.path() / "a.aiff";
    test::AiffSpec spec;
    spec.frames = 22050;
    test::writeAiff(aiff, spec);
    CHECK(decodeFile(aiff).mono.size() == 22050);

    for (const char* name : {"tone.flac", "tone.mp3", "tone.ogg"}) {
        INFO(name);
        const DecodedAudio a = decodeFile(test::fixture(name));
        CHECK(a.sampleRate == 44100);
        CHECK(a.seconds() == Catch::Approx(0.5).margin(0.06));
    }
}

TEST_CASE("maxSeconds stops early and says so", "[decode]")
{
    TempDir dir;
    const auto p = dir.path() / "long.wav";
    test::WavSpec spec;
    spec.frames = 44100 * 3;
    test::writeWav(p, spec);
    const DecodedAudio a = decodeFile(p, 1.0);
    CHECK(a.mono.size() == 44100);
    CHECK(a.truncated);
    CHECK_FALSE(decodeFile(p, 3.0).truncated);
}

TEST_CASE("The header decides the decoder, not the extension", "[decode]")
{
    TempDir dir;
    const auto p = dir.path() / "snare.wav";
    test::AiffSpec spec;
    spec.frames = 4410;
    test::writeAiff(p, spec);
    CHECK(decodeFile(p).mono.size() == 4410);
}

TEST_CASE("Undecodable and missing files throw", "[decode]")
{
    TempDir dir;
    const auto odd = dir.path() / "mp3-in-wav.wav";
    test::WavSpec spec;
    spec.formatTag = 0x55; // MPEG layer 3 inside RIFF: probes fine, cannot be decoded
    test::writeWav(odd, spec);
    CHECK_NOTHROW(probeFile(odd));
    CHECK_THROWS_AS(decodeFile(odd), ProbeError);
    CHECK_THROWS_AS(decodeFile(dir.path() / "nope.wav"), FileAccessError);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: 'asma/core/Decode.h' file not found`

- [ ] **Step 3: Implement**

In `core/src/vendor_dr.c`, replace:

```c
#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
```

with:

```c
#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
```

In `core/include/asma/core/AudioProbe.h`, replace:

```cpp
// Reads headers only
```

with:

```cpp
// The format named by the file's first bytes, falling back to the extension
// when the header is not recognisable. Throws ProbeError for an unsupported
// extension and FileAccessError when the file cannot be opened.
AudioFormat detectFormat(const std::filesystem::path& path);

// Reads headers only
```

In `core/src/AudioProbe.cpp`, replace the whole `probeFile` function (from
`ProbeResult probeFile(const fs::path& path)` down to, not including, the
closing `} // namespace asma`) with:

```cpp
AudioFormat detectFormat(const fs::path& path)
{
    const auto byExtension = formatFromExtension(path);
    if (!byExtension) throw ProbeError("unsupported file extension");
    FilePtr file(openFileRead(path));
    if (!file) throw FileAccessError("cannot open file");
    unsigned char header[12] = {};
    const std::size_t got = std::fread(header, 1, sizeof header, file.get());
    return sniffFormat(header, got).value_or(*byExtension);
}

ProbeResult probeFile(const fs::path& path)
{
    const AudioFormat format = detectFormat(path);

    std::error_code ec;
    const std::uint64_t size = fs::file_size(path, ec);
    if (ec) throw FileAccessError("cannot read file size: " + ec.message());

    switch (format) {
    case AudioFormat::Wav:
    case AudioFormat::Aiff: {
        FilePtr file(openFileRead(path));
        if (!file) throw FileAccessError("cannot open file");
        return format == AudioFormat::Wav ? detail::probeWav(file.get(), size) : detail::probeAiff(file.get(), size);
    }
    case AudioFormat::Flac: return probeFlac(path, size);
    case AudioFormat::Mp3: return probeMp3(path, size);
    case AudioFormat::Ogg: return probeOgg(path, size);
    }
    throw ProbeError("unsupported format");
}
```

Create `core/include/asma/core/Decode.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <vector>

namespace asma {

struct DecodedAudio {
    int sampleRate = 0;
    std::vector<float> mono; // channel average, -1..1
    bool truncated = false;  // stopped at maxSeconds before the end
    double seconds() const { return sampleRate > 0 ? static_cast<double>(mono.size()) / sampleRate : 0.0; }
};

// Decodes up to maxSeconds from the start of the file, downmixed to mono.
// The header decides the format, as in probeFile. Throws FileAccessError when
// the file cannot be read and ProbeError when it cannot be decoded.
DecodedAudio decodeFile(const std::filesystem::path& path, double maxSeconds = 30.0);

} // namespace asma
```

Create `core/src/Decode.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
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
            for (int c = 0; c < channels; ++c) sum += buffer[static_cast<std::size_t>(i * channels + c)];
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
    if (audio.sampleRate <= 0) throw ProbeError("stream has no sample rate");
    if (audio.mono.empty()) throw ProbeError("stream has no audio frames");
    return audio;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[decode],[probe]"`

Expected: no compiler warnings;
`All tests passed (81 assertions in 16 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 103`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: decode audio to mono float; share header sniffing"
```

---

### Task 2: Loudness (EBU R128)

Loudness first: it needs no DSP of our own, and it proves the libebur128 wiring.

**Files:**

- Create: `tests/Signals.h`, `tests/test_loudness.cpp`,
  `core/include/asma/core/Analysis.h`, `core/src/Analysis.cpp`
- Modify: `cmake/Dependencies.cmake`, `core/CMakeLists.txt`

**Interfaces:**

- Consumes: Nothing from plan 2 yet.
- Produces: `struct asma::Loudness { double peak; double lufs; }`;
  `asma::Loudness asma::measureLoudness(const std::vector<float>&, int sampleRate)`;
  CMake target `asma_ebur128`; `tests/Signals.h` (deterministic synthetic audio
  used by every later analysis test).

Behaviour the tests pin:

- A 1 kHz sine at amplitude 0.1 measures -23.01 LUFS (full scale is -3.01 LUFS).
- Sounds under 400 ms are too short to gate; they are measured ungated over
  their whole length.
- Silence reports the floor, -70 LUFS.

- [ ] **Step 1: Write the failing tests**

Create `tests/Signals.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Deterministic synthetic audio for analysis tests. Generated, so there is no
// third-party audio in the repo.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace asma::test {

constexpr double kTwoPi = 6.28318530717958647692;

// Uniform noise in -1..1 from a 32-bit LCG.
class Noise {
public:
    explicit Noise(std::uint32_t seed) : state_(seed) {}
    float next()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

inline std::vector<float> sine(double hz, double seconds, double amplitude, int rate)
{
    std::vector<float> out(static_cast<std::size_t>(std::lround(seconds * rate)));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(kTwoPi * hz * static_cast<double>(i) / rate));
    return out;
}

inline std::vector<float> noise(double seconds, double amplitude, int rate, std::uint32_t seed)
{
    Noise n(seed);
    std::vector<float> out(static_cast<std::size_t>(std::lround(seconds * rate)));
    for (auto& x : out) x = static_cast<float>(amplitude) * n.next();
    return out;
}

// Adds a hit starting at `start` (samples), clipped to the buffer.
inline void addKick(std::vector<float>& out, std::size_t start, int rate, double amplitude = 0.8)
{
    double phase = 0.0;
    const auto length = static_cast<std::size_t>(0.35 * rate);
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double hz = 50.0 + 100.0 * std::exp(-t / 0.03); // pitch drop 150 -> 50 Hz
        phase += kTwoPi * hz / rate;
        out[start + i] += static_cast<float>(amplitude * std::exp(-t / 0.08) * std::sin(phase));
    }
}

inline void addSnare(std::vector<float>& out, std::size_t start, int rate, Noise& noise, double amplitude = 0.5)
{
    const auto length = static_cast<std::size_t>(0.2 * rate);
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double body = 0.4 * std::sin(kTwoPi * 190.0 * t);
        out[start + i] += static_cast<float>(amplitude * std::exp(-t / 0.05) * (noise.next() + body));
    }
}

inline void addHat(std::vector<float>& out, std::size_t start, int rate, Noise& noise, double amplitude = 0.2)
{
    const auto length = static_cast<std::size_t>(0.06 * rate);
    float previous = 0.0f;
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const float white = noise.next();
        out[start + i] += static_cast<float>(amplitude * std::exp(-t / 0.015)) * (white - previous); // crude high-pass
        previous = white;
    }
}

// Kick on beats 1 and 3, snare on 2 and 4, hats on eighths, cut to whole bars.
inline std::vector<float> drumLoop(double bpm, int bars, int rate, std::uint32_t seed = 7)
{
    const double beat = 60.0 / bpm;
    std::vector<float> out(static_cast<std::size_t>(std::lround(bars * 4 * beat * rate)));
    Noise noise(seed);
    for (int b = 0; b < bars * 4; ++b) {
        const auto at = static_cast<std::size_t>(std::lround(b * beat * rate));
        if (b % 2 == 0) addKick(out, at, rate);
        else addSnare(out, at, rate, noise);
        addHat(out, at, rate, noise);
        addHat(out, static_cast<std::size_t>(std::lround((b + 0.5) * beat * rate)), rate, noise);
    }
    return out;
}

// A tone with five harmonics at 1/h amplitude.
inline void addTone(std::vector<float>& out, std::size_t start, std::size_t length, double hz, double amplitude,
                    int rate)
{
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double fade = std::min(1.0, std::min(t / 0.01, static_cast<double>(length - i) / (0.01 * rate)));
        double v = 0.0;
        for (int h = 1; h <= 5; ++h) v += std::sin(kTwoPi * hz * h * t) / h;
        out[start + i] += static_cast<float>(amplitude * fade * v);
    }
}

// I-IV-V-I (major) or i-iv-V-i (harmonic minor) with a bass note, two
// seconds per chord. tonic: pitch class, 0 = C.
inline std::vector<float> chordProgression(int tonic, bool minor, int rate)
{
    const double root = 130.8128 * std::pow(2.0, tonic / 12.0); // C3 upwards
    const int third = minor ? 3 : 4;
    // Each chord: semitone offsets from the tonic.
    const std::vector<std::vector<int>> chords = {
        {0, third, 7}, {5, 5 + (minor ? 3 : 4), 12}, {7, 11, 14}, {0, third, 7}};
    const auto chordLength = static_cast<std::size_t>(2 * rate);
    std::vector<float> out(chordLength * chords.size());
    for (std::size_t c = 0; c < chords.size(); ++c) {
        const std::size_t start = c * chordLength;
        addTone(out, start, chordLength, root * std::pow(2.0, (chords[c][0] - 12) / 12.0), 0.2, rate);
        for (int semitone : chords[c]) addTone(out, start, chordLength, root * std::pow(2.0, semitone / 12.0), 0.12, rate);
    }
    return out;
}

inline std::vector<float> kickHit(int rate) { std::vector<float> out(static_cast<std::size_t>(0.5 * rate)); addKick(out, 0, rate); return out; }

inline std::vector<float> hatHit(int rate, std::uint32_t seed)
{
    std::vector<float> out(static_cast<std::size_t>(0.25 * rate));
    Noise noise(seed);
    addHat(out, 0, rate, noise, 0.6);
    return out;
}

} // namespace asma::test
```

Create `tests/test_loudness.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;

namespace {
constexpr int kRate = 44100;
} // namespace

TEST_CASE("Loudness of a 1 kHz sine matches EBU R128", "[loudness]")
{
    // A full-scale 1 kHz sine is -3.01 LUFS; amplitude 0.1 is 20 dB lower.
    const Loudness long_ = measureLoudness(test::sine(1000.0, 2.0, 0.1, kRate), kRate);
    CHECK(long_.lufs == Catch::Approx(-23.01).margin(0.1));
    CHECK(long_.peak == Catch::Approx(0.1).margin(1e-3));
}

TEST_CASE("Sounds under 400 ms are measured ungated", "[loudness]")
{
    const Loudness shortSine = measureLoudness(test::sine(1000.0, 0.2, 0.1, kRate), kRate);
    CHECK(shortSine.lufs == Catch::Approx(-23.01).margin(0.5));
}

TEST_CASE("Silence has the loudness floor", "[loudness]")
{
    const Loudness silence = measureLoudness(std::vector<float>(kRate, 0.0f), kRate);
    CHECK(silence.lufs == -70.0);
    CHECK(silence.peak == 0.0);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: 'asma/core/Analysis.h' file not found`

- [ ] **Step 3: Implement**

In `cmake/Dependencies.cmake`, replace:

```cmake
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb)
```

with:

```cmake
FetchContent_Declare(ebur128
  GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
  GIT_TAG v1.2.6
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128)
```

Append to `cmake/Dependencies.cmake`:

```cmake

add_library(asma_ebur128 STATIC ${ebur128_SOURCE_DIR}/ebur128/ebur128.c)
target_include_directories(asma_ebur128
  SYSTEM PUBLIC ${ebur128_SOURCE_DIR}/ebur128
  PRIVATE ${ebur128_SOURCE_DIR}/ebur128/queue)
if(MSVC)
  target_compile_definitions(asma_ebur128 PRIVATE _USE_MATH_DEFINES)
  target_compile_options(asma_ebur128 PRIVATE /w)
else()
  target_compile_options(asma_ebur128 PRIVATE -w)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(asma_ebur128 PUBLIC m)
endif()
```

In `core/CMakeLists.txt`, replace:

```cmake
PRIVATE asma_xxhash asma_dr_libs asma_stb Threads::Threads)
```

with:

```cmake
PRIVATE asma_xxhash asma_dr_libs asma_stb asma_ebur128 Threads::Threads)
```

Create `core/include/asma/core/Analysis.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <vector>

namespace asma {

struct Loudness {
    double peak = 0.0;   // linear sample peak
    double lufs = -70.0; // integrated; ungated over the whole sound under 400 ms; floor -70
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);

} // namespace asma
```

Create `core/src/Analysis.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include <ebur128.h>

namespace asma {

// ---- Loudness

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate)
{
    Loudness result;
    if (mono.empty() || sampleRate <= 0) return result;
    ebur128_state* state =
        ebur128_init(1, static_cast<unsigned long>(sampleRate), EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK);
    if (!state) return result;
    const auto durationMs =
        std::max<unsigned long>(1, static_cast<unsigned long>(mono.size() * 1000 / static_cast<std::size_t>(sampleRate)));
    ebur128_add_frames_float(state, mono.data(), mono.size());

    double lufs = -HUGE_VAL;
    ebur128_loudness_global(state, &lufs);
    // Sounds under 400 ms are too short to gate: measure them ungated over
    // their whole length, which the default 400 ms window still holds.
    if (!std::isfinite(lufs) && durationMs <= 400) ebur128_loudness_window(state, durationMs, &lufs);
    double peak = 0.0;
    ebur128_sample_peak(state, 0, &peak);
    ebur128_destroy(&state);

    result.peak = peak;
    result.lufs = std::isfinite(lufs) ? std::max(lufs, -70.0) : -70.0;
    return result;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[loudness]"`

Expected: no compiler warnings;
`All tests passed (5 assertions in 3 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 106`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: measure loudness with libebur128"
```

---

### Task 3: Tempo

A free tempo estimate for audio of unknown length (Task 5 adds the bar-fit
estimate for complete loops).

**Files:**

- Create: `tests/test_tempo.cpp`, `core/src/Fft.h`, `core/src/Fft.cpp`,
  `core/include/asma/core/Analysis.h`
- Modify: `core/src/Analysis.cpp`

**Interfaces:**

- Consumes: `Loudness` header from Task 2.
- Produces: `struct asma::TempoEstimate { double bpm; double confidence; }`;
  `std::optional<asma::TempoEstimate> asma::estimateTempo(const std::vector<float>&, int sampleRate)`;
  internal `detail::fft`, `detail::stft`, `detail::powerOfTwoAtLeast` in
  `core/src/Fft.h`.

Behaviour the tests pin:

- Onsets are log-magnitude spectral flux on ~23 ms frames, hop a quarter frame.
- Beats are judged on flux below 1.5 kHz, where kicks and snares live; with
  almost nothing down there (a shaker loop) all bins are used.
- The autocorrelation runs on the mean-centred envelope, so a single hit does
  not look periodic.
- A log-Gaussian prior around 120 BPM picks between candidates; a metre check
  then halves the tempo when every other pulse is weak (hats between kicks and
  snares) or doubles it when the half-beat positions are as strong as the beats.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_tempo.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;

namespace {
constexpr int kRate = 44100;
} // namespace

TEST_CASE("Free tempo estimates of drum loops", "[tempo]")
{
    for (double bpm : {70.0, 100.0, 128.0, 174.0}) {
        INFO(bpm);
        const auto t = estimateTempo(test::drumLoop(bpm, 4, kRate), kRate);
        REQUIRE(t.has_value());
        CHECK(t->bpm == Catch::Approx(bpm).margin(1.0));
        CHECK(t->confidence > 0.5);
    }
}

TEST_CASE("Shaker loops with nothing below 1.5 kHz still get a tempo", "[tempo]")
{
    std::vector<float> shaker(static_cast<std::size_t>(8 * kRate));
    test::Noise noise(3);
    for (int i = 0; i < 32; ++i) test::addHat(shaker, static_cast<std::size_t>(i * 0.25 * kRate), kRate, noise, 0.5);
    const auto t = estimateTempo(shaker, kRate);
    REQUIRE(t.has_value());
    CHECK(t->bpm == Catch::Approx(120.0).margin(1.0));
}

TEST_CASE("No tempo for under a second or for silence", "[tempo]")
{
    CHECK_FALSE(estimateTempo(std::vector<float>(kRate / 2, 0.1f), kRate).has_value());
    CHECK_FALSE(estimateTempo(std::vector<float>(2 * kRate, 0.0f), kRate).has_value());
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: use of undeclared identifier 'estimateTempo'`

- [ ] **Step 3: Implement**

Create `core/src/Fft.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace asma::detail {

// In-place iterative radix-2 FFT. data.size() must be a power of two.
void fft(std::vector<std::complex<float>>& data);

// Smallest power of two >= value (at least 1).
std::size_t powerOfTwoAtLeast(double value);

// Magnitude spectra (size / 2 + 1 bins) of Hann-windowed frames starting every
// `hop` samples. The final partial frame is zero-padded; a signal shorter than
// one frame yields one frame.
std::vector<std::vector<float>> stft(const std::vector<float>& signal, std::size_t size, std::size_t hop);

} // namespace asma::detail
```

Create `core/src/Fft.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Fft.h"

#include <cmath>
#include <utility>

namespace asma::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Twiddle factors e^(-2*pi*i*k/n) for k < n/2.
std::vector<std::complex<float>> twiddles(std::size_t n)
{
    std::vector<std::complex<float>> w(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n);
        w[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }
    return w;
}

void transform(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& w)
{
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t stride = n / len;
        for (std::size_t i = 0; i < n; i += len) {
            for (std::size_t k = 0; k < half; ++k) {
                // Written out: std::complex operator* guards NaN/inf and is slow.
                const std::complex<float> tw = w[k * stride];
                const std::complex<float> odd = data[i + k + half];
                const float re = odd.real() * tw.real() - odd.imag() * tw.imag();
                const float im = odd.real() * tw.imag() + odd.imag() * tw.real();
                const std::complex<float> even = data[i + k];
                data[i + k] = {even.real() + re, even.imag() + im};
                data[i + k + half] = {even.real() - re, even.imag() - im};
            }
        }
    }
}

} // namespace

void fft(std::vector<std::complex<float>>& data) { transform(data, twiddles(data.size())); }

std::size_t powerOfTwoAtLeast(double value)
{
    std::size_t n = 1;
    while (static_cast<double>(n) < value) n <<= 1;
    return n;
}

std::vector<std::vector<float>> stft(const std::vector<float>& signal, std::size_t size, std::size_t hop)
{
    std::vector<float> window(size);
    for (std::size_t i = 0; i < size; ++i)
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(size)));
    const auto w = twiddles(size);

    std::vector<std::vector<float>> frames;
    frames.reserve(signal.size() / hop + 1);
    std::vector<std::complex<float>> buffer(size);
    for (std::size_t start = 0; start == 0 || start < signal.size(); start += hop) {
        for (std::size_t i = 0; i < size; ++i) {
            const float x = start + i < signal.size() ? signal[start + i] : 0.0f;
            buffer[i] = {x * window[i], 0.0f};
        }
        transform(buffer, w);
        std::vector<float> magnitudes(size / 2 + 1);
        for (std::size_t k = 0; k < magnitudes.size(); ++k)
            magnitudes[k] = std::sqrt(buffer[k].real() * buffer[k].real() + buffer[k].imag() * buffer[k].imag());
        frames.push_back(std::move(magnitudes));
    }
    return frames;
}

} // namespace asma::detail
```

Replace `core/include/asma/core/Analysis.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <vector>

namespace asma {

struct Loudness {
    double peak = 0.0;   // linear sample peak
    double lufs = -70.0; // integrated; ungated over the whole sound under 400 ms; floor -70
};

struct TempoEstimate {
    double bpm = 0.0;
    double confidence = 0.0; // 0..1
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);
// Free estimate for rhythmic material of unknown length. nullopt for sounds
// under one second or without onsets. Confidence is the normalised
// autocorrelation at the beat; below ~0.4 the tempo is a guess.
std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate);

} // namespace asma
```

In `core/src/Analysis.cpp`, replace:

```cpp
#include "asma/core/Analysis.h"

#include <algorithm>
```

with:

```cpp
#include "asma/core/Analysis.h"

#include "Fft.h"

#include <algorithm>
```

In `core/src/Analysis.cpp`, insert before the final `} // namespace asma` line:

```cpp
// ---- Tempo

namespace {

struct Onsets {
    std::vector<float> envelope; // positive spectral flux above its local mean
    double frameRate = 0.0;      // envelope frames per second
};

constexpr double kLowHz = 1500.0; // kick and snare bodies; hats sit above this

struct OnsetPair {
    Onsets full; // all bins
    Onsets low;  // bins up to kLowHz
};

// Positive spectral flux above its local mean (~0.25 s), from one list of
// frames, over the first `bins` bins.
std::vector<float> fluxEnvelope(const std::vector<std::vector<float>>& frames, std::size_t bins, double frameRate)
{
    std::vector<float> flux(frames.size(), 0.0f);
    std::vector<float> previous(bins, 0.0f);
    for (std::size_t t = 0; t < frames.size(); ++t) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < bins; ++k) {
            const float value = std::log1p(100.0f * frames[t][k]);
            if (t > 0 && value > previous[k]) sum += value - previous[k];
            previous[k] = value;
        }
        flux[t] = sum;
    }
    const auto half = static_cast<std::size_t>(std::max(1.0, std::round(frameRate * 0.125)));
    std::vector<double> prefix(flux.size() + 1, 0.0);
    for (std::size_t t = 0; t < flux.size(); ++t) prefix[t + 1] = prefix[t] + flux[t];
    std::vector<float> envelope(flux.size());
    for (std::size_t t = 0; t < flux.size(); ++t) {
        const std::size_t lo = t >= half ? t - half : 0;
        const std::size_t hi = std::min(flux.size(), t + half + 1);
        const double mean = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
        envelope[t] = static_cast<float>(std::max(0.0, flux[t] - mean));
    }
    return envelope;
}

// Log-magnitude spectral flux on ~23 ms frames with a 4x overlap.
OnsetPair onsetEnvelopes(const std::vector<float>& mono, int rate)
{
    const std::size_t size = detail::powerOfTwoAtLeast(rate * 0.023);
    const std::size_t hop = size / 4;
    const auto frames = detail::stft(mono, size, hop);
    const double frameRate = static_cast<double>(rate) / static_cast<double>(hop);
    const std::size_t bins = frames.front().size();
    const std::size_t lowBins = std::min(bins, static_cast<std::size_t>(kLowHz * static_cast<double>(size) / rate) + 1);
    OnsetPair pair;
    pair.full = {fluxEnvelope(frames, bins, frameRate), frameRate};
    pair.low = {fluxEnvelope(frames, lowBins, frameRate), frameRate};
    return pair;
}

// Onsets are one-frame spikes; smoothing lets beats that fall between frames
// still line up at integer lags.

// Onsets are one-frame spikes; smoothing lets beats that fall between frames
// still line up at integer lags.
std::vector<float> smoothed(const std::vector<float>& raw)
{
    static constexpr std::array<float, 5> kSmooth = {0.1f, 0.2f, 0.4f, 0.2f, 0.1f};
    std::vector<float> e(raw.size(), 0.0f);
    for (std::size_t t = 0; t < raw.size(); ++t)
        for (std::size_t j = 0; j < kSmooth.size(); ++j)
            if (t + j >= 2 && t + j - 2 < raw.size()) e[t] += kSmooth[j] * raw[t + j - 2];
    return e;
}

double autocorrelation(const std::vector<float>& e, std::size_t lag)
{
    if (lag >= e.size()) return 0.0;
    double sum = 0.0;
    for (std::size_t t = 0; t + lag < e.size(); ++t) sum += static_cast<double>(e[t]) * e[t + lag];
    return sum / static_cast<double>(e.size() - lag);
}

constexpr double kMinBpm = 55.0;
constexpr double kMaxBpm = 210.0;

double tempoPrior(double bpm)
{
    const double octaves = std::log2(bpm / 120.0);
    return std::exp(-0.5 * octaves * octaves); // log-Gaussian around 120, one octave wide
}

struct BeatEnvelope {
    std::vector<float> e;        // smoothed onset envelope, for pulse strengths
    std::vector<float> centered; // e minus its mean, for autocorrelation
    double frameRate = 0.0;
    double energy = 0.0;         // autocorrelation of `centered` at lag 0
};

// Beats are judged on onsets below 1.5 kHz, where kicks and snares live and
// hats barely register; otherwise eighth-note hats look like beats. Material
// with nothing down there (shaker loops) falls back to all bins.
BeatEnvelope beatEnvelope(const OnsetPair& pair)
{
    const double lowSum = std::accumulate(pair.low.envelope.begin(), pair.low.envelope.end(), 0.0);
    const double fullSum = std::accumulate(pair.full.envelope.begin(), pair.full.envelope.end(), 0.0);
    const Onsets& onsets = lowSum < 0.05 * fullSum ? pair.full : pair.low;
    BeatEnvelope b;
    b.e = smoothed(onsets.envelope);
    b.frameRate = onsets.frameRate;
    // The envelope is never negative, so its raw autocorrelation is high at
    // every lag. Centred, periodic pulses still peak while a single hit or a
    // noisy tail averages out.
    const double mean = b.e.empty() ? 0.0 : std::accumulate(b.e.begin(), b.e.end(), 0.0) / static_cast<double>(b.e.size());
    b.centered.resize(b.e.size());
    for (std::size_t i = 0; i < b.e.size(); ++i) b.centered[i] = static_cast<float>(b.e[i] - mean);
    b.energy = autocorrelation(b.centered, 0);
    return b;
}

// Metre check at a beat period (frames). Pulses aligned to the strongest
// phase: if every other pulse is weak (hats between kicks and snares), the
// beat is twice as slow (returns 0.5); if the midpoints are as strong as the
// pulses, twice as fast (returns 2). Otherwise 1.
double metreFactor(const BeatEnvelope& b, double period)
{
    const auto& e = b.e;
    auto strengthAt = [&](double position) {
        const auto i = static_cast<std::size_t>(std::lround(position));
        float m = 0.0f;
        for (std::size_t j = i >= 1 ? i - 1 : 0; j <= i + 1 && j < e.size(); ++j) m = std::max(m, e[j]);
        return static_cast<double>(m);
    };
    auto pulses = [&](double phase, int parity) { // parity: -1 all, 0 even, 1 odd
        double sum = 0.0;
        int count = 0;
        int index = 0;
        for (double x = phase; x < static_cast<double>(e.size()); x += period, ++index) {
            if (parity >= 0 && index % 2 != parity) continue;
            sum += strengthAt(x);
            ++count;
        }
        return count > 0 ? sum / count : 0.0;
    };
    double phase = 0.0;
    double phaseScore = -1.0;
    for (double p = 0.0; p < period; p += 1.0) {
        const double s = pulses(p, -1);
        if (s > phaseScore) {
            phaseScore = s;
            phase = p;
        }
    }
    const double even = pulses(phase, 0);
    const double odd = pulses(phase, 1);
    const double middle = pulses(phase + period / 2.0, -1);
    if (std::min(even, odd) < 0.5 * std::max(even, odd)) return 0.5;
    if (middle >= 0.6 * std::min(even, odd)) return 2.0;
    return 1.0;
}

// Free tempo estimate: the autocorrelation peak between 55 and 210 BPM,
// weighted towards 120, refined to sub-frame precision, then metre-checked.
std::optional<TempoEstimate> tempoFromOnsets(const OnsetPair& pair)
{
    const BeatEnvelope b = beatEnvelope(pair);
    if (b.energy <= 0.0) return std::nullopt;
    const auto lagMin = static_cast<std::size_t>(std::floor(60.0 * b.frameRate / kMaxBpm));
    const auto lagMax = std::min(static_cast<std::size_t>(std::ceil(60.0 * b.frameRate / kMinBpm)), b.centered.size() / 2);
    if (lagMin < 2 || lagMax <= lagMin + 2) return std::nullopt;

    std::size_t best = 0;
    double bestScore = 0.0;
    for (std::size_t lag = lagMin; lag <= lagMax; ++lag) {
        const double score = autocorrelation(b.centered, lag) * tempoPrior(60.0 * b.frameRate / static_cast<double>(lag));
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best == 0) return std::nullopt;

    // Parabolic interpolation around the peak.
    const double y0 = autocorrelation(b.centered, best - 1);
    const double y1 = autocorrelation(b.centered, best);
    const double y2 = autocorrelation(b.centered, best + 1);
    const double denominator = y0 - 2.0 * y1 + y2;
    const double offset = denominator < 0.0 ? std::clamp(0.5 * (y0 - y2) / denominator, -0.5, 0.5) : 0.0;
    const double period = static_cast<double>(best) + offset;

    double bpm = 60.0 * b.frameRate / period;
    const double factor = metreFactor(b, period);
    if (bpm * factor >= kMinBpm && bpm * factor <= kMaxBpm) bpm *= factor;

    TempoEstimate estimate;
    estimate.bpm = bpm;
    estimate.confidence = std::clamp(y1 / b.energy, 0.0, 1.0);
    return estimate;
}

} // namespace

std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate)
{
    if (sampleRate <= 0 || mono.size() < static_cast<std::size_t>(sampleRate)) return std::nullopt;
    return tempoFromOnsets(onsetEnvelopes(mono, sampleRate));
}
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[tempo],[loudness]"`

Expected: no compiler warnings;
`All tests passed (21 assertions in 6 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 109`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: estimate tempo from low-band onsets"
```

---

### Task 4: Key

Key from a chroma profile, as the spec says.

**Files:**

- Create: `tests/test_key.cpp`, `core/include/asma/core/Analysis.h`
- Modify: `core/src/Analysis.cpp`

**Interfaces:**

- Consumes: `detail::stft` from Task 3.
- Produces: `struct asma::KeyEstimate { std::string key; double confidence; }`;
  `std::optional<asma::KeyEstimate> asma::estimateKey(const std::vector<float>&, int sampleRate)`.

Behaviour the tests pin:

- Chroma from ~0.37 s frames (2.7 Hz bins at 44.1 kHz), 100 Hz to 5 kHz,
  magnitude weighted.
- Temperley-Kostka-Payne profiles. Krumhansl profiles mistook minor keys for
  their parallel major on the test progressions; Temperley got all 24 right.
- No key for noise-like material (flatness above 0.3), for correlations below
  0.5, or for under a quarter second.
- Key names are canonical as in plan 1 (`parseKeyToken`): sharps only, minor
  with `m`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_key.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;

namespace {
constexpr int kRate = 44100;
} // namespace

TEST_CASE("Chord progressions give their key", "[key]")
{
    CHECK(estimateKey(test::chordProgression(0, false, kRate), kRate)->key == "C");
    CHECK(estimateKey(test::chordProgression(9, true, kRate), kRate)->key == "Am");
    CHECK(estimateKey(test::chordProgression(6, true, kRate), kRate)->key == "F#m");
}

TEST_CASE("Noise has no key", "[key]")
{
    CHECK_FALSE(estimateKey(test::noise(2.0, 0.3, kRate, 3), kRate).has_value());
}

TEST_CASE("A quarter of a second or less has no key", "[key]")
{
    CHECK_FALSE(estimateKey(test::sine(440.0, 0.2, 0.5, kRate), kRate).has_value());
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: use of undeclared identifier 'estimateKey'`

- [ ] **Step 3: Implement**

Replace `core/include/asma/core/Analysis.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace asma {

struct Loudness {
    double peak = 0.0;   // linear sample peak
    double lufs = -70.0; // integrated; ungated over the whole sound under 400 ms; floor -70
};

struct TempoEstimate {
    double bpm = 0.0;
    double confidence = 0.0; // 0..1
};

struct KeyEstimate {
    std::string key; // canonical, as parseKeyToken
    double confidence = 0.0;
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);
// Free estimate for rhythmic material of unknown length. nullopt for sounds
// under one second or without onsets. Confidence is the normalised
// autocorrelation at the beat; below ~0.4 the tempo is a guess.
std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate);
// nullopt for noisy, atonal or very short sounds.
std::optional<KeyEstimate> estimateKey(const std::vector<float>& mono, int sampleRate);

} // namespace asma
```

In `core/src/Analysis.cpp`, insert before the final `} // namespace asma` line:

```cpp
// ---- Key

std::optional<KeyEstimate> estimateKey(const std::vector<float>& mono, int sampleRate)
{
    if (sampleRate <= 0 || mono.size() < static_cast<std::size_t>(sampleRate / 4)) return std::nullopt;
    const std::size_t size = detail::powerOfTwoAtLeast(sampleRate * 0.37); // ~2.7 Hz bins at 44.1 kHz
    const auto frames = detail::stft(mono, size, size / 2);
    const double binHz = static_cast<double>(sampleRate) / static_cast<double>(size);

    std::vector<std::pair<std::size_t, int>> binClass; // bin -> pitch class, 100 Hz .. 5 kHz
    for (std::size_t k = 1; k < size / 2 + 1; ++k) {
        const double f = static_cast<double>(k) * binHz;
        if (f < 100.0 || f > 5000.0) continue;
        const long midi = std::lround(69.0 + 12.0 * std::log2(f / 440.0));
        binClass.emplace_back(k, static_cast<int>(((midi % 12) + 12) % 12));
    }
    if (binClass.empty()) return std::nullopt;

    std::array<double, 12> chroma{};
    double flatnessSum = 0.0;
    double weightSum = 0.0;
    for (const auto& frame : frames) {
        double total = 0.0;
        double logSum = 0.0;
        for (const auto& [k, pc] : binClass) {
            const double p = static_cast<double>(frame[k]) * frame[k];
            total += p;
            logSum += std::log(p + 1e-12);
            chroma[static_cast<std::size_t>(pc)] += frame[k];
        }
        if (total <= 0.0) continue;
        const double mean = total / static_cast<double>(binClass.size());
        flatnessSum += total * std::exp(logSum / static_cast<double>(binClass.size())) / (mean + 1e-12);
        weightSum += total;
    }
    if (weightSum <= 0.0) return std::nullopt;
    if (flatnessSum / weightSum > 0.3) return std::nullopt; // noise-like: no key

    // Temperley-Kostka-Payne key profiles.
    static constexpr std::array<double, 12> kMajor = {0.748, 0.060, 0.488, 0.082, 0.670, 0.460,
                                                      0.096, 0.715, 0.104, 0.366, 0.057, 0.400};
    static constexpr std::array<double, 12> kMinor = {0.712, 0.084, 0.474, 0.618, 0.049, 0.460,
                                                      0.105, 0.747, 0.404, 0.067, 0.133, 0.330};
    static constexpr std::array<const char*, 12> kNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    auto correlate = [&](const std::array<double, 12>& profile, int tonic) {
        double mx = 0.0;
        double my = 0.0;
        for (int i = 0; i < 12; ++i) {
            mx += chroma[static_cast<std::size_t>(i)];
            my += profile[static_cast<std::size_t>((i - tonic + 12) % 12)];
        }
        mx /= 12.0;
        my /= 12.0;
        double sxy = 0.0;
        double sxx = 0.0;
        double syy = 0.0;
        for (int i = 0; i < 12; ++i) {
            const double x = chroma[static_cast<std::size_t>(i)] - mx;
            const double y = profile[static_cast<std::size_t>((i - tonic + 12) % 12)] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0.0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };

    double best = -2.0;
    double second = -2.0;
    std::string bestKey;
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (bool minor : {false, true}) {
            const double r = correlate(minor ? kMinor : kMajor, tonic);
            if (r > best) {
                second = best;
                best = r;
                bestKey = std::string(kNames[static_cast<std::size_t>(tonic)]) + (minor ? "m" : "");
            } else if (r > second) {
                second = r;
            }
        }
    }
    if (best < 0.5) return std::nullopt;
    KeyEstimate estimate;
    estimate.key = bestKey;
    estimate.confidence = std::clamp((best - second) / 0.2, 0.0, 1.0) * std::clamp(best, 0.0, 1.0);
    return estimate;
}
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[key],[tempo],[loudness]"`

Expected: no compiler warnings;
`All tests passed (26 assertions in 9 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 112`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: estimate key with Temperley profiles"
```

---

### Task 5: Descriptors, loop or one-shot, and the accuracy gate

Puts it together. Loops get their tempo from their length; one-shots are told
apart from loops.

**Files:**

- Create: `tests/test_analysis.cpp`, `tests/test_analysis_accuracy.cpp`,
  `core/include/asma/core/Analysis.h`
- Modify: `core/src/Analysis.cpp`

**Interfaces:**

- Consumes: `measureLoudness`, `estimateTempo` internals, `estimateKey`,
  `DecodedAudio`.
- Produces: `constexpr int asma::kAnalysisVersion = 1`;
  `constexpr std::size_t asma::kFeatureVectorSize = 31`;
  `struct asma::AnalysisResult` (loudness, bpm, bpmConfidence, key,
  keyConfidence, isLoop, centroid, rolloff, flatness, onsetDensity,
  featureVector); `asma::AnalysisResult asma::analyse(const DecodedAudio&)`.

Behaviour the tests pin:

- Under one second: one-shot, no tempo.
- Complete files of a second or more: only tempos that fit a whole number of 4/4
  bars (`240 * bars / seconds`, 55 to 210 BPM) are scored. The winner counts as
  a loop when its correlation is at least 0.15 and less than 60% of the energy
  sits in the first quarter.
- Otherwise, a sound that fades 40 dB by its last tenth is a one-shot; anything
  else is left unknown.
- Truncated files (longer than `maxSeconds`) are never called loops; they keep a
  free tempo only at confidence 0.4 or more.
- The feature vector is 13 MFCC means, 13 MFCC standard deviations, log
  centroid, log rolloff, flatness, log onset density and log duration.
- The accuracy gate: all 32 generated loops (16 tempos from 70 to 174 BPM, 2 and
  4 bars) exact to 0.5 BPM, all 24 keys right. Any regression fails CI.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_analysis.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;

namespace {
constexpr int kRate = 44100;
} // namespace

namespace {
DecodedAudio audio(std::vector<float> mono, int rate = kRate)
{
    DecodedAudio a;
    a.sampleRate = rate;
    a.mono = std::move(mono);
    return a;
}
} // namespace

TEST_CASE("A drum loop gets its exact tempo and is a loop", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(128.0, 4, kRate)));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(128.0).margin(0.01));
    CHECK(r.isLoop == true);
    CHECK(r.bpmConfidence > 0.15);
}

TEST_CASE("Hats between kicks and snares do not double the tempo", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(70.0, 2, kRate)));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(70.0).margin(0.01));
}

TEST_CASE("Fast loops are not halved", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(174.0, 4, kRate)));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(174.0).margin(0.01));
}

TEST_CASE("A kick hit is a one-shot with no tempo and no key", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::kickHit(kRate)));
    CHECK(r.isLoop == false);
    CHECK_FALSE(r.bpm.has_value());
    CHECK_FALSE(r.key.has_value());
}

TEST_CASE("A single hit followed by silence is a one-shot", "[analysis]")
{
    std::vector<float> hit(static_cast<std::size_t>(2 * kRate), 0.0f);
    test::addKick(hit, 0, kRate);
    const AnalysisResult r = analyse(audio(hit));
    CHECK(r.isLoop == false);
    CHECK_FALSE(r.bpm.has_value());
}

TEST_CASE("A long decaying hit is a one-shot", "[analysis]")
{
    std::vector<float> tone = test::sine(220.0, 3.0, 0.5, kRate);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] *= static_cast<float>(std::exp(-static_cast<double>(i) / kRate / 0.25));
    const AnalysisResult r = analyse(audio(tone));
    CHECK(r.isLoop == false);
    CHECK_FALSE(r.bpm.has_value());
}

TEST_CASE("Noise and drums have no key", "[analysis]")
{
    CHECK_FALSE(estimateKey(test::noise(2.0, 0.3, kRate, 3), kRate).has_value());
    CHECK_FALSE(analyse(audio(test::drumLoop(120.0, 4, kRate))).key.has_value());
}

TEST_CASE("Audio cut short by maxSeconds is not called a loop", "[analysis]")
{
    DecodedAudio a = audio(test::drumLoop(120.0, 8, kRate));
    a.truncated = true;
    const AnalysisResult r = analyse(a);
    CHECK_FALSE(r.isLoop.has_value());
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(120.0).margin(0.5));
}

TEST_CASE("Descriptors separate bright noise from a low tone", "[analysis]")
{
    const AnalysisResult hat = analyse(audio(test::hatHit(kRate, 5)));
    const AnalysisResult kick = analyse(audio(test::kickHit(kRate)));
    CHECK(hat.centroid > 4.0 * kick.centroid);
    CHECK(hat.flatness > kick.flatness);
}

TEST_CASE("Feature vectors are complete, finite and deterministic", "[analysis]")
{
    const DecodedAudio a = audio(test::drumLoop(100.0, 2, kRate));
    const AnalysisResult first = analyse(a);
    const AnalysisResult second = analyse(a);
    REQUIRE(first.featureVector.size() == kFeatureVectorSize);
    for (float v : first.featureVector) CHECK(std::isfinite(v));
    CHECK(first.featureVector == second.featureVector);
    CHECK(first.bpm == second.bpm);
}

TEST_CASE("Very short and silent audio does not break analysis", "[analysis]")
{
    const AnalysisResult tiny = analyse(audio(std::vector<float>(10, 0.1f)));
    CHECK(tiny.isLoop == false);
    CHECK(tiny.featureVector.size() == kFeatureVectorSize);
    const AnalysisResult silent = analyse(audio(std::vector<float>(kRate * 2, 0.0f)));
    CHECK_FALSE(silent.bpm.has_value());
    CHECK(silent.featureVector.size() == kFeatureVectorSize);
    for (float v : silent.featureVector) CHECK(std::isfinite(v));
}

TEST_CASE("Other sample rates work", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(120.0, 2, 96000), 96000));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(120.0).margin(0.01));
}
```

Create `tests/test_analysis_accuracy.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Accuracy gate on generated audio. The corpus is synthetic, so it is exact
// and contains no third-party material. A change that makes any of these
// worse fails CI. Real-library numbers come from the hidden [.real] test.
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

using namespace asma;

TEST_CASE("Tempo: every generated loop from 70 to 174 BPM, 2 and 4 bars", "[accuracy]")
{
    const int rate = 44100;
    int correct = 0;
    int total = 0;
    for (double bpm : {70.0, 80.0, 85.0, 90.0, 95.0, 100.0, 110.0, 120.0, 125.0, 128.0, 135.0, 140.0, 150.0, 160.0,
                       170.0, 174.0}) {
        for (int bars : {2, 4}) {
            DecodedAudio a;
            a.sampleRate = rate;
            a.mono = test::drumLoop(bpm, bars, rate);
            const AnalysisResult r = analyse(a);
            ++total;
            if (r.bpm && std::abs(*r.bpm - bpm) <= 0.5 && r.isLoop == true) ++correct;
            else UNSCOPED_INFO("missed " << bpm << " BPM, " << bars << " bars: got " << r.bpm.value_or(0.0));
        }
    }
    CHECK(correct == total);
}

TEST_CASE("Key: all 24 major and minor progressions", "[accuracy]")
{
    const int rate = 44100;
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int correct = 0;
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (bool minor : {false, true}) {
            const std::string want = std::string(names[tonic]) + (minor ? "m" : "");
            const auto key = estimateKey(test::chordProgression(tonic, minor, rate), rate);
            if (key && key->key == want) ++correct;
            else UNSCOPED_INFO("missed " << want << ": got " << (key ? key->key : "-"));
        }
    }
    CHECK(correct == 24);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error `error: unknown type name 'DecodedAudio'`

- [ ] **Step 3: Implement**

Replace `core/include/asma/core/Analysis.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Decode.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace asma {

// Bump when analysis output changes; stored rows with a lower version are
// re-analysed lazily.
constexpr int kAnalysisVersion = 1;

// 13 MFCC means, 13 MFCC standard deviations, log centroid, log rolloff,
// flatness, log onset density, log duration.
constexpr std::size_t kFeatureVectorSize = 31;

struct Loudness {
    double peak = 0.0;   // linear sample peak
    double lufs = -70.0; // integrated; ungated over the whole sound under 400 ms; floor -70
};

struct TempoEstimate {
    double bpm = 0.0;
    double confidence = 0.0; // 0..1
};

struct KeyEstimate {
    std::string key; // canonical, as parseKeyToken
    double confidence = 0.0;
};

struct AnalysisResult {
    Loudness loudness;
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    std::optional<std::string> key;
    double keyConfidence = 0.0;
    std::optional<bool> isLoop;
    double centroid = 0.0;     // Hz
    double rolloff = 0.0;      // Hz, 85% of spectral energy
    double flatness = 0.0;     // 0 (tonal) .. 1 (noise)
    double onsetDensity = 0.0; // onsets per second
    std::vector<float> featureVector; // kFeatureVectorSize values
};

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate);
// Free estimate for rhythmic material of unknown length. nullopt for sounds
// under one second or without onsets. Confidence is the normalised
// autocorrelation at the beat; below ~0.4 the tempo is a guess.
std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate);
// nullopt for noisy, atonal or very short sounds.
std::optional<KeyEstimate> estimateKey(const std::vector<float>& mono, int sampleRate);

// Pure and deterministic: the same audio always gives the same result.
AnalysisResult analyse(const DecodedAudio& audio);

} // namespace asma
```

In `core/src/Analysis.cpp`, insert before the final `} // namespace asma` line:

```cpp
// ---- Descriptors, loop or one-shot, and the full analysis

namespace {

constexpr double kPi = 3.14159265358979323846;

double onsetsPerSecond(const Onsets& onsets, double seconds)
{
    const auto& e = onsets.envelope;
    if (e.size() < 3 || seconds <= 0.0) return 0.0;
    const float peak = *std::max_element(e.begin(), e.end());
    if (peak <= 0.0f) return 0.0;
    const auto minGap = static_cast<std::size_t>(std::max(1.0, onsets.frameRate * 0.05));
    std::size_t count = 0;
    std::size_t last = 0;
    bool any = false;
    for (std::size_t t = 1; t + 1 < e.size(); ++t) {
        if (e[t] > 0.1f * peak && e[t] > e[t - 1] && e[t] >= e[t + 1] && (!any || t - last >= minGap)) {
            ++count;
            last = t;
            any = true;
        }
    }
    return static_cast<double>(count) / seconds;
}

struct Spectral {
    double centroid = 0.0;
    double rolloff = 0.0;
    double flatness = 0.0;
    std::array<double, 13> mfccMean{};
    std::array<double, 13> mfccStd{};
};

double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

Spectral spectralSummary(const std::vector<float>& mono, int rate)
{
    const std::size_t size = detail::powerOfTwoAtLeast(rate * 0.046);
    const auto frames = detail::stft(mono, size, size / 4);
    const std::size_t bins = size / 2 + 1;
    const double binHz = static_cast<double>(rate) / static_cast<double>(size);

    // 40 triangular mel bands between 20 Hz and min(Nyquist, 16 kHz).
    constexpr int kBands = 40;
    const double melLo = hzToMel(20.0);
    const double melHi = hzToMel(std::min(rate / 2.0, 16000.0));
    std::vector<std::vector<std::pair<std::size_t, double>>> bands(kBands);
    for (int b = 0; b < kBands; ++b) {
        const double lo = melToHz(melLo + (melHi - melLo) * b / (kBands + 1));
        const double mid = melToHz(melLo + (melHi - melLo) * (b + 1) / (kBands + 1));
        const double hi = melToHz(melLo + (melHi - melLo) * (b + 2) / (kBands + 1));
        for (std::size_t k = 1; k < bins; ++k) {
            const double f = static_cast<double>(k) * binHz;
            if (f <= lo || f >= hi) continue;
            bands[static_cast<std::size_t>(b)].emplace_back(k, f < mid ? (f - lo) / (mid - lo) : (hi - f) / (hi - mid));
        }
    }

    std::vector<double> energy(frames.size(), 0.0);
    for (std::size_t t = 0; t < frames.size(); ++t)
        for (float m : frames[t]) energy[t] += static_cast<double>(m) * m;
    const double maxEnergy = *std::max_element(energy.begin(), energy.end());

    Spectral s;
    if (maxEnergy <= 0.0) return s;
    std::array<double, 13> sum{};
    std::array<double, 13> sumSq{};
    std::size_t active = 0;
    std::vector<double> power(bins);
    for (std::size_t t = 0; t < frames.size(); ++t) {
        if (energy[t] < maxEnergy * 1e-6) continue; // quieter than -60 dB: skip
        ++active;
        double total = 0.0;
        double weighted = 0.0;
        double logSum = 0.0;
        for (std::size_t k = 0; k < bins; ++k) {
            power[k] = static_cast<double>(frames[t][k]) * frames[t][k];
            total += power[k];
            weighted += power[k] * static_cast<double>(k) * binHz;
            logSum += std::log(power[k] + 1e-12);
        }
        s.centroid += weighted / total;
        double cumulative = 0.0;
        std::size_t k85 = 0;
        while (k85 < bins && cumulative < 0.85 * total) cumulative += power[k85++];
        s.rolloff += static_cast<double>(k85) * binHz;
        s.flatness += std::exp(logSum / static_cast<double>(bins)) / (total / static_cast<double>(bins) + 1e-12);

        std::array<double, kBands> logBand{};
        for (int b = 0; b < kBands; ++b) {
            double e = 0.0;
            for (const auto& [k, w] : bands[static_cast<std::size_t>(b)]) e += w * power[k];
            logBand[static_cast<std::size_t>(b)] = std::log10(e + 1e-10);
        }
        for (int n = 0; n < 13; ++n) {
            double c = 0.0;
            for (int b = 0; b < kBands; ++b)
                c += logBand[static_cast<std::size_t>(b)] * std::cos(kPi * n * (b + 0.5) / kBands);
            sum[static_cast<std::size_t>(n)] += c;
            sumSq[static_cast<std::size_t>(n)] += c * c;
        }
    }
    const auto count = static_cast<double>(active);
    s.centroid /= count;
    s.rolloff /= count;
    s.flatness /= count;
    for (std::size_t n = 0; n < 13; ++n) {
        s.mfccMean[n] = sum[n] / count;
        s.mfccStd[n] = std::sqrt(std::max(0.0, sumSq[n] / count - s.mfccMean[n] * s.mfccMean[n]));
    }
    return s;
}

// Share of the sound's energy in its first quarter. One-shots are front-loaded;
// loops spread their energy across their length.
double frontEnergyShare(const std::vector<float>& mono)
{
    double total = 0.0;
    double front = 0.0;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const double e = static_cast<double>(mono[i]) * mono[i];
        total += e;
        if (i < mono.size() / 4) front += e;
    }
    return total > 0.0 ? front / total : 0.0;
}

// True when the last tenth of the sound is 40 dB below its loudest 50 ms.
bool decaysToSilence(const std::vector<float>& mono, int rate)
{
    const auto window = static_cast<std::size_t>(std::max(1, rate / 20));
    double loudest = 0.0;
    for (std::size_t start = 0; start < mono.size(); start += window) {
        double sum = 0.0;
        const std::size_t end = std::min(mono.size(), start + window);
        for (std::size_t i = start; i < end; ++i) sum += static_cast<double>(mono[i]) * mono[i];
        loudest = std::max(loudest, sum / static_cast<double>(end - start));
    }
    const std::size_t tailStart = mono.size() - mono.size() / 10;
    double tail = 0.0;
    for (std::size_t i = tailStart; i < mono.size(); ++i) tail += static_cast<double>(mono[i]) * mono[i];
    tail /= static_cast<double>(std::max<std::size_t>(1, mono.size() - tailStart));
    return loudest > 0.0 && tail < loudest * 1e-4;
}

// Autocorrelation at a fractional lag, by linear interpolation.
double autocorrelationAt(const std::vector<float>& e, double lag)
{
    const auto lo = static_cast<std::size_t>(std::floor(lag));
    const double frac = lag - static_cast<double>(lo);
    return (1.0 - frac) * autocorrelation(e, lo) + frac * autocorrelation(e, lo + 1);
}

// Loops are cut to whole bars, so their length allows only a few tempos:
// 240 * bars / seconds. Score each by its autocorrelation (weighted towards
// 120 BPM), metre-check the winner, and keep the result on a whole-bar tempo.
std::optional<TempoEstimate> barFitTempo(const OnsetPair& pair, double seconds)
{
    const BeatEnvelope b = beatEnvelope(pair);
    if (b.energy <= 0.0 || seconds <= 0.0) return std::nullopt;

    int bestBars = 0;
    double bestScore = 0.0;
    double bestCorrelation = 0.0;
    for (int bars = 1; bars <= 512; ++bars) {
        const double bpm = 240.0 * bars / seconds;
        if (bpm < kMinBpm) continue;
        if (bpm > kMaxBpm) break;
        const double lag = 60.0 * b.frameRate / bpm;
        if (lag + 1.0 >= static_cast<double>(b.e.size())) continue;
        const double correlation = autocorrelationAt(b.centered, lag) / b.energy;
        const double score = correlation * tempoPrior(bpm);
        if (score > bestScore) {
            bestScore = score;
            bestBars = bars;
            bestCorrelation = correlation;
        }
    }
    if (bestBars == 0) return std::nullopt;

    int bars = bestBars;
    const double factor = metreFactor(b, 60.0 * b.frameRate / (240.0 * bars / seconds));
    if (factor == 0.5 && bars % 2 == 0 && 240.0 * (bars / 2) / seconds >= kMinBpm) bars /= 2;
    if (factor == 2.0 && 240.0 * (bars * 2) / seconds <= kMaxBpm) bars *= 2;

    TempoEstimate estimate;
    estimate.bpm = 240.0 * bars / seconds;
    estimate.confidence = std::clamp(bestCorrelation, 0.0, 1.0);
    return estimate;
}

} // namespace

AnalysisResult analyse(const DecodedAudio& audio)
{
    AnalysisResult r;
    const int rate = audio.sampleRate;
    const double seconds = audio.seconds();
    r.loudness = measureLoudness(audio.mono, rate);

    const Spectral spectral = spectralSummary(audio.mono, rate);
    r.centroid = spectral.centroid;
    r.rolloff = spectral.rolloff;
    r.flatness = spectral.flatness;
    const OnsetPair onsets = onsetEnvelopes(audio.mono, rate);
    r.onsetDensity = onsetsPerSecond(onsets.full, seconds);

    if (audio.truncated) {
        // Only the first maxSeconds were decoded, so the length says nothing.
        const auto tempo = tempoFromOnsets(onsets);
        if (tempo && tempo->confidence >= 0.4) {
            r.bpm = tempo->bpm;
            r.bpmConfidence = tempo->confidence;
        }
    } else if (seconds < 1.0) {
        r.isLoop = false;
    } else {
        // Thresholds tuned on labelled loops and one-shots from a real sample
        // library: 75% of loops found, 13% of one-shots over a second mistaken.
        const auto tempo = barFitTempo(onsets, seconds);
        if (tempo && tempo->confidence >= 0.15 && frontEnergyShare(audio.mono) < 0.6) {
            r.bpm = tempo->bpm;
            r.bpmConfidence = tempo->confidence;
            r.isLoop = true;
        } else if (decaysToSilence(audio.mono, rate)) {
            r.isLoop = false;
        }
    }

    if (const auto key = estimateKey(audio.mono, rate)) {
        r.key = key->key;
        r.keyConfidence = key->confidence;
    }

    r.featureVector.reserve(kFeatureVectorSize);
    for (double v : spectral.mfccMean) r.featureVector.push_back(static_cast<float>(v));
    for (double v : spectral.mfccStd) r.featureVector.push_back(static_cast<float>(v));
    r.featureVector.push_back(static_cast<float>(std::log(r.centroid + 1.0)));
    r.featureVector.push_back(static_cast<float>(std::log(r.rolloff + 1.0)));
    r.featureVector.push_back(static_cast<float>(r.flatness));
    r.featureVector.push_back(static_cast<float>(std::log1p(r.onsetDensity)));
    r.featureVector.push_back(static_cast<float>(std::log(seconds + 0.01)));
    return r;
}
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[analysis],[accuracy]"`

Expected: no compiler warnings;
`All tests passed (95 assertions in 14 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 126`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: analyse loops and one-shots; gate accuracy on generated audio"
```

---

### Task 6: Schema v2 and analysis-aware features

Analysis must never overwrite what a file or its name says. The database learns
where every BPM, key and loop flag came from.

**Files:**

- Modify: `core/include/asma/core/Db.h`, `core/include/asma/core/Library.h`,
  `core/include/asma/core/Schema.h`, `core/src/Db.cpp`, `core/src/Library.cpp`,
  `core/src/Scanner.cpp`, `core/src/Schema.cpp`, `tests/test_db.cpp`,
  `tests/test_library.cpp`

**Interfaces:**

- Consumes: `AnalysisResult`, `kAnalysisVersion` (Task 5); `Library`, `Db`,
  `migrate` (plan 1).
- Produces: `Statement::bindBlob`, `Statement::getBlob`;
  `Db::openInMemory(int schemaVersion = -1)`;
  `migrate(Db&, int targetVersion = -1)`; schema version 2;
  `enum class asma::FeatureSource { Embedded, Filename, Analysis }`;
  `DerivedInfo::bpmSource/keySource/loopSource`;
  `Library::setAnalysis(fileId, const AnalysisResult&)`,
  `Library::setAnalysisError(fileId, reason)`,
  `Library::fileByAbsolutePath(path)`; `resetAnalysis` now clears analysis
  output.

Behaviour the tests pin:

- Migration 2 labels version-1 values: BPM with confidence 1.0 came from ACID
  (`embedded`), everything else from the file name.
- `setAnalysis` only writes BPM, key and loop where nothing embedded or
  file-name derived is stored.
- `setDerived` (rescans, re-links) keeps analysed values it has nothing to
  replace with, so moving a file does not lose its analysis.
- `resetAnalysis` (content changed) drops analysed values and descriptors and
  queues the file again.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_db.cpp`:

```cpp

TEST_CASE("Blobs round-trip", "[db]")
{
    Db db = Db::openInMemory();
    const std::vector<unsigned char> bytes = {0, 1, 2, 250, 255};
    auto q = db.prepare("SELECT ?");
    q.bindBlob(1, bytes.data(), bytes.size());
    REQUIRE(q.step());
    CHECK(q.getBlob(0) == bytes);
}

TEST_CASE("Migration 2 labels where version 1 values came from", "[db]")
{
    Db db = Db::openInMemory(1);
    CHECK(db.schemaVersion() == 1);
    db.exec("INSERT INTO roots(id, path) VALUES (1, '/r')");
    db.exec("INSERT INTO files(id, root_id, rel_path, name, size, mtime, format, status) VALUES "
            "(1, 1, 'a.wav', 'a.wav', 1, 1, 'wav', 'ok'), (2, 1, 'b.wav', 'b.wav', 1, 1, 'wav', 'ok')");
    db.exec("INSERT INTO features(file_id, bpm, bpm_confidence, key, key_confidence, is_loop) VALUES "
            "(1, 128, 1.0, NULL, NULL, 1), (2, 90, 0.9, 'Am', 0.9, NULL)");
    asma::migrate(db);
    CHECK(db.schemaVersion() == asma::currentSchemaVersion());
    auto q = db.prepare("SELECT bpm_source, key_source, loop_source FROM features ORDER BY file_id");
    REQUIRE(q.step());
    CHECK(q.getText(0) == "embedded");
    CHECK(q.isNull(1));
    CHECK(q.getText(2) == "filename");
    REQUIRE(q.step());
    CHECK(q.getText(0) == "filename");
    CHECK(q.getText(1) == "filename");
    CHECK(q.isNull(2));
}
```

Append to `tests/test_library.cpp`:

```cpp

namespace {

AnalysisResult analysed(std::optional<double> bpm, std::optional<std::string> key, std::optional<bool> loop)
{
    AnalysisResult r;
    r.loudness = {0.5, -12.0};
    r.bpm = bpm;
    r.bpmConfidence = 0.6;
    r.key = std::move(key);
    r.keyConfidence = 0.7;
    r.isLoop = loop;
    r.featureVector.assign(kFeatureVectorSize, 1.0f);
    return r;
}

int analysisVersion(Db& db, std::int64_t id)
{
    auto q = db.prepare("SELECT analysis_version FROM files WHERE id = ?");
    q.bind(1, id);
    q.step();
    return static_cast<int>(q.getInt(0));
}

} // namespace

TEST_CASE("setAnalysis fills gaps but never overrides file-name values", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "Loops/Bass_Loop_128.wav"));
    DerivedInfo named;
    named.bpm = 128.0;
    named.bpmConfidence = 0.9;
    named.isLoop = true;
    lib.setDerived(id, named);

    lib.setAnalysis(id, analysed(64.0, "Am", false));
    const auto d = lib.derived(id).value();
    CHECK(d.bpm == 128.0);
    CHECK(d.bpmSource == FeatureSource::Filename);
    CHECK(d.isLoop == true);
    CHECK(d.loopSource == FeatureSource::Filename);
    CHECK(d.key == "Am");
    CHECK(d.keySource == FeatureSource::Analysis);
    CHECK(d.keyConfidence == 0.7);
    CHECK(analysisVersion(db, id) == kAnalysisVersion);
}

TEST_CASE("setDerived keeps analysed values it has nothing to replace with", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    lib.setDerived(id, {});
    lib.setAnalysis(id, analysed(120.0, "C", true));

    lib.setDerived(id, {}); // e.g. the file was moved and re-linked
    auto d = lib.derived(id).value();
    CHECK(d.bpm == 120.0);
    CHECK(d.bpmSource == FeatureSource::Analysis);
    CHECK(d.key == "C");

    DerivedInfo named;
    named.key = "Dm";
    named.keyConfidence = 0.9;
    lib.setDerived(id, named); // renamed to carry a key: the name wins
    d = lib.derived(id).value();
    CHECK(d.key == "Dm");
    CHECK(d.keySource == FeatureSource::Filename);
    CHECK(d.bpm == 120.0);
}

TEST_CASE("resetAnalysis forgets analysis output only", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    DerivedInfo named;
    named.key = "Dm";
    named.keyConfidence = 0.9;
    lib.setDerived(id, named);
    lib.setAnalysis(id, analysed(100.0, "G", true));

    lib.resetAnalysis(id);
    const auto d = lib.derived(id).value();
    CHECK_FALSE(d.bpm.has_value());
    CHECK_FALSE(d.isLoop.has_value());
    CHECK(d.key == "Dm");
    CHECK(analysisVersion(db, id) == 0);
    auto q = db.prepare("SELECT lufs, feature_vector FROM features WHERE file_id = ?");
    q.bind(1, id);
    REQUIRE(q.step());
    CHECK(q.isNull(0));
    CHECK(q.isNull(1));
}

TEST_CASE("setAnalysisError marks the file done with a reason", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto id = lib.insertFile(sampleRecord(lib.addRoot(dir.path()), "a.wav"));
    lib.setAnalysisError(id, "cannot decode");
    CHECK(analysisVersion(db, id) == kAnalysisVersion);
    auto q = db.prepare("SELECT analysis_error FROM files WHERE id = ?");
    q.bind(1, id);
    REQUIRE(q.step());
    CHECK(q.getText(0) == "cannot decode");
}

TEST_CASE("fileByAbsolutePath finds a file through its root", "[library]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto id = lib.insertFile(sampleRecord(root, "Drums/Kick 01.wav"));
    CHECK(lib.fileByAbsolutePath(dir.path() / "Drums" / "Kick 01.wav")->id == id);
    CHECK_FALSE(lib.fileByAbsolutePath(dir.path() / "Drums" / "Nope.wav").has_value());
    CHECK_FALSE(lib.fileByAbsolutePath(dir.path().parent_path() / "elsewhere.wav").has_value());
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: no member named 'bindBlob' in 'asma::Statement'`

- [ ] **Step 3: Implement**

In `core/include/asma/core/Db.h`, replace:

```cpp
#include <string_view>
```

with:

```cpp
#include <string_view>
#include <vector>
```

In `core/include/asma/core/Db.h`, replace:

```cpp
    Statement& bindNull(int index);
```

with:

```cpp
    Statement& bindNull(int index);
    Statement& bindBlob(int index, const void* data, std::size_t size);
```

In `core/include/asma/core/Db.h`, replace:

```cpp
    std::string getText(int column) const;
```

with:

```cpp
    std::string getText(int column) const;
    std::vector<unsigned char> getBlob(int column) const;
```

In `core/include/asma/core/Db.h`, replace:

```cpp
    // Private in-memory database with the current schema, for tests.
    static Db openInMemory();
```

with:

```cpp
    // Private in-memory database migrated to schemaVersion (default: the
    // current one), for tests.
    static Db openInMemory(int schemaVersion = -1);
```

In `core/src/Db.cpp`, replace:

```cpp
Statement& Statement::bindNull(int index)
```

with:

```cpp
Statement& Statement::bindBlob(int index, const void* data, std::size_t size)
{
    check(sqlite3_bind_blob64(stmt_, index, data, static_cast<sqlite3_uint64>(size), SQLITE_TRANSIENT), "bind failed");
    return *this;
}

Statement& Statement::bindNull(int index)
```

In `core/src/Db.cpp`, replace:

```cpp
Db Db::openHandle(
```

with:

```cpp
std::vector<unsigned char> Statement::getBlob(int column) const
{
    const auto* data = static_cast<const unsigned char*>(sqlite3_column_blob(stmt_, column));
    const auto size = static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column));
    return data ? std::vector<unsigned char>(data, data + size) : std::vector<unsigned char>();
}

Db Db::openHandle(
```

In `core/src/Db.cpp`, replace:

```cpp
Db Db::openInMemory()
{
    Db db = openHandle(":memory:");
    migrate(db);
    return db;
}
```

with:

```cpp
Db Db::openInMemory(int schemaVersion)
{
    Db db = openHandle(":memory:");
    migrate(db, schemaVersion < 0 ? currentSchemaVersion() : schemaVersion);
    return db;
}
```

Create `core/include/asma/core/Schema.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace asma {

class Db;

int currentSchemaVersion();

// Applies pending migrations up to targetVersion (default: current), each in
// its own transaction. Throws DbError when the database was written by a newer
// asma.
void migrate(Db& db, int targetVersion = -1);

} // namespace asma
```

Create `core/src/Schema.cpp`:

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
constexpr std::array<std::string_view, 2> kMigrations = {
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
    R"SQL(
ALTER TABLE features ADD COLUMN bpm_source TEXT CHECK (bpm_source IN ('embedded', 'filename', 'analysis'));
ALTER TABLE features ADD COLUMN key_source TEXT CHECK (key_source IN ('embedded', 'filename', 'analysis'));
ALTER TABLE features ADD COLUMN loop_source TEXT CHECK (loop_source IN ('embedded', 'filename', 'analysis'));
ALTER TABLE features ADD COLUMN peak REAL;
ALTER TABLE features ADD COLUMN lufs REAL;
ALTER TABLE features ADD COLUMN centroid REAL;
ALTER TABLE features ADD COLUMN rolloff REAL;
ALTER TABLE features ADD COLUMN flatness REAL;
ALTER TABLE features ADD COLUMN onset_density REAL;
ALTER TABLE features ADD COLUMN feature_vector BLOB;
ALTER TABLE files ADD COLUMN analysis_error TEXT;

-- Version 1 only stored embedded (confidence 1.0) or file-name values.
UPDATE features SET bpm_source = CASE WHEN bpm_confidence >= 1.0 THEN 'embedded' ELSE 'filename' END
    WHERE bpm IS NOT NULL;
UPDATE features SET key_source = 'filename' WHERE key IS NOT NULL;
UPDATE features SET loop_source = 'filename' WHERE is_loop IS NOT NULL;

CREATE INDEX files_analysis_pending ON files(analysis_version) WHERE status = 'ok';
)SQL",
};

} // namespace

int currentSchemaVersion() { return static_cast<int>(kMigrations.size()); }

void migrate(Db& db, int targetVersion)
{
    if (targetVersion < 0 || targetVersion > currentSchemaVersion()) targetVersion = currentSchemaVersion();
    for (;;) {
        // Read the version inside the write transaction so two processes that
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
        }
        db.exec(kMigrations[static_cast<std::size_t>(version)]);
        db.exec("PRAGMA user_version = " + std::to_string(version + 1));
        tx.commit();
    }
}

} // namespace asma
```

Create `core/include/asma/core/Library.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Analysis.h"
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

// Where a BPM, key or loop flag came from. Embedded and file-name values
// always win over analysis.
enum class FeatureSource { Embedded, Filename, Analysis };

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

// Metadata derived from headers and file names; derived() also reports what
// analysis filled in.
struct DerivedInfo {
    std::optional<double> bpm;
    double bpmConfidence = 0.0;
    FeatureSource bpmSource = FeatureSource::Filename;
    std::optional<std::string> key;
    double keyConfidence = 0.0;
    FeatureSource keySource = FeatureSource::Filename;
    std::optional<bool> isLoop;
    FeatureSource loopSource = FeatureSource::Filename;
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
    // The file at an absolute path, looked up through the root that contains it.
    std::optional<FileRecord> fileByAbsolutePath(const std::filesystem::path& path);
    // Missing rows in any root whose content matches.
    std::vector<FileRecord> relinkCandidates(std::string_view contentHash, std::int64_t size);

    std::int64_t insertFile(const FileRecord& file);
    void updateFile(const FileRecord& file); // by file.id, every column
    void setStatus(std::int64_t fileId, FileStatus status, std::string_view reason = {});
    // Content changed: forget analysis output and queue the file again.
    void resetAnalysis(std::int64_t fileId);

    // Replaces header/file-name features and the auto/embedded tags. User tags
    // stay, and so do analysed values the new info has nothing to replace with.
    void setDerived(std::int64_t fileId, const DerivedInfo& info);
    // Stores analysis output and marks the file analysed at kAnalysisVersion.
    // BPM, key and loop are only written where no embedded or file-name value
    // exists.
    void setAnalysis(std::int64_t fileId, const AnalysisResult& result);
    // Marks the file analysed at kAnalysisVersion without results, so it is
    // not retried until its content changes.
    void setAnalysisError(std::int64_t fileId, std::string_view reason);
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

Create `core/src/Library.cpp`:

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

std::string_view featureSourceText(FeatureSource s)
{
    switch (s) {
    case FeatureSource::Embedded: return "embedded";
    case FeatureSource::Filename: return "filename";
    case FeatureSource::Analysis: return "analysis";
    }
    return "filename";
}

FeatureSource featureSourceFromText(std::string_view s)
{
    if (s == "embedded") return FeatureSource::Embedded;
    if (s == "analysis") return FeatureSource::Analysis;
    return FeatureSource::Filename;
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

std::optional<FileRecord> Library::fileByAbsolutePath(const fs::path& path)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::absolute(path), ec);
    if (ec) canonical = fs::absolute(path);
    const std::string full = toUtf8(canonical);
    for (const auto& r : roots()) {
        const std::string prefix = r.path.back() == '/' ? r.path : r.path + "/";
        if (full.size() > prefix.size() && full.compare(0, prefix.size(), prefix) == 0)
            if (auto file = fileByPath(r.id, std::string_view(full).substr(prefix.size()))) return file;
    }
    return std::nullopt;
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
    auto file = db_.prepare("UPDATE files SET analysis_version = 0, analysis_error = NULL WHERE id = ?");
    file.bind(1, fileId);
    file.run();
    auto features = db_.prepare(
        "UPDATE features SET "
        "bpm = CASE WHEN bpm_source = 'analysis' THEN NULL ELSE bpm END, "
        "bpm_confidence = CASE WHEN bpm_source = 'analysis' THEN NULL ELSE bpm_confidence END, "
        "bpm_source = CASE WHEN bpm_source = 'analysis' THEN NULL ELSE bpm_source END, "
        "key = CASE WHEN key_source = 'analysis' THEN NULL ELSE key END, "
        "key_confidence = CASE WHEN key_source = 'analysis' THEN NULL ELSE key_confidence END, "
        "key_source = CASE WHEN key_source = 'analysis' THEN NULL ELSE key_source END, "
        "is_loop = CASE WHEN loop_source = 'analysis' THEN NULL ELSE is_loop END, "
        "loop_source = CASE WHEN loop_source = 'analysis' THEN NULL ELSE loop_source END, "
        "peak = NULL, lufs = NULL, centroid = NULL, rolloff = NULL, flatness = NULL, onset_density = NULL, "
        "feature_vector = NULL WHERE file_id = ?");
    features.bind(1, fileId);
    features.run();
}

void Library::setDerived(std::int64_t fileId, const DerivedInfo& info)
{
    // Each value is replaced unless the new info has none and the stored one
    // came from analysis: a moved file keeps what analysis found.
    auto features = db_.prepare(
        "INSERT INTO features(file_id, bpm, bpm_confidence, bpm_source, key, key_confidence, key_source, "
        "is_loop, loop_source, root_note) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(file_id) DO UPDATE SET "
        "bpm = CASE WHEN excluded.bpm IS NOT NULL OR features.bpm_source IS NOT 'analysis' "
        "  THEN excluded.bpm ELSE features.bpm END, "
        "bpm_confidence = CASE WHEN excluded.bpm IS NOT NULL OR features.bpm_source IS NOT 'analysis' "
        "  THEN excluded.bpm_confidence ELSE features.bpm_confidence END, "
        "bpm_source = CASE WHEN excluded.bpm IS NOT NULL OR features.bpm_source IS NOT 'analysis' "
        "  THEN excluded.bpm_source ELSE features.bpm_source END, "
        "key = CASE WHEN excluded.key IS NOT NULL OR features.key_source IS NOT 'analysis' "
        "  THEN excluded.key ELSE features.key END, "
        "key_confidence = CASE WHEN excluded.key IS NOT NULL OR features.key_source IS NOT 'analysis' "
        "  THEN excluded.key_confidence ELSE features.key_confidence END, "
        "key_source = CASE WHEN excluded.key IS NOT NULL OR features.key_source IS NOT 'analysis' "
        "  THEN excluded.key_source ELSE features.key_source END, "
        "is_loop = CASE WHEN excluded.is_loop IS NOT NULL OR features.loop_source IS NOT 'analysis' "
        "  THEN excluded.is_loop ELSE features.is_loop END, "
        "loop_source = CASE WHEN excluded.is_loop IS NOT NULL OR features.loop_source IS NOT 'analysis' "
        "  THEN excluded.loop_source ELSE features.loop_source END, "
        "root_note = excluded.root_note");
    features.bind(1, fileId);
    features.bindOptional(2, info.bpm);
    if (info.bpm) features.bind(3, info.bpmConfidence).bind(4, featureSourceText(info.bpmSource));
    else features.bindNull(3).bindNull(4);
    if (info.key) {
        features.bind(5, std::string_view(*info.key)).bind(6, info.keyConfidence).bind(7, featureSourceText(info.keySource));
    } else {
        features.bindNull(5).bindNull(6).bindNull(7);
    }
    features.bindOptional(8, info.isLoop);
    if (info.isLoop) features.bind(9, featureSourceText(info.loopSource));
    else features.bindNull(9);
    features.bindOptional(10, info.rootNote);
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
    auto q = db_.prepare("SELECT bpm, bpm_confidence, bpm_source, key, key_confidence, key_source, is_loop, "
                         "loop_source, root_note FROM features WHERE file_id = ?");
    q.bind(1, fileId);
    if (!q.step()) return std::nullopt;
    DerivedInfo d;
    if (!q.isNull(0)) {
        d.bpm = q.getDouble(0);
        d.bpmConfidence = q.getDouble(1);
        d.bpmSource = featureSourceFromText(q.getText(2));
    }
    if (!q.isNull(3)) {
        d.key = q.getText(3);
        d.keyConfidence = q.getDouble(4);
        d.keySource = featureSourceFromText(q.getText(5));
    }
    if (!q.isNull(6)) {
        d.isLoop = q.getInt(6) != 0;
        d.loopSource = featureSourceFromText(q.getText(7));
    }
    if (!q.isNull(8)) d.rootNote = static_cast<int>(q.getInt(8));
    return d;
}

void Library::setAnalysis(std::int64_t fileId, const AnalysisResult& r)
{
    auto ensure = db_.prepare("INSERT INTO features(file_id) VALUES (?) ON CONFLICT(file_id) DO NOTHING");
    ensure.bind(1, fileId);
    ensure.run();

    // ?8..?12 are the analysed BPM, key and loop flag; they only land where
    // nothing better (embedded, file name) is stored.
    auto q = db_.prepare(
        "UPDATE features SET peak = ?1, lufs = ?2, centroid = ?3, rolloff = ?4, flatness = ?5, "
        "onset_density = ?6, feature_vector = ?7, "
        "bpm = CASE WHEN bpm_source IS NULL OR bpm_source = 'analysis' THEN ?8 ELSE bpm END, "
        "bpm_confidence = CASE WHEN bpm_source IS NULL OR bpm_source = 'analysis' THEN ?9 ELSE bpm_confidence END, "
        "bpm_source = CASE WHEN bpm_source IS NULL OR bpm_source = 'analysis' "
        "  THEN CASE WHEN ?8 IS NULL THEN NULL ELSE 'analysis' END ELSE bpm_source END, "
        "key = CASE WHEN key_source IS NULL OR key_source = 'analysis' THEN ?10 ELSE key END, "
        "key_confidence = CASE WHEN key_source IS NULL OR key_source = 'analysis' THEN ?11 ELSE key_confidence END, "
        "key_source = CASE WHEN key_source IS NULL OR key_source = 'analysis' "
        "  THEN CASE WHEN ?10 IS NULL THEN NULL ELSE 'analysis' END ELSE key_source END, "
        "is_loop = CASE WHEN loop_source IS NULL OR loop_source = 'analysis' THEN ?12 ELSE is_loop END, "
        "loop_source = CASE WHEN loop_source IS NULL OR loop_source = 'analysis' "
        "  THEN CASE WHEN ?12 IS NULL THEN NULL ELSE 'analysis' END ELSE loop_source END "
        "WHERE file_id = ?13");
    q.bind(1, r.loudness.peak).bind(2, r.loudness.lufs).bind(3, r.centroid).bind(4, r.rolloff);
    q.bind(5, r.flatness).bind(6, r.onsetDensity);
    q.bindBlob(7, r.featureVector.data(), r.featureVector.size() * sizeof(float));
    q.bindOptional(8, r.bpm);
    if (r.bpm) q.bind(9, r.bpmConfidence);
    else q.bindNull(9);
    if (r.key) q.bind(10, std::string_view(*r.key)).bind(11, r.keyConfidence);
    else q.bindNull(10).bindNull(11);
    q.bindOptional(12, r.isLoop);
    q.bind(13, fileId);
    q.run();

    auto file = db_.prepare("UPDATE files SET analysis_version = ?, analysis_error = NULL WHERE id = ?");
    file.bind(1, kAnalysisVersion).bind(2, fileId);
    file.run();
}

void Library::setAnalysisError(std::int64_t fileId, std::string_view reason)
{
    auto q = db_.prepare("UPDATE files SET analysis_version = ?, analysis_error = ? WHERE id = ?");
    q.bind(1, kAnalysisVersion).bind(2, reason).bind(3, fileId);
    q.run();
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

In `core/src/Scanner.cpp`, replace:

```cpp
        d.bpm = probe.acid->tempo;
        d.bpmConfidence = 1.0;
```

with:

```cpp
        d.bpm = probe.acid->tempo;
        d.bpmConfidence = 1.0;
        d.bpmSource = FeatureSource::Embedded;
```

In `core/src/Scanner.cpp`, replace:

```cpp
    d.isLoop = probe.acid ? std::optional<bool>(!probe.acid->oneShot) : r.name.isLoop;
```

with:

```cpp
    d.isLoop = probe.acid ? std::optional<bool>(!probe.acid->oneShot) : r.name.isLoop;
    d.loopSource = probe.acid ? FeatureSource::Embedded : FeatureSource::Filename;
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[db],[library],[scanner]"`

Expected: no compiler warnings;
`All tests passed (163 assertions in 42 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 133`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: schema v2 with feature sources and analysis columns"
```

---

### Task 7: Analysing pending files

Lazy background analysis: every ok file below `kAnalysisVersion` is analysed
once.

**Files:**

- Create: `tests/test_analyser.cpp`, `core/src/Parallel.h`,
  `core/include/asma/core/Analyser.h`, `core/src/Analyser.cpp`
- Modify: `core/src/Scanner.cpp`

**Interfaces:**

- Consumes: `decodeFile`, `analyse`, `Library::setAnalysis/setAnalysisError`.
- Produces: `struct asma::AnalyseStats { analysed, failed, skipped }`;
  `struct asma::AnalyseOptions { threads, batchSize, rootId, maxSeconds, onFileStart, onProgress }`;
  `asma::AnalyseStats asma::analysePending(Db&, const AnalyseOptions& = {})`;
  `void asma::markAnalysisFailed(Db&, rootId, relPath, reason)`; internal
  `detail::parallelFor` and `detail::threadCount` in `core/src/Parallel.h`, now
  also used by the scanner.

Behaviour the tests pin:

- Decode failures are recorded (`analysis_error`) and not retried until the
  content changes.
- Files that cannot be read right now are skipped and retried next run.
- A moved file keeps its analysis; changed content is analysed again.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_analyser.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Analyser.h"
#include "asma/core/Analysis.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 44100;

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
    void write(const std::string& rel, const std::vector<float>& samples)
    {
        test::writeWavSamples(root / fromUtf8(rel), kRate, samples);
    }
    FileRecord file(const std::string& rel) { return lib.fileByPath(rootId, rel).value(); }
    int version(const std::string& rel)
    {
        auto q = db.prepare("SELECT analysis_version FROM files WHERE id = ?");
        q.bind(1, file(rel).id);
        q.step();
        return static_cast<int>(q.getInt(0));
    }
};

} // namespace

TEST_CASE("pending files are analysed once", "[analyser]")
{
    Fixture f;
    f.write("groove.wav", test::drumLoop(100.0, 2, kRate));
    f.write("kick.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);

    const AnalyseStats first = analysePending(f.db);
    CHECK(first.analysed == 2);
    CHECK(f.version("groove.wav") == kAnalysisVersion);
    const auto groove = f.lib.derived(f.file("groove.wav").id).value();
    REQUIRE(groove.bpm.has_value());
    CHECK(*groove.bpm == Catch::Approx(100.0).margin(0.01));
    CHECK(groove.bpmSource == FeatureSource::Analysis);
    CHECK(f.lib.derived(f.file("kick.wav").id)->isLoop == false);

    CHECK(analysePending(f.db).analysed == 0);
}

TEST_CASE("file-name values survive analysis", "[analyser]")
{
    Fixture f;
    f.write("Loops/Groove_Loop_90bpm.wav", test::drumLoop(120.0, 2, kRate)); // the name disagrees
    scanRoot(f.db, f.rootId);
    analysePending(f.db);
    const auto d = f.lib.derived(f.file("Loops/Groove_Loop_90bpm.wav").id).value();
    CHECK(d.bpm == 90.0);
    CHECK(d.bpmSource == FeatureSource::Filename);
}

TEST_CASE("undecodable files are recorded and not retried", "[analyser]")
{
    Fixture f;
    test::WavSpec spec;
    spec.formatTag = 0x55;
    test::writeWav(f.root / "odd.wav", spec);
    scanRoot(f.db, f.rootId);
    REQUIRE(f.file("odd.wav").status == FileStatus::Ok); // it probes fine

    const AnalyseStats s = analysePending(f.db);
    CHECK(s.failed == 1);
    CHECK(analysePending(f.db).failed == 0);
    auto q = f.db.prepare("SELECT analysis_error FROM files WHERE rel_path = 'odd.wav'");
    REQUIRE(q.step());
    CHECK_FALSE(q.getText(0).empty());
}

TEST_CASE("changed content is analysed again; moved files are not", "[analyser]")
{
    Fixture f;
    f.write("a.wav", test::drumLoop(100.0, 2, kRate));
    scanRoot(f.db, f.rootId);
    analysePending(f.db);

    fs::create_directories(f.root / "Moved");
    fs::rename(f.root / "a.wav", f.root / "Moved" / "a.wav");
    scanRoot(f.db, f.rootId);
    CHECK(analysePending(f.db).analysed == 0);
    CHECK(f.lib.derived(f.file("Moved/a.wav").id)->bpm.has_value());

    f.write("Moved/a.wav", test::drumLoop(140.0, 2, kRate));
    fs::last_write_time(f.root / "Moved" / "a.wav",
                        fs::last_write_time(f.root / "Moved" / "a.wav") + std::chrono::seconds(2));
    scanRoot(f.db, f.rootId);
    CHECK(analysePending(f.db).analysed == 1);
    CHECK(*f.lib.derived(f.file("Moved/a.wav").id)->bpm == Catch::Approx(140.0).margin(0.01));
}

#ifndef _WIN32
TEST_CASE("unreadable files are skipped and retried", "[analyser]")
{
    Fixture f;
    f.write("a.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    fs::permissions(f.root / "a.wav", fs::perms::none);
    const AnalyseStats s = analysePending(f.db);
    fs::permissions(f.root / "a.wav", fs::perms::owner_read | fs::perms::owner_write);
    CHECK(s.skipped == 1);
    CHECK(f.version("a.wav") == 0);
    CHECK(analysePending(f.db).analysed == 1);
}
#endif

TEST_CASE("markAnalysisFailed stops a file being analysed", "[analyser]")
{
    Fixture f;
    f.write("crashy.wav", test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    markAnalysisFailed(f.db, f.rootId, "crashy.wav", "crashed the analyser");
    markAnalysisFailed(f.db, f.rootId, "unknown.wav", "ignored");
    CHECK(analysePending(f.db).analysed == 0);
    CHECK(f.version("crashy.wav") == kAnalysisVersion);
}

TEST_CASE("rootId limits the run and callbacks cover every file", "[analyser]")
{
    Fixture f;
    const fs::path other = f.dir.path() / "other";
    fs::create_directories(other);
    const auto otherId = f.lib.addRoot(other);
    f.write("a.wav", test::kickHit(kRate));
    test::writeWavSamples(other / "b.wav", kRate, test::kickHit(kRate));
    scanRoot(f.db, f.rootId);
    scanRoot(f.db, otherId);

    std::size_t starts = 0;
    std::size_t lastTotal = 0;
    AnalyseOptions options;
    options.rootId = otherId;
    options.batchSize = 1;
    options.onFileStart = [&](std::string_view) { ++starts; };
    options.onProgress = [&](std::size_t, std::size_t total, std::string_view) { lastTotal = total; };
    CHECK(analysePending(f.db, options).analysed == 1);
    CHECK(starts == 1);
    CHECK(lastTotal == 1);
    CHECK(f.version("a.wav") == 0);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: 'asma/core/Analyser.h' file not found`

- [ ] **Step 3: Implement**

Create `core/src/Parallel.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace asma::detail {

// 0 means one thread per hardware thread.
inline unsigned threadCount(unsigned requested)
{
    return requested ? requested : std::max(1u, std::thread::hardware_concurrency());
}

// Runs work(i) for every i in [begin, end) on up to `threads` threads, the
// calling thread included, and returns when all are done. work must not throw.
template <typename Work>
void parallelFor(std::size_t begin, std::size_t end, unsigned threads, Work work)
{
    if (begin >= end) return;
    std::atomic<std::size_t> next{begin};
    auto worker = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= end) return;
            work(i);
        }
    };
    const auto poolSize = static_cast<unsigned>(std::min<std::size_t>(std::max(1u, threads), end - begin));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < poolSize; ++t) pool.emplace_back(worker);
    worker();
    for (auto& thread : pool) thread.join();
}

} // namespace asma::detail
```

Create `core/include/asma/core/Analyser.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace asma {

struct AnalyseStats {
    std::size_t analysed = 0;
    std::size_t failed = 0;  // could not be decoded; not retried until the file changes
    std::size_t skipped = 0; // could not be read this time; retried next run
};

struct AnalyseOptions {
    unsigned threads = 0;              // 0 = std::thread::hardware_concurrency()
    std::size_t batchSize = 50;        // files per write transaction
    std::optional<std::int64_t> rootId; // only this root; all enabled roots otherwise
    double maxSeconds = 30.0;          // analyse at most this much of each file
    // Both callbacks may be called from worker threads; calls are serialised.
    std::function<void(std::string_view relPath)> onFileStart;
    std::function<void(std::size_t done, std::size_t total, std::string_view relPath)> onProgress;
};

// Analyses every ok file whose analysis_version is below kAnalysisVersion.
AnalyseStats analysePending(Db& db, const AnalyseOptions& options = {});

// Records that analysing relPath crashed the process, so it is not analysed
// again until its content changes. No-op for an unknown path.
void markAnalysisFailed(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason);

} // namespace asma
```

Create `core/src/Analyser.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Analyser.h"

#include "Parallel.h"
#include "asma/core/Analysis.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/Decode.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace asma {

namespace {

struct Pending {
    std::int64_t id = 0;
    fs::path path;
    std::string relPath;
};

struct Outcome {
    std::optional<AnalysisResult> result;
    std::string error;
    bool unreadable = false;
};

std::vector<Pending> pendingFiles(Db& db, const std::optional<std::int64_t>& rootId)
{
    std::string sql = "SELECT f.id, r.path, f.rel_path FROM files f JOIN roots r ON r.id = f.root_id "
                      "WHERE f.status = 'ok' AND r.enabled = 1 AND f.analysis_version < ?";
    if (rootId) sql += " AND f.root_id = ?";
    sql += " ORDER BY f.id";
    auto q = db.prepare(sql);
    q.bind(1, kAnalysisVersion);
    if (rootId) q.bind(2, *rootId);
    std::vector<Pending> out;
    while (q.step()) {
        Pending p;
        p.id = q.getInt(0);
        p.relPath = q.getText(2);
        p.path = fromUtf8(q.getText(1)) / fromUtf8(p.relPath);
        out.push_back(std::move(p));
    }
    return out;
}

Outcome analyseOne(const Pending& file, double maxSeconds)
{
    Outcome o;
    try {
        o.result = analyse(decodeFile(file.path, maxSeconds));
    } catch (const FileAccessError& e) {
        o.error = e.what();
        o.unreadable = true;
    } catch (const std::exception& e) {
        o.error = e.what();
    }
    return o;
}

} // namespace

AnalyseStats analysePending(Db& db, const AnalyseOptions& options)
{
    const std::vector<Pending> files = pendingFiles(db, options.rootId);
    const unsigned threads = detail::threadCount(options.threads);
    const std::size_t batchSize = std::max<std::size_t>(1, options.batchSize);
    Library lib(db);
    AnalyseStats stats;
    std::mutex callbackMutex;
    std::size_t done = 0;

    for (std::size_t start = 0; start < files.size(); start += batchSize) {
        const std::size_t end = std::min(files.size(), start + batchSize);
        std::vector<Outcome> outcomes(end - start);
        detail::parallelFor(start, end, threads, [&](std::size_t i) {
            if (options.onFileStart) {
                std::lock_guard lock(callbackMutex);
                options.onFileStart(files[i].relPath);
            }
            outcomes[i - start] = analyseOne(files[i], options.maxSeconds);
            std::lock_guard lock(callbackMutex);
            ++done;
            if (options.onProgress) options.onProgress(done, files.size(), files[i].relPath);
        });

        Transaction tx(db);
        for (std::size_t i = start; i < end; ++i) {
            const Outcome& o = outcomes[i - start];
            if (o.result) {
                lib.setAnalysis(files[i].id, *o.result);
                ++stats.analysed;
            } else if (o.unreadable) {
                ++stats.skipped; // leave it pending
            } else {
                lib.setAnalysisError(files[i].id, o.error);
                ++stats.failed;
            }
        }
        tx.commit();
    }
    return stats;
}

void markAnalysisFailed(Db& db, std::int64_t rootId, std::string_view relPath, std::string_view reason)
{
    Library lib(db);
    if (const auto file = lib.fileByPath(rootId, relPath)) lib.setAnalysisError(file->id, reason);
}

} // namespace asma
```

Replace `core/src/Scanner.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Scanner.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/ContentHash.h"
#include "asma/core/Fs.h"
#include "asma/core/InstrumentTags.h"
#include "asma/core/Library.h"
#include "asma/core/NameParse.h"
#include "Parallel.h"

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
    bool unreadable = false; // access error: retry next scan, change nothing now
};

JobResult process(const fs::path& root, const Job& job)
{
    JobResult r;
    r.name = parseName(job.disk.relPath);
    try {
        const fs::path full = root / fromUtf8(job.disk.relPath);
        r.probe = probeFile(full);
        r.hash = contentHash(full, r.probe->hashOffset, r.probe->hashLength);
    } catch (const FileAccessError& e) {
        r.probe.reset();
        r.error = e.what();
        r.unreadable = true;
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
        d.bpmSource = FeatureSource::Embedded;
    } else if (r.name.bpm) {
        d.bpm = r.name.bpm;
        d.bpmConfidence = 0.9;
    }
    if (r.name.key) {
        d.key = r.name.key;
        d.keyConfidence = 0.9;
    }
    d.isLoop = probe.acid ? std::optional<bool>(!probe.acid->oneShot) : r.name.isLoop;
    d.loopSource = probe.acid ? FeatureSource::Embedded : FeatureSource::Filename;
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
    if (r.unreadable) {
        // Permissions, a vanished file or a drive going away mid-scan. Not the
        // file's fault: leave any existing row as it is and try again next time.
        ++stats.skipped;
        return;
    }

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

    // A missing row whose root folder is absent is offline, not moved: an
    // unplugged drive must get its rows back on remount, even if copies of
    // its files exist elsewhere.
    const auto candidates = lib.relinkCandidates(rec.contentHash, rec.size);
    const auto moved = std::find_if(candidates.begin(), candidates.end(), [&](const FileRecord& c) {
        if (c.rootId == rootId) return true;
        const auto candidateRoot = lib.root(c.rootId);
        std::error_code ec;
        return candidateRoot && fs::is_directory(fromUtf8(candidateRoot->path), ec);
    });
    if (moved != candidates.end()) {
        rec.id = moved->id;
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
                    // Back unchanged. A row that was failed before it went
                    // missing kept its reason and stays failed, so a file that
                    // crashed the scanner is never retried just for returning.
                    const bool wasFailed = !file.failureReason.empty();
                    lib.setStatus(file.id, wasFailed ? FileStatus::Failed : FileStatus::Ok, file.failureReason);
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
                lib.setStatus(file.id, FileStatus::Missing, file.failureReason); // keep why it failed
                gone.insert(file.id);
            }
        }
        tx.commit();
    }

    const unsigned threads = detail::threadCount(options.threads);
    const std::size_t batchSize = std::max<std::size_t>(1, options.batchSize);
    const std::size_t total = jobs.size();
    std::mutex callbackMutex;
    std::size_t done = 0;

    for (std::size_t start = 0; start < total; start += batchSize) {
        const std::size_t end = std::min(total, start + batchSize);
        std::vector<JobResult> results(end - start);
        detail::parallelFor(start, end, threads, [&](std::size_t i) {
            if (options.onFileStart) {
                std::lock_guard lock(callbackMutex);
                options.onFileStart(jobs[i].disk.relPath);
            }
            results[i - start] = process(rootPath, jobs[i]);
            std::lock_guard lock(callbackMutex);
            ++done;
            if (options.onProgress) options.onProgress(done, total, jobs[i].disk.relPath);
        });

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

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[analyser],[scanner]"`

Expected: no compiler warnings;
`All tests passed (97 assertions in 26 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 140`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: analyse pending files in parallel"
```

---

### Task 8: Find similar

Find-similar, brute force as the spec says.

**Files:**

- Create: `tests/test_similar.cpp`, `core/include/asma/core/Similar.h`,
  `core/src/Similar.cpp`
- Modify: `core/include/asma/core/Query.h`, `core/src/Query.cpp`,
  `tests/test_query.cpp`

**Interfaces:**

- Consumes: `features.feature_vector` written by Task 6/7.
- Produces: `struct asma::SimilarMatch { std::int64_t id; double similarity; }`;
  `std::vector<asma::SimilarMatch> asma::findSimilar(Db&, std::int64_t fileId, int limit = 10)`;
  `std::vector<asma::SearchRow> asma::rowsForIds(Db&, const std::vector<std::int64_t>&)`.

Behaviour the tests pin:

- Every dimension is z-scored across the library before the cosine, so no
  descriptor dominates.
- Only ok files in enabled roots; the file itself is never its own match.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_similar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Analysis.h"
#include "asma/core/Library.h"
#include "asma/core/Similar.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

namespace {

constexpr int kRate = 44100;

std::int64_t addAnalysed(Library& lib, std::int64_t root, const std::string& rel, std::vector<float> mono)
{
    FileRecord f;
    f.rootId = root;
    f.relPath = rel;
    f.size = 1;
    f.mtime = 1;
    f.format = "wav";
    const auto id = lib.insertFile(f);
    DecodedAudio a;
    a.sampleRate = kRate;
    a.mono = std::move(mono);
    lib.setAnalysis(id, analyse(a));
    return id;
}

std::vector<float> scaled(std::vector<float> v, float gain)
{
    for (auto& x : v) x *= gain;
    return v;
}

} // namespace

TEST_CASE("the closest sound comes first and the file itself is left out", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto kick = addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    const auto kick2 = addAnalysed(lib, root, "kick2.wav", scaled(test::kickHit(kRate), 0.7f));
    const auto hat = addAnalysed(lib, root, "hat.wav", test::hatHit(kRate, 1));
    const auto hat2 = addAnalysed(lib, root, "hat2.wav", test::hatHit(kRate, 2));
    addAnalysed(lib, root, "chords.wav", test::chordProgression(0, false, kRate));
    addAnalysed(lib, root, "loop.wav", test::drumLoop(120.0, 2, kRate));

    const auto forKick = findSimilar(db, kick, 3);
    REQUIRE(forKick.size() == 3);
    CHECK(forKick[0].id == kick2);
    for (const auto& m : forKick) CHECK(m.id != kick);
    CHECK(forKick[0].similarity >= forKick[1].similarity);

    CHECK(findSimilar(db, hat, 1).at(0).id == hat2);
}

TEST_CASE("files without a feature vector give no matches", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    FileRecord f;
    f.rootId = root;
    f.relPath = "unanalysed.wav";
    f.format = "wav";
    const auto id = lib.insertFile(f);
    CHECK(findSimilar(db, id).empty());
    CHECK(findSimilar(db, 9999).empty());
}

TEST_CASE("missing files are not suggested", "[similar]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    const auto kick = addAnalysed(lib, root, "kick.wav", test::kickHit(kRate));
    const auto gone = addAnalysed(lib, root, "gone.wav", scaled(test::kickHit(kRate), 0.9f));
    addAnalysed(lib, root, "hat.wav", test::hatHit(kRate, 1));
    lib.setStatus(gone, FileStatus::Missing);
    for (const auto& m : findSimilar(db, kick)) CHECK(m.id != gone);
}
```

Append to `tests/test_query.cpp`:

```cpp

TEST_CASE("rowsForIds keeps the given order and drops unusable ids", "[query]")
{
    Seeded s;
    s.lib.setStatus(s.snare, FileStatus::Missing);
    CHECK(ids(rowsForIds(s.db, {s.padLoop, s.snare, 9999, s.kick})) == Ids{s.padLoop, s.kick});
    CHECK(rowsForIds(s.db, {}).empty());
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build`

Expected: FAIL to compile, first error
`error: 'asma/core/Similar.h' file not found`

- [ ] **Step 3: Implement**

Create `core/include/asma/core/Similar.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <vector>

namespace asma {

struct SimilarMatch {
    std::int64_t id = 0;
    double similarity = 0.0; // cosine of z-scored feature vectors, -1..1
};

// Files that sound most like fileId, best first, from analysed feature
// vectors. Each dimension is z-scored across the library before comparing,
// so no single descriptor dominates. Only ok files in enabled roots are
// considered. Empty when fileId has no feature vector.
std::vector<SimilarMatch> findSimilar(Db& db, std::int64_t fileId, int limit = 10);

} // namespace asma
```

Create `core/src/Similar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Similar.h"

#include "asma/core/Analysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace asma {

std::vector<SimilarMatch> findSimilar(Db& db, std::int64_t fileId, int limit)
{
    std::vector<std::int64_t> ids;
    std::vector<std::vector<double>> vectors;
    auto q = db.prepare("SELECT f.id, ft.feature_vector FROM files f JOIN roots r ON r.id = f.root_id "
                        "JOIN features ft ON ft.file_id = f.id "
                        "WHERE f.status = 'ok' AND r.enabled = 1 AND ft.feature_vector IS NOT NULL");
    std::size_t target = SIZE_MAX;
    while (q.step()) {
        const auto blob = q.getBlob(1);
        if (blob.size() != kFeatureVectorSize * sizeof(float)) continue; // written by another analyser version
        std::vector<double> v(kFeatureVectorSize);
        for (std::size_t d = 0; d < kFeatureVectorSize; ++d) {
            float x = 0.0f;
            std::memcpy(&x, blob.data() + d * sizeof(float), sizeof(float));
            v[d] = x;
        }
        if (q.getInt(0) == fileId) target = ids.size();
        ids.push_back(q.getInt(0));
        vectors.push_back(std::move(v));
    }
    if (target == SIZE_MAX || limit <= 0) return {};

    // z-score every dimension across the library.
    for (std::size_t d = 0; d < kFeatureVectorSize; ++d) {
        double mean = 0.0;
        for (const auto& v : vectors) mean += v[d];
        mean /= static_cast<double>(vectors.size());
        double var = 0.0;
        for (const auto& v : vectors) var += (v[d] - mean) * (v[d] - mean);
        const double sd = std::sqrt(var / static_cast<double>(vectors.size()));
        for (auto& v : vectors) v[d] = sd > 1e-12 ? (v[d] - mean) / sd : 0.0;
    }

    auto norm = [](const std::vector<double>& v) {
        double s = 0.0;
        for (double x : v) s += x * x;
        return std::sqrt(s);
    };
    const std::vector<double>& t = vectors[target];
    const double tn = norm(t);
    std::vector<SimilarMatch> matches;
    for (std::size_t i = 0; i < vectors.size(); ++i) {
        if (i == target) continue;
        double dot = 0.0;
        for (std::size_t d = 0; d < kFeatureVectorSize; ++d) dot += t[d] * vectors[i][d];
        const double n = tn * norm(vectors[i]);
        matches.push_back({ids[i], n > 0.0 ? dot / n : 0.0});
    }
    const auto keep = std::min<std::size_t>(matches.size(), static_cast<std::size_t>(limit));
    std::partial_sort(matches.begin(), matches.begin() + static_cast<std::ptrdiff_t>(keep), matches.end(),
                      [](const SimilarMatch& a, const SimilarMatch& b) {
                          return a.similarity != b.similarity ? a.similarity > b.similarity : a.id < b.id;
                      });
    matches.resize(keep);
    return matches;
}

} // namespace asma
```

Create `core/include/asma/core/Query.h`:

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

// Rows for these file ids, in the given order. Ids that are unknown, not ok
// or in a disabled root are left out.
std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids);

} // namespace asma
```

Create `core/src/Query.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"

#include <algorithm>
#include <cctype>
#include <type_traits>

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

constexpr std::string_view kRowSelect =
    "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop "
    "FROM files f JOIN roots r ON r.id = f.root_id "
    "LEFT JOIN features ft ON ft.file_id = f.id "
    "WHERE f.status = 'ok' AND r.enabled = 1";

SearchRow readRow(const Statement& s)
{
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
    return r;
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
    q.sql = std::string(kRowSelect);

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
    while (s.step()) rows.push_back(readRow(s));
    return rows;
}

std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids)
{
    std::vector<SearchRow> rows;
    if (ids.empty()) return rows;
    Statement s = db.prepare(std::string(kRowSelect) + " AND f.id IN (" + placeholders(ids.size()) + ")");
    for (std::size_t i = 0; i < ids.size(); ++i) s.bind(static_cast<int>(i) + 1, ids[i]);
    std::vector<SearchRow> found;
    while (s.step()) found.push_back(readRow(s));
    for (const auto id : ids) {
        const auto it = std::find_if(found.begin(), found.end(), [&](const SearchRow& r) { return r.id == id; });
        if (it != found.end()) rows.push_back(*it);
    }
    return rows;
}

} // namespace asma
```

- [ ] **Step 4: Run the tests**

Run:
`cmake -B build && cmake --build build && ./build/tests/asma_tests "[similar],[query]"`

Expected: no compiler warnings;
`All tests passed (52 assertions in 12 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 144`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "core: find similar sounds by feature vector"
```

---

### Task 9: CLI: analysis in scan, `asma similar`, analysis events

Makes analysis visible: `asma scan` analyses, `asma similar` answers,
`asma-scan` reports progress in a way the plan 3 supervisor can recover from.

**Files:**

- Modify: `apps/asma_main.cpp`, `apps/asma_scan_main.cpp`,
  `docs/scan-protocol.md`, `tests/test_cli_e2e.cpp`

**Interfaces:**

- Consumes: `analysePending`, `markAnalysisFailed`, `findSimilar`, `rowsForIds`,
  `Library::fileByAbsolutePath`.
- Produces: `asma scan [--no-analysis]`,
  `asma similar <file> [--id N] [--limit N] [--json]`;
  `asma-scan --no-analysis --fail-analysis RELPATH`; events `analyse_start`,
  `analyse_progress`, `analyse_done`, `marked_analysis_failed`.

Behaviour the tests pin:

- Existing events keep their exact format, so plan 1 consumers do not break.
- A crash while analysing is fed back with `--fail-analysis`, which keeps the
  file searchable; `--fail` still means the file itself is broken.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_cli_e2e.cpp`:

```cpp

TEST_CASE("scan analyses by default and --no-analysis skips it", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    const RunResult quick = cli.runAsma("scan --no-analysis");
    CHECK(quick.out.find("analysis:") == std::string::npos);
    const RunResult full = cli.runAsma("scan");
    CHECK(full.out.find("analysis: analysed 2, failed 0, skipped 0") != std::string::npos);
}

TEST_CASE("similar lists other files with a similarity score", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runAsma("scan").exitCode == 0);
    const RunResult r = cli.runAsma("similar " + quote(cli.lib / "Loops" / "Bass_Loop_Am_128.wav") + " --json");
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("Kick") != std::string::npos);
    CHECK(r.out.find("\"similarity\":") != std::string::npos);
    CHECK(r.out.find("Bass_Loop") == std::string::npos);

    CHECK(cli.runAsma("similar " + quote(cli.lib / "nope.wav")).exitCode == 1);
    CHECK(cli.runAsma("similar").exitCode == 2);
}

TEST_CASE("asma-scan reports analysis and honours --fail-analysis", "[e2e]")
{
    Cli cli;
    REQUIRE(cli.runAsma("root add " + quote(cli.lib)).exitCode == 0);
    REQUIRE(cli.runScan("--root 1 --no-analysis").exitCode == 0);
    const RunResult r =
        cli.runScan("--root 1 --fail-analysis " + quote(asma::fromUtf8("Loops/Bass_Loop_Am_128.wav")));
    CHECK(r.exitCode == 0);
    CHECK(r.out.find("{\"event\":\"marked_analysis_failed\",\"path\":\"Loops/Bass_Loop_Am_128.wav\"}") !=
          std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_start\",\"path\":\"Drums/Kick") != std::string::npos);
    CHECK(r.out.find("{\"event\":\"analyse_done\",\"analysed\":1,\"failed\":0,\"skipped\":0}") != std::string::npos);
}
```

- [ ] **Step 2: Build and run to see the failure**

Run: `cmake --build build && ./build/tests/asma_tests "[e2e]"`

Expected: builds, then the new tests FAIL
(`test cases:  8 |  5 passed | 3 failed`)

- [ ] **Step 3: Implement**

Create `apps/asma_main.cpp`:

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
#include "asma/core/WriterLock.h"

#include <cmath>
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
        const SearchRow& row = rows[i];
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
            if (i < similarity.size()) line.real("similarity", similarity[i]);
            std::cout << line.build() << "\n";
        } else {
            char duration[32];
            std::snprintf(duration, sizeof duration, "%.2f", row.duration);
            char bpm[32] = "-";
            if (row.bpm) std::snprintf(bpm, sizeof bpm, "%g", std::round(*row.bpm * 100.0) / 100.0);
            const char* type = !row.isLoop ? "-" : (*row.isLoop ? "loop" : "oneshot");
            std::cout << path << "\t" << bpm << "\t" << row.key.value_or("-") << "\t" << type << "\t" << duration;
            if (i < similarity.size()) {
                char score[32];
                std::snprintf(score, sizeof score, "%.3f", similarity[i]);
                std::cout << "\t" << score;
            }
            std::cout << "\n";
        }
    }
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
    const bool analyseFiles = !args.flag("no-analysis");
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
                  << s.failed << ", skipped " << s.skipped << "\n";
    }
    if (analyseFiles) {
        AnalyseOptions analysis;
        analysis.threads = options.threads;
        if (root) analysis.rootId = static_cast<std::int64_t>(toDouble(*root, "--root"));
        analysis.onProgress = options.onProgress;
        const AnalyseStats a = analysePending(db, analysis);
        std::cerr << "\r";
        std::cout << "analysis: analysed " << a.analysed << ", failed " << a.failed << ", skipped " << a.skipped
                  << "\n";
    }
    return kOk;
}

int cmdSimilar(Args& args, Db& db)
{
    const auto id = args.option("id");
    int limit = 10;
    if (const auto v = args.option("limit")) limit = static_cast<int>(toDouble(*v, "--limit"));
    const bool json = args.flag("json");
    const auto target = args.positional();
    rejectLeftovers(args);

    std::int64_t fileId = 0;
    if (id) {
        fileId = static_cast<std::int64_t>(toDouble(*id, "--id"));
    } else {
        if (!target) throw UsageError("similar needs a file path or --id N");
        Library lib(db);
        const auto file = lib.fileByAbsolutePath(fromUtf8(*target));
        if (!file) {
            std::cerr << "asma: not in the library: " << *target << "\n";
            return kError;
        }
        fileId = file->id;
    }
    const auto matches = findSimilar(db, fileId, limit);
    std::vector<std::int64_t> ids;
    std::vector<double> scores;
    for (const auto& m : matches) ids.push_back(m.id);
    const auto rows = rowsForIds(db, ids);
    for (const auto& row : rows)
        for (const auto& m : matches)
            if (m.id == row.id) scores.push_back(m.similarity);
    printRows(rows, json, scores);
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

    printRows(search(db, m), json, {});
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
    } catch (const std::exception& e) {
        std::cerr << "asma: " << e.what() << "\n";
        return kError;
    }
}
```

Create `apps/asma_scan_main.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// asma-scan: out-of-process scanner. Protocol: docs/scan-protocol.md.
#include "Args.h"
#include "CliCommon.h"

#include "asma/core/Analyser.h"
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
        const auto analysisFailed = args.options("fail-analysis");
        const bool analyseFiles = !args.flag("no-analysis");
        if (!db || !root || !args.rest().empty())
            throw UsageError("usage: asma-scan --db PATH --root ID [--threads N] [--no-analysis] "
                             "[--fail RELPATH]... [--fail-analysis RELPATH]...");

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
        for (const auto& path : analysisFailed) {
            const std::string rel = toUtf8(fromUtf8(path));
            markAnalysisFailed(database, rootId, rel, "crashed the analyser");
            emit(JsonLine().str("event", "marked_analysis_failed").str("path", rel));
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
                 .num("failed", static_cast<std::int64_t>(s.failed))
                 .num("skipped", static_cast<std::int64_t>(s.skipped)));

        if (analyseFiles) {
            AnalyseOptions analysis;
            analysis.threads = options.threads;
            analysis.rootId = rootId;
            analysis.onFileStart = [](std::string_view rel) {
                emit(JsonLine().str("event", "analyse_start").str("path", rel));
            };
            analysis.onProgress = [](std::size_t done, std::size_t total, std::string_view rel) {
                emit(JsonLine()
                         .str("event", "analyse_progress")
                         .num("done", static_cast<std::int64_t>(done))
                         .num("total", static_cast<std::int64_t>(total))
                         .str("path", rel));
            };
            const AnalyseStats a = analysePending(database, analysis);
            emit(JsonLine()
                     .str("event", "analyse_done")
                     .num("analysed", static_cast<std::int64_t>(a.analysed))
                     .num("failed", static_cast<std::int64_t>(a.failed))
                     .num("skipped", static_cast<std::int64_t>(a.skipped)));
        }
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

Create `docs/scan-protocol.md`:

```markdown
# asma-scan protocol

`asma-scan` is the out-of-process scanner. The UI starts it, reads its stdout
line by line, and restarts it if it dies. Every line is one JSON object with an
`event` field.

A run has two phases. First it indexes the root (probe, hash, file-name
metadata), then it analyses every file in that root that has not been analysed
by the current analyser version (decode, loudness, tempo, key, descriptors).

## Invocation

    asma-scan --db PATH --root ID [--threads N] [--no-analysis]
              [--fail RELPATH]... [--fail-analysis RELPATH]...

`--fail` marks a root-relative path as failed with the reason "crashed the
scanner" before indexing starts, so indexing skips it until the file changes.

`--fail-analysis` marks a path as analysed with the error "crashed the
analyser", so analysis skips it until its content changes. The file stays in
search results.

`--no-analysis` stops after indexing.

## Events

| event                    | fields                                                                      | meaning                               |
| ------------------------ | --------------------------------------------------------------------------- | ------------------------------------- |
| `marked_failed`          | `path`                                                                      | a `--fail` path was recorded          |
| `marked_analysis_failed` | `path`                                                                      | a `--fail-analysis` path was recorded |
| `start`                  | `path`                                                                      | indexing began on this file           |
| `progress`               | `done`, `total`, `path`                                                     | indexing finished this file           |
| `done`                   | `added`, `updated`, `unchanged`, `relinked`, `missing`, `failed`, `skipped` | indexing completed                    |
| `analyse_start`          | `path`                                                                      | analysis began on this file           |
| `analyse_progress`       | `done`, `total`, `path`                                                     | analysis finished this file           |
| `analyse_done`           | `analysed`, `failed`, `skipped`                                             | analysis completed                    |
| `error`                  | `code` (`locked` or `failed`), optional `pid`, `message`                    | the run did not start or did not end  |

Paths are root-relative, UTF-8, with `/` separators.

`skipped` counts files that could not be read this time (permissions, a file
that vanished, a drive that went away mid-scan). Their rows are left as they
were and the next run tries again. `failed` is only for files whose content
could not be parsed or decoded; they are not retried until they change.

## Exit codes

0 success, 1 error, 2 usage, 3 another process holds the writer lock.

## Crash recovery (supervisor contract)

Several files are processed in parallel, so a crash cannot be pinned on one
file from the last line alone. The supervisor keeps the set of paths that have
a `start` (or `analyse_start`) but no matching `progress` (or
`analyse_progress`). When the worker exits without finishing, the supervisor
restarts it with `--threads 1`, and when a single-threaded worker dies, the
path it started last is passed back with `--fail` if it died while indexing,
or `--fail-analysis` if it died while analysing.
```

- [ ] **Step 4: Run the tests**

Run: `cmake -B build && cmake --build build && ./build/tests/asma_tests "[e2e]"`

Expected: no compiler warnings;
`All tests passed (44 assertions in 8 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 147`.

Run `prettier -w docs/scan-protocol.md`.

- [ ] **Step 5: Commit**

```sh
git add -A
git commit -m "apps: analyse during scan; add asma similar and analysis events"
```

---

### Task 10: Timings in CI, real-library evaluation, docs

The spec asks for scan timings in CI and an accuracy baseline; this adds both,
plus the real-library evaluation used while tuning.

**Files:**

- Create: `tests/test_perf.cpp`, `tests/test_real_eval.cpp`
- Modify: `.github/workflows/ci.yml`, `README.md`,
  `docs/superpowers/specs/2026-09-25-asma-design.md`

**Interfaces:**

- Consumes: Everything above.
- Produces: Hidden tests `[.perf]` and `[.real]`; a CI step that writes timings
  to the job summary; README analysis section; spec wording for the accuracy
  corpus.

Behaviour the tests pin:

- `[.perf]` reports, it does not gate.
- `[.real]` needs `ASMA_EVAL_DIR` and reads files in place; nothing is copied
  into the repo.

- [ ] **Step 1: Implement**

Create `tests/test_perf.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Hidden: timings reported by CI, never a pass/fail gate beyond sanity.
// Run with ./build/tests/asma_tests "[.perf]" on a Release build.
#include "Signals.h"
#include "TestUtil.h"
#include "asma/core/Analyser.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace asma;

TEST_CASE("scan and analyse a generated library", "[.perf]")
{
    constexpr int kRate = 44100;
    constexpr int kHits = 1500;
    constexpr int kLoops = 200;
    test::TempDir dir;
    const auto kick = test::kickHit(kRate);
    const auto loop = test::drumLoop(120.0, 2, kRate);
    for (int i = 0; i < kHits; ++i) {
        auto v = kick;
        v[0] = static_cast<float>(i) / kHits; // distinct content, distinct hash
        test::writeWavSamples(dir.path() / "Hits" / ("Kick_" + std::to_string(i) + ".wav"), kRate, v);
    }
    for (int i = 0; i < kLoops; ++i) {
        auto v = loop;
        v[0] = static_cast<float>(i) / kLoops;
        test::writeWavSamples(dir.path() / "Loops" / ("Beat_" + std::to_string(i) + ".wav"), kRate, v);
    }

    Db db = Db::open(dir.path() / "data" / "library.db");
    Library lib(db);
    const auto root = lib.addRoot(dir.path());
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    const ScanStats scan = scanRoot(db, root);
    const double scanSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    start = Clock::now();
    const AnalyseStats analysis = analysePending(db);
    const double analyseSeconds = std::chrono::duration<double>(Clock::now() - start).count();

    const double files = kHits + kLoops;
    WARN("scan took " << scanSeconds << " s: " << files / scanSeconds << " files/s");
    WARN("analysis took " << analyseSeconds << " s: " << files / analyseSeconds << " files/s");
    CHECK(scan.added == files);
    CHECK(analysis.analysed == files);
}
```

Create `tests/test_real_eval.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Hidden: compares analysis against the BPM and key that file names and ACID
// chunks already state, on a real library. Nothing from that library is
// stored in the repo. Run with:
//   ASMA_EVAL_DIR=~/Samples ./build/tests/asma_tests "[.real]"
#include "TestUtil.h"
#include "asma/core/Analysis.h"
#include "asma/core/Decode.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace asma;

namespace {

// A in Am, C in Cm: the relative and parallel keys that key finders confuse.
bool relative(const std::string& a, const std::string& b)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    auto pc = [&](const std::string& k) {
        const std::string root = k.back() == 'm' ? k.substr(0, k.size() - 1) : k;
        for (int i = 0; i < 12; ++i)
            if (root == names[i]) return i;
        return -1;
    };
    const bool am = a.back() == 'm';
    const bool bm = b.back() == 'm';
    if (am == bm) return false;
    const int major = am ? pc(b) : pc(a);
    const int minor = am ? pc(a) : pc(b);
    return (major + 9) % 12 == minor;
}

} // namespace

TEST_CASE("analysis agrees with labelled files in a real library", "[.real]")
{
    const char* dir = std::getenv("ASMA_EVAL_DIR");
    if (!dir) SKIP("set ASMA_EVAL_DIR to a sample folder");
    const int maxFiles = std::getenv("ASMA_EVAL_MAX") ? std::atoi(std::getenv("ASMA_EVAL_MAX")) : 400;

    Db db = Db::openInMemory();
    Library lib(db);
    const auto root = lib.addRoot(fromUtf8(dir));
    scanRoot(db, root);

    int bpmTotal = 0, bpmExact = 0, bpmOctave = 0, bpmNone = 0;
    int keyTotal = 0, keyExact = 0, keyRelative = 0, keyNone = 0;
    auto q = db.prepare("SELECT r.path, f.rel_path, ft.bpm, ft.bpm_source, ft.key, ft.is_loop FROM files f "
                        "JOIN roots r ON r.id = f.root_id JOIN features ft ON ft.file_id = f.id "
                        "WHERE f.status = 'ok' AND (ft.bpm IS NOT NULL OR ft.key IS NOT NULL) ORDER BY f.content_hash");
    int seen = 0;
    while (q.step() && seen < maxFiles) {
        ++seen;
        DecodedAudio audio;
        try {
            audio = decodeFile(fromUtf8(q.getText(0)) / fromUtf8(q.getText(1)));
        } catch (const std::exception&) {
            continue;
        }
        const AnalysisResult r = analyse(audio);
        if (!q.isNull(2) && q.getInt(5) == 1) {
            const double truth = q.getDouble(2);
            ++bpmTotal;
            if (!r.bpm) ++bpmNone;
            else if (std::abs(*r.bpm - truth) <= 0.5) ++bpmExact;
            else if (std::abs(*r.bpm * 2 - truth) <= 1.0 || std::abs(*r.bpm / 2 - truth) <= 0.5) ++bpmOctave;
        }
        if (!q.isNull(4)) {
            const std::string truth = q.getText(4);
            ++keyTotal;
            if (!r.key) ++keyNone;
            else if (*r.key == truth) ++keyExact;
            else if (relative(*r.key, truth)) ++keyRelative;
        }
    }
    auto pct = [](int n, int d) { return d ? 100.0 * n / d : 0.0; };
    std::printf("BPM (labelled loops): %d files, exact %.1f%%, octave %.1f%%, none %.1f%%, wrong %.1f%%\n", bpmTotal,
                pct(bpmExact, bpmTotal), pct(bpmOctave, bpmTotal), pct(bpmNone, bpmTotal),
                pct(bpmTotal - bpmExact - bpmOctave - bpmNone, bpmTotal));
    std::printf("Key (labelled files): %d files, exact %.1f%%, relative %.1f%%, none %.1f%%, wrong %.1f%%\n", keyTotal,
                pct(keyExact, keyTotal), pct(keyRelative, keyTotal), pct(keyNone, keyTotal),
                pct(keyTotal - keyExact - keyRelative - keyNone, keyTotal));
}
```

Create `.github/workflows/ci.yml`:

````yaml
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
      - uses: actions/checkout@v5
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
        run: |
          ./build/tests/asma_tests "[.perf]" > perf.txt 2>&1 || true
          {
            echo "### Performance on ${{ matrix.os }}"
            echo '```'
            grep -E "took" perf.txt || cat perf.txt
            echo '```'
          } >> "$GITHUB_STEP_SUMMARY"
````

Create `README.md`:

```markdown
# asma

Anode Labs Sample Manager. Free, open-source sample manager for macOS, Windows
and Linux. Work in progress.

## Build

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

## Command line

    asma root add ~/Samples
    asma scan                    # index, then analyse new and changed files
    asma query kick
    asma query --type loop --bpm 120-130 --key Am
    asma query dusty --tag drums --json
    asma similar ~/Samples/Drums/Kick_01.wav

The library lives in the platform data directory (on macOS
`~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
`ASMA_DATA_DIR` environment variable override it.

## Analysis

`asma scan` analyses each new or changed file once: loudness (EBU R128),
tempo, key, loop or one-shot, and a timbre fingerprint for `asma similar`.
BPM, key and loop flags found in the file (ACID chunks) or its name always win
over analysis.

Measured on a real 70,000-file library against the BPM and key that file
names already state: loop tempo exact for 67% of loops and off by an octave
for another 14%; key right for 69% of the files it reports a key for. To
measure your own library (nothing is uploaded or copied):

    ASMA_EVAL_DIR=~/Samples ./build/tests/asma_tests "[.real]"

## License

GPLv3. See `LICENSE`.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```markdown
- **Analysis accuracy corpus:** generated fixtures (click tracks at known BPMs,
  tones and chords in known keys, one-shots vs loops) plus a small CC0 set,
  checked into the repo with expected results. CI fails if BPM or key accuracy
  drops below the recorded baseline.
```

with:

```markdown
- **Analysis accuracy corpus:** generated fixtures (drum loops at known BPMs,
  chord progressions in known keys, loops vs one-shots), built at test time so
  no third-party audio is stored. CI fails if BPM or key accuracy drops below
  the recorded baseline. Real-world accuracy is measured by an opt-in test
  against the BPM and key a user's own library already states in file names
  and ACID chunks; nothing from that library is copied or uploaded.
```

In `docs/superpowers/specs/2026-09-25-asma-design.md`, replace:

```markdown
  correction; confidence from peak prominence. Embedded ACID tempo and filename
  BPM override when present.
```

with:

```markdown
  correction; confidence from peak prominence. For a complete file, only the
  tempos that fit a whole number of bars are considered. Embedded ACID tempo
  and filename BPM override when present.
```

- [ ] **Step 2: Run the tests**

Run: `cmake -B build && cmake --build build && ./build/tests/asma_tests "[e2e]"`

Expected: no compiler warnings;
`All tests passed (44 assertions in 8 test cases)`. Then
`ctest --test-dir build --output-on-failure`: `100% tests passed out of 147`.

Also run the timings on a Release build (informational):

```sh
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/tests/asma_tests "[.perf]"
```

Expected: three `took` lines (scan, analysis, search). On an M-series Mac: scan
~8,000 files/s, analysis ~1,500 files/s, search ~23 ms. Then run
`prettier -w README.md docs/scan-protocol.md docs/superpowers/specs/2026-09-25-asma-design.md`.

- [ ] **Step 3: Commit**

```sh
git add -A
git commit -m "ci: report scan and analysis timings; document analysis accuracy"
```

---

### Task 11: Verify on real audio and merge

- [ ] **Step 1: Full suites, Debug and Release**

```sh
ctest --test-dir build --output-on-failure
cmake --build build-release && ctest --test-dir build-release --output-on-failure
```

Expected: all green in both.

- [ ] **Step 2: Real-library evaluation**

```sh
ASMA_EVAL_DIR=~/Music ASMA_EVAL_MAX=2500 ./build-release/tests/asma_tests "[.real]"
```

Expected: two summary lines. Compare with the table under "Measured on a real
library"; a drop of more than 5 points in loop-tempo exact or key precision
means something regressed and needs a look before merging.

- [ ] **Step 3: Smoke test**

```sh
export ASMA_DATA_DIR=$(mktemp -d)
./build-release/apps/asma root add ~/Music
time ./build-release/apps/asma scan
./build-release/apps/asma query --type loop --bpm 118-122 --limit 5
./build-release/apps/asma similar "$(./build-release/apps/asma query kick --limit 1 | cut -f1)" --limit 5
```

Expected: `analysis: analysed ...` with only a handful of failures; loops near
120 BPM; five kick-like results with similarity scores.

- [ ] **Step 4: Text rules**

```sh
grep -rn $'\xe2\x80\x94' --exclude-dir=.git --exclude-dir='build*' --exclude-dir=.superpowers --exclude=LICENSE . && echo "em-dash found" || echo "no em-dashes"
git log --format=%B main..HEAD | grep -i "co-authored" && echo "trailer found" || echo "no trailers"
```

Expected: `no em-dashes`, `no trailers`.

- [ ] **Step 5: Merge**

```sh
git -C product/asma merge --ff-only plan-2-analysis
git -C product/asma worktree remove .worktrees/plan-2
git -C product/asma branch -d plan-2-analysis
```

Pushing to `origin` publishes to the public repo and starts CI on all three
platforms; do it once the user agrees, then check the run and its timing
summary.
