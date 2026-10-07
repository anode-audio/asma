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
    // No folders, no heading for them; COLLECTIONS stays, for its "+".
    CHECK(view.sectionTitles() == juce::StringArray{"COLLECTIONS"});
    CHECK(view.addButton().isVisible());
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

TEST_CASE("+ opens a name field: Return keeps the name, Escape drops it", "[sidebar][organise]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    std::vector<std::pair<int, juce::String>> named;
    view.onNamed = [&](int index, const juce::String& name) { named.emplace_back(index, name); };

    view.addButton().triggerClick();
    settle();
    REQUIRE(view.isEditing());
    CHECK(view.nameField().isVisible());
    // The field ends the collections, above the saved searches.
    CHECK(view.nameField().getY() > view.row(4).getY());
    CHECK(view.nameField().getY() < view.row(5).getY());
    view.nameField().setText("Basslines", true);
    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    settle(); // the field reports Return and Escape through the message loop
    CHECK_FALSE(view.isEditing());
    CHECK(named == std::vector<std::pair<int, juce::String>>{{-1, "Basslines"}});

    view.startNewCollection();
    view.nameField().setText("Dropped", true);
    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    settle(); // the field reports Return and Escape through the message loop
    CHECK_FALSE(view.isEditing());
    CHECK(named.size() == 1);
}

TEST_CASE("a refused name keeps the field open and says why", "[sidebar][organise]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    int calls = 0;
    view.nameRefusal = [](int, const juce::String& name) -> std::optional<juce::String> {
        if (name.trim().isEmpty()) return juce::String();
        if (name.trim().equalsIgnoreCase("low end")) return juce::String("A collection with that name exists.");
        return std::nullopt;
    };
    view.onNamed = [&](int, const juce::String&) { ++calls; };
    view.startNewCollection();
    view.nameField().setText("LOW END", true);
    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    settle(); // the field reports Return and Escape through the message loop
    CHECK(view.isEditing());
    CHECK(view.refusalText() == "A collection with that name exists.");
    view.nameField().setText("  ", true);
    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    settle(); // the field reports Return and Escape through the message loop
    CHECK(view.isEditing());
    CHECK(calls == 0);
}

TEST_CASE("collections and saved searches have Rename and Delete; renaming edits in place", "[sidebar][organise]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SidebarView view;
    view.setBounds(0, 0, 220, 482);
    view.setEntries(entries());
    CHECK(view.entryMenu(2).getNumItems() == 0); // a folder
    CHECK(view.entryMenu(4).getNumItems() == 2); // a collection
    CHECK(view.entryMenu(5).getNumItems() == 2); // a saved search
    int deleted = -1;
    view.onDelete = [&](int index) { deleted = index; };
    view.entryMenuChosen(5, SidebarView::kDelete);
    CHECK(deleted == 5);

    view.entryMenuChosen(4, SidebarView::kRename);
    REQUIRE(view.isEditing());
    CHECK(view.nameField().getText() == "Low end");
    CHECK(view.nameField().getY() == view.row(4).getY() + 2);
    CHECK_FALSE(view.row(4).isVisible());
    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    settle(); // the field reports Return and Escape through the message loop
    CHECK(view.row(4).isVisible());
}
