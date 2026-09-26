// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/core/AudioProbe.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using asma::test::TempDir;

TEST_CASE("formatFromExtension is case-insensitive and rejects unknown types", "[probe]")
{
    CHECK(formatFromExtension("KICK.WAV") == AudioFormat::Wav);
    CHECK(formatFromExtension("x.wave") == AudioFormat::Wav);
    CHECK(formatFromExtension("pad.Aif") == AudioFormat::Aiff);
    CHECK(formatFromExtension("pad.aifc") == AudioFormat::Aiff);
    CHECK(formatFromExtension("x.flac") == AudioFormat::Flac);
    CHECK(formatFromExtension("x.MP3") == AudioFormat::Mp3);
    CHECK(formatFromExtension("x.ogg") == AudioFormat::Ogg);
    CHECK_FALSE(formatFromExtension("notes.txt").has_value());
    CHECK_FALSE(formatFromExtension("wav").has_value());
    CHECK(formatName(AudioFormat::Aiff) == "aiff");
}

TEST_CASE("WAV header fields and data range", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "a.wav";
    test::WavSpec spec;
    spec.channels = 2;
    spec.frames = 44100;
    test::writeWav(p, spec);

    const ProbeResult r = probeFile(p);
    CHECK(r.format == AudioFormat::Wav);
    CHECK(r.sampleRate == 44100);
    CHECK(r.channels == 2);
    CHECK(r.bitDepth == 16);
    CHECK(r.durationSeconds == Catch::Approx(1.0));
    CHECK(r.hashOffset == 44);
    CHECK(r.hashLength == 44100u * 4u);
    CHECK_FALSE(r.acid.has_value());
    CHECK_FALSE(r.smplUnityNote.has_value());
}

TEST_CASE("Chunks before data move the offset but not the length", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "b.wav";
    test::WavSpec spec;
    spec.chunksBeforeData = {{"LIST", std::string(10, 'x')}, {"junk", "abc"}}; // odd size is padded
    test::writeWav(p, spec);

    const ProbeResult r = probeFile(p);
    CHECK(r.hashOffset == 44u + (8u + 10u) + (8u + 3u + 1u));
    CHECK(r.hashLength == 4410u * 2u);
}

TEST_CASE("ACID and smpl chunks are read", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "c.wav";
    test::WavSpec spec;
    spec.acid = std::make_pair(false, 128.0f);
    spec.smplUnityNote = 48;
    test::writeWav(p, spec);

    const ProbeResult r = probeFile(p);
    REQUIRE(r.acid.has_value());
    CHECK_FALSE(r.acid->oneShot);
    CHECK(r.acid->tempo == Catch::Approx(128.0));
    CHECK(r.smplUnityNote == 48);
}

TEST_CASE("AIFF header fields and data range", "[probe]")
{
    TempDir dir;
    const auto p = dir.path() / "d.aiff";
    test::AiffSpec spec;
    spec.sampleRate = 48000;
    spec.bitsPerSample = 24;
    spec.frames = 24000;
    test::writeAiff(p, spec);

    const ProbeResult r = probeFile(p);
    CHECK(r.format == AudioFormat::Aiff);
    CHECK(r.sampleRate == 48000);
    CHECK(r.channels == 1);
    CHECK(r.bitDepth == 24);
    CHECK(r.durationSeconds == Catch::Approx(0.5));
    CHECK(r.hashLength == 24000u * 3u);
}

TEST_CASE("FLAC, MP3 and Ogg fixtures", "[probe]")
{
    const auto flacPath = test::fixture("tone.flac");
    const ProbeResult flac = probeFile(flacPath);
    CHECK(flac.format == AudioFormat::Flac);
    CHECK(flac.sampleRate == 44100);
    CHECK(flac.channels == 1);
    CHECK(flac.bitDepth == 16);
    CHECK(flac.durationSeconds == Catch::Approx(0.5).margin(0.001));
    CHECK(flac.hashOffset == 0);
    CHECK(flac.hashLength == std::filesystem::file_size(flacPath));

    const ProbeResult mp3 = probeFile(test::fixture("tone.mp3"));
    CHECK(mp3.format == AudioFormat::Mp3);
    CHECK(mp3.sampleRate == 44100);
    CHECK(mp3.channels == 1);
    CHECK(mp3.bitDepth == 0);
    CHECK(mp3.durationSeconds == Catch::Approx(0.5).margin(0.06));

    const ProbeResult ogg = probeFile(test::fixture("tone.ogg"));
    CHECK(ogg.format == AudioFormat::Ogg);
    CHECK(ogg.sampleRate == 44100);
    CHECK(ogg.channels == 2);
    CHECK(ogg.durationSeconds == Catch::Approx(0.5).margin(0.01));
}

TEST_CASE("Empty, truncated and mislabelled files throw ProbeError", "[probe]")
{
    TempDir dir;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"empty.wav", ""},
        {"text.wav", "hello, this is not audio at all"},
        {"truncated.wav", std::string("RIFF\x10\0\0\0WAVE", 12)},
        {"nodata.wav", std::string("RIFF\x04\0\0\0WAVE", 12)},
        {"text.aiff", "FORM but not really"},
        {"text.flac", std::string(2000, 'a')},
        {"text.mp3", std::string(2000, 'a')},
        {"text.ogg", std::string(2000, 'a')},
    };
    for (const auto& [name, bytes] : cases) {
        INFO(name);
        const auto p = dir.path() / name;
        test::writeBytes(p, bytes);
        CHECK_THROWS_AS(probeFile(p), ProbeError);
    }
}

TEST_CASE("A missing file throws ProbeError", "[probe]")
{
    TempDir dir;
    CHECK_THROWS_AS(probeFile(dir.path() / "nope.wav"), ProbeError);
}
