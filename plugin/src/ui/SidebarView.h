// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Sidebar.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace asma::app {

// The left column: All samples and Favourites, then the folders, the
// collections and the saved searches under their headings, each with its
// count, scrolling when they do not fit; Problems at the foot when anything
// failed. It shows what it is told and reports what is picked.
class SidebarView : public juce::Component {
public:
    SidebarView();
    ~SidebarView() override;

    void setEntries(std::vector<SidebarEntry> entries);
    // The lit entry; -1 for none.
    void setSelected(int index);
    void setProblems(std::int64_t count);

    std::function<void(int)> onPick;
    std::function<void()> onProblems;

    int rowCount() const { return rows_.size(); }
    juce::Button& row(int index) { return *rows_[index]; }
    juce::String countText(int index) const;
    juce::StringArray sectionTitles() const;
    juce::Button& problemsButton() { return *problems_; }
    juce::String problemsText() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Content;
    std::vector<SidebarEntry> entries_;
    juce::OwnedArray<juce::Button> rows_;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    std::unique_ptr<juce::Button> problems_;
};

} // namespace asma::app
