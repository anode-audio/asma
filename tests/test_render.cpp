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

TEST_CASE("RenderStore hands out the original when nothing changes the audio", "[render]")
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 48000, {counting(4800)});
    RenderStore cache(dir.path() / "renders");
    RenderSettings s;
    s.sampleRate = 44100; // not an edit
    s.edits.loop = LoopMode::On;
    CHECK(cache.fileFor(src, s) == src);
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
{
    TempDir dir;
    const auto src = dir.path() / "a.wav";
    test::writeWavFloat(src, 1000, {counting(1000)}); // 1 s
    const auto out = dir.path() / "out.wav";
    RenderSettings s;
    s.edits.trimStart = 2.0; // past the end
    CHECK_THROWS_AS(renderToFile(src, s, out), std::invalid_argument);
    s.edits.trimStart = 0.6;
    s.edits.trimEnd = 0.4; // ends before it starts
    CHECK_THROWS_AS(renderToFile(src, s, out), std::invalid_argument);
    s.edits = {};
    s.edits.direction = Direction::Reverse;
    s.sampleRate = 10;
    CHECK_THROWS_AS(renderToFile(src, s, out), std::invalid_argument);
    s.sampleRate = 1000000;
    CHECK_THROWS_AS(renderToFile(src, s, out), std::invalid_argument);
    CHECK_FALSE(fs::exists(out));
}
