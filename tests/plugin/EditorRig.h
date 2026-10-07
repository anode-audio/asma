// SPDX-License-Identifier: GPL-3.0-only
// The window over a scanned library, headless, as a user would drive it.
#pragma once

#include "AsmaEditor.h"
#include "LibraryFixture.h"
#include "PluginTestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <memory>
#include <thread>

namespace asma::test {

struct EditorRig {
    LibraryFixture f;
    const juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<app::AsmaProcessor> p;
    std::unique_ptr<app::AsmaEditor> editor;
    explicit EditorRig(app::AsmaProcessor::Mode mode = app::AsmaProcessor::Mode::FromWrapper)
    {
        f.scan();
        p = std::make_unique<app::AsmaProcessor>(mode);
        p->prepareToPlay(48000.0, 512);
        editor.reset(dynamic_cast<app::AsmaEditor*>(p->createEditorAndMakeActive()));
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

} // namespace asma::test
