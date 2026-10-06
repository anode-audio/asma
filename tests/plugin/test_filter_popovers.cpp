// SPDX-License-Identifier: GPL-3.0-only
#include "ui/FilterPopovers.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::app;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

// Collects what a popover reports.
struct Sink {
    SearchModel last;
    int changes = 0;
    std::function<void(const SearchModel&)> fn()
    {
        return [this](const SearchModel& m) {
            last = m;
            ++changes;
        };
    }
};

void type(juce::TextEditor& field, const char* text)
{
    field.setText(text, true);
    settle();
}

} // namespace

TEST_CASE("the Type popover switches between any, loops and one-shots", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    SearchModel m;
    m.text = "bass";
    TypePopover p(m, sink.fn());
    p.choice().segment(1).triggerClick();
    settle();
    CHECK(sink.last.type == SampleType::Loop);
    CHECK(sink.last.text == "bass"); // only the filter changes
    p.choice().segment(2).triggerClick();
    settle();
    CHECK(sink.last.type == SampleType::OneShot);
}

TEST_CASE("the BPM popover takes a range, or one near a tempo", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    BpmPopover p({}, 124.0, sink.fn());
    type(p.from(), "118");
    type(p.to(), "132.5");
    CHECK(sink.last.bpmMin == 118.0);
    CHECK(sink.last.bpmMax == 132.5);
    type(p.from(), "");
    CHECK_FALSE(sink.last.bpmMin); // an empty end is open
    type(p.to(), "fast");
    CHECK_FALSE(sink.last.bpmMax); // not a number: no limit, not 0
    p.nearTempo().triggerClick();
    settle();
    CHECK(sink.last.bpmMin == 120.0); // within 3% of 124
    CHECK(sink.last.bpmMax == 128.0);
    CHECK(p.from().getText() == "120");
    BpmPopover none({}, 0.0, sink.fn());
    CHECK_FALSE(none.nearTempo().isEnabled()); // no tempo to be near
}

TEST_CASE("the Key popover picks any number of keys, in the order picked", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    KeyPopover p({}, sink.fn());
    REQUIRE(p.keyCount() == 24);
    p.key(21).triggerClick(); // Am
    settle();
    p.key(0).triggerClick();  // C
    settle();
    CHECK(sink.last.keys == std::vector<std::string>{"Am", "C"});
    p.key(21).triggerClick(); // Am again: off
    settle();
    CHECK(sink.last.keys == std::vector<std::string>{"C"});
    SearchModel saved;
    saved.keys = {"Dm"};
    KeyPopover shown(saved, sink.fn());
    CHECK(shown.key(14).getToggleState()); // a saved pick shows lit
}

TEST_CASE("the Instrument popover lists the library's tags and ticks what is picked", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    SearchModel m;
    m.tags = {"kick"};
    InstrumentPopover p(m, {{"bass", 214}, {"kick", 188}, {"synth", 96}}, sink.fn());
    REQUIRE(p.tagCount() == 3);
    CHECK(p.tag(1).getToggleState());
    CHECK(p.countText(0) == "214");
    p.tag(0).triggerClick();
    settle();
    CHECK(sink.last.tags == std::vector<std::string>{"kick", "bass"}); // all of them must match
    p.tag(1).triggerClick();
    settle();
    CHECK(sink.last.tags == std::vector<std::string>{"bass"});
    InstrumentPopover empty({}, {}, sink.fn());
    CHECK(empty.tagCount() == 0);
    CHECK(empty.emptyText().isNotEmpty()); // says there are none yet
}

TEST_CASE("the Length popover takes a preset or a range", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    LengthPopover p({}, sink.fn());
    p.preset(1).triggerClick(); // 1-10 s
    settle();
    CHECK(sink.last.durationMin == 1.0);
    CHECK(sink.last.durationMax == 10.0);
    CHECK(p.preset(1).getToggleState());
    type(p.to(), "4");
    CHECK(sink.last.durationMax == 4.0);
    CHECK_FALSE(p.preset(1).getToggleState()); // no longer the preset
    p.preset(0).triggerClick(); // under 1 s
    settle();
    CHECK_FALSE(sink.last.durationMin);
    CHECK(sink.last.durationMax == 1.0);
}

TEST_CASE("the Rating popover sets a minimum, and the same star again clears it", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    RatingPopover p({}, sink.fn());
    p.star(2).triggerClick(); // three stars
    settle();
    CHECK(sink.last.minRating == 3);
    CHECK(p.star(2).getToggleState());
    CHECK_FALSE(p.star(3).getToggleState());
    p.star(2).triggerClick();
    settle();
    CHECK_FALSE(sink.last.minRating);
}

TEST_CASE("makeFilterPopover makes the right panel for each chip", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    for (const Facet f : kFacets) {
        auto p = makeFilterPopover(f, {}, {}, [](const SearchModel&) {});
        REQUIRE(p);
        CHECK(p->getWidth() > 0);
        CHECK(p->getHeight() > 0);
    }
    CHECK(dynamic_cast<KeyPopover*>(makeFilterPopover(Facet::Key, {}, {}, [](const SearchModel&) {}).get()));
}

TEST_CASE("a popover's title says how its picks combine, as the design does", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto none = [](const SearchModel&) {};
    CHECK(KeyPopover({}, none).hint() == "any of");
    CHECK(InstrumentPopover({}, {}, none).hint() == "all of");
    CHECK(RatingPopover({}, none).hint() == "at least");
    CHECK(TypePopover({}, none).hint().isEmpty());
    KeyPopover keys({}, none);
    CHECK(keys.key(0).getProperties()["asma.mono"]); // keys read as names, in the mono face
}

TEST_CASE("a range typed backwards means the same range", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    BpmPopover bpm({}, 0.0, sink.fn());
    type(bpm.from(), "130");
    type(bpm.to(), "120");
    CHECK(sink.last.bpmMin == 120.0); // not an empty range
    CHECK(sink.last.bpmMax == 130.0);
    LengthPopover length({}, sink.fn());
    type(length.from(), "10");
    type(length.to(), "2");
    CHECK(sink.last.durationMin == 2.0);
    CHECK(sink.last.durationMax == 10.0);
}

TEST_CASE("a library with many tags scrolls in the Instrument popover", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    std::vector<TagCount> many;
    for (int i = 0; i < 40; ++i) many.push_back({"tag" + std::to_string(i), 40 - i});
    InstrumentPopover p({}, many, [](const SearchModel&) {});
    InstrumentPopover eight({}, std::vector<TagCount>(many.begin(), many.begin() + 8), [](const SearchModel&) {});
    CHECK(p.getHeight() == eight.getHeight()); // no taller than eight rows
    CHECK(p.tagCount() == 40);                 // every tag is there to scroll to
}

TEST_CASE("a field holding only a point is no limit, never 0", "[popovers]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Sink sink;
    BpmPopover p({}, 0.0, sink.fn());
    type(p.to(), "."); // on the way to typing ".5"
    CHECK_FALSE(sink.last.bpmMax);
    type(p.to(), ".5");
    CHECK(sink.last.bpmMax == 0.5);
}
