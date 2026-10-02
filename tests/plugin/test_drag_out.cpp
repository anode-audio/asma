// SPDX-License-Identifier: GPL-3.0-only
#include "DragOut.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::dragSettings;
using app::dragSummary;

TEST_CASE("a drag-out carries the selection's edits and what sync does now", "[drag]")
{
    audio::Edits e;
    e.trimStart = 0.5;
    e.loop = audio::LoopMode::On;
    audio::SyncPlan plan;
    plan.ratio = 1.5;
    plan.semitones = -2.0;
    const audio::RenderSettings s = dragSettings(e, plan, 48000);
    CHECK(s.edits.trimStart == 0.5);
    CHECK(s.ratio == 1.5);
    CHECK(s.semitones == -2.0);
    CHECK(s.sampleRate == 48000);
}

TEST_CASE("the footer says what a drag-out will carry", "[drag]")
{
    audio::RenderSettings s;
    CHECK(dragSummary(s, 120.0) == "Drag out: the original file");
    s.sampleRate = 96000; // the DAW converts on import: not worth a render
    CHECK(dragSummary(s, 120.0) == "Drag out: the original file");
    s.edits.loop = audio::LoopMode::On; // looping is not baked in
    CHECK(dragSummary(s, 120.0) == "Drag out: the original file");

    s.edits.direction = audio::Direction::Reverse;
    s.edits.trimEnd = 6.8;
    s.ratio = 1.5;
    CHECK(dragSummary(s, 120.0) == "Drag out renders: reversed, trimmed, stretched to 180 BPM");
    CHECK(dragSummary(s, std::nullopt) == "Drag out renders: reversed, trimmed, stretched x1.50");

    audio::RenderSettings t;
    t.edits.direction = audio::Direction::PingPong;
    t.semitones = 2.0;
    CHECK(dragSummary(t, 120.0) == "Drag out renders: ping-pong, transposed +2");
    t.semitones = -3.0;
    CHECK(dragSummary(t, 120.0) == "Drag out renders: ping-pong, transposed -3");
}
