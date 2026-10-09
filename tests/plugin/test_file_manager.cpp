// SPDX-License-Identifier: GPL-3.0-only
#include "EditorRig.h"
#include "FileOpsJob.h"
#include "ui/EditMenu.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::app;
using asma::test::EditorRig;
namespace fs = std::filesystem;

namespace {

// A trash in a folder of its own: the tests never touch the system's.
TrashBackend folderTrash(const fs::path& dir)
{
    fs::create_directories(dir);
    TrashBackend t;
    t.available = [](const fs::path&) { return true; };
    t.move = [dir](const fs::path& file) {
        TrashResult r;
        r.where = dir / file.filename();
        r.ok = !renameNoReplace(file, r.where);
        return r;
    };
    t.restore = [](const fs::path& where, const fs::path& to) {
        return renameNoReplace(where, to) ? std::string("its old place is taken") : std::string();
    };
    return t;
}

// The standalone over the fixture's library, with a trash of its own.
struct App : EditorRig {
    App()
        : EditorRig(AsmaProcessor::Mode::Standalone,
                    [this](AsmaProcessor& processor) { processor.setTrash(folderTrash(f.dir.path() / "Trash")); })
    {
        settle();
    }
    // Until the file operations are done and the window has looked.
    void settle()
    {
        auto* job = p->fileOps();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (job && !job->idle() && std::chrono::steady_clock::now() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        editor->poll();
    }
    // Selects the row showing this file name.
    SearchRow select(const std::string& name)
    {
        for (int i = 0; i < editor->table().getNumRows(); ++i)
            if (editor->shownRow(i)->name == name) {
                editor->table().selectRow(i);
                return *editor->shownRow(i);
            }
        FAIL("no row " << name);
        return {};
    }
    std::string selectedName()
    {
        const auto row = editor->shownRow(editor->table().getSelectedRow());
        return row ? row->name : std::string();
    }
    juce::String status() { return editor->footer().rightText(); }
    // Runs audio until the engine plays its newest selection; false after 5 s.
    bool playingSelection()
    {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            p->processBlock(buffer, midi);
            const auto status = p->engine().status();
            if (status.playing && status.generation == p->engine().selected()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    // The file name of the project's selection; empty for none.
    std::string projectSelection() { return toUtf8(fromUtf8(p->pluginState().selected).filename()); }
};

std::vector<std::string> items(const juce::PopupMenu& menu)
{
    std::vector<std::string> out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (!it.getItem().isSeparator) out.push_back(it.getItem().text.toStdString());
    return out;
}

int folderEntry(EditorRig& rig)
{
    for (int i = 0;; ++i) {
        const auto menu = rig.editor->sidebar().entryMenu(i);
        if (i > 20) return -1;
        if (!items(menu).empty() && items(menu).front() == "Remove from Library") return i;
    }
}

const std::string undoHint = " " + FileOpsJob::undoKey() + " to undo.";

} // namespace

TEST_CASE("A row's menu offers the file operations in the standalone only", "[filemanager]")
{
    EditorRig plugin;
    const SearchRow row = *plugin.editor->shownRow(0);
    const auto pluginItems = items(plugin.editor->rowMenu(row));
    CHECK(std::find(pluginItems.begin(), pluginItems.end(), "Move to Trash") == pluginItems.end());
    CHECK(plugin.p->fileOps() == nullptr);
    CHECK_FALSE(plugin.editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));

    App app;
    const auto appItems = items(app.editor->rowMenu(*app.editor->shownRow(0)));
    CHECK(std::vector<std::string>(appItems.end() - 3, appItems.end())
          == std::vector<std::string>{"Rename…", "Move to…", "Move to Trash"});
}

TEST_CASE("Renaming from the popover renames the sample, which stays selected", "[filemanager]")
{
    App app;
    const SearchRow kick = app.select("Kick_01.wav");
    auto popover = app.editor->renamePopover(kick);
    CHECK(popover->field().getText() == "Kick_01.wav");
    popover->field().setText("Snare_02.wav", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(popover->refusalText() == "Snare_02.wav already exists in Drums.");
    popover->field().setText("Kick_01.aif", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(popover->refusalText() == "A rename keeps the extension: .wav.");
    popover->field().setText("Kick_09.wav", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(popover->refusalText().isEmpty());
    popover->saveButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    app.settle();
    CHECK(fs::exists(app.f.lib / "Drums" / "Kick_09.wav"));
    CHECK(app.selectedName() == "Kick_09.wav");
    CHECK(app.status().contains("Renamed Kick_01.wav to Kick_09.wav." + undoHint));
}

TEST_CASE("Delete trashes the selected sample, the next is selected, and undo brings it back", "[filemanager]")
{
    App app;
    app.select("Kick_01.wav");
    CHECK(app.editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    app.settle();
    CHECK_FALSE(fs::exists(app.f.kick));
    CHECK(app.editor->table().getNumRows() == 2);
    CHECK(app.selectedName() == "Snare_02.wav");
    CHECK(app.status().contains("Moved Kick_01.wav to the Trash." + undoHint));
    CHECK(app.p->fileOps()->undoLabel() == "Move Kick_01.wav to the Trash");

    CHECK(app.editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    app.settle();
    CHECK(fs::exists(app.f.kick));
    CHECK(app.editor->table().getNumRows() == 3);
    CHECK(app.selectedName() == "Kick_01.wav");
    CHECK(app.status().contains("Undid Move Kick_01.wav to the Trash."));
    CHECK(app.p->fileOps()->undoLabel().empty());
}

TEST_CASE("Move to puts the sample in the folder chosen, or says why not", "[filemanager]")
{
    App app;
    const SearchRow kick = app.select("Kick_01.wav");
    app.editor->moveSample(kick, app.f.lib / "Loops");
    app.settle();
    CHECK(fs::exists(app.f.lib / "Loops" / "Kick_01.wav"));
    CHECK(app.status().contains("Moved Kick_01.wav to Loops." + undoHint));
    CHECK(app.selectedName() == "Kick_01.wav");
    app.editor->moveSample(*app.editor->shownRow(app.editor->table().getSelectedRow()), app.f.dir.path());
    app.settle();
    CHECK(app.status().contains("That folder is outside the library's folders."));
}

TEST_CASE("Remove from Library takes a folder out, and undo puts it back", "[filemanager]")
{
    EditorRig plugin;
    CHECK(folderEntry(plugin) == -1);

    App app;
    const int folder = folderEntry(app);
    REQUIRE(folder >= 0);
    app.editor->sidebar().entryMenuChosen(folder, SidebarView::kRemoveFolder);
    app.settle();
    CHECK(app.editor->table().getNumRows() == 0);
    CHECK(app.status().contains("Removed Samples from the library." + undoHint));
    CHECK(fs::exists(app.f.kick));
    CHECK(app.editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    app.settle();
    CHECK(app.editor->table().getNumRows() == 3);
}

TEST_CASE("Adding a folder inside one is refused; one holding some asks first", "[filemanager]")
{
    App app;
    app.editor->addFolder(app.f.lib / "Drums");
    app.settle();
    CHECK(app.status().contains("Drums is already in the library, inside Samples."));

    juce::String asked;
    app.editor->confirmMerge = [&](const juce::String& question, std::function<void(bool)> answer) {
        asked = question;
        answer(true);
    };
    const auto outer = app.f.dir.path();
    const std::string name = toUtf8(outer.filename());
    app.editor->addFolder(outer);
    app.settle();
    CHECK(asked.toStdString()
          == name + " contains a folder already in the library (Samples). Add " + name + " in its place?");
    CHECK(app.status().contains("Added " + name + " in place of Samples."));
    Db db = Db::open(app.f.dbPath);
    REQUIRE(Library(db).roots().size() == 1);
    CHECK(Library(db).roots().front().path == Library::folderPath(outer));
}

TEST_CASE("The Edit menu names what undo would undo", "[filemanager]")
{
    std::string label = "Move Kick_01.wav to the Trash";
    int undone = 0;
    EditMenu menu([&] { return label; }, [&] { ++undone; });
    CHECK(menu.getMenuBarNames() == juce::StringArray{"Edit"});
    auto edit = menu.getMenuForIndex(0, "Edit");
    juce::PopupMenu::MenuItemIterator it(edit);
    REQUIRE(it.next());
    CHECK(it.getItem().text == "Undo Move Kick_01.wav to the Trash");
    CHECK(it.getItem().isEnabled);
    menu.menuItemSelected(it.getItem().itemID, 0);
    CHECK(undone == 1);
    label.clear();
    edit = menu.getMenuForIndex(0, "Edit");
    juce::PopupMenu::MenuItemIterator again(edit);
    REQUIRE(again.next());
    CHECK(again.getItem().text == "Undo");
    CHECK_FALSE(again.getItem().isEnabled);
}

TEST_CASE("After a trash the next sample plays, and after its undo the one brought back", "[filemanager]")
{
    App app;
    app.select("Kick_01.wav");
    REQUIRE(app.playingSelection());
    const auto before = app.p->engine().selected();
    CHECK(app.editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    app.settle();
    CHECK(app.p->engine().selected() != before);
    CHECK(app.projectSelection() == "Snare_02.wav");
    CHECK(app.playingSelection());

    const auto trashed = app.p->engine().selected();
    CHECK(app.editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    app.settle();
    CHECK(app.p->engine().selected() != trashed);
    CHECK(app.projectSelection() == "Kick_01.wav");
    CHECK(app.playingSelection());
}

TEST_CASE("Removing the folder of the sample playing stops it", "[filemanager]")
{
    App app;
    app.select("Kick_01.wav");
    REQUIRE(app.playingSelection());
    const auto before = app.p->engine().selected();
    const int folder = folderEntry(app);
    REQUIRE(folder >= 0);
    app.editor->sidebar().entryMenuChosen(folder, SidebarView::kRemoveFolder);
    app.settle();
    CHECK(app.p->engine().selected() != before); // the gone sample is dropped
    CHECK(app.projectSelection().empty());
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 20; ++i) app.p->processBlock(buffer, midi);
    CHECK_FALSE(app.p->engine().status().playing);
}
