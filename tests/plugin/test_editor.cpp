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

TEST_CASE("the status line says when a sync is a guess", "[editor]")
{
    EditorRig rig;
    CHECK(rig.editor->statusText().isNotEmpty());
    rig.type("bass");
    rig.editor->table().selectRow(0);
    REQUIRE(rig.playing());
    rig.editor->poll();
    CHECK(rig.editor->statusText().contains("120")); // the loop's own tempo, from its name
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
