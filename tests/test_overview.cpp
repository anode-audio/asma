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
