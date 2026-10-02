// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// The standalone's manual tempo: minus, the value, plus. Double-click the
// value to type one.
class TempoBox : public juce::Component {
public:
    static constexpr double kMin = 20.0, kMax = 300.0;

    TempoBox();
    double getValue() const { return value_; }
    // Clamped to kMin..kMax, to tenths.
    void setValue(double bpm, juce::NotificationType notification = juce::sendNotificationAsync);
    std::function<void()> onValueChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    double value_ = 120.0;
    juce::TextButton minus_, plus_;
    juce::Label text_;
};

// The top of the window: the asma mark, the search field with its result
// count, and the tempo source: in the standalone Link, the manual tempo and
// "Add folder…"; in a plugin the host's tempo, read-only.
class TopBar : public juce::Component {
public:
    explicit TopBar(bool standalone);

    juce::TextEditor& searchBox() { return search_; }
    ChipButton& linkChip() { return link_; }
    TempoBox& tempoBox() { return tempo_; }
    juce::TextButton& addFolderButton() { return addFolder_; }

    void setCount(int shown, int total);
    // A plugin's host tempo; 0 when the host sends none.
    void setHostBpm(double bpm);
    juce::String hostTempoText() const { return hostTempo_; }
    juce::String countText() const { return count_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    const bool standalone_;
    juce::TextEditor search_;
    juce::String count_;
    juce::String hostTempo_;
    ChipButton link_{"Link"};
    TempoBox tempo_;
    juce::TextButton addFolder_{juce::String::fromUTF8("Add folder…")};
    juce::Rectangle<int> searchArea_;
};

} // namespace asma::app
