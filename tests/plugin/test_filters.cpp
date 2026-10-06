// SPDX-License-Identifier: GPL-3.0-only
#include "Filters.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::Facet;
using app::chipLabel;

TEST_CASE("an unset chip says what it filters", "[filters]")
{
    const SearchModel m;
    CHECK(chipLabel(Facet::Type, m) == "Type");
    CHECK(chipLabel(Facet::Bpm, m) == "BPM");
    CHECK(chipLabel(Facet::Key, m) == "Key");
    CHECK(chipLabel(Facet::Instrument, m) == "Instrument");
    CHECK(chipLabel(Facet::Length, m) == "Length");
    CHECK(chipLabel(Facet::Rating, m) == "Rating");
    for (const Facet f : app::kFacets) CHECK_FALSE(app::isSet(f, m));
}

TEST_CASE("a set chip says its value", "[filters]")
{
    SearchModel m;
    m.type = SampleType::Loop;
    CHECK(chipLabel(Facet::Type, m) == "Loops");
    m.type = SampleType::OneShot;
    CHECK(chipLabel(Facet::Type, m) == "One-shots");

    m.bpmMin = 118.0;
    m.bpmMax = 132.0;
    CHECK(chipLabel(Facet::Bpm, m) == "118–132 BPM");
    m.bpmMax.reset();
    CHECK(chipLabel(Facet::Bpm, m) == "from 118 BPM");
    m.bpmMin.reset();
    m.bpmMax = 97.5;
    CHECK(chipLabel(Facet::Bpm, m) == "up to 97.5 BPM");

    m.keys = {"Am", "C"};
    CHECK(chipLabel(Facet::Key, m) == "Am, C");
    m.keys = {"Am", "C", "Dm", "F", "G"};
    CHECK(chipLabel(Facet::Key, m) == "Am, C, Dm +2"); // a long pick stays short

    m.tags = {"bass", "synth"};
    CHECK(chipLabel(Facet::Instrument, m) == "bass, synth");

    m.durationMin = 1.0;
    m.durationMax = 10.0;
    CHECK(chipLabel(Facet::Length, m) == "1–10 s");
    m.durationMin.reset();
    CHECK(chipLabel(Facet::Length, m) == "under 10 s");
    m.durationMax.reset();
    m.durationMin = 60.0;
    CHECK(chipLabel(Facet::Length, m) == "over 60 s");

    m.minRating = 3;
    CHECK(chipLabel(Facet::Rating, m) == "★★★ and up");
    m.minRating = 5;
    CHECK(chipLabel(Facet::Rating, m) == "★★★★★");
    for (const Facet f : app::kFacets) CHECK(app::isSet(f, m));
}

TEST_CASE("clearing a chip clears its filter, and Clear all keeps the scope and text", "[filters]")
{
    SearchModel m;
    m.text = "bass";
    m.rootId = 2;
    m.sort = SortField::Bpm;
    m.type = SampleType::Loop;
    m.bpmMin = 100.0;
    m.keys = {"Am"};
    m.tags = {"bass"};
    m.durationMax = 4.0;
    m.minRating = 2;
    const SearchModel noKey = app::cleared(Facet::Key, m);
    CHECK(noKey.keys.empty());
    CHECK(noKey.bpmMin == 100.0);
    const SearchModel none = app::clearedAll(m);
    for (const Facet f : app::kFacets) CHECK_FALSE(app::isSet(f, none));
    CHECK(none.text == "bass");
    CHECK(none.rootId == 2);
    CHECK(none.sort == SortField::Bpm);
}

TEST_CASE("length presets and near-tempo ranges", "[filters]")
{
    REQUIRE(app::kLengthPresets.size() == 4);
    CHECK(app::kLengthPresets[0].label == std::string("Under 1 s"));
    CHECK_FALSE(app::kLengthPresets[0].min);
    CHECK(app::kLengthPresets[0].max == 1.0);
    CHECK(app::kLengthPresets[3].min == 60.0);
    CHECK_FALSE(app::kLengthPresets[3].max);
    const auto near = app::bpmNear(120.0); // within 3%, whole BPM
    CHECK(near.first == 116.0);
    CHECK(near.second == 124.0);
}
