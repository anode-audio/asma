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
