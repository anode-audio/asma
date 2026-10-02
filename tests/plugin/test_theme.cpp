// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma::app;

TEST_CASE("the theme's fonts are the embedded Anode faces", "[theme]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    CHECK(theme::typeface(theme::Face::Heading)->getName() == "Space Grotesk");
    CHECK(theme::typeface(theme::Face::Text)->getName() == "Inter");
    CHECK(theme::typeface(theme::Face::Mono)->getName() == "JetBrains Mono");
    CHECK(theme::font(theme::Face::Text, 13.0f).getTypefacePtr() == theme::typeface(theme::Face::Text));
}

TEST_CASE("plain fonts come out in Inter, not a system face", "[theme]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaLookAndFeel lnf;
    CHECK(lnf.getTypefaceForFont(juce::Font(juce::FontOptions(14.0f)))->getName() == "Inter");
    CHECK(lnf.getTypefaceForFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, 0)))
              ->getName()
          == "JetBrains Mono");
}

TEST_CASE("a pressed accent segment is amber, a pressed plain one is lifted", "[theme]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaLookAndFeel lnf;
    juce::TextButton b("x");
    b.setLookAndFeel(&lnf);
    b.setBounds(0, 0, 40, 28);
    b.setClickingTogglesState(true);
    b.getProperties().set("asma.segment", "middle");
    const auto colourAt = [&](int x, int y) {
        juce::Image image(juce::Image::ARGB, 40, 28, true, juce::SoftwareImageType{});
        juce::Graphics g(image);
        b.paintEntireComponent(g, false);
        return image.getPixelAt(x, y);
    };
    CHECK(colourAt(30, 4).getAlpha() == 0); // off: transparent, the switch's ground shows
    b.setToggleState(true, juce::dontSendNotification);
    CHECK(colourAt(30, 4) == theme::raised);
    b.getProperties().set("asma.accent", true);
    CHECK(colourAt(30, 4) == theme::amber);
    CHECK(colourAt(0, 4) == theme::border); // the line before a middle segment
    b.setLookAndFeel(nullptr);
}

TEST_CASE("the fonts go when JUCE shuts down, not after it", "[theme]")
{
    // A plugin's statics outlive JUCE in the host; fonts held past shutdown
    // crash some hosts as they unload the plugin.
    const juce::Typeface* first = nullptr;
    {
        const juce::ScopedJuceInitialiser_GUI gui;
        first = theme::typeface(theme::Face::Text).get();
    }
    const juce::ScopedJuceInitialiser_GUI gui;
    CHECK(theme::typeface(theme::Face::Text).get() != first); // made afresh
    CHECK(theme::typeface(theme::Face::Text)->getName() == "Inter");
}
