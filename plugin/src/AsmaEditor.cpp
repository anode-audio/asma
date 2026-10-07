// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"

#include "AsmaProcessor.h"
#include "DragOut.h"
#include "TempoChip.h"
#include "asma/audio/Render.h"
#include "asma/core/Fs.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace asma::app {

namespace {

constexpr int kTimerHz = 30;      // the playhead moves smoothly
constexpr int kLibraryEvery = 6;  // ticks between library checks: 5 a second
constexpr int kRendersEvery = 60; // ticks between measuring the renders: every 2 s
constexpr int kStarColumn = 48;   // the favourite star, after the table's margin

// The sort a column gives; nothing for those that do not sort.
std::optional<SortField> sortFor(int column)
{
    switch (column) {
    case 2: return SortField::Name;
    case 4: return SortField::Bpm;
    case 5: return SortField::Key;
    case 6: return SortField::Duration;
    case 7: return SortField::Rating;
    default: return std::nullopt;
    }
}

juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

// The rating column's stars, lit and faint alike.
juce::Font starFont() { return theme::font(theme::Face::Text, 11.0f).withExtraKerningFactor(0.15f); }

} // namespace

AsmaEditor::AsmaEditor(AsmaProcessor& owner)
    : juce::AudioProcessorEditor(owner), processor_(owner),
      library_(owner.libraryPath(), owner.isStandalone() ? LibraryView::Access::MayMigrate : LibraryView::Access::ReadOnly),
      top_(owner.isStandalone())
{
    setLookAndFeel(&lookAndFeel_);
    const PluginState state = processor_.pluginState();

    top_.searchBox().onTextChange = [this] { searchChanged(); };
    addAndMakeVisible(top_);
    sidebar_.onPick = [this](int index) {
        showProblems(false);
        if (index >= 0 && index < static_cast<int>(entries_.size()))
            applySearch(withEntry(entries_[static_cast<std::size_t>(index)], browser_.searchModel()));
    };
    sidebar_.onProblems = [this] { showProblems(true); };
    problems_.onRetry = [this](std::vector<std::int64_t> ids) { retry(std::move(ids)); };
    addChildComponent(problems_);
    sidebar_.nameRefusal = [this](int index, const juce::String& name) { return nameRefusal(index, name); };
    sidebar_.onNamed = [this](int index, const juce::String& name) { named(index, name); };
    sidebar_.onDelete = [this](int index) { deleteEntry(index); };
    addAndMakeVisible(sidebar_);
    chips_.onChange = [this](const SearchModel& model) { applySearch(model); };
    similar_.onPick = [this](const SearchRow& row) { pickSimilar(row); };
    addAndMakeVisible(similar_);
    chips_.onOpen = [this](Facet facet, juce::Component& anchor) {
        auto popover = makeFilterPopover(facet, browser_.searchModel(), popoverContext(),
                                         [this](const SearchModel& model) { applySearch(model); });
        // Inside the editor, so a plugin's popover stays in its own window.
        juce::CallOutBox::launchAsynchronously(std::move(popover), getLocalArea(&anchor, anchor.getLocalBounds()), this);
    };
    chips_.onSaveSearch = [this](juce::Component& anchor) {
        juce::CallOutBox::launchAsynchronously(saveSearchPopover(), getLocalArea(&anchor, anchor.getLocalBounds()), this);
    };
    addAndMakeVisible(chips_);
    if (processor_.isStandalone()) {
        top_.tempoBox().onValueChange = [this] {
            const double bpm = top_.tempoBox().getValue();
            processor_.setManualBpm(bpm);
            if (top_.linkChip().getToggleState()) processor_.setLinkTempo(bpm);
        };
        top_.linkChip().onClick = [this] { processor_.setLinkEnabled(top_.linkChip().getToggleState()); };
        top_.addFolderButton().onClick = [this] { chooseFolder(); };
        emptyAddFolder_.onClick = [this] { chooseFolder(); };
    }

    auto& header = table_.getHeader();
    using Header = juce::TableHeaderComponent;
    const int fixed = Header::visible | Header::notSortable; // neither sorts nor resizes
    const int sorts = Header::visible | Header::resizable | Header::sortable;
    const int plain = Header::visible | Header::resizable | Header::notSortable;
    header.addColumn({}, kFavourite, kStarColumn, kStarColumn, kStarColumn, fixed);
    header.addColumn("Name", kName, 400, 120, -1, sorts);
    header.addColumn("Type", kType, 76, 60, 120, plain);
    header.addColumn("BPM", kBpm, 70, 50, 120, sorts);
    header.addColumn("Key", kKey, 56, 40, 100, sorts);
    header.addColumn("Length", kLength, 72, 50, 120, sorts);
    header.addColumn("Rating", kRating, 88, 70, 120, sorts);
    header.addColumn("Tags", kTags, 140, 60, 400, plain);
    header.setStretchToFitActive(true);
    table_.setHeaderHeight(theme::kHeaderRowHeight);
    table_.setRowHeight(theme::kRowHeight);
    table_.setMultipleSelectionEnabled(false);
    table_.setTitle("Samples");
    addAndMakeVisible(table_);

    empty_.setJustificationType(juce::Justification::centred);
    empty_.setFont(theme::font(theme::Face::Text, 13.0f));
    empty_.setColour(juce::Label::textColourId, theme::muted);
    addChildComponent(empty_);
    addChildComponent(emptyAddFolder_);

    preview_.onPlayStop = [this] {
        if (processor_.engine().status().playing) processor_.engine().stop();
        else processor_.engine().play();
    };
    preview_.onEditsChanged = [this](const audio::Edits& edits) {
        processor_.setEdits(edits);
        updateReadouts();
    };
    preview_.onTempoSync = [this](bool on) {
        audio::SyncSettings sync = processor_.pluginState().sync;
        sync.tempo = on;
        syncChanged(sync);
    };
    preview_.onKeySync = [this](std::optional<std::string> key) {
        audio::SyncSettings sync = processor_.pluginState().sync;
        sync.key = key.has_value();
        if (key) sync.projectKey = audio::KeyName(*key);
        syncChanged(sync);
    };
    preview_.onGainMatch = [this](bool on) {
        processor_.updateState([&](PluginState& s) { s.gainMatch = on; });
        processor_.engine().setGainMatch(on);
    };
    preview_.onQuantise = [this](double beats) {
        processor_.updateState([&](PluginState& s) { s.quantise = beats; });
        processor_.engine().setQuantise(beats);
    };
    addAndMakeVisible(preview_);

    footer_.onClearRenders = [this] {
        const auto bytes = audio::RenderStore(audio::RenderStore::defaultDir()).bytes();
        juce::NativeMessageBox::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Clear renders",
            "Delete " + Footer::sizeText(bytes) + " of rendered drag-outs? A project that plays a render from where it lies "
                "(Reaper, or Live without Collect All and Save) will lose that audio.",
            this, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<AsmaEditor>(this)](int ok) {
                if (ok != 0 && safe) safe->clearRenders();
            }));
    };
    addAndMakeVisible(footer_);

    setResizable(true, true);
    setResizeLimits(kMinWidth, kMinHeight, 8000, 8000);
    setSize(std::max(state.width, kMinWidth), std::max(state.height, kMinHeight));
    setWantsKeyboardFocus(true);
    loadState();
    updateRenderSize();
    poll();
    startTimerHz(kTimerHz);
}

AsmaEditor::~AsmaEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void AsmaEditor::loadState()
{
    loadedStates_ = processor_.stateLoads();
    const PluginState state = processor_.pluginState();
    top_.searchBox().setText(state.search.text, false);
    preview_.setEdits(state.edits);
    preview_.setGainMatch(state.gainMatch);
    preview_.setQuantise(state.quantise);
    preview_.setKey(state.sync.key, std::string(state.sync.projectKey.view()), {});
    if (processor_.isStandalone()) {
        top_.tempoBox().setValue(state.sync.hostBpm > 0.0 ? state.sync.hostBpm : 120.0, juce::dontSendNotification);
        top_.linkChip().setToggleState(state.link, juce::dontSendNotification);
    }
    browser_.setSearch(state.search);
    chips_.setModel(state.search);
    refreshSidebar();
    showSort(state.search);
    table_.updateContent();
    showSelection();
}

void AsmaEditor::showSort(const SearchModel& model)
{
    for (int column = kFavourite; column <= kTags; ++column)
        if (sortFor(column) == model.sort) table_.getHeader().setSortColumnId(column, !model.descending);
}

void AsmaEditor::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    // The header tells us later (it reports through the message loop), also
    // when it only shows the search's own sort: that is no change.
    const auto field = sortFor(newSortColumnId);
    const SearchModel& current = browser_.searchModel();
    if (!field || (*field == current.sort && current.descending == !isForwards)) return;
    SearchModel model = current;
    model.sort = *field;
    model.descending = !isForwards;
    applySearch(model); // the selection keeps its sample, on its new row
}

void AsmaEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    auto area = getLocalBounds();
    area.removeFromTop(theme::kTopBarHeight);
    area.removeFromBottom(theme::kFooterHeight);
    auto bottom = area.removeFromBottom(theme::kPreviewHeight);
    // The bottom panel, with the Similar list's place (plan 3c2b) on its right.
    g.setColour(theme::panel);
    g.fillRect(bottom);
    g.setColour(theme::border);
    g.fillRect(bottom.getX(), bottom.getY(), bottom.getWidth(), 1);
    g.fillRect(bottom.getRight() - theme::kSimilarWidth, bottom.getY(), 1, bottom.getHeight());

}

void AsmaEditor::resized()
{
    auto area = getLocalBounds();
    top_.setBounds(area.removeFromTop(theme::kTopBarHeight));
    footer_.setBounds(area.removeFromBottom(theme::kFooterHeight));
    auto bottom = area.removeFromBottom(theme::kPreviewHeight);
    bottom.removeFromTop(1);
    similar_.setBounds(bottom.removeFromRight(theme::kSimilarWidth).withTrimmedLeft(1));
    preview_.setBounds(bottom);
    sidebar_.setBounds(area.removeFromLeft(theme::kSidebarWidth));
    problems_.setBounds(area);
    chips_.setBounds(area.removeFromTop(theme::kChipRowHeight));
    table_.setBounds(area);
    empty_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 520), 60).translated(0, -20));
    emptyAddFolder_.setBounds(area.withSizeKeepingCentre(120, 30).translated(0, 30));
    processor_.updateState([&](PluginState& s) {
        s.width = getWidth();
        s.height = getHeight();
    });
}

bool AsmaEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) {
        if (processor_.engine().status().playing) processor_.engine().stop();
        else processor_.engine().play();
        return true;
    }
    // F and 0 to 5 organise the selection. A text field keeps what is typed
    // in it; this is for keys that reach the window from anywhere else.
    const int c = key.getTextCharacter() != 0 ? static_cast<int>(key.getTextCharacter()) : key.getKeyCode();
    const bool typing = dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    if (!typing && selected_ && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown()) {
        const auto row = pending_.apply(*selected_);
        if (c == 'f' || c == 'F') {
            toggleFavourite(row);
            return true;
        }
        if (c >= '0' && c <= '5') {
            const int stars = static_cast<int>(c - '0');
            if (stars != row.rating.value_or(0)) rate(row, stars);
            return true;
        }
    }
    return false;
}

int AsmaEditor::starAt(int x)
{
    const float advance =
        juce::GlyphArrangement::getStringWidth(starFont(), juce::String::fromUTF8("\u2605\u2605\u2605\u2605\u2605")) / 5.0f;
    if (x < 0 || advance <= 0.0f) return 0;
    const int star = static_cast<int>(static_cast<float>(x) / advance) + 1;
    return star <= 5 ? star : 0;
}

std::optional<SearchRow> AsmaEditor::shownRow(int row)
{
    const SearchRow* found = browser_.row(row);
    if (!found) return std::nullopt;
    return pending_.apply(*found);
}

void AsmaEditor::toggleFavourite(const SearchRow& row)
{
    const bool on = !pending_.apply(row).favourite;
    write(Write::favourite(row.id, on), pending_.setFavourite(row.id, on));
}

void AsmaEditor::rate(const SearchRow& row, int stars)
{
    const int rating = pending_.apply(row).rating == stars ? 0 : stars;
    write(Write::rate(row.id, rating), pending_.setRating(row.id, rating));
}

void AsmaEditor::write(const Write& w, std::uint64_t ticket)
{
    table_.repaint();
    processor_.writer().write(w, [safe = juce::Component::SafePointer<AsmaEditor>(this), w, ticket](const std::string& error) {
        if (!safe) return;
        safe->pending_.finished(ticket, error.empty());
        if (!error.empty()) safe->scanMessage_ = utf8(failureText(w, error));
        safe->table_.repaint();
        safe->updateReadouts();
    });
}

std::vector<std::string> AsmaEditor::namesOf(EntryKind kind) const
{
    std::vector<std::string> names;
    for (const auto& e : entries_)
        if (e.kind == kind) names.push_back(e.name);
    return names;
}

std::optional<juce::String> AsmaEditor::nameRefusal(int index, const juce::String& name)
{
    const bool existing = index >= 0 && index < static_cast<int>(entries_.size());
    const EntryKind kind = existing ? entries_[static_cast<std::size_t>(index)].kind : EntryKind::Collection;
    const std::string current = existing ? entries_[static_cast<std::size_t>(index)].name : std::string();
    switch (checkName(name.toStdString(), namesOf(kind), current)) {
    case NameCheck::Empty: return juce::String();
    case NameCheck::Taken:
        return kind == EntryKind::SavedSearch ? "A saved search with that name exists."
                                              : "A collection with that name exists.";
    case NameCheck::Ok:
    case NameCheck::Unchanged: break;
    }
    return std::nullopt;
}

void AsmaEditor::named(int index, const juce::String& name)
{
    const std::string wanted = trimmedName(name.toStdString());
    if (index < 0) {
        write(Write::createCollection(wanted, std::exchange(newCollectionFile_, 0)));
        return;
    }
    if (index >= static_cast<int>(entries_.size())) return;
    const SidebarEntry& entry = entries_[static_cast<std::size_t>(index)];
    if (checkName(wanted, {}, entry.name) == NameCheck::Unchanged) return;
    if (entry.kind == EntryKind::Collection) write(Write::renameCollection(entry.name, wanted));
    if (entry.kind == EntryKind::SavedSearch) write(Write::renameSearch(entry.name, wanted));
}

bool AsmaEditor::asksBeforeDeleting(const SidebarEntry& entry)
{
    return entry.kind == EntryKind::Collection && entry.count > 0;
}

void AsmaEditor::deleteEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    const SidebarEntry& entry = entries_[static_cast<std::size_t>(index)];
    if (!asksBeforeDeleting(entry)) return deleteEntryNow(index);
    const juce::String name = juce::String::fromUTF8(entry.name.c_str());
    juce::NativeMessageBox::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Delete collection",
        "Delete the collection \"" + name + "\"? Its " + juce::String(entry.count)
            + (entry.count == 1 ? " sample stays" : " samples stay") + " in the library.",
        this, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<AsmaEditor>(this), index, name](int ok) {
            // Only if the entry is still the one asked about.
            if (ok != 0 && safe && index < static_cast<int>(safe->entries_.size())
                && juce::String::fromUTF8(safe->entries_[static_cast<std::size_t>(index)].name.c_str()) == name)
                safe->deleteEntryNow(index);
        }));
}

void AsmaEditor::deleteEntryNow(int index)
{
    const SidebarEntry entry = entries_[static_cast<std::size_t>(index)];
    const bool lit = entryFor(entries_, browser_.searchModel()) == index;
    if (entry.kind == EntryKind::Collection) write(Write::deleteCollection(entry.name));
    else if (entry.kind == EntryKind::SavedSearch) write(Write::deleteSearch(entry.name));
    else return;
    // What was showing has gone: back to All samples.
    if (lit && !entries_.empty()) applySearch(withEntry(entries_.front(), browser_.searchModel()));
}

std::unique_ptr<NamePopover> AsmaEditor::saveSearchPopover()
{
    return std::make_unique<NamePopover>(
        "Save search", juce::String(),
        [this](const juce::String& name) -> std::optional<juce::String> {
            switch (checkName(name.toStdString(), namesOf(EntryKind::SavedSearch))) {
            case NameCheck::Empty: return juce::String();
            case NameCheck::Taken: return juce::String("A saved search with that name exists.");
            case NameCheck::Ok:
            case NameCheck::Unchanged: break;
            }
            return std::nullopt;
        },
        [this](const juce::String& name) {
            write(Write::saveSearch(trimmedName(name.toStdString()), browser_.searchModel()));
        });
}

juce::String AsmaEditor::revealText()
{
#if JUCE_MAC
    return "Show in Finder";
#elif JUCE_WINDOWS
    return "Show in Explorer";
#else
    return "Show in the file manager";
#endif
}

juce::PopupMenu AsmaEditor::rowMenu(const SearchRow& row)
{
    const auto in = library_.collectionsOf(row.id);
    juce::PopupMenu collections;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (e.kind != EntryKind::Collection) continue;
        const bool ticked = std::find(in.begin(), in.end(), e.id) != in.end();
        collections.addItem(kFirstCollection + static_cast<int>(i), utf8(e.name), true, ticked);
    }
    if (collections.getNumItems() > 0) collections.addSeparator();
    collections.addItem(kNewCollection, juce::String::fromUTF8("New collection…"));
    juce::PopupMenu menu;
    menu.addSubMenu("Add to collection", collections);
    menu.addItem(kEditTags, juce::String::fromUTF8("Tags…"));
    menu.addSeparator();
    menu.addItem(kReveal, revealText());
    return menu;
}

void AsmaEditor::rowMenuChosen(const SearchRow& row, int result)
{
    if (result == kNewCollection) {
        newCollectionFile_ = row.id; // the sidebar's name field makes it, with this sample in it
        sidebar_.startNewCollection();
    } else if (result == kEditTags) {
        const int at = browser_.rowOf(LibraryView::pathOf(row));
        const auto area = at >= 0 ? getLocalArea(&table_, table_.getRowPosition(at, true)) : table_.getBounds();
        juce::CallOutBox::launchAsynchronously(tagsPopover(row), area, this);
    } else if (result == kReveal) {
        juce::File(utf8(toUtf8(LibraryView::pathOf(row)))).revealToUser();
    } else if (result >= kFirstCollection) {
        const auto index = static_cast<std::size_t>(result - kFirstCollection);
        if (index >= entries_.size() || entries_[index].kind != EntryKind::Collection) return;
        const auto in = library_.collectionsOf(row.id);
        const auto& e = entries_[index];
        if (std::find(in.begin(), in.end(), e.id) != in.end()) write(Write::removeFromCollection(e.name, row.id));
        else write(Write::addToCollection(e.name, row.id));
    }
}

std::unique_ptr<TagsPopover> AsmaEditor::tagsPopover(const SearchRow& row)
{
    std::vector<TagsPopover::Tag> tags;
    for (const auto& [name, source] : library_.tagsOf(row.id)) tags.push_back({name, source == TagSource::User});
    // The user's first, then the rest, each by name.
    std::stable_sort(tags.begin(), tags.end(), [](const auto& a, const auto& b) {
        return a.user != b.user ? a.user : a.name < b.name;
    });
    return std::make_unique<TagsPopover>(utf8(row.name), std::move(tags), library_.tagCounts(),
                                         [this, id = row.id](const std::string& tag, bool added) {
                                             changeTag(id, tag, added);
                                         });
}

void AsmaEditor::changeTag(std::int64_t fileId, const std::string& tag, bool added)
{
    // The table's tags column shows the change at once.
    std::vector<std::string> shown;
    for (const auto& [name, source] : library_.tagsOf(fileId)) shown.push_back(name);
    SearchRow probe;
    probe.id = fileId;
    probe.tags = shown;
    shown = pending_.apply(probe).tags;
    shown.erase(std::remove(shown.begin(), shown.end(), tag), shown.end());
    if (added) shown.push_back(tag);
    const auto ticket = pending_.setTags(fileId, shown);
    write(added ? Write::addTag(fileId, tag) : Write::removeTag(fileId, tag), ticket);
}

void AsmaEditor::showProblems(bool show)
{
    showingProblems_ = show;
    if (show) problems_.setProblems(library_.problems());
    problems_.setVisible(show);
    chips_.setVisible(!show);
    table_.setVisible(!show);
    sidebar_.setProblemsLit(show);
    updateReadouts();
}

void AsmaEditor::retry(std::vector<std::int64_t> ids)
{
    retrying_.insert(ids.begin(), ids.end());
    problems_.setRetrying(retrying_, retryWaiting_);
    processor_.writer().retry(ids, [safe = juce::Component::SafePointer<AsmaEditor>(this), ids](const RetryEvent& e) {
        if (!safe) return;
        if (e.kind == RetryEvent::Kind::Waiting) {
            safe->retryWaiting_ = true;
            safe->scanMessage_ = "Retrying after the scan";
        } else {
            for (const auto id : ids) safe->retrying_.erase(id);
            if (safe->retrying_.empty()) safe->retryWaiting_ = false;
            safe->scanMessage_ = e.kind == RetryEvent::Kind::Failed ? "Could not retry: " + utf8(e.error) : juce::String();
            // Whatever the library now says about those files.
            safe->problems_.setProblems(safe->library_.problems());
            safe->refreshSidebar();
        }
        safe->problems_.setRetrying(safe->retrying_, safe->retryWaiting_);
        safe->updateReadouts();
    });
}

void AsmaEditor::cellClicked(int row, int column, const juce::MouseEvent& event)
{
    const auto shown = shownRow(row);
    if (!shown) return;
    if (event.mods.isPopupMenu()) {
        const auto area = getLocalArea(&table_, table_.getRowPosition(row, true));
        rowMenu(*shown).showMenuAsync(
            juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(area)).withParentComponent(this),
            [safe = juce::Component::SafePointer<AsmaEditor>(this), r = *shown](int result) {
                if (safe && result != 0) safe->rowMenuChosen(r, result);
            });
        return;
    }
    if (column == kFavourite) toggleFavourite(*shown);
    if (column == kRating) {
        // The event is the row's: measure from the cell's left.
        auto& header = table_.getHeader();
        const int left = header.getColumnPosition(header.getIndexOfColumnId(kRating, true)).getX();
        if (const int star = starAt(event.x - left)) rate(*shown, star);
    }
}

void AsmaEditor::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>("Add a sample folder");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser) {
                              const juce::File folder = chooser.getResult();
                              if (folder != juce::File()) addFolder(fromUtf8(folder.getFullPathName().toStdString()));
                          });
}

void AsmaEditor::addFolder(const std::filesystem::path& folder)
{
    ScanJob* scans = processor_.scans();
    if (!scans) return;
    std::string why;
    scanMessage_ = scans->addAndScan(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
    updateReadouts();
}

void AsmaEditor::clearRenders()
{
    audio::RenderStore store(audio::RenderStore::defaultDir());
    store.clear();
    // A render a program still holds open, or a folder asma may not write,
    // stays: say so rather than leave the size standing unexplained.
    if (store.bytes() > 0) scanMessage_ = "Some renders could not be deleted; another program may be using them.";
    updateRenderSize();
    updateReadouts();
}

void AsmaEditor::updateRenderSize()
{
    std::uintmax_t bytes = 0;
    try {
        bytes = audio::RenderStore(audio::RenderStore::defaultDir()).bytes();
    } catch (const std::exception&) {
        // No renders folder yet, or not readable: nothing to offer.
    }
    footer_.setRenderBytes(bytes);
}

void AsmaEditor::timerCallback()
{
    ++ticks_;
    if (ticks_ % kRendersEvery == 0) updateRenderSize();
    if (ticks_ % kLibraryEvery == 0) poll();
    else updateReadouts(); // the playhead and the chips keep up between library checks
}

void AsmaEditor::poll()
{
    // A host or preset menu loaded state while the window was open.
    if (processor_.stateLoads() != loadedStates_) loadState();
    if (ScanJob* scans = processor_.scans())
        if (const auto report = scans->takeReport()) {
            using Result = ScanReport::Result;
            switch (report->result) {
            case Result::Finished:
                scanMessage_ = "Scan finished: " + juce::String(report->index.added) + " added";
                break;
            case Result::Locked: scanMessage_ = "Another asma is scanning this library; try again when it is done."; break;
            case Result::Cancelled: scanMessage_ = "Scan cancelled."; break;
            case Result::Failed:
            case Result::Crashed: scanMessage_ = "Scan failed: " + juce::String(report->message); break;
            }
        }
    if (browser_.poll()) {
        pending_.libraryChanged();
        table_.updateContent();
        similarFor_ = 0; // the library changed: analysis may have reached the selection
        showSelection();
        refreshSidebar();
        if (showingProblems_) problems_.setProblems(library_.problems());
    }
    updateReadouts();
}

void AsmaEditor::searchChanged()
{
    SearchModel model = browser_.searchModel();
    model.text = top_.searchBox().getText().toStdString();
    applySearch(model);
}

void AsmaEditor::applySearch(const SearchModel& model)
{
    browser_.setSearch(model);
    processor_.updateState([&](PluginState& s) { s.search = model; });
    if (top_.searchBox().getText().toStdString() != model.text) top_.searchBox().setText(model.text, false);
    chips_.setModel(model);
    showSort(model); // a saved search brings its sort
    table_.updateContent();
    showSelection();
    updateReadouts();
}

PopoverContext AsmaEditor::popoverContext() { return {library_.tagCounts(), processor_.tempoInForce()}; }

void AsmaEditor::refreshSidebar()
{
    entries_ = sidebarEntries(library_);
    sidebar_.setEntries(entries_);
    sidebar_.setProblems(library_.problemCount());
}

void AsmaEditor::syncChanged(const audio::SyncSettings& sync)
{
    processor_.updateState([&](PluginState& s) { s.sync = sync; });
    processor_.engine().setSync(sync);
    updateReadouts();
}

void AsmaEditor::showSelection()
{
    int row = -1;
    try {
        row = browser_.rowOf(fromUtf8(processor_.pluginState().selected));
    } catch (const std::exception&) {
        // A saved path that is not valid UTF-8 selects nothing.
    }
    const juce::ScopedValueSetter quiet(quietSelection_, true);
    if (row >= 0) table_.selectRow(row);
    else table_.deselectAllRows();
    selectionChanged();
}

void AsmaEditor::selectionChanged()
{
    if (const SearchRow* r = browser_.row(table_.getSelectedRow())) {
        selected_ = *r;
    } else {
        // No row: keep a Similar pick the search does not show, as long as it
        // is still the project's selection.
        const bool kept = selected_ && toUtf8(LibraryView::pathOf(*selected_)) == processor_.pluginState().selected;
        if (!kept) selected_.reset();
    }
    selectedInfo_ = selected_ ? library_.info(selected_->id) : audio::SampleInfo{};
    selectedFolder_.clear();
    if (selected_) {
        const auto slash = selected_->relPath.find_last_of('/');
        if (slash != std::string::npos) selectedFolder_ = selected_->relPath.substr(0, slash);
    }
    const std::int64_t id = selected_ ? selected_->id : 0;
    if (id != similarFor_) {
        similarFor_ = id;
        if (id) similar_.setResult(library_.similar(id));
        else similar_.clear();
    }
}

void AsmaEditor::select(const SearchRow& row)
{
    scanMessage_.clear();
    processor_.select(LibraryView::pathOf(row), library_.info(row.id));
    preview_.setEdits({}); // a new selection plays as it is
}

void AsmaEditor::pickSimilar(const SearchRow& row)
{
    select(row);
    selected_ = row;
    const int at = browser_.rowOf(LibraryView::pathOf(row));
    {
        const juce::ScopedValueSetter quiet(quietSelection_, true); // already playing
        if (at >= 0) table_.selectRow(at);
        else table_.deselectAllRows();
    }
    selectionChanged();
    updateReadouts();
}

void AsmaEditor::updateReadouts()
{
    const PluginState state = processor_.pluginState();
    const audio::EngineStatus status = processor_.engine().status();
    const bool current = status.generation == processor_.engine().selected(); // the status is the selection's

    // The top bar and the sidebar's lit entry.
    sidebar_.setSelected(entryFor(entries_, browser_.searchModel()));
    top_.setCount(browser_.count(), static_cast<int>(browser_.total()));
    if (!processor_.isStandalone()) top_.setHostBpm(processor_.hostBpm());

    // The table, or what it says instead.
    juce::String empty;
    const bool standalone = processor_.isStandalone();
    if (library_.state() == LibraryState::Missing)
        empty = standalone ? "No library yet. Add a folder of samples to start."
                           : "No library yet. Open the asma app and add a folder of samples.";
    else if (library_.state() != LibraryState::Open)
        empty = juce::String(library_.message());
    else if (browser_.count() == 0)
        // Empty because there is nothing at all, or because the search (its
        // text, chips or scope) matches nothing.
        empty = browser_.total() == 0 ? (standalone ? "The library is empty. Add a folder of samples." : "The library is empty.")
                                      : "No samples match.";
    if (showingProblems_) empty.clear(); // the panel says its own
    if (empty != empty_.getText()) empty_.setText(empty, juce::dontSendNotification);
    empty_.setVisible(empty.isNotEmpty());
    // "Add folder" where there is nothing yet, not where a search found nothing.
    const bool nothing = library_.state() == LibraryState::Missing
                      || (library_.state() == LibraryState::Open && browser_.total() == 0);
    emptyAddFolder_.setVisible(standalone && nothing && !showingProblems_);

    // The preview.
    if (selected_) {
        const SearchRow& r = *selected_;
        const auto overview = processor_.engine().overview();
        preview_.waveform().setOverview(overview);
        preview_.setFile(utf8(r.name), overview ? PreviewPanel::fileLine(selectedFolder_, overview->sampleRate,
                                                                          overview->channels(), overview->seconds())
                                                : PreviewPanel::fileLine(selectedFolder_, 0, 0, r.duration));
        preview_.waveform().setPlayhead(current && status.playing ? std::optional<double>(status.position) : std::nullopt);
    } else {
        preview_.waveform().setOverview(nullptr);
        preview_.setFile({}, {});
    }
    preview_.setPlaying(status.playing);
    audio::SyncSettings sync = state.sync;
    sync.hostBpm = processor_.tempoInForce();
    preview_.setTempo(state.sync.tempo, selected_ ? tempoChip(selectedInfo_, sync, current && status.failed)
                                                          : ChipText{state.sync.tempo ? "" : "off", Tone::Muted});
    std::string keyStatus;
    if (current && status.keyUnsure) keyStatus = "?";
    else if (current && status.keySynced && (status.semitones < 0.0 || status.semitones > 0.0))
        keyStatus = (status.semitones > 0.0 ? "+" : "") + std::to_string(std::llround(status.semitones));
    preview_.setKey(state.sync.key, std::string(state.sync.projectKey.view()), keyStatus);

    // The footer.
    if (selected_) {
        const audio::RenderSettings drag =
            dragSettings(state.edits, audio::planSync(selectedInfo_, sync), static_cast<int>(processor_.sampleRate()));
        footer_.setDrag(utf8(dragSummary(drag, selectedInfo_.bpm)));
    } else {
        footer_.setDrag({});
    }
    ScanJob* scans = processor_.scans();
    footer_.setStatus(scans && scans->busy() ? juce::String(scans->progress()) : scanMessage_);
}

int AsmaEditor::getNumRows() { return browser_.count(); }

void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int width, int height, bool selected)
{
    if (selected) {
        g.fillAll(theme::amber.withAlpha(0.10f));
        g.setColour(theme::amber);
        g.fillRect(0, 0, 2, height);
    }
    g.setColour(theme::raised);
    g.fillRect(0, height - 1, width, 1);
}

void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto shown = shownRow(row);
    if (!shown) return;
    const SearchRow& r = *shown;
    juce::String text;
    juce::Font font = theme::font(theme::Face::Mono, 12.0f);
    juce::Colour colour = theme::text;
    int x = 0;
    switch (column) {
    case kFavourite:
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(r.favourite ? theme::amber : theme::faint);
        g.drawText(juce::String::fromUTF8(r.favourite ? "\u2605" : "\u2606"), AsmaLookAndFeel::kTableMargin, 0, 20,
                   height - 1, juce::Justification::centredLeft, false);
        return;
    case kRating: {
        // Lit stars for the rating, faint ones for the rest.
        const int lit = r.rating.value_or(0);
        g.setFont(starFont());
        juce::String on, off;
        for (int i = 0; i < 5; ++i) (i < lit ? on : off) << juce::String::fromUTF8("\u2605");
        const int onWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), on)));
        g.setColour(theme::amber);
        g.drawText(on, 0, 0, onWidth, height - 1, juce::Justification::centredLeft, false);
        g.setColour(theme::faint);
        g.drawText(off, onWidth, 0, width - onWidth, height - 1, juce::Justification::centredLeft, false);
        return;
    }
    case kTags: {
        juce::StringArray tags;
        for (const auto& t : r.tags) tags.add(utf8(t));
        text = tags.joinIntoString(", ");
        font = theme::font(theme::Face::Text, 12.0f);
        colour = theme::muted;
        break;
    }
    case kName:
        text = utf8(r.name);
        font = theme::font(theme::Face::Text, 13.0f);
        break;
    case kType:
        text = !r.isLoop ? "" : (*r.isLoop ? "loop" : "one-shot");
        font = theme::font(theme::Face::Text, 13.0f);
        colour = theme::muted;
        break;
    case kBpm: text = r.bpm ? utf8(bpmText(*r.bpm)) : juce::String(); break;
    case kKey: text = r.key ? utf8(*r.key) : juce::String(); break;
    case kLength:
        text = utf8(lengthText(r.duration));
        colour = theme::muted;
        break;
    default: break;
    }
    g.setFont(font);
    g.setColour(colour);
    g.drawText(text, x, 0, width - x - 6, height - 1, juce::Justification::centredLeft, true);
}

void AsmaEditor::selectedRowsChanged(int lastRowSelected)
{
    selectionChanged();
    if (quietSelection_ || lastRowSelected < 0 || !selected_) return;
    select(*selected_);
    updateReadouts();
}

void AsmaEditor::returnKeyPressed(int) { processor_.engine().play(); }

juce::var AsmaEditor::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var("asma-sample");
}

bool AsmaEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails&,
                                                      juce::StringArray& files, bool& canMoveFiles)
{
    if (!selected_) return false;
    canMoveFiles = false;
    const auto path = LibraryView::pathOf(*selected_);
    const PluginState state = processor_.pluginState();
    audio::SyncSettings sync = state.sync;
    sync.hostBpm = processor_.tempoInForce();
    const audio::RenderSettings settings =
        dragSettings(state.edits, audio::planSync(selectedInfo_, sync), static_cast<int>(processor_.sampleRate()));
    std::filesystem::path file = path;
    try {
        file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, library_.contentHash(selected_->id));
    } catch (const std::exception&) {
        // A render that fails still leaves the original to drag.
    }
    files.add(juce::String::fromUTF8(toUtf8(file).c_str()));
    updateRenderSize();
    return true;
}

} // namespace asma::app
