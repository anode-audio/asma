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
