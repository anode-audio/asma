// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// A switch for one setting that also says what it does: a dot, a label and
// a detail, as the preview's Tempo, Key, Match loudness and Start chips.
class ChipButton : public juce::Button {
public:
    explicit ChipButton(const juce::String& label);

    // The detail after the label, in its own colour ("120 -> 180 x1.50").
    void setDetail(const juce::String& detail, juce::Colour colour);
    // The dot: filled in `colour` when on, a grey ring when empty.
    void setDot(juce::Colour colour, bool filled);
    const juce::String& detail() const { return detail_; }
    // The width that fits label and detail.
    int idealWidth() const;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::String detail_;
    juce::Colour detailColour_;
    juce::Colour dotColour_;
    bool dotFilled_ = false;
};

// One choice of a few, as touching buttons: direction and loop mode. Accent
// fills the chosen one amber; otherwise it is lifted.
class SegmentedControl : public juce::Component {
public:
    // titles: what a screen reader says for each label (the arrows say nothing).
    SegmentedControl(const juce::StringArray& labels, bool accent, const juce::StringArray& titles = {});

    void setSelected(int index, juce::NotificationType notification = juce::sendNotification);
    int selected() const { return selected_; }
    juce::TextButton& segment(int index) { return *segments_[index]; }
    int idealWidth() const;

    std::function<void(int)> onChange;

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    juce::OwnedArray<juce::TextButton> segments_;
    int selected_ = 0;
};

} // namespace asma::app
