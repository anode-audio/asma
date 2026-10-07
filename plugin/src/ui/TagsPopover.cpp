// SPDX-License-Identifier: GPL-3.0-only
#include "ui/TagsPopover.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace asma::app {

namespace {

constexpr int kWidth = 360;
constexpr int kPad = 14;
constexpr int kTitle = 22;
constexpr int kChipHeight = 24;
constexpr int kChipGap = 6;
constexpr int kCross = 16;
constexpr int kFieldHeight = 28;
constexpr int kSuggestionHeight = 24;
constexpr int kMaxSuggestions = 5;

// As the library stores a tag: trimmed and lower case.
std::string normal(const juce::String& text)
{
    std::string out = text.trim().toStdString();
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

juce::Font chipFont() { return theme::font(theme::Face::Text, 11.0f); }

class Cross final : public juce::Button {
public:
    explicit Cross(const juce::String& tag) : juce::Button("Remove " + tag) { setTitle(getName()); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        g.setFont(theme::font(theme::Face::Text, 11.0f));
        g.setColour(highlighted ? theme::amberLight : theme::amber);
        g.drawText(juce::String::fromUTF8("×"), getLocalBounds(), juce::Justification::centred, false);
    }
};

class Suggestion final : public juce::Button {
public:
    Suggestion(const juce::String& name, std::int64_t count) : juce::Button(name), count_(count) { setTitle(name); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down) {
            g.setColour(theme::border);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
        }
        auto area = getLocalBounds().reduced(8, 0);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(theme::muted);
        g.drawText(juce::String(count_), area, juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(40), juce::Justification::centredLeft, true);
    }

private:
    std::int64_t count_;
};

} // namespace

TagsPopover::TagsPopover(const juce::String& sampleName, std::vector<Tag> tags, std::vector<TagCount> library,
                         Changed onChange)
    : sampleName_(sampleName), tags_(std::move(tags)), library_(std::move(library)), onChange_(std::move(onChange))
{
    setTitle("Tags");
    field_.setTitle("Add a tag");
    field_.setFont(theme::font(theme::Face::Text, 13.0f));
    field_.setIndents(8, 6);
    field_.setColour(juce::TextEditor::focusedOutlineColourId, theme::amber);
    field_.onTextChange = [this] { rebuild(); };
    field_.onReturnKey = [this] {
        const std::string tag = normal(field_.getText());
        field_.clear();
        add(tag);
    };
    field_.onEscapeKey = [this] {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    addAndMakeVisible(field_);
    rebuild();
}

juce::String TagsPopover::chipText(int index) const { return utf8(tags_[static_cast<std::size_t>(index)].name); }

bool TagsPopover::isRemovable(int index) const { return tags_[static_cast<std::size_t>(index)].user; }

juce::Button* TagsPopover::removeButton(int index)
{
    int user = 0;
    for (int i = 0; i < index; ++i) user += tags_[static_cast<std::size_t>(i)].user ? 1 : 0;
    return isRemovable(index) ? removeButtons_[user] : nullptr;
}

juce::StringArray TagsPopover::suggestions() const
{
    juce::StringArray out;
    for (auto* b : suggestionButtons_) out.add(b->getButtonText());
    return out;
}

void TagsPopover::add(std::string tag)
{
    if (tag.empty()) return;
    if (std::any_of(tags_.begin(), tags_.end(), [&](const Tag& t) { return t.name == tag; })) return;
    // The user's go first, as the library lists them.
    tags_.push_back({tag, true});
    std::stable_sort(tags_.begin(), tags_.end(), [](const Tag& a, const Tag& b) {
        return a.user != b.user ? a.user : a.name < b.name;
    });
    rebuild();
    if (onChange_) onChange_(tag, true);
}

void TagsPopover::remove(std::string tag)
{
    tags_.erase(std::remove_if(tags_.begin(), tags_.end(), [&](const Tag& t) { return t.user && t.name == tag; }),
                tags_.end());
    rebuild();
    if (onChange_) onChange_(tag, false);
}

void TagsPopover::rebuild()
{
    removeButtons_.clear();
    for (const auto& t : tags_) {
        if (!t.user) continue;
        auto* cross = removeButtons_.add(new Cross(utf8(t.name)));
        cross->onClick = [this, name = t.name] { remove(name); };
        addAndMakeVisible(cross);
    }
    suggestionButtons_.clear();
    const std::string typed = normal(field_.getText());
    if (!typed.empty()) {
        for (const auto& t : library_) {
            if (static_cast<int>(suggestionButtons_.size()) == kMaxSuggestions) break;
            if (t.name.rfind(typed, 0) != 0) continue;
            if (std::any_of(tags_.begin(), tags_.end(), [&](const Tag& have) { return have.name == t.name; })) continue;
            auto* s = suggestionButtons_.add(new Suggestion(utf8(t.name), t.count));
            s->onClick = [this, name = t.name] {
                field_.clear();
                add(name);
            };
            addAndMakeVisible(s);
        }
    }

    // The chips flow in rows; the height follows what there is.
    chipBounds_.clear();
    const int inner = kWidth - 2 * kPad;
    int x = 0, y = 0;
    for (const auto& t : tags_) {
        const int text = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(chipFont(), utf8(t.name))));
        const int w = std::min(inner, 9 + text + (t.user ? 4 + kCross + 4 : 9));
        if (x > 0 && x + w > inner) {
            x = 0;
            y += kChipHeight + kChipGap;
        }
        chipBounds_.emplace_back(kPad + x, kPad + kTitle + 6 + y, w, kChipHeight);
        x += w + kChipGap;
    }
    const int chipsHeight = tags_.empty() ? 0 : y + kChipHeight + 12;
    const int height = kPad + kTitle + 6 + chipsHeight + 16 + kFieldHeight
                     + (suggestionButtons_.isEmpty() ? 0 : 4 + suggestionButtons_.size() * kSuggestionHeight + 6) + 36;
    setSize(kWidth, height);
    resized();
    repaint();
}

void TagsPopover::paint(juce::Graphics& g)
{
    auto title = getLocalBounds().reduced(kPad).removeFromTop(kTitle);
    const auto titleFont = theme::font(theme::Face::Heading, 13.0f);
    g.setFont(titleFont);
    g.setColour(theme::text);
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(titleFont, "Tags")));
    g.drawText("Tags", title.removeFromLeft(w), juce::Justification::centredLeft, false);
    title.removeFromLeft(6);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText(sampleName_, title, juce::Justification::centredLeft, true);

    for (std::size_t i = 0; i < tags_.size(); ++i) {
        const auto r = chipBounds_[i].toFloat().reduced(0.5f);
        const bool user = tags_[i].user;
        if (user) {
            g.setColour(theme::amber.withAlpha(0.12f));
            g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
        }
        g.setColour(user ? theme::amber : theme::border);
        g.drawRoundedRectangle(r, r.getHeight() / 2.0f, 1.0f);
        g.setFont(chipFont());
        g.setColour(user ? theme::amberLight : theme::muted);
        g.drawText(utf8(tags_[i].name), chipBounds_[i].withTrimmedLeft(9).withTrimmedRight(user ? kCross + 8 : 9),
                   juce::Justification::centredLeft, true);
    }

    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText("Add a tag", field_.getBounds().translated(0, -16).withHeight(14), juce::Justification::bottomLeft, false);
    if (!suggestionButtons_.isEmpty()) {
        const auto list = suggestionButtons_.getFirst()->getBounds().getUnion(suggestionButtons_.getLast()->getBounds()).expanded(3);
        g.setColour(theme::raised);
        g.fillRoundedRectangle(list.toFloat(), theme::kRadius);
        g.setColour(theme::border);
        g.drawRoundedRectangle(list.toFloat().reduced(0.5f), theme::kRadius, 1.0f);
    }
    g.setFont(theme::font(theme::Face::Text, 12.0f));
    g.setColour(theme::muted);
    g.drawText("Return adds the tag. Changes apply at once.", getLocalBounds().reduced(kPad).removeFromBottom(18),
               juce::Justification::centredLeft, true);
}

void TagsPopover::resized()
{
    int user = 0;
    for (std::size_t i = 0; i < tags_.size() && i < chipBounds_.size(); ++i) {
        if (!tags_[i].user) continue;
        const auto r = chipBounds_[i];
        removeButtons_[user++]->setBounds(r.getRight() - 4 - kCross, r.getCentreY() - kCross / 2, kCross, kCross);
    }
    const int chipsBottom = chipBounds_.empty() ? kPad + kTitle + 6 : chipBounds_.back().getBottom() + 12;
    field_.setBounds(kPad, chipsBottom + 16, kWidth - 2 * kPad, kFieldHeight);
    int y = field_.getBottom() + 4 + 3;
    for (auto* s : suggestionButtons_) {
        s->setBounds(kPad + 3, y, kWidth - 2 * kPad - 6, kSuggestionHeight);
        y += kSuggestionHeight;
    }
}

} // namespace asma::app
