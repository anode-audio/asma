// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Browser.h"
#include "FileOpsJob.h"
#include "LibraryKeeper.h"
#include "LibraryView.h"
#include "LibraryWriter.h"
#include "Names.h"
#include "PendingEdits.h"
#include "Sidebar.h"
#include "ui/AsmaLookAndFeel.h"
#include "ui/ChipRow.h"
#include "ui/EditMenu.h"
#include "ui/FilterPopovers.h"
#include "ui/Footer.h"
#include "ui/NamePopover.h"
#include "ui/PreviewPanel.h"
#include "ui/ProblemsView.h"
#include "ui/SidebarView.h"
#include "ui/SimilarView.h"
#include "ui/TagsPopover.h"
#include "ui/TopBar.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>

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
    SidebarView& sidebar() { return sidebar_; }
    ChipRow& chipRow() { return chips_; }
    SimilarView& similar() { return similar_; }
    ProblemsView& problems() { return problems_; }
    // The Problems panel in the table's place, until a sidebar entry is picked.
    void showProblems(bool show);
    bool showingProblems() const { return showingProblems_; }
    // What a chip's popover needs: the library's tags and the tempo in force.
    PopoverContext popoverContext();
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
    // What keeps the library in step with its folders while this window,
    // or any other asma window in the process, is open.
    LibraryKeeper& keeper() { return *keeper_; }

    // Organising: the table shows each change at once and the library
    // confirms it; a write that fails rolls back and the footer says why.
    void toggleFavourite(const SearchRow& row);
    void rate(const SearchRow& row, int stars); // the rating it has clears it
    // The table's row as shown, pending edits included; null out of range.
    std::optional<SearchRow> shownRow(int row);
    // Which star of the rating column an x (from the cell's left) falls on,
    // 1 to 5; 0 past the fifth.
    static int starAt(int x);
    // Collections and saved searches from the sidebar and the chip row.
    // Deleting asks first when the collection holds samples.
    void deleteEntry(int index);
    static bool asksBeforeDeleting(const SidebarEntry& entry);
    // The chip row's Save search: names the search in force and saves it.
    std::unique_ptr<NamePopover> saveSearchPopover();
    // A row's right-click menu: Add to collection (ticking those it is in,
    // and New collection…), Tags… and Show in Finder (Explorer, the file
    // manager); and what picking an item does.
    // In the standalone it also offers Rename…, Move to… and Move to Trash.
    juce::PopupMenu rowMenu(const SearchRow& row);
    void rowMenuChosen(const SearchRow& row, int result);
    enum RowMenuItem { kNewCollection = 1, kEditTags, kReveal, kRename, kMoveTo, kTrash, kFirstCollection = 100 };
    static juce::String revealText();
    // A sample's tags to change, as Tags… opens it.
    std::unique_ptr<TagsPopover> tagsPopover(const SearchRow& row);

    // The standalone's file operations (spec section 6), each run by the
    // processor's FileOpsJob; the footer says what came of it. F2 renames,
    // Delete or Backspace trashes, Cmd/Ctrl+Z undoes (outside text fields).
    std::unique_ptr<NamePopover> renamePopover(const SearchRow& row);
    void moveSample(const SearchRow& row, const std::filesystem::path& folder); // the chooser ends here
    void trashSample(const SearchRow& row);
    void undoFileOperation();
    // Asks whether a folder holding library folders takes their place; the
    // app asks in a dialog, tests answer themselves.
    std::function<void(const juce::String& question, std::function<void(bool)> answer)> confirmMerge;

private:
    enum Column { kFavourite = 1, kName, kType, kBpm, kKey, kLength, kRating, kTags };
    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    void returnKeyPressed(int lastRowSelected) override;
    void cellClicked(int row, int column, const juce::MouseEvent& event) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    void timerCallback() override;
    void searchChanged();
    // The one way the search changes: the table, the project, the search box
    // and the sidebar's lit entry all follow.
    void applySearch(const SearchModel& model);
    void refreshSidebar(); // after the library changed
    void showSort(const SearchModel& model); // the header's arrow on the search's sort
    void syncChanged(const audio::SyncSettings& sync);
    void chooseFolder();
    void chooseDestination(const SearchRow& row);
    void showRename(const SearchRow& row);
    // Runs a file operation; when it is done, selects what it names (or,
    // after a trash, the row that took the sample's place).
    void runFileOperation(FileRequest request, int selectRowAfter = -1);
    void selectFile(std::int64_t id);
    void showSelection();   // selects the saved file's row without playing it
    void loadState();       // every control from the processor's state
    void selectionChanged(); // re-reads what the readouts need about the selection
    void pickSimilar(const SearchRow& row);
    void select(const SearchRow& row); // auditions it as the selection, as it is
    void updateReadouts();  // the preview, the chips, the footer, the empty state
    void updateRenderSize();
    // Sends a write; `ticket` is its pending edit (0: none).
    void write(const Write& write, std::uint64_t ticket = 0);
    // Why a collection (index -1: a new one) or saved search may not take a
    // name; nothing when it may.
    std::optional<juce::String> nameRefusal(int index, const juce::String& name);
    void named(int index, const juce::String& name);
    void deleteEntryNow(int index);
    void changeTag(std::int64_t fileId, const std::string& tag, bool added);
    void retry(std::vector<std::int64_t> ids);
    std::vector<std::string> namesOf(EntryKind kind) const;

    AsmaProcessor& processor_;
    AsmaLookAndFeel lookAndFeel_; // first in, last out: every child uses it
    LibraryView library_;
    std::shared_ptr<LibraryKeeper> keeper_;
    std::uint64_t keeperMessages_ = 0; // the keeper's messages this window has shown
    std::uint64_t fileOpsMessages_ = 0; // the file operations' messages it has shown
    std::unique_ptr<EditMenu> editMenu_; // the standalone's, on macOS
    Browser browser_{library_};
    TopBar top_;
    SidebarView sidebar_;
    ChipRow chips_;
    SimilarView similar_;
    std::vector<SidebarEntry> entries_; // what the sidebar lists
    juce::TableListBox table_{"Samples", this};
    juce::Label empty_;
    juce::TextButton emptyAddFolder_{juce::String::fromUTF8("Add folder…")};
    PreviewPanel preview_;
    Footer footer_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String scanMessage_; // the last scan's outcome or failed write, until the next selection
    bool quietSelection_ = false;    // selection changes that must not play
    std::uint64_t loadedStates_ = 0; // the processor's stateLoads() the controls show
    int ticks_ = 0;
    // What the readouts know about the selection.
    std::optional<SearchRow> selected_; // the selection, in the table or not (a Similar pick)
    std::int64_t similarFor_ = 0;       // the sample the Similar list is about
    audio::SampleInfo selectedInfo_;
    std::string selectedFolder_;
    PendingEdits pending_;
    std::int64_t newCollectionFile_ = 0; // the sample a new collection starts with (0: none)
    std::vector<SidebarEntry> menuCollections_; // the collections the last row menu showed
    ProblemsView problems_;
    bool showingProblems_ = false;
    std::set<std::int64_t> retrying_; // files a retry runs for
    bool retryWaiting_ = false;       // a retry waits for a scan

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};

} // namespace asma::app
