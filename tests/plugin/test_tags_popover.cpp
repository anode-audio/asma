// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/TagsPopover.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::TagsPopover;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

struct Rig {
    const juce::ScopedJuceInitialiser_GUI gui;
    std::vector<std::pair<std::string, bool>> changes;
    TagsPopover popover{"Bass_Loop_Am_120.wav",
                        {{"dark", true}, {"bass", false}, {"synth", false}},
                        {{"bass", 214}, {"gritty", 31}, {"groove", 9}, {"dark", 4}, {"grime", 2}},
                        [this](const std::string& tag, bool added) { changes.emplace_back(tag, added); }};
    void type(const char* text)
    {
        popover.field().setText(text, true);
        settle(); // the field reports changes later
    }
    void pressReturn()
    {
        popover.field().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        settle();
    }
};

} // namespace

TEST_CASE("the user's tags can be removed, the analyser's cannot", "[tags]")
{
    Rig rig;
    REQUIRE(rig.popover.chipCount() == 3);
    CHECK(rig.popover.chipText(0) == "dark");
    CHECK(rig.popover.isRemovable(0));
    CHECK_FALSE(rig.popover.isRemovable(1));
    CHECK(rig.popover.removeButton(1) == nullptr);
    rig.popover.removeButton(0)->triggerClick();
    settle();
    CHECK(rig.changes == std::vector<std::pair<std::string, bool>>{{"dark", false}});
    CHECK(rig.popover.chipCount() == 2);
}

TEST_CASE("typing offers the library's tags that start with it, not ones the sample has", "[tags]")
{
    Rig rig;
    rig.type("Gr");
    CHECK(rig.popover.suggestions() == juce::StringArray{"gritty", "groove", "grime"});
    rig.type("d");
    CHECK(rig.popover.suggestions().isEmpty()); // "dark" is on it already
    rig.type("gro");
    rig.popover.suggestion(0).triggerClick();
    settle();
    CHECK(rig.changes == std::vector<std::pair<std::string, bool>>{{"groove", true}});
    CHECK(rig.popover.field().isEmpty());
}

TEST_CASE("Return adds the tag, trimmed and lower case, and never one it has", "[tags]")
{
    Rig rig;
    rig.type("  Warm ");
    rig.pressReturn();
    rig.type(" Bass");
    rig.pressReturn();
    rig.type("   ");
    rig.pressReturn();
    CHECK(rig.changes == std::vector<std::pair<std::string, bool>>{{"warm", true}});
    CHECK(rig.popover.chipCount() == 4);
    CHECK(rig.popover.chipText(1) == "warm"); // the user's together, first
}
