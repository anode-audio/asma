// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Browser.h"
#include "LibraryView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace asma::app {

class AsmaProcessor;

// A plain browser: search, results, sync switches, a status line, and
// drag-out. Plan 3c2 replaces the looks; the behaviour stays.
class AsmaEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer,
                   private juce::TableListBoxModel,
                   private juce::Timer {
public:
    explicit AsmaEditor(AsmaProcessor& owner);
    ~AsmaEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Opens a library that appeared, refreshes the rows when it changed, and
    // updates the status line. The timer calls it; tests call it directly.
    void poll();

    // The file a drag out of the window carries: the original, or a render
    // of it when edits or sync change the audio. Never moved.
    bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& details,
                                              juce::StringArray& files, bool& canMoveFiles) override;

    juce::TableListBox& table() { return table_; }
    juce::TextEditor& searchBox() { return search_; }
    juce::String statusText() const { return status_.getText(); }
    // The standalone's tempo source; hidden in a plugin.
    juce::ToggleButton& linkToggle() { return link_; }
    juce::Slider& bpmBox() { return bpm_; }

private:
    enum Column { kName = 1, kBpm, kKey, kType, kLength };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;

    void timerCallback() override { poll(); }
    void searchChanged();
    void syncChanged();
    void showSelection(); // selects the saved file's row without playing it
    void updateStatus();

    AsmaProcessor& processor_;
    LibraryView library_;
    Browser browser_{library_};
    juce::TextEditor search_;
    juce::ToggleButton tempoSync_{"Tempo sync"};
    juce::ToggleButton keySync_{"Key sync"};
    juce::ComboBox projectKey_;
    juce::ToggleButton gainMatch_{"Match loudness"};
    juce::ToggleButton link_{"Ableton Link"};
    juce::Slider bpm_{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft};
    juce::TableListBox table_{"results", this};
    juce::Label status_;
    bool quietSelection_ = false; // selection changes that must not play

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};

} // namespace asma::app
