// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Filters.h"
#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace asma::app {

using SearchChanged = std::function<void(const SearchModel&)>;

// A chip's panel: it edits its own copy of the search and reports every
// change at once; there is no OK button.
class FilterPopover : public juce::Component {
public:
    // hint: how the picks combine ("any of"), drawn after the title.
    FilterPopover(const juce::String& title, const SearchModel& model, SearchChanged onChange,
                  const juce::String& hint = {});
    const juce::String& hint() const { return hint_; }
    void paint(juce::Graphics& g) override;

protected:
    void changed(); // reports model_
    juce::Rectangle<int> body() const; // below the title
    SearchModel model_;

private:
    juce::String title_, hint_;
    SearchChanged onChange_;
};

class TypePopover : public FilterPopover {
public:
    TypePopover(const SearchModel& model, SearchChanged onChange);
    SegmentedControl& choice() { return choice_; }
    void resized() override;

private:
    SegmentedControl choice_;
};

class BpmPopover : public FilterPopover {
public:
    // tempo: the tempo in force, for "near the tempo"; 0 when there is none.
    BpmPopover(const SearchModel& model, double tempo, SearchChanged onChange);
    juce::TextEditor& from() { return from_; }
    juce::TextEditor& to() { return to_; }
    juce::Button& near120() { return near120_; }
    juce::Button& nearTempo() { return nearTempo_; }
    void resized() override;

private:
    void setRange(double lo, double hi);
    juce::TextEditor from_, to_;
    juce::TextButton near120_{"Near 120"}, nearTempo_{"Near the tempo"};
    double tempo_;
};

class KeyPopover : public FilterPopover {
public:
    KeyPopover(const SearchModel& model, SearchChanged onChange);
    int keyCount() const { return keys_.size(); }
    juce::Button& key(int index) { return *keys_[index]; }
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> keys_;
};

class InstrumentPopover : public FilterPopover {
public:
    InstrumentPopover(const SearchModel& model, std::vector<TagCount> tags, SearchChanged onChange);
    int tagCount() const { return tags_.size(); }
    juce::Button& tag(int index) { return *tags_[index]; }
    juce::String countText(int index) const;
    juce::String emptyText() const { return empty_; }
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    std::vector<TagCount> counts_;
    juce::OwnedArray<juce::Button> tags_;
    juce::Viewport viewport_;
    juce::Component list_;
    juce::String empty_;
};

class LengthPopover : public FilterPopover {
public:
    LengthPopover(const SearchModel& model, SearchChanged onChange);
    juce::Button& preset(int index) { return *presets_[index]; }
    juce::TextEditor& from() { return from_; }
    juce::TextEditor& to() { return to_; }
    void resized() override;

private:
    void showPreset();
    juce::OwnedArray<juce::TextButton> presets_;
    juce::TextEditor from_, to_;
};

class RatingPopover : public FilterPopover {
public:
    RatingPopover(const SearchModel& model, SearchChanged onChange);
    juce::Button& star(int index) { return *stars_[index]; }
    void resized() override;

private:
    void show();
    juce::OwnedArray<juce::Button> stars_;
};

// What a popover needs from outside the search.
struct PopoverContext {
    std::vector<TagCount> tags; // the library's, most used first
    double tempo = 0.0;         // in force; 0 when none
};

// The panel for a chip, sized to its content.
std::unique_ptr<FilterPopover> makeFilterPopover(Facet facet, const SearchModel& model, const PopoverContext& context,
                                                 SearchChanged onChange);

} // namespace asma::app
