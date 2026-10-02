// SPDX-License-Identifier: GPL-3.0-only
#include "TempoChip.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::ChipText;
using app::Tone;
using app::tempoChip;

namespace {

audio::SampleInfo loop(double bpm, double confidence = 0.9)
{
    audio::SampleInfo s;
    s.bpm = bpm;
    s.bpmConfidence = confidence;
    s.isLoop = true;
    return s;
}

audio::SyncSettings at(double bpm)
{
    audio::SyncSettings s;
    s.hostBpm = bpm;
    return s;
}

void check(const ChipText& chip, const char* text, Tone tone)
{
    CHECK(chip.text == text);
    CHECK(chip.tone == tone);
}

} // namespace

TEST_CASE("the Tempo chip says what sync does, and why when it does nothing", "[chip]")
{
    check(tempoChip(loop(120), at(180), false), "120 → 180 · x1.50", Tone::Synced);
    check(tempoChip(loop(120), at(20), false), "120 → 30 · x0.25 max", Tone::Warning);
    check(tempoChip(loop(60), at(300), false), "60 → 240 · x4.00 max", Tone::Warning);
    check(tempoChip(loop(97.3, 0.21), at(120), false), "~97 ? · plays as is", Tone::Warning);

    audio::SampleInfo kick;
    kick.isLoop = false;
    check(tempoChip(kick, at(120), false), "one-shot · plays as is", Tone::Muted);
    check(tempoChip(audio::SampleInfo{}, at(120), false), "not a loop · plays as is", Tone::Muted); // a long stem

    audio::SyncSettings off = at(120);
    off.tempo = false;
    check(tempoChip(loop(120), off, false), "off", Tone::Muted);
    check(tempoChip(loop(120), at(0), false), "no tempo · plays as is", Tone::Muted); // a host that sends none
    check(tempoChip(loop(120), at(120), true), "can't read this file", Tone::Warning);
}

TEST_CASE("a loop the library knows no tempo for is a guess with nothing to show", "[chip]")
{
    audio::SampleInfo s;
    s.isLoop = true;
    check(tempoChip(s, at(120), false), "? · plays as is", Tone::Warning);
}

TEST_CASE("tempos and ratios read the same in every locale", "[chip]")
{
    CHECK(app::bpmText(120.0) == "120");
    CHECK(app::bpmText(97.46) == "97.5");
    CHECK(app::bpmText(127.96) == "128");
    CHECK(app::ratioText(1.5) == "1.50");
    CHECK(app::ratioText(0.25) == "0.25");
    CHECK(app::ratioText(1.0 / 3.0) == "0.33");
    CHECK(app::ratioText(1.006) == "1.01"); // rounds, never truncates
}
