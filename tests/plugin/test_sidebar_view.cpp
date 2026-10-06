// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"
#include "ui/SidebarView.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::EntryKind;
using app::SidebarEntry;
using app::SidebarView;

namespace {

std::vector<SidebarEntry> entries()
{
    return {{EntryKind::All, 0, "All samples", 585, {}},
            {EntryKind::Favourites, 0, "Favourites", 12, {}},
            {EntryKind::Folder, 1, "Samples", 412, {}},
            {EntryKind::Folder, 2, "Splice", 151, {}},
            {EntryKind::Collection, 7, "Low end", 18, {}},
            {EntryKind::SavedSearch, 3, "Short kicks", -1, {}}};
}

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

} // namespace

TEST_CASE("the sidebar shows its entries under their sections", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    REQUIRE(view.rowCount() == 6);
    CHECK(view.row(2).getButtonText() == "Samples");
    CHECK(view.countText(0) == "585");
    CHECK(view.countText(5).isEmpty()); // a saved search has no count
    CHECK(view.sectionTitles() == juce::StringArray{"FOLDERS", "COLLECTIONS", "SAVED SEARCHES"});
    view.setEntries({entries()[0], entries()[1]});
    CHECK(view.sectionTitles().isEmpty()); // no folders, no heading for them
}

TEST_CASE("clicking an entry picks it, and the picked one is lit", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaLookAndFeel lnf;
    SidebarView view;
    view.setLookAndFeel(&lnf);
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    int picked = -1;
    view.onPick = [&](int i) { picked = i; };
    view.row(4).triggerClick();
    settle();
    CHECK(picked == 4);
    view.setSelected(4);
    CHECK(view.row(4).getToggleState());
    CHECK_FALSE(view.row(0).getToggleState());
    view.setSelected(-1); // a scope not listed: nothing lit
    CHECK_FALSE(view.row(4).getToggleState());
    view.setLookAndFeel(nullptr);
}

TEST_CASE("the Problems entry shows only when something failed", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    view.setProblems(0);
    CHECK_FALSE(view.problemsButton().isVisible());
    view.setProblems(3);
    CHECK(view.problemsButton().isVisible());
    CHECK(view.problemsText() == "3");
}

TEST_CASE("a sidebar with many folders scrolls, and Problems stays in view", "[sidebar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    std::vector<SidebarEntry> lots{{EntryKind::All, 0, "All samples", 5000, {}}};
    for (int i = 0; i < 40; ++i) lots.push_back({EntryKind::Folder, i + 1, "Folder " + std::to_string(i), 100, {}});
    view.setEntries(lots);
    view.setProblems(2);
    CHECK(view.getLocalBounds().contains(view.problemsButton().getBounds()));
    CHECK(view.row(40).getBottom() > view.getHeight()); // past the view: scrolled to, not squeezed
}
