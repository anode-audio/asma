// SPDX-License-Identifier: GPL-3.0-only
#include "ui/TopBar.h"

#include "TempoChip.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace asma::app {

TempoBox::TempoBox()
{
    minus_.setButtonText(juce::String::fromUTF8("−"));
    plus_.setButtonText("+");
    minus_.setTitle("Slower");
    plus_.setTitle("Faster");
    for (auto* b : {&minus_, &plus_}) {
        b->getProperties().set("asma.quiet", true);
        b->getProperties().set("asma.segment", "middle"); // no frame of its own: the box draws it
        addAndMakeVisible(b);
    }
    minus_.onClick = [this] { setValue(value_ - 1.0); };
    plus_.onClick = [this] { setValue(value_ + 1.0); };
    text_.setFont(theme::font(theme::Face::Mono, 13.0f));
    text_.setJustificationType(juce::Justification::centred);
    text_.setEditable(false, true, false);
    text_.setTitle("Tempo");
    text_.onTextChange = [this] { setValue(text_.getText().getDoubleValue()); };
    addAndMakeVisible(text_);
    setValue(value_, juce::dontSendNotification);
}

void TempoBox::setValue(double bpm, juce::NotificationType notification)
{
    const double v = std::round(std::clamp(bpm, kMin, kMax) * 10.0) / 10.0;
    const bool changed = v < value_ || v > value_;
    value_ = v;
    const long long tenths = std::llround(v * 10.0);
    text_.setText(juce::String(tenths / 10) + "." + juce::String(tenths % 10), juce::dontSendNotification);
    if (changed && notification != juce::dontSendNotification && onValueChange) onValueChange();
}

void TempoBox::resized()
{
    auto area = getLocalBounds();
    minus_.setBounds(area.removeFromLeft(26));
    plus_.setBounds(area.removeFromRight(26));
    area.removeFromRight(34); // "BPM"
    text_.setBounds(area);
}

void TempoBox::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::ground);
    g.fillRoundedRectangle(r, theme::kRadius);
    g.setColour(theme::border);
    g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText("BPM", getLocalBounds().withTrimmedRight(26).removeFromRight(34), juce::Justification::centredLeft, false);
}

TopBar::TopBar(bool standalone) : standalone_(standalone)
{
    search_.setTextToShowWhenEmpty("Search samples", theme::muted);
    search_.setFont(theme::font(theme::Face::Text, 13.0f));
    search_.setIndents(30, 9);
    search_.setTitle("Search");
    // The bar draws the field's frame, with the magnifier and the count in it.
    search_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(search_);
    link_.setClickingTogglesState(true);
    addChildComponent(link_);
    addChildComponent(tempo_);
    addChildComponent(addFolder_);
    link_.setVisible(standalone);
    tempo_.setVisible(standalone);
    addFolder_.setVisible(standalone);
    link_.onStateChange = [this] { link_.setDot(theme::amber, link_.getToggleState()); };
}

void TopBar::setCount(int shown, int total)
{
    const juce::String text = shown == total ? juce::String(total) : juce::String(shown) + " of " + juce::String(total);
    if (text == count_) return;
    count_ = text;
    repaint();
}

void TopBar::setHostBpm(double bpm)
{
    const juce::String text = bpm > 0.0 ? "host " + juce::String::fromUTF8(bpmText(bpm).c_str()) + " BPM" : "host tempo unknown";
    if (text == hostTempo_) return;
    hostTempo_ = text;
    repaint();
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced(16, 0);
    area.removeFromLeft(188 + 16); // the mark and the wordmark
    auto right = area.removeFromRight(standalone_ ? 104 + 8 + 140 + 8 + link_.idealWidth() : 160);
    if (standalone_) {
        const int y = (getHeight() - 30) / 2;
        auto row = right.withY(y).withHeight(30);
        addFolder_.setBounds(row.removeFromRight(104));
        row.removeFromRight(8);
        tempo_.setBounds(row.removeFromRight(140));
        row.removeFromRight(8);
        link_.setBounds(row.removeFromRight(link_.idealWidth()));
    }
    area.removeFromRight(16);
    searchArea_ = area.withWidth(std::min(area.getWidth(), 520)).withSizeKeepingCentre(std::min(area.getWidth(), 520), 34);
    search_.setBounds(searchArea_.withTrimmedRight(80));
}

void TopBar::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    // The mark: an amber ring with its gap at half past one.
    const juce::Point<float> c(16.0f + 11.0f, static_cast<float>(getHeight()) / 2.0f);
    juce::Path ring;
    ring.addCentredArc(c.x, c.y, 9.0f, 9.0f, 0.0f, juce::degreesToRadians(75.0f), juce::degreesToRadians(405.0f) - 0.5f, true);
    g.setColour(theme::amber);
    g.strokePath(ring, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(theme::text);
    g.setFont(theme::font(theme::Face::Heading, 18.0f).withExtraKerningFactor(0.06f));
    g.drawText("asma", juce::Rectangle<int>(16 + 32, 0, 120, getHeight()), juce::Justification::centredLeft, false);

    // The search field's frame holds the count and a magnifier.
    const auto frame = searchArea_.toFloat().reduced(0.5f);
    g.setColour(theme::ground);
    g.fillRoundedRectangle(frame, theme::kRadius);
    g.setColour(theme::border);
    g.drawRoundedRectangle(frame, theme::kRadius, 1.0f);
    const juce::Point<float> m(frame.getX() + 18.0f, frame.getCentreY() - 1.0f);
    g.setColour(theme::muted);
    g.drawEllipse(m.x - 4.5f, m.y - 4.5f, 9.0f, 9.0f, 1.5f);
    g.drawLine(m.x + 3.5f, m.y + 3.5f, m.x + 7.0f, m.y + 7.0f, 1.5f);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.drawText(count_, searchArea_.withTrimmedRight(12).removeFromRight(80), juce::Justification::centredRight, false);

    if (!standalone_) {
        g.setFont(theme::font(theme::Face::Mono, 12.0f));
        g.setColour(theme::muted);
        g.drawText(hostTempo_, getLocalBounds().reduced(16, 0), juce::Justification::centredRight, false);
    }
}

} // namespace asma::app
