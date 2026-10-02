// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"
#include "LibraryFixture.h"
#include "PluginTestUtil.h"
#include "asma/audio/Render.h"
#include "asma/core/Fs.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace asma;
namespace fs = std::filesystem;
using app::AsmaEditor;
using app::AsmaProcessor;

namespace {

struct EditorRig {
    test::LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<AsmaProcessor> p;
    std::unique_ptr<AsmaEditor> editor;
    explicit EditorRig(AsmaProcessor::Mode mode = AsmaProcessor::Mode::FromWrapper)
    {
        f.scan();
        p = std::make_unique<AsmaProcessor>(mode);
        p->prepareToPlay(48000.0, 512);
        editor.reset(dynamic_cast<AsmaEditor*>(p->createEditorAndMakeActive()));
        REQUIRE(editor);
        editor->poll(); // what its timer does
    }
    ~EditorRig()
    {
        p->editorBeingDeleted(editor.get());
        editor.reset();
    }
    // Types into the search box the way a user would: the change arrives
    // through the message loop.
    void type(const char* text)
    {
        editor->searchBox().setText(text, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    // Blocks until the preview for the current selection is playing.
    bool playing()
    {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            p->processBlock(buffer, midi);
            if (p->engine().status().playing) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
};

} // namespace

TEST_CASE("typing in the search box filters the table", "[editor]")
{
    EditorRig rig;
    CHECK(rig.editor->table().getNumRows() == 3);
    rig.type("snare");
    CHECK(rig.editor->table().getNumRows() == 1);
    CHECK(rig.p->pluginState().search.text == "snare"); // saved with the project
}

TEST_CASE("selecting a row plays it and space stops it", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    CHECK(rig.playing());
    CHECK(fs::equivalent(fromUtf8(rig.p->pluginState().selected), rig.f.kick));
    CHECK(rig.editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4; ++i) rig.p->processBlock(buffer, midi);
    CHECK_FALSE(rig.p->engine().status().playing);
    CHECK(rig.editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))); // and again plays
    CHECK(rig.playing());
}

TEST_CASE("the Tempo chip says what sync does to the selection", "[editor]")
{
    EditorRig rig(AsmaProcessor::Mode::Standalone); // at its manual 120 BPM
    rig.type("bass");
    rig.editor->table().selectRow(0);
    REQUIRE(rig.playing());
    rig.editor->poll();
    CHECK(rig.editor->preview().tempoChip().detail() == juce::String::fromUTF8("120 \u2192 120 \u00b7 x1.00"));
    rig.editor->bpmBox().setValue(180.0, juce::sendNotificationSync);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    rig.p->processBlock(buffer, midi); // the new tempo reaches the processor's transport
    rig.editor->poll();
    CHECK(rig.editor->preview().tempoChip().detail() == juce::String::fromUTF8("120 \u2192 180 \u00b7 x1.50"));

    EditorRig plugin; // no host playing: no tempo to sync to
    plugin.type("bass");
    plugin.editor->table().selectRow(0);
    plugin.editor->poll();
    CHECK(plugin.editor->preview().tempoChip().detail() == juce::String::fromUTF8("no tempo \u00b7 plays as is"));
}

TEST_CASE("dragging a row out hands over the original or a render", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    juce::StringArray files;
    bool canMove = true;
    const juce::DragAndDropTarget::SourceDetails details({}, rig.editor.get(), {});
    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
    CHECK_FALSE(canMove); // never move a sample out of the library
    REQUIRE(files.size() == 1);
    CHECK(fs::equivalent(fromUtf8(files[0].toStdString()), rig.f.kick)); // no edits: the file itself

    app::PluginState s = rig.p->pluginState();
    s.edits.direction = audio::Direction::Reverse;
    rig.p->setPluginState(s);
    files.clear();
    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
    const auto rendered = fromUtf8(files[0].toStdString());
    CHECK(rendered.parent_path() == audio::RenderStore::defaultDir());
    CHECK(fs::exists(rendered));
}

TEST_CASE("a restored project's loop syncs as it did", "[editor]")
{
    test::LibraryFixture f;
    f.scan();
    AsmaProcessor p;
    p.prepareToPlay(48000.0, 512);
    test::FakePlayHead host;
    host.bpm = 90.0;
    host.playing = true;
    p.setPlayHead(&host);
    app::PluginState s;
    s.selected = toUtf8(f.loop);
    p.setPluginState(s);
    REQUIRE(test::waitForPreview(p, 1));
    p.engine().play();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    CHECK(p.engine().status().tempoSynced); // 120 from the library, stretched to 90
    p.setPlayHead(nullptr);
}

TEST_CASE("the standalone shows its tempo controls, a plugin does not", "[editor]")
{
    EditorRig plugin;
    CHECK_FALSE(plugin.editor->linkToggle().isVisible());
    CHECK_FALSE(plugin.editor->bpmBox().isVisible());

    EditorRig standalone(AsmaProcessor::Mode::Standalone);
    REQUIRE(standalone.editor->linkToggle().isVisible());
    REQUIRE(standalone.editor->bpmBox().isVisible());
    standalone.editor->bpmBox().setValue(126.0, juce::sendNotificationSync);
    CHECK(standalone.p->pluginState().sync.hostBpm == 126.0);
    standalone.editor->linkToggle().setToggleState(true, juce::sendNotificationSync);
    CHECK(standalone.p->pluginState().link);
}

TEST_CASE("the standalone adds a folder and shows the scan in the table", "[editor]")
{
    EditorRig plugin;
    CHECK_FALSE(plugin.editor->addFolderButton().isVisible());

    EditorRig rig(AsmaProcessor::Mode::Standalone);
    REQUIRE(rig.editor->addFolderButton().isVisible());
    const auto more = rig.f.dir.path() / "More";
    test::writeWavFloat(more / "Pad_Cm.wav", 48000, {test::sine(261.6, 1.0, 0.3, 48000)});
    // The app would find asma-scan beside itself; the test points at the build's.
    rig.p->scans()->setWorker(ASMA_SCAN_PATH);
    rig.editor->addFolder(more);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (rig.p->scans()->busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    rig.editor->poll();
    CHECK(rig.editor->footer().rightText().contains("Scan finished: 1 added"));
    CHECK(rig.editor->table().getNumRows() == 4);
}

TEST_CASE("loading state with the editor open updates it, and old settings stay gone", "[editor]")
{
    EditorRig rig;
    app::PluginState loaded = rig.p->pluginState();
    loaded.search.text = "snare";
    loaded.sync.tempo = false;
    loaded.gainMatch = false;
    const std::string json = app::toJson(loaded);
    rig.p->setStateInformation(json.data(), static_cast<int>(json.size())); // e.g. a preset menu
    rig.editor->poll();
    CHECK(rig.editor->searchBox().getText() == "snare");
    CHECK(rig.editor->table().getNumRows() == 1);
    CHECK_FALSE(rig.editor->preview().tempoChip().getToggleState());
    CHECK_FALSE(rig.editor->preview().gainChip().getToggleState());

    rig.editor->preview().chooseKey(1); // key sync on, in C
    const app::PluginState after = rig.p->pluginState();
    CHECK(after.sync.key);
    CHECK_FALSE(after.sync.tempo); // not written back from the editor's old view
    CHECK_FALSE(after.gainMatch);
    CHECK(after.search.text == "snare");
}

TEST_CASE("the table area says why it has nothing to show", "[editor]")
{
    test::LibraryFixture f; // no scan: no library
    const juce::ScopedJuceInitialiser_GUI gui;
    {
        AsmaProcessor p;
        std::unique_ptr<AsmaEditor> editor(dynamic_cast<AsmaEditor*>(p.createEditorAndMakeActive()));
        editor->poll();
        CHECK(editor->emptyText().contains("Open the asma app")); // a plugin never makes one
        p.editorBeingDeleted(editor.get());
    }
    {
        AsmaProcessor p(AsmaProcessor::Mode::Standalone);
        std::unique_ptr<AsmaEditor> editor(dynamic_cast<AsmaEditor*>(p.createEditorAndMakeActive()));
        editor->poll();
        CHECK(editor->emptyText().contains("Add a folder"));
        p.editorBeingDeleted(editor.get());
    }
    EditorRig rig;
    CHECK(rig.editor->emptyText().isEmpty());
    rig.type("nothing like this");
    CHECK(rig.editor->emptyText() == "No samples match.");
    CHECK(rig.editor->topBar().countText() == "0 of 3");
    rig.type("kick");
    CHECK(rig.editor->emptyText().isEmpty());
    CHECK(rig.editor->topBar().countText() == "1 of 3");
}

TEST_CASE("a plugin shows the host's tempo where the standalone sets its own", "[editor]")
{
    EditorRig rig;
    test::FakePlayHead host;
    host.bpm = 124.0;
    rig.p->setPlayHead(&host);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    rig.p->processBlock(buffer, midi);
    rig.editor->poll();
    CHECK(rig.editor->topBar().hostTempoText() == "host 124 BPM");
    rig.p->setPlayHead(nullptr);
}

TEST_CASE("an edit in the preview reaches the project, the engine and the footer, and a new selection drops it",
          "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    REQUIRE(rig.playing());
    rig.editor->poll();
    CHECK(rig.editor->footer().dragText() == "Drag out: the original file");
    rig.editor->preview().direction().segment(1).triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(rig.p->pluginState().edits.direction == audio::Direction::Reverse);
    rig.editor->poll();
    CHECK(rig.editor->footer().dragText() == "Drag out renders: reversed");

    rig.type("");
    rig.editor->table().selectRow(rig.editor->table().getSelectedRow() == 0 ? 1 : 0);
    CHECK(rig.p->pluginState().edits.direction == audio::Direction::Forward);
    CHECK(rig.editor->preview().direction().selected() == 0);
}

TEST_CASE("the preview draws the selection's waveform", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    REQUIRE(rig.playing());
    rig.editor->poll();
    REQUIRE(rig.editor->preview().waveform().overview());
    CHECK(rig.editor->preview().waveform().overview()->sampleRate == 48000);
}

TEST_CASE("Clear renders empties the renders folder", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    app::PluginState s = rig.p->pluginState();
    s.edits.direction = audio::Direction::Reverse;
    rig.p->setPluginState(s);
    juce::StringArray files;
    bool canMove = true;
    const juce::DragAndDropTarget::SourceDetails details({}, rig.editor.get(), {});
    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
    CHECK(rig.editor->footer().clearButton().isVisible());
    rig.editor->clearRenders();
    CHECK(audio::RenderStore(audio::RenderStore::defaultDir()).bytes() == 0);
    CHECK_FALSE(rig.editor->footer().clearButton().isVisible());
}

TEST_CASE("the window opens at its default size and keeps its minimum", "[editor]")
{
    EditorRig rig;
    CHECK(rig.editor->getWidth() == AsmaEditor::kDefaultWidth);
    CHECK(rig.editor->getHeight() == AsmaEditor::kDefaultHeight);
    REQUIRE(rig.editor->getConstrainer());
    CHECK(rig.editor->getConstrainer()->getMinimumWidth() == AsmaEditor::kMinWidth);
    CHECK(rig.editor->getConstrainer()->getMinimumHeight() == AsmaEditor::kMinHeight);
}

TEST_CASE("a project whose sample has gone opens with nothing selected", "[editor]")
{
    EditorRig rig;
    app::PluginState s = rig.p->pluginState();
    s.selected = toUtf8(rig.f.dir.path() / "gone.wav");
    s.edits.direction = audio::Direction::Reverse;
    rig.p->setPluginState(s);
    rig.editor->poll();
    CHECK(rig.editor->table().getSelectedRow() < 0);
    CHECK(rig.editor->footer().dragText().isEmpty()); // nothing to drag
    CHECK_FALSE(rig.editor->preview().waveform().overview());
}

#if !JUCE_WINDOWS
TEST_CASE("Clear renders says when a render could not be deleted", "[editor]")
{
    EditorRig rig;
    rig.type("kick");
    rig.editor->table().selectRow(0);
    app::PluginState s = rig.p->pluginState();
    s.edits.direction = audio::Direction::Reverse;
    rig.p->setPluginState(s);
    juce::StringArray files;
    bool canMove = true;
    const juce::DragAndDropTarget::SourceDetails details({}, rig.editor.get(), {});
    REQUIRE(rig.editor->shouldDropFilesWhenDraggedExternally(details, files, canMove));
    const auto dir = audio::RenderStore::defaultDir();
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec); // nothing in it can be deleted
    rig.editor->clearRenders();
    fs::permissions(dir, fs::perms::owner_all);
    CHECK(rig.editor->footer().rightText().contains("could not be deleted"));
    CHECK(rig.editor->footer().clearButton().isVisible()); // still there to try again
}
#endif

TEST_CASE("the footer and the drag agree on the tempo before any audio has run", "[editor]")
{
    test::LibraryFixture f;
    f.scan();
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaProcessor p(AsmaProcessor::Mode::Standalone); // no audio device yet: no blocks, no transport
    app::PluginState s = p.pluginState();
    s.sync.hostBpm = 180.0;
    p.setPluginState(s);
    std::unique_ptr<AsmaEditor> editor(dynamic_cast<AsmaEditor*>(p.createEditorAndMakeActive()));
    editor->searchBox().setText("bass", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    editor->table().selectRow(0);
    editor->poll();
    CHECK(editor->footer().dragText() == "Drag out renders: stretched to 180 BPM");
    CHECK(editor->preview().tempoChip().detail() == juce::String::fromUTF8("120 \u2192 180 \u00b7 x1.50"));
    p.editorBeingDeleted(editor.get());
}
