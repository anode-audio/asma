// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/Stretcher.h"
#include "asma/audio/Sync.h"
#include "asma/core/Library.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;

namespace {

SampleInfo loop(double bpm, double confidence = 0.9)
{
    SampleInfo s;
    s.bpm = bpm;
    s.bpmConfidence = confidence;
    s.isLoop = true;
    return s;
}

SyncSettings host(double bpm)
{
    SyncSettings s;
    s.hostBpm = bpm;
    return s;
}

} // namespace

TEST_CASE("planSync stretches loops to exactly the host tempo, within the stretcher's range", "[sync]")
{
    CHECK(planSync(loop(120), host(120)).ratio == 1.0);
    CHECK(planSync(loop(90), host(120)).ratio == Catch::Approx(4.0 / 3.0));
    CHECK(planSync(loop(70), host(140)).ratio == Catch::Approx(2.0));        // no half time
    CHECK(planSync(loop(120), host(180)).ratio == Catch::Approx(1.5));
    CHECK(planSync(loop(120), host(60)).ratio == Catch::Approx(0.5));
    const SyncPlan slow = planSync(loop(120), host(20));
    CHECK(slow.ratio == Catch::Approx(Stretcher::kMinRatio)); // what plays, not what was asked
    CHECK(slow.tempoClamped);
    const SyncPlan fast = planSync(loop(60), host(300));
    CHECK(fast.ratio == Catch::Approx(Stretcher::kMaxRatio));
    CHECK(fast.tempoClamped);
    CHECK_FALSE(planSync(loop(120), host(30)).tempoClamped); // exactly 0.25x is in range
    const SyncPlan p = planSync(loop(100), host(120));
    CHECK(p.tempoSynced);
    CHECK_FALSE(p.tempoUnsure);
    CHECK_FALSE(p.tempoClamped);
}

TEST_CASE("planSync leaves one-shots, unknown tempos and a switched-off sync alone", "[sync]")
{
    SampleInfo hit = loop(120);
    hit.isLoop = false;
    CHECK_FALSE(planSync(hit, host(100)).tempoSynced);
    CHECK_FALSE(planSync(hit, host(100)).tempoUnsure); // no "?" on a kick

    CHECK_FALSE(planSync(loop(120), host(0)).tempoSynced); // host tempo unknown

    SyncSettings off = host(100);
    off.tempo = false;
    CHECK(planSync(loop(120), off).ratio == 1.0);
}

TEST_CASE("planSync plays an uncertain tempo unmodified and says so", "[sync]")
{
    const SyncPlan guess = planSync(loop(120, kMinTempoConfidence - 0.01), host(100));
    CHECK(guess.ratio == 1.0);
    CHECK_FALSE(guess.tempoSynced);
    CHECK(guess.tempoUnsure);
    CHECK(planSync(loop(120, kMinTempoConfidence), host(100)).tempoSynced);

    SampleInfo noTempo;
    noTempo.isLoop = true;
    CHECK(planSync(noTempo, host(100)).tempoUnsure);
}

TEST_CASE("keyInterval takes the shortest way, to the relative key across modes", "[sync]")
{
    CHECK(keyInterval("C", "D") == 2);
    CHECK(keyInterval("C", "A#") == -2);
    CHECK(keyInterval("C", "F#") == -6); // a tritone goes down
    CHECK(keyInterval("F#", "C") == -6);
    CHECK(keyInterval("Am", "Dm") == 5);
    CHECK(keyInterval("Am", "C") == 0); // relative minor
    CHECK(keyInterval("C", "Am") == 0);
    CHECK(keyInterval("Em", "D") == -5); // D's relative minor is Bm
    CHECK(keyInterval("A", "Cm") == -6); // Cm's relative major is D#
    CHECK_FALSE(keyInterval("H", "C"));
    CHECK_FALSE(keyInterval("C", ""));
}

TEST_CASE("planSync transposes to the project key only when sure of the sample's key", "[sync]")
{
    SampleInfo s;
    s.key = "G";
    s.keyConfidence = 0.9;
    SyncSettings settings;
    settings.key = true;
    settings.projectKey = "A";
    SyncPlan p = planSync(s, settings);
    CHECK(p.keySynced);
    CHECK(p.semitones == 2.0);

    s.keyConfidence = kMinKeyConfidence - 0.01;
    p = planSync(s, settings);
    CHECK_FALSE(p.keySynced);
    CHECK(p.keyUnsure);
    CHECK(p.semitones == 0.0);

    SampleInfo drum; // no key at all: nothing to be unsure about
    CHECK_FALSE(planSync(drum, settings).keyUnsure);

    settings.key = false;
    s.keyConfidence = 0.9;
    CHECK(planSync(s, settings).semitones == 0.0);
}

TEST_CASE("framesToNextBoundary counts to the next beat or bar", "[sync]")
{
    CHECK(framesToNextBoundary(0.0, 120, 48000, 1) == 0);
    CHECK(framesToNextBoundary(0.5, 120, 48000, 1) == 12000); // half a beat at 120 is 0.25 s
    CHECK(framesToNextBoundary(3.5, 120, 48000, 4) == 12000);
    CHECK(framesToNextBoundary(4.0, 120, 48000, 4) == 0);
    CHECK(framesToNextBoundary(4.0000000001, 120, 48000, 4) == 0); // host rounding
    CHECK(framesToNextBoundary(1.0, 120, 48000, 4) == 72000);
    CHECK(framesToNextBoundary(-0.25, 120, 48000, 1) == 6000); // count-in before bar 1
}

TEST_CASE("sampleInfo reads what the library knows", "[sync]")
{
    TempDir dir;
    Db db = Db::openInMemory();
    Library lib(db);
    FileRecord f;
    f.rootId = lib.addRoot(dir.path());
    f.relPath = "Bass_Loop_Am_128.wav";
    f.contentHash = "00000000000000aa";
    f.format = "wav";
    const auto id = lib.insertFile(f);
    CHECK_FALSE(sampleInfo(lib, id).bpm);

    DerivedInfo d;
    d.bpm = 128.0;
    d.bpmConfidence = 0.9;
    d.key = "Am";
    d.keyConfidence = 0.9;
    d.isLoop = true;
    d.rootNote = 57;
    lib.setDerived(id, d);
    AnalysisResult r;
    r.loudness = {0.8, -12.0};
    r.featureVector.assign(kFeatureVectorSize, 0.0f);
    lib.setAnalysis(id, r);

    const SampleInfo s = sampleInfo(lib, id);
    CHECK(s.bpm == 128.0);
    CHECK(s.bpmConfidence == 0.9);
    CHECK(s.key == "Am");
    CHECK(s.isLoop == true);
    CHECK(s.rootNote == 57);
    CHECK(s.lufs == -12.0);
    CHECK(s.peak == 0.8);
}
