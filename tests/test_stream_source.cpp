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
