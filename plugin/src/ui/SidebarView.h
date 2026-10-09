// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Sidebar.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
#include <vector>

namespace asma::app {

// The left column: All samples and Favourites, then the folders, the
// collections and the saved searches under their headings, each with its
// count, scrolling when they do not fit; Problems at the foot when anything
// failed. It shows what it is told and reports what is picked.
// COLLECTIONS is always there, with a "+" that makes one. A new collection,
// or a collection or saved search being renamed, is a name field in place:
// Return keeps the name unless it is refused, Escape drops it.
class SidebarView : public juce::Component {
public:
    SidebarView();
    ~SidebarView() override;

    void setEntries(std::vector<SidebarEntry> entries);
    // The lit entry; -1 for none.
    void setSelected(int index);
    void setProblems(std::int64_t count);
    // Problems is lit while its panel shows.
    void setProblemsLit(bool lit);

    std::function<void(int)> onPick;
    std::function<void()> onProblems;
    // Naming: index -1 is a new collection. A refusal keeps the field open.
    std::function<std::optional<juce::String>(int index, const juce::String& name)> nameRefusal;
    std::function<void(int index, const juce::String& name)> onNamed;
    std::function<void(int index)> onDelete;
    // Set in the standalone: a folder's menu offers Remove from Library.
    std::function<void(int index)> onRemoveFolder;
    // A new collection's field was dropped (Escape, or another field opened).
    std::function<void()> onNewCancelled;

    // The name field, for a new collection or in place of an entry.
    void startNewCollection();
    void startRename(int index);
    bool isEditing() const { return editing_.has_value(); }
    juce::TextEditor& nameField() { return nameField_; }
    juce::String refusalText() const { return refusal_; }
    juce::Button& addButton() { return *add_; }
    // A collection's or saved search's right-click menu (Rename…, Delete);
    // empty for the others.
    juce::PopupMenu entryMenu(int index) const;
    void entryMenuChosen(int index, int result);
    // What the menu's choice does once it is made.
    std::function<void(int result)> entryMenuHandler(int index);
    juce::Label& refusalLabel() { return refusalLabel_; }
    enum MenuItem { kRename = 1, kDelete, kRemoveFolder };

    int rowCount() const { return rows_.size(); }
    juce::Button& row(int index) { return *rows_[index]; }
    juce::String countText(int index) const;
    juce::StringArray sectionTitles() const;
    juce::Button& problemsButton() { return *problems_; }
    juce::String problemsText() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void finishEditing(bool keep);
    int indexOf(EntryKind kind, std::int64_t id) const; // -1 when not listed
    void showRefusal(const juce::String& text);       // empty: none

    class Content;
    std::vector<SidebarEntry> entries_;
    juce::OwnedArray<juce::Button> rows_;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    std::unique_ptr<juce::Button> problems_;
    std::unique_ptr<juce::Button> add_;
    juce::TextEditor nameField_;
    std::optional<int> editing_; // -1: a new collection
    // What the field renames, so it follows the entry as the list changes.
    EntryKind editingKind_ = EntryKind::All;
    std::int64_t editingId_ = 0;
    juce::String refusal_;
    juce::Label refusalLabel_;
};

} // namespace asma::app
