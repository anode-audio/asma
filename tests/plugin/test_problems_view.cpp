// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/ProblemsView.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::ProblemsView;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

std::vector<Problem> problems()
{
    return {{1, "/Users/me/Samples", "Drums/Kicks/kick_broken_header.wav", Problem::Kind::Read, "not a valid WAV header"},
            {2, "/Users/me/Samples", "pad_long_take.flac", Problem::Kind::Read, "The file is gone"},
            {3, "/Users/me/Samples", "Vocals/vox_chop_07.mp3", Problem::Kind::Analysis, "the file is silent"}};
}

} // namespace

TEST_CASE("the Problems panel lists each file with its folder and what went wrong", "[problems]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ProblemsView view;
    view.setBounds(0, 0, 840, 400);
    view.setProblems(problems());
    REQUIRE(view.rowCount() == 3);
    CHECK(view.countText() == "3 files");
    CHECK(view.nameText(0) == "kick_broken_header.wav");
    CHECK(view.folderText(0) == "Drums / Kicks");
    CHECK(view.folderText(1) == "Samples");
    CHECK(view.shownReason(0) == "Could not read the file: not a valid WAV header");
    CHECK(view.shownReason(1) == "The file is gone");
    CHECK(view.shownReason(2) == "Analysis failed: the file is silent");
    CHECK(view.message().isEmpty());
    view.setProblems({});
    CHECK(view.message() == "Nothing has failed.");
    CHECK_FALSE(view.retryAllButton().isVisible());
}

TEST_CASE("Retry and Retry all ask for the files not already retrying", "[problems]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ProblemsView view;
    view.setBounds(0, 0, 840, 400);
    view.setProblems(problems());
    std::vector<std::vector<std::int64_t>> asked;
    view.onRetry = [&](std::vector<std::int64_t> ids) { asked.push_back(ids); };
    view.retryButton(1).triggerClick();
    settle();
    view.setRetrying({2}, false);
    CHECK_FALSE(view.retryButton(1).isEnabled());
    CHECK(view.shownReason(1) == juce::String::fromUTF8("Retrying…"));
    view.setRetrying({2}, true);
    CHECK(view.shownReason(1) == juce::String::fromUTF8("Retrying after the scan…"));
    view.retryAllButton().triggerClick();
    settle();
    CHECK(asked == std::vector<std::vector<std::int64_t>>{{2}, {1, 3}});
    view.setRetrying({1, 2, 3}, false);
    CHECK_FALSE(view.retryAllButton().isEnabled());
}
