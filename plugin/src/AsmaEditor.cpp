// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"

#include "AsmaProcessor.h"
#include "asma/audio/Render.h"
#include "asma/core/Fs.h"

#include <cmath>

namespace asma::app {

namespace {

const char* const kKeys[] = {"C",  "C#",  "D",  "D#",  "E",  "F",  "F#",  "G",  "G#",  "A",  "A#",  "B",
                             "Cm", "C#m", "Dm", "D#m", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "A#m", "Bm"};

juce::String bpmText(const std::optional<double>& bpm)
{
    if (!bpm) return {};
    return juce::String(std::round(*bpm * 100.0) / 100.0);
}

} // namespace

AsmaEditor::AsmaEditor(AsmaProcessor& owner)
    : juce::AudioProcessorEditor(owner), processor_(owner),
      library_(owner.libraryPath(), owner.isStandalone() ? LibraryView::Access::MayMigrate : LibraryView::Access::ReadOnly)
{
    const PluginState state = processor_.pluginState();

    search_.setTextToShowWhenEmpty("Search samples", juce::Colours::grey);
    search_.onTextChange = [this] { searchChanged(); };
    addAndMakeVisible(search_);

    for (int i = 0; i < static_cast<int>(std::size(kKeys)); ++i) projectKey_.addItem(kKeys[i], i + 1);
    projectKey_.setTextWhenNothingSelected("Project key");
    for (auto* b : {&tempoSync_, &keySync_, &gainMatch_}) {
        b->onClick = [this] { syncChanged(); };
        addAndMakeVisible(*b);
    }
    projectKey_.onChange = [this] { syncChanged(); };
    addAndMakeVisible(projectKey_);

    if (processor_.isStandalone()) {
        bpm_.setRange(20.0, 300.0, 0.1);
        bpm_.setTextValueSuffix(" BPM");
        bpm_.onValueChange = [this] {
            processor_.setManualBpm(bpm_.getValue());
            if (link_.getToggleState()) processor_.setLinkTempo(bpm_.getValue());
        };
        link_.onClick = [this] { processor_.setLinkEnabled(link_.getToggleState()); };
        addAndMakeVisible(bpm_);
        addAndMakeVisible(link_);
        addFolder_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Add a sample folder");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this](const juce::FileChooser& chooser) {
                                      const juce::File folder = chooser.getResult();
                                      if (folder != juce::File()) addFolder(fromUtf8(folder.getFullPathName().toStdString()));
                                  });
        };
        addAndMakeVisible(addFolder_);
    }

    auto& header = table_.getHeader();
    header.addColumn("Name", kName, 320);
    header.addColumn("BPM", kBpm, 70);
    header.addColumn("Key", kKey, 60);
    header.addColumn("Type", kType, 80);
    header.addColumn("Length", kLength, 80);
    table_.setMultipleSelectionEnabled(false);
    addAndMakeVisible(table_);

    status_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status_);

    setResizable(true, true);
    setResizeLimits(600, 400, 8000, 8000);
    setSize(state.width, state.height);
    setWantsKeyboardFocus(true);
    loadState();
    poll();
    startTimerHz(5);
}

void AsmaEditor::loadState()
{
    loadedStates_ = processor_.stateLoads();
    const PluginState state = processor_.pluginState();
    search_.setText(state.search.text, false);
    tempoSync_.setToggleState(state.sync.tempo, juce::dontSendNotification);
    keySync_.setToggleState(state.sync.key, juce::dontSendNotification);
    gainMatch_.setToggleState(state.gainMatch, juce::dontSendNotification);
    projectKey_.setSelectedId(0, juce::dontSendNotification);
    for (int i = 0; i < static_cast<int>(std::size(kKeys)); ++i)
        if (state.sync.projectKey.view() == kKeys[i]) projectKey_.setSelectedId(i + 1, juce::dontSendNotification);
    if (processor_.isStandalone()) {
        bpm_.setValue(state.sync.hostBpm > 0.0 ? state.sync.hostBpm : 120.0, juce::dontSendNotification);
        link_.setToggleState(state.link, juce::dontSendNotification);
    }
    browser_.setSearch(state.search);
    table_.updateContent();
    showSelection();
}

AsmaEditor::~AsmaEditor() { stopTimer(); }

void AsmaEditor::paint(juce::Graphics& g) { g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId)); }

void AsmaEditor::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(28);
    search_.setBounds(top.removeFromLeft(top.getWidth() / 2).reduced(0, 2));
    for (juce::Component* c : {static_cast<juce::Component*>(&tempoSync_), static_cast<juce::Component*>(&keySync_),
                               static_cast<juce::Component*>(&projectKey_), static_cast<juce::Component*>(&gainMatch_)})
        c->setBounds(top.removeFromLeft(top.getWidth() / 4).reduced(4, 2));
    if (processor_.isStandalone()) {
        auto row = area.removeFromTop(28);
        bpm_.setBounds(row.removeFromLeft(180).reduced(0, 2));
        link_.setBounds(row.removeFromLeft(140).reduced(4, 2));
        addFolder_.setBounds(row.removeFromRight(140).reduced(0, 2));
    }
    status_.setBounds(area.removeFromBottom(24));
    table_.setBounds(area.reduced(0, 4));
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
    return false;
}

void AsmaEditor::addFolder(const std::filesystem::path& folder)
{
    ScanJob* scans = processor_.scans();
    if (!scans) return;
    std::string why;
    scanMessage_ = scans->addAndScan(folder, &why) ? juce::String() : juce::String("Cannot add that folder: ") + why;
    updateStatus();
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
        table_.updateContent();
        showSelection();
    }
    updateStatus();
}

void AsmaEditor::searchChanged()
{
    SearchModel model = browser_.searchModel();
    model.text = search_.getText().toStdString();
    browser_.setSearch(model);
    processor_.updateState([&](PluginState& s) { s.search = model; });
    table_.updateContent();
    showSelection();
}

void AsmaEditor::syncChanged()
{
    audio::SyncSettings sync = processor_.pluginState().sync;
    sync.tempo = tempoSync_.getToggleState();
    sync.key = keySync_.getToggleState();
    const int id = projectKey_.getSelectedId();
    sync.projectKey = id > 0 ? audio::KeyName(kKeys[id - 1]) : audio::KeyName();
    const bool gain = gainMatch_.getToggleState();
    processor_.updateState([&](PluginState& s) {
        s.sync = sync;
        s.gainMatch = gain;
    });
    processor_.engine().setSync(sync);
    processor_.engine().setGainMatch(gain);
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
}

void AsmaEditor::updateStatus()
{
    if (ScanJob* scans = processor_.scans(); scans && scans->busy()) {
        status_.setText(scans->progress(), juce::dontSendNotification);
        return;
    }
    if (scanMessage_.isNotEmpty()) {
        status_.setText(scanMessage_, juce::dontSendNotification);
        return;
    }
    if (library_.state() != LibraryState::Open) {
        status_.setText(library_.message(), juce::dontSendNotification);
        return;
    }
    const int row = table_.getSelectedRow();
    if (row < 0) {
        status_.setText(juce::String(browser_.rows().size()) + " samples", juce::dontSendNotification);
        return;
    }
    const SearchRow& r = browser_.rows()[static_cast<std::size_t>(row)];
    const audio::EngineStatus st = processor_.engine().status();
    juce::String text = juce::String::fromUTF8(r.name.c_str());
    if (r.bpm) text << "   " << bpmText(r.bpm) << " BPM" << (st.tempoUnsure ? " ?" : "");
    if (st.tempoSynced) text << " (synced x" << juce::String(st.ratio, 2) << ")";
    if (r.key) text << "   " << juce::String(*r.key) << (st.keyUnsure ? " ?" : "");
    if (st.keySynced && (st.semitones < 0.0 || st.semitones > 0.0)) text << " (" << (st.semitones > 0 ? "+" : "") << juce::String(st.semitones, 0) << ")";
    status_.setText(text, juce::dontSendNotification);
}

int AsmaEditor::getNumRows() { return static_cast<int>(browser_.rows().size()); }

void AsmaEditor::paintRowBackground(juce::Graphics& g, int, int, int, bool selected)
{
    if (selected) g.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId));
}

void AsmaEditor::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const SearchRow& r = browser_.rows()[static_cast<std::size_t>(row)];
    juce::String text;
    switch (column) {
    case kName: text = juce::String::fromUTF8(r.name.c_str()); break;
    case kBpm: text = bpmText(r.bpm); break;
    case kKey: text = r.key ? juce::String(*r.key) : juce::String(); break;
    case kType: text = !r.isLoop ? "" : (*r.isLoop ? "loop" : "one-shot"); break;
    case kLength: text = juce::String(r.duration, 2) + " s"; break;
    default: break;
    }
    g.setColour(getLookAndFeel().findColour(juce::ListBox::textColourId));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void AsmaEditor::selectedRowsChanged(int lastRowSelected)
{
    if (quietSelection_ || lastRowSelected < 0) return;
    scanMessage_.clear();
    const auto path = browser_.path(lastRowSelected);
    processor_.engine().select(path, browser_.info(lastRowSelected), true);
    processor_.updateState([&](PluginState& s) { s.selected = toUtf8(path); });
    updateStatus();
}

void AsmaEditor::returnKeyPressed(int) { processor_.engine().play(); }

juce::var AsmaEditor::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var("asma-sample");
}

bool AsmaEditor::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails&,
                                                      juce::StringArray& files, bool& canMoveFiles)
{
    const int row = table_.getSelectedRow();
    if (row < 0) return false;
    canMoveFiles = false;
    const auto path = browser_.path(row);
    const PluginState state = processor_.pluginState();
    audio::SyncSettings sync = state.sync;
    if (processor_.hostBpm() > 0.0) sync.hostBpm = processor_.hostBpm();
    const audio::SyncPlan plan = audio::planSync(browser_.info(row), sync);
    audio::RenderSettings settings;
    settings.edits = state.edits;
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones;
    settings.sampleRate = static_cast<int>(processor_.sampleRate());
    std::filesystem::path file = path;
    try {
        file = audio::RenderStore(audio::RenderStore::defaultDir()).fileFor(path, settings, browser_.contentHash(row));
    } catch (const std::exception&) {
        // A render that fails still leaves the original to drag.
    }
    files.add(juce::String::fromUTF8(toUtf8(file).c_str()));
    return true;
}

} // namespace asma::app
