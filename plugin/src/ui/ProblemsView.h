// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Library.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

namespace asma::app {

// In the table's place: the files that could not be read or analysed, each
// with its folder, what went wrong and a Retry button, and Retry all in the
// header. A file being retried says so and cannot be retried again until it
// is done. With nothing left it says that nothing has failed.
class ProblemsView : public juce::Component {
public:
    ProblemsView();
    ~ProblemsView() override;

    void setProblems(std::vector<Problem> problems);
    // The files a retry is running for; `waiting` while it waits for a scan.
    void setRetrying(std::set<std::int64_t> ids, bool waiting);

    std::function<void(std::vector<std::int64_t> ids)> onRetry;

    // What a problem says: "Could not read the file: …", "Analysis failed: …".
    static juce::String reasonText(const Problem& problem);

    int rowCount() const { return static_cast<int>(problems_.size()); }
    juce::String nameText(int index) const;
    juce::String folderText(int index) const;
    juce::String shownReason(int index) const; // what the row says now, retrying or not
    juce::Button& retryButton(int index) { return *retryButtons_[index]; }
    juce::Button& retryAllButton() { return retryAll_; }
    juce::String countText() const;
    juce::String message() const { return problems_.empty() ? juce::String("Nothing has failed.") : juce::String(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Rows;
    std::vector<Problem> problems_;
    std::set<std::int64_t> retrying_;
    bool waiting_ = false;
    juce::OwnedArray<juce::TextButton> retryButtons_;
    juce::TextButton retryAll_{"Retry all"};
    std::unique_ptr<Rows> rows_;
    juce::Viewport viewport_;
};

} // namespace asma::app
