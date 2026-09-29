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
