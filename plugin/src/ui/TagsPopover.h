// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Query.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>
#include <vector>

namespace asma::app {

// A sample's tags: the user's as chips with an x that removes them, the
// analyser's and the file's own greyed (they cannot be removed), and a field
// that adds one. As the field is typed in, the library's tags that start
// with it are offered with their counts; clicking one adds it. Return adds
// what the field holds. Tags are trimmed and lower
// case, so " Bass" adds nothing to a sample tagged "bass". Every change is
// reported at once.
class TagsPopover : public juce::Component {
public:
    struct Tag {
        std::string name;
        bool user = false; // the user's: removable
    };
    using Changed = std::function<void(const std::string& tag, bool added)>;
    TagsPopover(const juce::String& sampleName, std::vector<Tag> tags, std::vector<TagCount> library, Changed onChange);

    int chipCount() const { return static_cast<int>(tags_.size()); }
    juce::String chipText(int index) const;
    bool isRemovable(int index) const;
    juce::Button* removeButton(int index); // null for a tag that cannot be removed
    juce::TextEditor& field() { return field_; }
    // What the field's text offers, most used first.
    juce::StringArray suggestions() const;
    juce::Button& suggestion(int index) { return *suggestionButtons_[index]; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // By value: the button that calls them goes in rebuild(), with its copy.
    void add(std::string tag);
    void remove(std::string tag);
    void rebuild(); // the chips and the suggestions from tags_ and the field

    juce::String sampleName_;
    std::vector<Tag> tags_;
    std::vector<TagCount> library_;
    Changed onChange_;
    juce::OwnedArray<juce::Button> removeButtons_; // one per user tag, in tags_ order
    juce::TextEditor field_;
    juce::OwnedArray<juce::Button> suggestionButtons_;
    std::vector<juce::Rectangle<int>> chipBounds_;
};

} // namespace asma::app
