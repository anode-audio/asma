// SPDX-License-Identifier: GPL-3.0-only
// Accuracy gate on generated audio. The corpus is synthetic, so it is exact
// and contains no third-party material. A change that makes any of these
// worse fails CI. Real-library numbers come from the hidden [.real] test.
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

using namespace asma;

TEST_CASE("Tempo: every generated loop from 70 to 174 BPM, 2 and 4 bars", "[accuracy]")
{
    const int rate = 44100;
    int correct = 0;
    int total = 0;
    for (double bpm : {70.0, 80.0, 85.0, 90.0, 95.0, 100.0, 110.0, 120.0, 125.0, 128.0, 135.0, 140.0, 150.0, 160.0,
                       170.0, 174.0}) {
        for (int bars : {2, 4}) {
            DecodedAudio a;
            a.sampleRate = rate;
            a.mono = test::drumLoop(bpm, bars, rate);
            const AnalysisResult r = analyse(a);
            ++total;
            if (r.bpm && std::abs(*r.bpm - bpm) <= 0.5 && r.isLoop == true) ++correct;
            else UNSCOPED_INFO("missed " << bpm << " BPM, " << bars << " bars: got " << r.bpm.value_or(0.0));
        }
    }
    CHECK(correct == total);
}

TEST_CASE("Key: all 24 major and minor progressions", "[accuracy]")
{
    const int rate = 44100;
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int correct = 0;
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (bool minor : {false, true}) {
            const std::string want = std::string(names[tonic]) + (minor ? "m" : "");
            const auto key = estimateKey(test::chordProgression(tonic, minor, rate), rate);
            if (key && key->key == want) ++correct;
            else UNSCOPED_INFO("missed " << want << ": got " << (key ? key->key : "-"));
        }
    }
    CHECK(correct == 24);
}
