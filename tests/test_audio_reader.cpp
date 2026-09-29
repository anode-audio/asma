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
