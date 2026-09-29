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
