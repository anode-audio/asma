// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/AudioProbe.h"
#include "asma/core/Decode.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

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

TEST_CASE("An absurd sample rate is a decode error, not a runaway", "[decode]")
{
    TempDir dir;
    const auto p = dir.path() / "40hz.wav";
    test::WavSpec spec;
    spec.sampleRate = 40;
    spec.frames = 400;
    test::writeWav(p, spec);
    CHECK_THROWS_AS(decodeFile(p), ProbeError);
}

TEST_CASE("Non-finite and huge float samples decode as finite values", "[decode]")
{
    TempDir dir;
    const auto p = dir.path() / "garbage-float.wav";
    test::WavSpec spec;
    spec.formatTag = 3; // IEEE float: the random bytes include NaN, inf and 1e38
    spec.bitsPerSample = 32;
    spec.frames = 8000;
    test::writeWav(p, spec);
    const DecodedAudio a = decodeFile(p);
    REQUIRE(a.mono.size() == 8000);
    for (float x : a.mono) {
        REQUIRE(std::isfinite(x));
        REQUIRE(std::abs(x) <= 16.0f);
    }
}
