// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Filters.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// One filter as a pill: its label (amber with an x when set, grey when not).
// The pill and the x are separate buttons, so both take the keyboard.
class FilterChip : public juce::Component {
public:
    FilterChip();

    void setLabel(const juce::String& label, bool active);
    const juce::String& label() const { return label_; }
    bool isActive() const { return active_; }
    int idealWidth() const;
    // Where the label goes, left of the x when there is one.
    juce::Rectangle<int> textArea() const;
    int labelWidth() const;

    juce::Button& mainButton() { return *main_; }
    juce::Button& clearButton() { return *clear_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String label_;
    bool active_ = false;
    std::unique_ptr<juce::Button> main_, clear_;
};

// The row over the table: a chip per filter, "Clear all" and, at its right
// end, "Save search". It shows the search it is given and reports what the
// user changes.
class ChipRow : public juce::Component {
public:
    ChipRow();

    void setModel(const SearchModel& model);

    // A chip's x or Clear all: the search without those filters.
    std::function<void(const SearchModel&)> onChange;
    // A chip was clicked: open its popover against `anchor`.
    std::function<void(Facet, juce::Component& anchor)> onOpen;
    // Save search was clicked: ask for a name against `anchor`.
    std::function<void(juce::Component& anchor)> onSaveSearch;

    FilterChip& chip(Facet facet) { return *chips_[static_cast<int>(facet)]; }
    juce::Button& clearAllButton() { return clearAll_; }
    juce::Button& saveSearchButton() { return saveSearch_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    SearchModel model_;
    juce::OwnedArray<FilterChip> chips_;
    juce::TextButton clearAll_{"Clear all"};
    juce::TextButton saveSearch_{"Save search"};
};

} // namespace asma::app
