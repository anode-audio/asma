// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SidebarView.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kRowHeight = 30;
constexpr int kHeadingHeight = 22;
constexpr int kSectionGap = 18;
constexpr int kTop = 14;
constexpr int kRefusalHeight = 18; // why a name is refused, under the field

const char* headingFor(EntryKind kind)
{
    switch (kind) {
    case EntryKind::Folder: return "FOLDERS";
    case EntryKind::Collection: return "COLLECTIONS";
    case EntryKind::SavedSearch: return "SAVED SEARCHES";
    case EntryKind::All:
    case EntryKind::Favourites: break;
    }
    return nullptr;
}

// One entry: its name, its count on the right, lit when picked.
class Row final : public juce::Button {
public:
    Row(const juce::String& name, const juce::String& count) : juce::Button(name), count_(count)
    {
        setTitle(name);
        if (count.isNotEmpty()) setDescription(count + " samples");
        setClickingTogglesState(false);
    }
    const juce::String& count() const { return count_; }
    std::function<void()> onMenu; // a right click, for entries that have a menu
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu) return onMenu();
        juce::Button::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu) return;
        juce::Button::mouseUp(e);
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool lit = getToggleState();
        if (lit || highlighted || down) {
            g.setColour(lit ? theme::raised : theme::raised.withAlpha(0.5f));
            g.fillRect(getLocalBounds());
        }
        if (lit) {
            g.setColour(theme::amber);
            g.fillRect(0, 0, 2, getHeight());
        }
        auto area = getLocalBounds().reduced(18, 0);
        const auto countFont = theme::font(theme::Face::Mono, 11.0f);
        const int countWidth = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(countFont, count_)));
        g.setFont(countFont);
        g.setColour(theme::muted);
        g.drawText(count_, area.removeFromRight(countWidth), juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(8), juce::Justification::centredLeft, true);
    }

private:
    juce::String count_;
};

// At the foot: Problems with an amber count.
class ProblemsButton final : public juce::Button {
public:
    ProblemsButton() : juce::Button("Problems") { setTitle("Problems"); }
    juce::String count;
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        if (highlighted || down || getToggleState()) {
            g.setColour(theme::raised);
            g.fillRoundedRectangle(r, theme::kRadius);
        }
        g.setColour(getToggleState() ? theme::amber : theme::border);
        g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
        auto area = getLocalBounds().reduced(10, 0);
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::text);
        g.drawText("Problems", area, juce::Justification::centredLeft, false);
        const auto font = theme::font(theme::Face::Mono, 11.0f);
        const float w = juce::GlyphArrangement::getStringWidth(font, count) + 14.0f;
        const auto badge = area.toFloat().removeFromRight(w).withSizeKeepingCentre(w, 16.0f);
        g.setColour(theme::amber);
        g.fillRoundedRectangle(badge, 8.0f);
        g.setFont(font);
        g.setColour(theme::ground);
        g.drawText(count, badge, juce::Justification::centred, false);
    }
};

// The "+" beside COLLECTIONS.
class AddButton final : public juce::Button {
public:
    AddButton() : juce::Button("New collection") { setTitle("New collection"); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down) {
            g.setColour(theme::raised);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::kRadius);
        }
        g.setFont(theme::font(theme::Face::Text, 15.0f));
        g.setColour(highlighted ? theme::text : theme::muted);
        g.drawText("+", getLocalBounds(), juce::Justification::centred, false);
    }
};

} // namespace

// What scrolls: the rows and the headings between them.
class SidebarView::Content final : public juce::Component {
public:
    struct Heading {
        juce::String title;
        int y;
    };
    std::vector<Heading> headings;
    void paint(juce::Graphics& g) override
    {
        g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.08f));
        g.setColour(theme::muted);
        for (const auto& h : headings) g.drawText(h.title, 18, h.y, getWidth() - 36, kHeadingHeight, juce::Justification::centredLeft, false);
    }
};

SidebarView::SidebarView()
    : content_(std::make_unique<Content>()), problems_(std::make_unique<ProblemsButton>()),
      add_(std::make_unique<AddButton>())
{
    add_->onClick = [this] { startNewCollection(); };
    content_->addChildComponent(*add_);
    nameField_.setTitle("Name");
    nameField_.setFont(theme::font(theme::Face::Text, 13.0f));
    nameField_.setIndents(6, 5);
    nameField_.setColour(juce::TextEditor::focusedOutlineColourId, theme::amber);
    nameField_.onReturnKey = [this] { finishEditing(true); };
    nameField_.onEscapeKey = [this] { finishEditing(false); };
    nameField_.onTextChange = [this] {
        if (refusal_.isNotEmpty()) showRefusal({});
    };
    content_->addChildComponent(nameField_);
    refusalLabel_.setFont(theme::font(theme::Face::Text, 11.0f));
    refusalLabel_.setColour(juce::Label::textColourId, theme::refusedText);
    refusalLabel_.setBorderSize({0, 0, 0, 0});
    content_->addChildComponent(refusalLabel_);
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    problems_->onClick = [this] {
        if (onProblems) onProblems();
    };
    addChildComponent(*problems_);
}

SidebarView::~SidebarView() = default;

void SidebarView::setEntries(std::vector<SidebarEntry> entries)
{
    entries_ = std::move(entries);
    rows_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        auto* row = rows_.add(new Row(juce::String::fromUTF8(e.name.c_str()), e.count >= 0 ? juce::String(e.count) : juce::String()));
        row->onClick = [this, i] {
            if (onPick) onPick(static_cast<int>(i));
        };
        if (e.kind == EntryKind::Collection || e.kind == EntryKind::SavedSearch)
            static_cast<Row*>(row)->onMenu = [this, i, row] {
                entryMenu(static_cast<int>(i)).showMenuAsync(
                    juce::PopupMenu::Options().withTargetComponent(row).withParentComponent(getTopLevelComponent()),
                    [safe = juce::Component::SafePointer<SidebarView>(this), choose = entryMenuHandler(static_cast<int>(i))](int result) {
                        if (safe) choose(result);
                    });
            };
        content_->addAndMakeVisible(row);
    }
    // The list changes under a rename whenever the library does: the field
    // follows its entry, and goes only when the entry has.
    if (editing_ && *editing_ >= 0) {
        const int moved = indexOf(editingKind_, editingId_);
        if (moved >= 0) editing_ = moved;
        else finishEditing(false);
    }
    resized();
}

int SidebarView::indexOf(EntryKind kind, std::int64_t id) const
{
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].kind == kind && entries_[i].id == id) return static_cast<int>(i);
    return -1;
}

void SidebarView::showRefusal(const juce::String& text)
{
    refusal_ = text;
    refusalLabel_.setText(text, juce::dontSendNotification);
    refusalLabel_.setVisible(text.isNotEmpty());
    nameField_.setColour(juce::TextEditor::outlineColourId, text.isNotEmpty() ? theme::refused : theme::amber);
    resized();
}

void SidebarView::setSelected(int index)
{
    for (int i = 0; i < rows_.size(); ++i) rows_[i]->setToggleState(i == index, juce::dontSendNotification);
}

void SidebarView::setProblems(std::int64_t count)
{
    auto* p = static_cast<ProblemsButton*>(problems_.get());
    const juce::String text(count);
    if (p->isVisible() == (count > 0) && p->count == text) return;
    p->count = text;
    p->setVisible(count > 0);
    resized();
    repaint();
}

void SidebarView::setProblemsLit(bool lit) { problems_->setToggleState(lit, juce::dontSendNotification); }

juce::String SidebarView::countText(int index) const
{
    return index >= 0 && index < rows_.size() ? static_cast<const Row*>(rows_[index])->count() : juce::String();
}

juce::StringArray SidebarView::sectionTitles() const
{
    juce::StringArray out;
    for (const auto& h : content_->headings) out.add(h.title);
    return out;
}

void SidebarView::startNewCollection()
{
    finishEditing(false); // one field at a time
    editing_ = -1;
    nameField_.setText({}, false);
    showRefusal({});
    nameField_.setVisible(true);
    resized();
    nameField_.grabKeyboardFocus();
}

void SidebarView::startRename(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    finishEditing(false); // one field at a time
    const auto& entry = entries_[static_cast<std::size_t>(index)];
    editing_ = index;
    editingKind_ = entry.kind;
    editingId_ = entry.id;
    nameField_.setText(juce::String::fromUTF8(entry.name.c_str()), false);
    nameField_.selectAll();
    showRefusal({});
    nameField_.setVisible(true);
    resized();
    nameField_.grabKeyboardFocus();
}

void SidebarView::finishEditing(bool keep)
{
    if (!editing_) return;
    const int index = *editing_;
    const juce::String name = nameField_.getText();
    if (keep) {
        const auto refused = nameRefusal ? nameRefusal(index, name) : std::nullopt;
        if (refused) {
            showRefusal(refused->isNotEmpty() ? *refused : juce::String("A name is needed."));
            return; // the field stays for another try
        }
    }
    editing_.reset();
    nameField_.setVisible(false);
    showRefusal({});
    if (keep && onNamed) onNamed(index, name);
    if (!keep && index < 0 && onNewCancelled) onNewCancelled();
}

juce::PopupMenu SidebarView::entryMenu(int index) const
{
    juce::PopupMenu menu;
    if (index < 0 || index >= static_cast<int>(entries_.size())) return menu;
    const auto kind = entries_[static_cast<std::size_t>(index)].kind;
    if (kind != EntryKind::Collection && kind != EntryKind::SavedSearch) return menu;
    menu.addItem(kRename, juce::String::fromUTF8("Rename…"));
    menu.addItem(kDelete, "Delete");
    return menu;
}

void SidebarView::entryMenuChosen(int index, int result)
{
    if (result == kRename) startRename(index);
    if (result == kDelete && onDelete) onDelete(index);
}

std::function<void(int)> SidebarView::entryMenuHandler(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size())) return [](int) {};
    // The choice comes after the menu closes, by when the list may have
    // changed: it acts on the entry, wherever that is then.
    const auto kind = entries_[static_cast<std::size_t>(index)].kind;
    const auto id = entries_[static_cast<std::size_t>(index)].id;
    return [this, kind, id](int result) {
        const int now = indexOf(kind, id);
        if (now >= 0) entryMenuChosen(now, result);
    };
}

juce::String SidebarView::problemsText() const { return static_cast<const ProblemsButton*>(problems_.get())->count; }

void SidebarView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void SidebarView::resized()
{
    auto area = getLocalBounds().withTrimmedRight(1);
    if (problems_->isVisible()) {
        problems_->setBounds(area.removeFromBottom(14 + 34).reduced(12, 0).withTrimmedBottom(14));
        area.removeFromBottom(8);
    }
    viewport_.setBounds(area);
    content_->headings.clear();
    const int width = area.getWidth();
    int y = kTop;
    EntryKind section = EntryKind::All;
    const auto heading = [&](const char* title) {
        y += kSectionGap;
        content_->headings.push_back({title, y});
        if (juce::String(title) == "COLLECTIONS") add_->setBounds(width - 10 - 22, y, 22, kHeadingHeight - 2);
        y += kHeadingHeight;
    };
    // COLLECTIONS always shows, and a new collection's field ends it.
    bool collectionsDone = false;
    const auto endCollections = [&] {
        if (collectionsDone) return;
        collectionsDone = true;
        if (section != EntryKind::Collection) heading("COLLECTIONS");
        if (editing_ == -1) {
            nameField_.setBounds(14, y + 2, width - 24, kRowHeight - 4);
            y += kRowHeight;
            if (refusal_.isNotEmpty()) {
                refusalLabel_.setBounds(14, y, width - 24, kRefusalHeight);
                y += kRefusalHeight;
            }
        }
    };
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto kind = entries_[i].kind;
        if (kind == EntryKind::SavedSearch) endCollections();
        if (const char* title = headingFor(kind); title && kind != section) heading(title);
        if (kind == EntryKind::Folder || kind == EntryKind::Collection || kind == EntryKind::SavedSearch) section = kind;
        auto* row = rows_[static_cast<int>(i)];
        row->setBounds(0, y, width, kRowHeight);
        const bool renaming = editing_ == static_cast<int>(i);
        row->setVisible(!renaming);
        if (renaming) nameField_.setBounds(14, y + 2, width - 24, kRowHeight - 4);
        y += kRowHeight;
        if (renaming && refusal_.isNotEmpty()) {
            refusalLabel_.setBounds(14, y, width - 24, kRefusalHeight);
            y += kRefusalHeight;
        }
    }
    if (!entries_.empty()) endCollections();
    add_->setVisible(!entries_.empty());
    content_->setSize(width, y + kTop);
    content_->repaint();
}

} // namespace asma::app
