// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "asma/core/Fs.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
namespace fs = std::filesystem;
using app::AsmaEditor;
using app::AsmaProcessor;
using test::EditorRig;

namespace {

constexpr int kFavourite = 1, kRating = 7; // the table's column ids

// Runs the message loop until the processor's writer is done, then checks
// the library as the editor's timer would.
void settle(EditorRig& rig)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!rig.p->writer().idle() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    rig.editor->poll();
}

// The x in the rating cell that lands on a star.
int xOfStar(int star)
{
    for (int x = 0; x < 200; ++x)
        if (AsmaEditor::starAt(x) == star) return x + 1;
    return -1;
}

// The sidebar entry with this name.
int entryNamed(EditorRig& rig, const juce::String& name)
{
    for (int i = 0; i < rig.editor->sidebar().rowCount(); ++i)
        if (rig.editor->sidebar().row(i).getButtonText() == name) return i;
    return -1;
}

std::string collectionsText(EditorRig& rig)
{
    Db db = Db::open(rig.f.dbPath);
    std::string out;
    for (const auto& c : UserData(db).collections()) out += c.name + "=" + std::to_string(c.size) + ";";
    return out;
}

std::string searchesText(EditorRig& rig)
{
    Db db = Db::open(rig.f.dbPath);
    std::string out;
    for (const auto& s : UserData(db).savedSearches()) out += s.name + "=" + searchModelToJson(s.model) + ";";
    return out;
}

void name(app::SidebarView& view, const char* text)
{
    view.nameField().setText(text, true);
    view.nameField().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the field reports Return later
}

std::optional<int> storedRating(EditorRig& rig, const fs::path& file)
{
    Db db = Db::open(rig.f.dbPath);
    return UserData(db).rating(Library(db).fileByAbsolutePath(file)->id);
}

bool storedFavourite(EditorRig& rig, const fs::path& file)
{
    Db db = Db::open(rig.f.dbPath);
    return UserData(db).isFavourite(Library(db).fileByAbsolutePath(file)->id);
}

} // namespace

TEST_CASE("the rating column's stars are where it draws them", "[organise]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    CHECK(AsmaEditor::starAt(-1) == 0);
    CHECK(AsmaEditor::starAt(0) == 1);
    CHECK(xOfStar(1) < xOfStar(2));
    CHECK(xOfStar(4) < xOfStar(5));
    CHECK(AsmaEditor::starAt(200) == 0);
}

TEST_CASE("clicking a row's star favourites it at once, and the library confirms it", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kFavourite, 10);
    CHECK(rig.editor->shownRow(0)->favourite);
    settle(rig);
    CHECK(storedFavourite(rig, rig.f.kick));
    CHECK(rig.editor->shownRow(0)->favourite); // now from the library
    test::clickCell(rig.editor->table(), 0, kFavourite, 10);
    settle(rig);
    CHECK_FALSE(storedFavourite(rig, rig.f.kick));
}

TEST_CASE("clicking a star rates, and clicking the rating it has clears it", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(3));
    CHECK(rig.editor->shownRow(0)->rating == 3);
    settle(rig);
    CHECK(storedRating(rig, rig.f.kick) == 3);
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(3));
    CHECK_FALSE(rig.editor->shownRow(0)->rating);
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
    test::clickCell(rig.editor->table(), 0, kRating, 200); // past the stars: nothing
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
}

TEST_CASE("in a plugin the helper writes, and the row shows the change before it does", "[organise]")
{
    EditorRig rig;
    rig.p->writer().setCli(ASMA_CLI_PATH);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(5));
    CHECK(rig.editor->shownRow(0)->rating == 5); // before the helper has run
    settle(rig);
    CHECK(storedRating(rig, rig.f.kick) == 5);
    CHECK(rig.editor->shownRow(0)->rating == 5);
}

TEST_CASE("a write that fails rolls back and the footer says why", "[organise]")
{
    EditorRig rig;
    rig.p->writer().setCli(rig.f.dir.path() / "no-such-asma");
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kFavourite, 10);
    CHECK(rig.editor->shownRow(0)->favourite);
    settle(rig);
    CHECK_FALSE(rig.editor->shownRow(0)->favourite);
    CHECK(rig.editor->footer().rightText().contains(
        "Could not save the favourite: asma's command-line helper is missing"));
}

TEST_CASE("F and 0 to 5 organise the selection", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    CHECK_FALSE(rig.editor->keyPressed(juce::KeyPress('f', {}, 'f'))); // nothing selected
    rig.editor->table().selectRow(0);
    CHECK(rig.editor->keyPressed(juce::KeyPress('f', {}, 'f')));
    CHECK(rig.editor->keyPressed(juce::KeyPress('4', {}, '4')));
    CHECK_FALSE(rig.editor->keyPressed(juce::KeyPress('2', juce::ModifierKeys::commandModifier, '2')));
    settle(rig);
    CHECK(storedFavourite(rig, rig.f.kick));
    CHECK(storedRating(rig, rig.f.kick) == 4);
    CHECK(rig.editor->keyPressed(juce::KeyPress('0', {}, '0')));
    CHECK(rig.editor->keyPressed(juce::KeyPress('F', {}, 'F')));
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
    CHECK_FALSE(storedFavourite(rig, rig.f.kick));
}

TEST_CASE("+ in the sidebar makes a collection; a taken name is refused", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    auto& sidebar = rig.editor->sidebar();
    sidebar.addButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // a click arrives later
    name(sidebar, "  Live set ");
    settle(rig);
    CHECK(collectionsText(rig) == "Live set=0;");
    REQUIRE(entryNamed(rig, "Live set") >= 0);

    sidebar.startNewCollection();
    name(sidebar, "LIVE SET");
    CHECK(sidebar.isEditing());
    CHECK(sidebar.refusalText() == "A collection with that name exists.");
    sidebar.nameField().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    settle(rig);
    CHECK(collectionsText(rig) == "Live set=0;");
}

TEST_CASE("renaming a collection or saved search, and to its own name changes nothing", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    {
        Db db = Db::open(rig.f.dbPath);
        UserData user(db);
        user.createCollection("Live set");
        user.createCollection("Album");
        user.saveSearch("Kicks", {});
    }
    rig.editor->poll();
    auto& sidebar = rig.editor->sidebar();
    sidebar.startRename(entryNamed(rig, "Live set"));
    name(sidebar, "Live set"); // its own name: no write, no refusal
    CHECK_FALSE(sidebar.isEditing());
    sidebar.startRename(entryNamed(rig, "Live set"));
    name(sidebar, "album");
    CHECK(sidebar.refusalText() == "A collection with that name exists.");
    name(sidebar, "Set 2");
    sidebar.startRename(entryNamed(rig, "Kicks")); // the entries have not refreshed yet
    name(sidebar, "Short kicks");
    settle(rig);
    CHECK(collectionsText(rig) == "Album=0;Set 2=0;");
    CHECK(searchesText(rig) == "Short kicks={\"v\":1};");
}

TEST_CASE("deleting asks first only for a collection with samples, and the lit entry falls back to All", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    {
        Db db = Db::open(rig.f.dbPath);
        UserData user(db);
        const auto set = user.createCollection("Live set");
        user.addToCollection(set, Library(db).fileByAbsolutePath(rig.f.kick)->id);
        user.createCollection("Empty");
    }
    rig.editor->poll();
    using app::SidebarEntry;
    const auto entries = app::sidebarEntries(*std::make_unique<app::LibraryView>(rig.f.dbPath));
    for (const auto& e : entries) {
        if (e.name == "Live set") CHECK(AsmaEditor::asksBeforeDeleting(e));
        if (e.name == "Empty") CHECK_FALSE(AsmaEditor::asksBeforeDeleting(e));
        if (e.kind == app::EntryKind::Folder) CHECK_FALSE(AsmaEditor::asksBeforeDeleting(e));
    }

    rig.editor->sidebar().row(entryNamed(rig, "Empty")).triggerClick(); // lit: the table shows it
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(rig.p->pluginState().search.collectionId);
    rig.editor->deleteEntry(entryNamed(rig, "Empty"));
    CHECK_FALSE(rig.p->pluginState().search.collectionId); // All samples
    settle(rig);
    CHECK(collectionsText(rig) == "Live set=1;");
    CHECK(rig.editor->sidebar().row(0).getToggleState());
}

TEST_CASE("Save search names the search in force and saves it", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    {
        Db db = Db::open(rig.f.dbPath);
        UserData(db).saveSearch("Kicks", {});
    }
    rig.editor->poll();
    rig.type("snare");
    auto popover = rig.editor->saveSearchPopover();
    CHECK_FALSE(popover->saveButton().isEnabled()); // no name yet
    const auto typeName = [&](const char* text) {
        popover->field().setText(text, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the field reports changes later
    };
    typeName(" kicks");
    CHECK(popover->refusalText() == "A saved search with that name exists.");
    CHECK_FALSE(popover->saveButton().isEnabled());
    typeName("Snares");
    CHECK(popover->refusalText().isEmpty());
    popover->saveButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    settle(rig);
    CHECK(searchesText(rig) == "Kicks={\"v\":1};Snares={\"v\":1,\"text\":\"snare\"};");
    // The search in force is the saved one now: it is lit.
    CHECK(rig.editor->sidebar().row(entryNamed(rig, "Snares")).getToggleState());
}

namespace {

// The submenu's items, as text with a tick where ticked.
std::vector<std::string> collectionItems(const juce::PopupMenu& menu)
{
    std::vector<std::string> out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
        const auto& item = it.getItem();
        if (!item.subMenu) continue;
        for (juce::PopupMenu::MenuItemIterator sub(*item.subMenu); sub.next();)
            if (!sub.getItem().isSeparator)
                out.push_back((sub.getItem().isTicked ? "+" : "") + sub.getItem().text.toStdString());
    }
    return out;
}

std::vector<std::string> topItems(const juce::PopupMenu& menu)
{
    std::vector<std::string> out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (!it.getItem().isSeparator) out.push_back(it.getItem().text.toStdString());
    return out;
}

int itemId(const juce::PopupMenu& menu, const std::string& text)
{
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
        if (it.getItem().text.toStdString() == text) return it.getItem().itemID;
    return 0;
}

} // namespace

TEST_CASE("a row's menu ticks the collections it is in, and picking one adds or takes it out", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    {
        Db db = Db::open(rig.f.dbPath);
        UserData user(db);
        user.addToCollection(user.createCollection("Live set"), Library(db).fileByAbsolutePath(rig.f.kick)->id);
        user.createCollection("Album");
    }
    rig.editor->poll();
    rig.type("kick");
    const SearchRow kick = *rig.editor->shownRow(0);
    const auto menu = rig.editor->rowMenu(kick);
    CHECK(topItems(menu) == std::vector<std::string>{"Add to collection", "Tags\u2026",
                                                      AsmaEditor::revealText().toStdString()});
    CHECK(collectionItems(menu) == std::vector<std::string>{"Album", "+Live set", "New collection\u2026"});

    rig.editor->rowMenuChosen(kick, itemId(menu, "Album"));
    rig.editor->rowMenuChosen(kick, itemId(menu, "Live set"));
    settle(rig);
    CHECK(collectionsText(rig) == "Album=1;Live set=0;");
    CHECK(collectionItems(rig.editor->rowMenu(kick)) == std::vector<std::string>{"+Album", "Live set", "New collection\u2026"});
}

TEST_CASE("New collection from a row's menu makes one with the sample in it", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    const SearchRow kick = *rig.editor->shownRow(0);
    rig.editor->rowMenuChosen(kick, AsmaEditor::kNewCollection);
    REQUIRE(rig.editor->sidebar().isEditing());
    name(rig.editor->sidebar(), "Kicks");
    settle(rig);
    CHECK(collectionsText(rig) == "Kicks=1;");
    // The next new collection starts empty.
    rig.editor->sidebar().startNewCollection();
    name(rig.editor->sidebar(), "Empty");
    settle(rig);
    CHECK(collectionsText(rig) == "Empty=0;Kicks=1;");
}

TEST_CASE("Tags changes a sample's tags, shown in the table at once", "[organise]")
{
    EditorRig rig;
    rig.p->writer().setCli(ASMA_CLI_PATH);
    rig.type("kick");
    const SearchRow kick = *rig.editor->shownRow(0);
    const auto analysed = kick.tags; // the scanner's, from the file name
    auto popover = rig.editor->tagsPopover(kick);
    for (int i = 0; i < popover->chipCount(); ++i) CHECK_FALSE(popover->isRemovable(i));
    popover->field().setText("Punchy", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    popover->field().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    const auto shown = rig.editor->shownRow(0)->tags;
    CHECK(std::find(shown.begin(), shown.end(), "punchy") != shown.end()); // before the helper has run
    settle(rig);
    {
        Db db = Db::open(rig.f.dbPath);
        const auto tags = Library(db).tags(kick.id);
        CHECK(std::find(tags.begin(), tags.end(), std::pair<std::string, TagSource>{"punchy", TagSource::User}) != tags.end());
    }
    auto again = rig.editor->tagsPopover(*rig.editor->shownRow(0));
    REQUIRE(again->chipCount() == static_cast<int>(analysed.size()) + 1);
    CHECK(again->chipText(0) == "punchy");
    CHECK(again->isRemovable(0));
    again->removeButton(0)->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    settle(rig);
    CHECK(rig.editor->shownRow(0)->tags == analysed);
}

TEST_CASE("a right click on a row opens its menu, not a rating", "[organise]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone);
    rig.type("kick");
    test::clickCell(rig.editor->table(), 0, kRating, xOfStar(4), juce::ModifierKeys::rightButtonModifier);
    juce::PopupMenu::dismissAllActiveMenus();
    settle(rig);
    CHECK_FALSE(storedRating(rig, rig.f.kick));
}
