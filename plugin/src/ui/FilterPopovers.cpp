// SPDX-License-Identifier: GPL-3.0-only
#include "ui/FilterPopovers.h"

#include "TempoChip.h"
#include "ui/PreviewPanel.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace asma::app {

namespace {

constexpr int kPad = 14;
constexpr int kTitle = 22;
constexpr int kField = 28;

// Empty, or not a plain number, is no limit; never 0 by accident.
std::optional<double> number(const juce::String& text)
{
    const auto t = text.trim();
    if (t.isEmpty() || !t.containsOnly("0123456789.") || t.indexOfChar('.') != t.lastIndexOfChar('.')) return std::nullopt;
    return t.getDoubleValue();
}

juce::String shown(const std::optional<double>& value)
{
    return value ? juce::String::fromUTF8(bpmText(*value).c_str()) : juce::String();
}

void styleField(juce::TextEditor& field, const char* title)
{
    field.setTitle(title);
    field.setFont(theme::font(theme::Face::Mono, 13.0f));
    field.setInputRestrictions(7, "0123456789.");
    field.setIndents(8, 6);
}

void stylePill(juce::TextButton& button)
{
    button.getProperties().set("asma.accent", true);
    button.getProperties().set("asma.size", 11.0f);
}

// A tag to tick: a box, the name, how many samples carry it.
class TagRow final : public juce::Button {
public:
    TagRow(const juce::String& name, const juce::String& count) : juce::Button(name), count_(count)
    {
        setTitle(name);
        setClickingTogglesState(true);
    }
    const juce::String& count() const { return count_; }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const bool on = getToggleState();
        if (on || highlighted) {
            g.setColour(on ? theme::amber.withAlpha(0.08f) : theme::raised.withAlpha(0.6f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::kRadius);
        }
        auto area = getLocalBounds().reduced(6, 0);
        const auto box = area.removeFromLeft(14).withSizeKeepingCentre(14, 14).toFloat();
        if (on) {
            g.setColour(theme::amber);
            g.fillRoundedRectangle(box, 3.0f);
        } else {
            g.setColour(theme::muted);
            g.drawRoundedRectangle(box.reduced(0.75f), 3.0f, 1.5f);
        }
        area.removeFromLeft(10);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(theme::muted);
        g.drawText(count_, area, juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(40), juce::Justification::centredLeft, true);
    }

private:
    juce::String count_;
};

// One star of five: lit up to the minimum.
class Star final : public juce::Button {
public:
    explicit Star(int n) : juce::Button(juce::String(n) + (n == 1 ? " star" : " stars")) { setTitle(getName()); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        g.setFont(theme::font(theme::Face::Text, 22.0f));
        g.setColour(getToggleState() ? theme::amber : (highlighted ? theme::muted : theme::faint));
        g.drawText(juce::String::fromUTF8("★"), getLocalBounds(), juce::Justification::centred, false);
    }
};

} // namespace

FilterPopover::FilterPopover(const juce::String& title, const SearchModel& model, SearchChanged onChange,
                             const juce::String& hint)
    : model_(model), title_(title), hint_(hint), onChange_(std::move(onChange))
{
    setTitle(title);
}

void FilterPopover::changed()
{
    if (!onChange_) return;
    // A range typed backwards means the same range: 130 to 120 is 120 to 130.
    SearchModel reported = model_;
    if (reported.bpmMin && reported.bpmMax && *reported.bpmMin > *reported.bpmMax) std::swap(reported.bpmMin, reported.bpmMax);
    if (reported.durationMin && reported.durationMax && *reported.durationMin > *reported.durationMax)
        std::swap(reported.durationMin, reported.durationMax);
    onChange_(reported);
}

juce::Rectangle<int> FilterPopover::body() const { return getLocalBounds().reduced(kPad).withTrimmedTop(kTitle + 6); }

void FilterPopover::paint(juce::Graphics& g)
{
    auto line = getLocalBounds().reduced(kPad).removeFromTop(kTitle);
    const auto titleFont = theme::font(theme::Face::Heading, 13.0f);
    g.setFont(titleFont);
    g.setColour(theme::text);
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(titleFont, title_)));
    g.drawText(title_, line.removeFromLeft(w), juce::Justification::centredLeft, false);
    if (hint_.isEmpty()) return;
    line.removeFromLeft(6);
    g.setFont(theme::font(theme::Face::Text, 11.0f));
    g.setColour(theme::muted);
    g.drawText(hint_, line, juce::Justification::centredLeft, false);
}

TypePopover::TypePopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Type", model, std::move(onChange)), choice_({"Any", "Loops", "One-shots"}, true)
{
    choice_.setSelected(static_cast<int>(model.type), juce::dontSendNotification);
    choice_.onChange = [this](int i) {
        model_.type = static_cast<SampleType>(i);
        changed();
    };
    addAndMakeVisible(choice_);
    setSize(300, 2 * kPad + kTitle + 6 + kField);
}

void TypePopover::resized() { choice_.setBounds(body().removeFromTop(kField)); }

BpmPopover::BpmPopover(const SearchModel& model, double tempo, SearchChanged onChange)
    : FilterPopover("BPM", model, std::move(onChange)), tempo_(tempo)
{
    styleField(from_, "From BPM");
    styleField(to_, "To BPM");
    from_.setText(shown(model.bpmMin), false);
    to_.setText(shown(model.bpmMax), false);
    from_.onTextChange = [this] {
        model_.bpmMin = number(from_.getText());
        changed();
    };
    to_.onTextChange = [this] {
        model_.bpmMax = number(to_.getText());
        changed();
    };
    for (auto* b : {&near120_, &nearTempo_}) {
        b->getProperties().set("asma.quiet", true);
        b->getProperties().set("asma.size", 11.0f);
    }
    near120_.onClick = [this] {
        const auto r = bpmNear(120.0);
        setRange(r.first, r.second);
    };
    nearTempo_.setEnabled(tempo > 0.0);
    nearTempo_.onClick = [this] {
        const auto r = bpmNear(tempo_);
        setRange(r.first, r.second);
    };
    for (juce::Component* c : {static_cast<juce::Component*>(&from_), static_cast<juce::Component*>(&to_),
                               static_cast<juce::Component*>(&near120_), static_cast<juce::Component*>(&nearTempo_)})
        addAndMakeVisible(c);
    setSize(300, 2 * kPad + kTitle + 6 + kField + 10 + 24);
}

void BpmPopover::setRange(double lo, double hi)
{
    model_.bpmMin = lo;
    model_.bpmMax = hi;
    from_.setText(shown(model_.bpmMin), false);
    to_.setText(shown(model_.bpmMax), false);
    changed();
}

void BpmPopover::resized()
{
    auto area = body();
    auto fields = area.removeFromTop(kField);
    from_.setBounds(fields.removeFromLeft(90));
    fields.removeFromLeft(24);
    to_.setBounds(fields.removeFromLeft(90));
    area.removeFromTop(10);
    auto pills = area.removeFromTop(24);
    near120_.setBounds(pills.removeFromLeft(84));
    pills.removeFromLeft(6);
    nearTempo_.setBounds(pills.removeFromLeft(118));
}

KeyPopover::KeyPopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Key", model, std::move(onChange), "any of")
{
    const auto& names = PreviewPanel::keys();
    for (int i = 0; i < names.size(); ++i) {
        auto* b = keys_.add(new juce::TextButton(names[i]));
        b->setClickingTogglesState(true);
        b->getProperties().set("asma.accent", true);
        b->getProperties().set("asma.size", 12.0f);
        b->getProperties().set("asma.mono", true);
        b->getProperties().set("asma.quiet", false); // unpicked keys read plainly, as the design has them
        b->setToggleState(std::find(model.keys.begin(), model.keys.end(), names[i].toStdString()) != model.keys.end(),
                          juce::dontSendNotification);
        b->onClick = [this, i, b] {
            const std::string key = PreviewPanel::keys()[i].toStdString();
            auto& keys = model_.keys;
            keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
            if (b->getToggleState()) keys.push_back(key); // in the order picked
            changed();
        };
        addAndMakeVisible(b);
    }
    setSize(360, 2 * kPad + kTitle + 6 + 4 * kField + 3 * 4);
}

void KeyPopover::resized()
{
    const auto area = body();
    const int w = (area.getWidth() - 5 * 4) / 6;
    for (int i = 0; i < keys_.size(); ++i)
        keys_[i]->setBounds(area.getX() + (i % 6) * (w + 4), area.getY() + (i / 6) * (kField + 4), w, kField);
}

InstrumentPopover::InstrumentPopover(const SearchModel& model, std::vector<TagCount> tags, SearchChanged onChange)
    : FilterPopover("Instrument", model, std::move(onChange), "all of"), counts_(std::move(tags))
{
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        const auto& t = counts_[i];
        auto* row = tags_.add(new TagRow(juce::String::fromUTF8(t.name.c_str()), juce::String(t.count)));
        row->setToggleState(std::find(model.tags.begin(), model.tags.end(), t.name) != model.tags.end(),
                            juce::dontSendNotification);
        row->onClick = [this, i, row] {
            const std::string& name = counts_[i].name;
            auto& picked = model_.tags;
            picked.erase(std::remove(picked.begin(), picked.end(), name), picked.end());
            if (row->getToggleState()) picked.push_back(name);
            changed();
        };
        list_.addAndMakeVisible(row);
    }
    if (counts_.empty()) empty_ = "No instrument tags yet: they come from file names when a folder is scanned.";
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    const int rows = std::clamp(static_cast<int>(counts_.size()), 1, 8);
    setSize(300, 2 * kPad + kTitle + 6 + rows * 28);
}

juce::String InstrumentPopover::countText(int index) const
{
    return index >= 0 && index < tags_.size() ? static_cast<const TagRow*>(tags_[index])->count() : juce::String();
}

void InstrumentPopover::paint(juce::Graphics& g)
{
    FilterPopover::paint(g);
    if (empty_.isEmpty()) return;
    g.setFont(theme::font(theme::Face::Text, 12.0f));
    g.setColour(theme::muted);
    g.drawFittedText(empty_, body(), juce::Justification::topLeft, 2);
}

void InstrumentPopover::resized()
{
    viewport_.setBounds(body());
    const int w = viewport_.getWidth() - (tags_.size() > 8 ? viewport_.getScrollBarThickness() : 0);
    for (int i = 0; i < tags_.size(); ++i) tags_[i]->setBounds(0, i * 28, w, 26);
    list_.setSize(w, tags_.size() * 28);
}

LengthPopover::LengthPopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Length", model, std::move(onChange))
{
    for (std::size_t i = 0; i < kLengthPresets.size(); ++i) {
        auto* b = presets_.add(new juce::TextButton(juce::String::fromUTF8(kLengthPresets[i].label)));
        stylePill(*b);
        b->onClick = [this, i] {
            model_.durationMin = kLengthPresets[i].min;
            model_.durationMax = kLengthPresets[i].max;
            from_.setText(shown(model_.durationMin), false);
            to_.setText(shown(model_.durationMax), false);
            showPreset();
            changed();
        };
        addAndMakeVisible(b);
    }
    styleField(from_, "From seconds");
    styleField(to_, "To seconds");
    from_.setText(shown(model.durationMin), false);
    to_.setText(shown(model.durationMax), false);
    from_.onTextChange = [this] {
        model_.durationMin = number(from_.getText());
        showPreset();
        changed();
    };
    to_.onTextChange = [this] {
        model_.durationMax = number(to_.getText());
        showPreset();
        changed();
    };
    addAndMakeVisible(from_);
    addAndMakeVisible(to_);
    showPreset();
    setSize(360, 2 * kPad + kTitle + 6 + 24 + 10 + kField);
}

void LengthPopover::showPreset()
{
    for (std::size_t i = 0; i < kLengthPresets.size(); ++i)
        presets_[static_cast<int>(i)]->setToggleState(
            kLengthPresets[i].min == model_.durationMin && kLengthPresets[i].max == model_.durationMax,
            juce::dontSendNotification);
}

void LengthPopover::resized()
{
    auto area = body();
    auto pills = area.removeFromTop(24);
    for (auto* b : presets_) {
        const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(theme::font(theme::Face::SemiBold, 11.0f),
                                                                                         b->getButtonText())))
                    + 20;
        b->setBounds(pills.removeFromLeft(w));
        pills.removeFromLeft(6);
    }
    area.removeFromTop(10);
    auto fields = area.removeFromTop(kField);
    from_.setBounds(fields.removeFromLeft(90));
    fields.removeFromLeft(24);
    to_.setBounds(fields.removeFromLeft(90));
}

RatingPopover::RatingPopover(const SearchModel& model, SearchChanged onChange)
    : FilterPopover("Rating", model, std::move(onChange), "at least")
{
    for (int n = 1; n <= 5; ++n) {
        auto* s = stars_.add(new Star(n));
        s->onClick = [this, n] {
            if (model_.minRating == n) model_.minRating.reset(); // the same star again: no minimum
            else model_.minRating = n;
            show();
            changed();
        };
        addAndMakeVisible(s);
    }
    show();
    setSize(300, 2 * kPad + kTitle + 6 + 34);
}

void RatingPopover::show()
{
    for (int i = 0; i < stars_.size(); ++i)
        stars_[i]->setToggleState(model_.minRating && i < *model_.minRating, juce::dontSendNotification);
}

void RatingPopover::resized()
{
    auto area = body().removeFromTop(34);
    for (auto* s : stars_) {
        s->setBounds(area.removeFromLeft(34));
        area.removeFromLeft(4);
    }
}

std::unique_ptr<FilterPopover> makeFilterPopover(Facet facet, const SearchModel& model, const PopoverContext& context,
                                                 SearchChanged onChange)
{
    switch (facet) {
    case Facet::Type: return std::make_unique<TypePopover>(model, std::move(onChange));
    case Facet::Bpm: return std::make_unique<BpmPopover>(model, context.tempo, std::move(onChange));
    case Facet::Key: return std::make_unique<KeyPopover>(model, std::move(onChange));
    case Facet::Instrument: return std::make_unique<InstrumentPopover>(model, context.tags, std::move(onChange));
    case Facet::Length: return std::make_unique<LengthPopover>(model, std::move(onChange));
    case Facet::Rating: return std::make_unique<RatingPopover>(model, std::move(onChange));
    }
    return nullptr;
}

} // namespace asma::app
