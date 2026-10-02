// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Controls.h"

#include "ui/Theme.h"

namespace asma::app {

namespace {

int textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text)));
}

} // namespace

ChipButton::ChipButton(const juce::String& label) : juce::Button(label)
{
    setTitle(label);
    detailColour_ = theme::muted;
    dotColour_ = theme::amber;
}

void ChipButton::setDetail(const juce::String& detail, juce::Colour colour)
{
    if (detail == detail_ && colour == detailColour_) return;
    detail_ = detail;
    detailColour_ = colour;
    setDescription(detail);
    repaint();
}

void ChipButton::setDot(juce::Colour colour, bool filled)
{
    if (colour == dotColour_ && filled == dotFilled_) return;
    dotColour_ = colour;
    dotFilled_ = filled;
    repaint();
}

int ChipButton::idealWidth() const
{
    int w = 10 + 8 + 8 + textWidth(theme::font(theme::Face::Text, 12.0f), getButtonText()) + 10;
    if (detail_.isNotEmpty()) w += 8 + textWidth(theme::font(theme::Face::Mono, 11.0f), detail_);
    return w;
}

void ChipButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    if (on || highlighted || down) {
        g.setColour(on ? theme::raised : theme::raised.withAlpha(0.6f));
        g.fillRoundedRectangle(r, theme::kRadius);
    }
    g.setColour(theme::border);
    g.drawRoundedRectangle(r, theme::kRadius, 1.0f);

    auto area = getLocalBounds().reduced(10, 0);
    const auto dot = area.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat();
    if (dotFilled_) {
        g.setColour(dotColour_);
        g.fillEllipse(dot);
    } else {
        g.setColour(theme::muted);
        g.drawEllipse(dot.reduced(0.75f), 1.5f);
    }
    area.removeFromLeft(8);
    const auto labelFont = theme::font(theme::Face::Text, 12.0f);
    g.setFont(labelFont);
    g.setColour(on ? theme::text : theme::muted);
    g.drawText(getButtonText(), area.removeFromLeft(textWidth(labelFont, getButtonText())), juce::Justification::centredLeft, false);
    if (detail_.isNotEmpty()) {
        area.removeFromLeft(8);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(detailColour_);
        g.drawText(detail_, area, juce::Justification::centredLeft, true);
    }
}

SegmentedControl::SegmentedControl(const juce::StringArray& labels, bool accent, const juce::StringArray& titles)
{
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = segments_.add(new juce::TextButton(labels[i]));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1);
        b->getProperties().set("asma.segment", i == 0 ? "first" : (i == labels.size() - 1 ? "last" : "middle"));
        b->getProperties().set("asma.accent", accent);
        b->getProperties().set("asma.size", 12.0f);
        if (i < titles.size()) b->setTitle(titles[i]);
        b->onClick = [this, i] {
            if (segments_[i]->getToggleState() && selected_ != i) setSelected(i);
        };
        addAndMakeVisible(b);
    }
    if (!segments_.isEmpty()) segments_[0]->setToggleState(true, juce::dontSendNotification);
}

void SegmentedControl::setSelected(int index, juce::NotificationType notification)
{
    if (index < 0 || index >= segments_.size()) return;
    selected_ = index;
    segments_[index]->setToggleState(true, juce::dontSendNotification);
    if (notification != juce::dontSendNotification && onChange) onChange(index);
}

int SegmentedControl::idealWidth() const
{
    int w = 0;
    const auto font = theme::font(theme::Face::SemiBold, 12.0f);
    for (auto* b : segments_) w += textWidth(font, b->getButtonText()) + 20;
    return w;
}

void SegmentedControl::resized()
{
    auto area = getLocalBounds();
    const int total = idealWidth();
    const auto font = theme::font(theme::Face::SemiBold, 12.0f);
    for (int i = 0; i < segments_.size(); ++i) {
        const int w = i == segments_.size() - 1 ? area.getWidth()
                                                : (textWidth(font, segments_[i]->getButtonText()) + 20) * getWidth() / std::max(1, total);
        segments_[i]->setBounds(area.removeFromLeft(w));
    }
}

void SegmentedControl::paintOverChildren(juce::Graphics& g)
{
    g.setColour(theme::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), theme::kRadius, 1.0f);
}

} // namespace asma::app
