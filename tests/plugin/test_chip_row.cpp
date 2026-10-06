// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ChipRow.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::ChipRow;
using app::Facet;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

SearchModel loopsAt120()
{
    SearchModel m;
    m.text = "bass";
    m.type = SampleType::Loop;
    m.bpmMin = 118.0;
    m.bpmMax = 132.0;
    return m;
}

} // namespace

TEST_CASE("the chip row labels each chip from the search", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44);
    row.setModel(SearchModel{});
    CHECK(row.chip(Facet::Bpm).label() == "BPM");
    CHECK_FALSE(row.chip(Facet::Bpm).isActive());
    CHECK_FALSE(row.clearAllButton().isVisible());
    row.setModel(loopsAt120());
    CHECK(row.chip(Facet::Type).label() == "Loops");
    CHECK(row.chip(Facet::Bpm).label() == juce::String::fromUTF8("118–132 BPM"));
    CHECK(row.chip(Facet::Bpm).isActive());
    CHECK(row.chip(Facet::Bpm).clearButton().isVisible());
    CHECK_FALSE(row.chip(Facet::Key).clearButton().isVisible());
    CHECK(row.clearAllButton().isVisible());
}

TEST_CASE("a chip's x clears its filter; Clear all clears them all", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44);
    row.setModel(loopsAt120());
    SearchModel last;
    int changes = 0;
    row.onChange = [&](const SearchModel& m) {
        last = m;
        ++changes;
    };
    row.chip(Facet::Bpm).clearButton().triggerClick();
    settle();
    CHECK(changes == 1);
    CHECK_FALSE(last.bpmMin);
    CHECK(last.type == SampleType::Loop); // only that one
    CHECK(last.text == "bass");
    row.clearAllButton().triggerClick();
    settle();
    CHECK(last.type == SampleType::Any);
    CHECK(last.text == "bass"); // the text is not a chip
}

TEST_CASE("clicking a chip asks for its popover", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44);
    row.setModel(SearchModel{});
    std::optional<Facet> opened;
    row.onOpen = [&](Facet f, juce::Component&) { opened = f; };
    row.chip(Facet::Key).mainButton().triggerClick();
    settle();
    CHECK(opened == Facet::Key);
}

TEST_CASE("the chips fit the row at the narrowest window", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 680, 44); // a 900 px window less the 220 px sidebar
    SearchModel busy = loopsAt120();
    busy.keys = {"Am", "C", "Dm", "F"};
    busy.tags = {"bass", "synth"};
    busy.durationMin = 1.0;
    busy.durationMax = 10.0;
    busy.minRating = 3;
    row.setModel(busy);
    for (const Facet f : app::kFacets) CHECK(row.getLocalBounds().contains(row.chip(f).getBounds()));
}

TEST_CASE("a set chip is wide enough for its whole label beside its x", "[chips]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipRow row;
    row.setBounds(0, 0, 1060, 44); // room to spare
    row.setModel(loopsAt120());
    auto& chip = row.chip(Facet::Bpm);
    CHECK(chip.getWidth() == chip.idealWidth());
    CHECK(chip.textArea().getRight() <= chip.clearButton().getX()); // the label stops before the x
    CHECK(chip.textArea().getWidth() >= chip.labelWidth());         // and is not cut short
}
