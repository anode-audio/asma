// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma::app;

namespace {

juce::Image render(juce::Component& c)
{
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType{});
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
    return image;
}

} // namespace

TEST_CASE("a segmented switch keeps one choice and reports the user's", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SegmentedControl s({"Auto", "On", "Off"}, false);
    s.setBounds(0, 0, 150, 28);
    int changed = -1, calls = 0;
    s.onChange = [&](int i) {
        changed = i;
        ++calls;
    };
    CHECK(s.selected() == 0);
    s.segment(2).triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // clicks arrive through the message loop
    CHECK(changed == 2);
    CHECK(s.selected() == 2);
    CHECK_FALSE(s.segment(0).getToggleState());
    s.setSelected(1, juce::dontSendNotification); // from saved state: no echo
    CHECK(calls == 1);
    CHECK(s.segment(1).getToggleState());
    CHECK_FALSE(s.segment(2).getToggleState());
}

TEST_CASE("a segmented switch tells screen readers what its arrows mean", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SegmentedControl s({"→", "←", "↔"}, true, {"Forward", "Reverse", "Ping-pong"});
    CHECK(s.segment(1).getTitle() == "Reverse");
}

TEST_CASE("an accent switch fills its choice amber", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaLookAndFeel lnf;
    SegmentedControl s({"A", "B", "C"}, true);
    s.setLookAndFeel(&lnf);
    s.setBounds(0, 0, 120, 28);
    s.setSelected(1, juce::dontSendNotification);
    const auto image = render(s);
    CHECK(image.getPixelAt(46, 6) == theme::amber);
    CHECK(image.getPixelAt(10, 6).getAlpha() == 0);
    s.setLookAndFeel(nullptr);
}

TEST_CASE("a chip shows its dot and its detail", "[controls]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ChipButton chip("Tempo");
    chip.setClickingTogglesState(true);
    chip.setDetail("120 -> 180", theme::green);
    chip.setDot(theme::green, true);
    chip.setToggleState(true, juce::dontSendNotification);
    chip.setBounds(0, 0, chip.idealWidth(), 28);
    CHECK(chip.idealWidth() > 100);
    const auto on = render(chip);
    CHECK(on.getPixelAt(14, 14) == theme::green); // the dot's centre
    CHECK(on.getPixelAt(chip.getWidth() - 4, 4) == theme::raised);
    chip.setToggleState(false, juce::dontSendNotification);
    chip.setDot(theme::green, false);
    const auto off = render(chip);
    CHECK(off.getPixelAt(14, 14).getAlpha() == 0); // a ring: hollow
    CHECK(off.getPixelAt(chip.getWidth() - 4, 4).getAlpha() == 0);
}
