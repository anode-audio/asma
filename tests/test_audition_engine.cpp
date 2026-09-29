// SPDX-License-Identifier: GPL-3.0-only
#include "AllocCounter.h"
#include "Signals.h"
#include "TestUtil.h"
#include "asma/audio/AuditionEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 256;
constexpr int kFade = 240; // 5 ms

// (i + 1) / 100000: every frame recognisable, never silent.
std::vector<float> counting(int frames, float offset = 0.0f)
{
    std::vector<float> x(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) x[static_cast<std::size_t>(i)] = offset + static_cast<float>(i + 1) / 100000.0f;
    return x;
}

struct Rig {
    TempDir dir;
    PreviewCache cache;
    AuditionEngine engine{cache};
    Transport transport;

    Rig()
    {
        engine.prepare(kRate, kBlock);
        engine.setGainMatch(false);
    }
    fs::path file(const char* name, const std::vector<float>& samples)
    {
        const auto p = dir.path() / name;
        test::writeWavFloat(p, kRate, {samples});
        return p;
    }
    // Renders whole blocks, loading between them as the loader thread would.
    std::vector<float> run(int frames)
    {
        std::vector<float> left;
        std::vector<float> l(kBlock), r(kBlock);
        float* out[] = {l.data(), r.data()};
        while (static_cast<int>(left.size()) < frames) {
            engine.loader().pump();
            engine.setTransport(transport);
            engine.process(out, kBlock);
            left.insert(left.end(), l.begin(), l.end());
            if (transport.playing) transport.ppq += kBlock * transport.bpm / 60.0 / kRate;
        }
        return left;
    }
};

std::size_t firstSound(const std::vector<float>& x)
{
    std::size_t i = 0;
    while (i < x.size() && x[i] == 0.0f) ++i;
    return i;
}

} // namespace

TEST_CASE("AuditionEngine plays a selection as it arrives", "[engine]")
{
    Rig rig;
    const auto samples = counting(2000);
    rig.engine.select(rig.file("a.wav", samples), {}, true);
    const auto out = rig.run(2560);
    CHECK(out[0] == samples[0]);
    CHECK(out[1999] == samples[1999]);
    CHECK(out[2000] == 0.0f); // a one-shot plays once
    CHECK_FALSE(rig.engine.status().playing);
}

TEST_CASE("AuditionEngine waits for play without autoplay", "[engine]")
{
    Rig rig;
    const auto samples = counting(1000);
    const auto g = rig.engine.select(rig.file("a.wav", samples), {}, false);
    CHECK(firstSound(rig.run(1024)) == 1024);
    CHECK(rig.engine.status().generation == g);
    rig.engine.play();
    const auto out = rig.run(1024);
    CHECK(out[0] == samples[0]);

    // play() straight after select(), before the file has loaded.
    rig.engine.select(rig.file("b.wav", counting(1000, 0.5f)), {}, false);
    rig.engine.play();
    CHECK(rig.run(256)[0] == Catch::Approx(0.50001f));
}

TEST_CASE("AuditionEngine fades out on stop", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
    rig.run(1024);
    CHECK(rig.engine.status().playing);
    rig.engine.stop();
    const auto out = rig.run(512);
    CHECK(out[0] > 0.0f);
    CHECK(std::abs(out[kFade - 1]) < out[0] / 100.0f);
    CHECK(out[kFade] == 0.0f);
    CHECK_FALSE(rig.engine.status().playing);
}

TEST_CASE("AuditionEngine fades the old sample out before the new one starts", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
    rig.run(1024);
    const auto b = counting(4800, 0.5f);
    rig.engine.select(rig.file("b.wav", b), {}, true);
    const auto out = rig.run(1024);
    CHECK(out[0] < 0.1f);         // still a, fading
    CHECK(out[kFade] == b[0]);    // then b from its first frame
    CHECK(out[kFade + 100] == b[100]);

    rig.engine.select(rig.file("c.wav", counting(4800)), {}, false); // no autoplay: b stops
    const auto quiet = rig.run(512);
    CHECK(quiet[kFade] == 0.0f);
}

TEST_CASE("AuditionEngine restarts on an edit", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    const auto samples = counting(48000);
    rig.engine.select(rig.file("a.wav", samples), loop, true);
    rig.run(2048);
    Edits e;
    e.trimStart = 0.5; // frame 24000
    rig.engine.setEdits(e);
    const auto out = rig.run(1024);
    // The old position fades out, then the trim point fades in: a trim is
    // not an edge the sound was made with.
    CHECK(out[kFade] == Catch::Approx(samples[24000] / kFade));
    CHECK(out[2 * kFade + 10] == samples[24000 + kFade + 10]);
}

TEST_CASE("AuditionEngine matches loudness when asked", "[engine]")
{
    Rig rig;
    rig.engine.setGainMatch(true);
    SampleInfo info;
    info.lufs = -22.0; // 6 dB under the target
    const auto samples = counting(1000);
    rig.engine.select(rig.file("a.wav", samples), info, true);
    const auto out = rig.run(512);
    CHECK(out[100] == Catch::Approx(samples[100] * std::pow(10.0f, 6.0f / 20.0f)));
}

TEST_CASE("AuditionEngine syncs a loop to the host tempo and follows it", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 120.0;
    loop.bpmConfidence = 0.9;
    rig.transport.bpm = 90.0;
    rig.engine.select(rig.file("a.wav", test::sine(220.0, 2.0, 0.5, kRate)), loop, true);
    rig.run(1024);
    EngineStatus s = rig.engine.status();
    CHECK(s.tempoSynced);
    CHECK(s.ratio == Catch::Approx(0.75));
    rig.transport.bpm = 150.0;
    rig.run(512);
    CHECK(rig.engine.status().ratio == Catch::Approx(1.25));

    loop.bpmConfidence = 0.1; // a guess: plays as it is, with a "?"
    rig.engine.select(rig.file("b.wav", test::sine(220.0, 2.0, 0.5, kRate)), loop, true);
    rig.run(1024);
    s = rig.engine.status();
    CHECK_FALSE(s.tempoSynced);
    CHECK(s.tempoUnsure);
    CHECK(s.ratio == 1.0);
}

TEST_CASE("AuditionEngine starts on the next bar while the host plays", "[engine]")
{
    Rig rig;
    rig.engine.setQuantise(4.0);
    rig.transport = {120.0, 3.5, true}; // half a beat before bar 2: 0.25 s
    rig.engine.select(rig.file("a.wav", counting(48000)), {}, true);
    const auto out = rig.run(24000);
    CHECK(firstSound(out) == 12000);

    Rig stopped; // the host is not playing: no waiting
    stopped.engine.setQuantise(4.0);
    stopped.transport = {120.0, 3.5, false};
    stopped.engine.select(stopped.file("a.wav", counting(4800)), {}, true);
    CHECK(firstSound(stopped.run(1024)) == 0);
}

TEST_CASE("AuditionEngine plays MIDI notes on the selection and keeps it alive while they ring", "[engine]")
{
    Rig rig;
    const auto samples = counting(48000);
    rig.engine.select(rig.file("a.wav", samples), {}, false);
    rig.run(256);
    rig.engine.noteOn(60, 1.0f);
    auto out = rig.run(512);
    CHECK(out[200] == Catch::Approx(samples[200])); // past the 2 ms attack, at its own pitch
    CHECK(rig.engine.status().voices == 1);

    rig.engine.select(rig.file("b.wav", counting(4800)), {}, false);
    rig.run(512);
    CHECK(rig.engine.loader().liveCount() == 2); // a still rings
    rig.engine.noteOff(60);
    rig.run(8192); // release
    rig.engine.loader().pump();
    CHECK(rig.engine.loader().liveCount() == 1);
    CHECK(rig.engine.status().voices == 0);
}

TEST_CASE("AuditionEngine never allocates on the audio thread", "[engine]")
{
    Rig rig;
    rig.engine.setGainMatch(true);
    SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 100.0;
    loop.bpmConfidence = 0.9;
    loop.key = "Am";
    loop.keyConfidence = 0.9;
    SyncSettings sync;
    sync.key = true;
    sync.projectKey = "C#m";
    rig.engine.setSync(sync);
    rig.engine.setQuantise(1.0);
    rig.transport = {120.0, 0.25, true};
    const auto a = rig.file("a.wav", test::sine(220.0, 3.0, 0.5, kRate));
    const auto longFile = rig.file("long.wav", test::sine(330.0, 12.0, 0.5, kRate)); // streams

    std::vector<float> l(kBlock), r(kBlock);
    float* out[] = {l.data(), r.data()};
    std::size_t allocations = 0;
    const auto block = [&] {
        rig.engine.loader().pump(); // the loader thread's work, not counted
        rig.engine.setTransport(rig.transport);
        {
            const test::CountAllocations counter;
            rig.engine.process(out, kBlock);
            allocations += counter.count();
        }
        rig.transport.ppq += kBlock * rig.transport.bpm / 60.0 / kRate;
    };

    rig.engine.select(a, loop, true);
    for (int i = 0; i < 200; ++i) block();
    rig.engine.noteOn(64, 0.8f);
    rig.engine.noteOn(67, 0.8f);
    for (int i = 0; i < 50; ++i) block();
    Edits e;
    e.direction = Direction::PingPong;
    e.trimStart = 0.1;
    rig.engine.setEdits(e);
    for (int i = 0; i < 100; ++i) block();
    rig.transport.bpm = 97.0;
    for (int i = 0; i < 50; ++i) block();
    rig.engine.select(longFile, {}, true);
    for (int i = 0; i < 400; ++i) block();
    rig.engine.stop();
    rig.engine.noteOff(64);
    rig.engine.noteOff(67);
    for (int i = 0; i < 100; ++i) block();
    CHECK(allocations == 0);
    CHECK(rig.engine.status().voices == 0);
}

TEST_CASE("AuditionEngine keeps a stop pressed while an auto-play selection loads", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
    rig.run(1024);
    const auto b = rig.file("b.wav", counting(4800, 0.5f));
    rig.engine.select(b, loop, true); // arrowed onto b...
    rig.engine.stop();                // ...and stopped before b arrived
    const auto out = rig.run(2048);
    CHECK_FALSE(rig.engine.status().playing);
    CHECK(out[kFade + 10] == 0.0f);
    rig.engine.play(); // b is still the selection
    CHECK(rig.run(512)[0] == Catch::Approx(0.50001f));
}

TEST_CASE("AuditionEngine takes blocks larger than it was prepared for", "[engine]")
{
    Rig rig;
    const auto samples = counting(4800);
    rig.engine.select(rig.file("a.wav", samples), {}, true);
    rig.engine.loader().pump();
    std::vector<float> l(1024), r(1024);
    float* out[] = {l.data(), r.data()};
    rig.engine.process(out, 1024); // prepared for 256
    CHECK(l[0] == samples[0]);
    CHECK(l[1023] == samples[1023]);
    CHECK(r[700] == samples[700]);
}

TEST_CASE("AuditionEngine follows a manual tempo change without restarting", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 100.0;
    loop.bpmConfidence = 0.9;
    SyncSettings sync;
    sync.hostBpm = 120.0; // the standalone's tempo field, no transport
    rig.engine.setSync(sync);
    rig.engine.select(rig.file("a.wav", test::sine(220.0, 4.0, 0.5, kRate)), loop, true);
    rig.run(9600);
    const double before = rig.engine.status().position;
    CHECK(rig.engine.status().ratio == Catch::Approx(1.2));
    sync.hostBpm = 110.0;
    rig.engine.setSync(sync);
    const auto out = rig.run(512);
    CHECK(rig.engine.status().position > before); // kept going
    CHECK(rig.engine.status().ratio == Catch::Approx(1.1));
    CHECK(std::abs(out[kFade]) + std::abs(out[kFade + 1]) + std::abs(out[kFade + 2]) > 0.0f); // no stop fade
}

TEST_CASE("AuditionEngine silences the old sample when auto-play lands on a broken file", "[engine]")
{
    Rig rig;
    SampleInfo loop;
    loop.isLoop = true;
    rig.engine.select(rig.file("a.wav", counting(4800)), loop, true);
    rig.run(1024);
    const auto bad = rig.dir.path() / "bad.wav";
    test::writeBytes(bad, "RIFF\x04\x00\x00\x00WAVEjunk");
    rig.engine.select(bad, {}, true);
    const auto out = rig.run(1024);
    CHECK(out[kFade + 10] == 0.0f);
    CHECK_FALSE(rig.engine.status().playing);
}

TEST_CASE("AuditionEngine waits for a trimmed start deep in a long file to load", "[engine]")
{
    Rig rig;
    const auto longFile = rig.dir.path() / "long.wav";
    test::writeWavFloat(longFile, 1000, {counting(1000000)}); // 1000 s at 1 kHz: streams
    Edits e;
    e.trimStart = 800.0; // past the 10 s kept in memory and the first read-ahead
    rig.engine.setEdits(e);
    rig.engine.select(longFile, {}, true);
    const auto out = rig.run(4096);
    const std::size_t first = firstSound(out);
    REQUIRE(first < out.size());
    // It starts with its fade-in (1/5 of the level on the first frame), not
    // cutting in once the block turns up.
    CHECK(out[first] < counting(800001)[800000] / 2.0f);
}
