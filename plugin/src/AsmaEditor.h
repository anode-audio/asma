// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Browser.h"
#include "LibraryView.h"
#include "ui/AsmaLookAndFeel.h"
#include "ui/Footer.h"
#include "ui/PreviewPanel.h"
#include "ui/TopBar.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace asma::app {

class AsmaProcessor;

// asma's window, laid out as spec section 9 and the approved design: the
// top bar, the sample table, the preview panel and the footer. The sidebar,
// the chip row and the Similar list keep their places empty until plan 3c2b.
class AsmaEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer,
                   private juce::TableListBoxModel,
                   private juce::Timer {
public:
    static constexpr int kDefaultWidth = 1100, kDefaultHeight = 720;
    static constexpr int kMinWidth = 900, kMinHeight = 600;

    explicit AsmaEditor(AsmaProcessor& owner);
    ~AsmaEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    // Opens a library that appeared, refreshes the rows when it changed, and
    // brings every readout up to date. The timer calls it; tests call it directly.
    void poll();
    // The file a drag out of the window carries: the original, or a render
    // of it when edits or sync change the audio. Never moved.
    bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& details,
                                              juce::StringArray& files, bool& canMoveFiles) override;
    juce::TableListBox& table() { return table_; }
    juce::TextEditor& searchBox() { return top_.searchBox(); }
    PreviewPanel& preview() { return preview_; }
    Footer& footer() { return footer_; }
    TopBar& topBar() { return top_; }
    // The standalone's tempo source and folders; hidden in a plugin.
    juce::Button& linkToggle() { return top_.linkChip(); }
    TempoBox& bpmBox() { return top_.tempoBox(); }
    juce::TextButton& addFolderButton() { return top_.addFolderButton(); }
    // What the table area says when it has no rows to show; empty when it has.
    juce::String emptyText() const { return empty_.getText(); }
    // Standalone: adds a sample folder and scans it (the button's chooser
    // ends here).
    void addFolder(const std::filesystem::path& folder);
    // Deletes every kept render; the footer's button asks first.
    void clearRenders();

private:
    enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    void returnKeyPressed(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    void timerCallback() override;
    void searchChanged();
    void syncChanged(const audio::SyncSettings& sync);
    void chooseFolder();
    void showSelection();   // selects the saved file's row without playing it
    void loadState();       // every control from the processor's state
    void selectionChanged(); // re-reads what the readouts need about the selection
    void updateReadouts();  // the preview, the chips, the footer, the empty state
    void updateRenderSize();

    AsmaProcessor& processor_;
    AsmaLookAndFeel lookAndFeel_; // first in, last out: every child uses it
    LibraryView library_;
    Browser browser_{library_};
    TopBar top_;
    juce::TableListBox table_{"Samples", this};
    juce::Label empty_;
    juce::TextButton emptyAddFolder_{juce::String::fromUTF8("Add folder…")};
    PreviewPanel preview_;
    Footer footer_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String scanMessage_; // the last scan's outcome, until the next selection
    bool quietSelection_ = false;    // selection changes that must not play
    bool quietSort_ = false;         // a header showing the saved sort is not a new sort
    std::uint64_t loadedStates_ = 0; // the processor's stateLoads() the controls show
    int ticks_ = 0;
    // What the readouts know about the selection.
    int selectedRow_ = -1;
    audio::SampleInfo selectedInfo_;
    std::string selectedFolder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};

} // namespace asma::app
