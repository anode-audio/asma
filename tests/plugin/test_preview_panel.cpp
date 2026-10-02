// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Footer.h"
#include "ui/PreviewPanel.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::Footer;
using app::PreviewPanel;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

std::shared_ptr<const audio::Overview> eightSeconds()
{
    audio::AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(8000, 0.5f)};
    return std::make_shared<const audio::Overview>(audio::makeOverview(b));
}

struct Rig {
    const juce::ScopedJuceInitialiser_GUI gui;
    PreviewPanel panel;
    std::vector<audio::Edits> edits;
    Rig()
    {
        panel.setBounds(0, 0, 964, 236);
        panel.setFile("Bass_Loop_Am_120.wav", "Loops");
        panel.waveform().setOverview(eightSeconds());
        panel.onEditsChanged = [this](const audio::Edits& e) { edits.push_back(e); };
    }
};

} // namespace

TEST_CASE("the preview names the file's folder, rate, channels and length", "[preview]")
{
    CHECK(PreviewPanel::fileLine("Loops/Bass", 44100, 2, 8.0) == juce::String::fromUTF8("Loops / Bass · 44.1 kHz · stereo · 8.00 s"));
    CHECK(PreviewPanel::fileLine("", 48000, 1, 0.0) == juce::String::fromUTF8("48 kHz · mono"));
    CHECK(PreviewPanel::fileLine("Kicks", 0, 0, 0.25) == juce::String::fromUTF8("Kicks · 0.25 s"));
}

TEST_CASE("the direction switch, the loop switch and the trim handles all edit", "[preview]")
{
    Rig rig;
    CHECK_FALSE(rig.panel.resetButton().isEnabled()); // nothing to reset
    rig.panel.direction().segment(1).triggerClick();
    settle();
    REQUIRE(rig.edits.size() == 1);
    CHECK(rig.edits.back().direction == audio::Direction::Reverse);
    rig.panel.loop().segment(2).triggerClick();
    settle();
    CHECK(rig.edits.back().loop == audio::LoopMode::Off);
    CHECK(rig.edits.back().direction == audio::Direction::Reverse); // the earlier edit stays
    auto& wave = rig.panel.waveform();
    wave.press(wave.xFor(0.0) + 1.0f);
    wave.drag(wave.xFor(1.0));
    wave.release();
    CHECK(rig.edits.back().trimStart > 0.9);
    CHECK(rig.edits.back().trimStart < 1.1);
    CHECK(rig.panel.resetButton().isEnabled());
    rig.panel.resetButton().triggerClick();
    settle();
    CHECK(rig.edits.back().direction == audio::Direction::Forward);
    CHECK(rig.edits.back().trimStart == 0.0);
    CHECK(rig.panel.direction().selected() == 0);
}

TEST_CASE("edits from the project show without being sent back", "[preview]")
{
    Rig rig;
    audio::Edits e;
    e.direction = audio::Direction::PingPong;
    e.trimEnd = 6.8;
    rig.panel.setEdits(e);
    CHECK(rig.edits.empty());
    CHECK(rig.panel.direction().selected() == 2);
    CHECK(rig.panel.waveform().trimEnd() == 6.8);
    CHECK(rig.panel.resetButton().isEnabled());
}

TEST_CASE("the chips show what sync does and switch it", "[preview]")
{
    Rig rig;
    rig.panel.setTempo(true, {"120 → 180 · x1.50", app::Tone::Synced});
    CHECK(rig.panel.tempoChip().detail() == juce::String::fromUTF8("120 → 180 · x1.50"));
    CHECK(rig.panel.tempoChip().getToggleState());
    bool tempo = true;
    rig.panel.onTempoSync = [&](bool on) { tempo = on; };
    rig.panel.tempoChip().triggerClick();
    settle();
    CHECK_FALSE(tempo);

    std::optional<std::string> key = "unset";
    rig.panel.onKeySync = [&](std::optional<std::string> k) { key = k; };
    rig.panel.chooseKey(22); // the 22nd key: Am
    CHECK(key == "Am");
    CHECK(rig.panel.keyChip().detail() == "Am");
    rig.panel.setKey(true, "Am", "+2");
    CHECK(rig.panel.keyChip().detail() == "Am +2");
    rig.panel.chooseKey(0);
    CHECK_FALSE(key);
    CHECK(rig.panel.keyChip().detail() == "off");

    std::vector<double> starts;
    rig.panel.onQuantise = [&](double b) { starts.push_back(b); };
    for (int i = 0; i < 3; ++i) {
        rig.panel.startChip().triggerClick();
        settle();
    }
    CHECK(starts == std::vector<double>{1.0, 4.0, 0.0}); // beat, bar, at once
    CHECK(rig.panel.startChip().detail() == "now");
}

TEST_CASE("the controls fit the panel at every width the window allows", "[preview]")
{
    Rig rig;
    rig.panel.setTempo(true, {"120 → 180 · x1.50", app::Tone::Synced});
    for (const int width : {964, 800, 580}) { // 1280 and 1100 wide windows, and the 900 minimum
        rig.panel.setBounds(0, 0, width, 236);
        const auto inside = rig.panel.getLocalBounds();
        for (auto* c : rig.panel.getChildren()) {
            CHECK(inside.contains(c->getBounds()));
        }
        CHECK(rig.panel.waveform().getHeight() >= 60);
        CHECK(rig.panel.startChip().getBottom() <= 236 - 12);
    }
}

TEST_CASE("the play button and a click on the waveform both play", "[preview]")
{
    Rig rig;
    int plays = 0;
    rig.panel.onPlayStop = [&] { ++plays; };
    rig.panel.playButton().triggerClick();
    settle();
    rig.panel.waveform().press(400.0f);
    rig.panel.waveform().release();
    CHECK(plays == 2);
}

TEST_CASE("the footer gives sizes as a person reads them", "[footer]")
{
    CHECK(Footer::sizeText(0) == "0 B");
    CHECK(Footer::sizeText(820) == "820 B");
    CHECK(Footer::sizeText(820'000) == "820 KB");
    CHECK(Footer::sizeText(41'200'000) == "41 MB");
    CHECK(Footer::sizeText(1'240'000'000) == "1.2 GB");
    CHECK(Footer::sizeText(9'960) == "10 KB"); // rounds up into whole numbers
}

TEST_CASE("the footer offers to clear renders only when there are some", "[footer]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    Footer footer;
    footer.setBounds(0, 0, 1280, 26);
    footer.setStatus("Scan finished: 585 added");
    CHECK_FALSE(footer.clearButton().isVisible());
    CHECK(footer.rightText() == "Scan finished: 585 added");
    footer.setRenderBytes(41'000'000);
    CHECK(footer.clearButton().isVisible());
    CHECK(footer.rightText() == juce::String::fromUTF8("Scan finished: 585 added · renders 41 MB · "));
    int clears = 0;
    footer.onClearRenders = [&] { ++clears; };
    footer.clearButton().triggerClick();
    settle();
    CHECK(clears == 1);
}
