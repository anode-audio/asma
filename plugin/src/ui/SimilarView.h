// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace asma::app {

// The bottom panel's right side: the samples nearest the selection by sound,
// with their distance, or why there are none.
class SimilarView : public juce::Component {
public:
    SimilarView();

    void setResult(const SimilarResult& result);
    // Nothing selected.
    void clear();

    std::function<void(const SearchRow&)> onPick;

    juce::String message() const { return message_; }
    int rowCount() const { return rows_.size(); }
    juce::Button& row(int index) { return *rows_[index]; }
    juce::String distanceText(int index) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    std::vector<SimilarResult::Match> matches_;
    juce::OwnedArray<juce::Button> rows_;
    juce::String message_;
};

} // namespace asma::app
