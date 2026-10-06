// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ChipRow.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kChipHeight = 26;
constexpr int kClear = 16;   // the x's circle
constexpr int kClearInset = 5; // from the chip's right edge to the x
constexpr int kClearGap = 4;   // between the label and the x
constexpr int kPad = 10;
constexpr int kGap = 8;
constexpr int kMargin = 14;

const juce::Font& chipFont()
{
    static const juce::Font font = theme::font(theme::Face::Text, 12.0f);
    return font;
}

// The chip's own area: transparent, the pill draws everything.
class Invisible final : public juce::Button {
public:
    explicit Invisible(const juce::String& name) : juce::Button(name) { setTitle(name); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        if (!highlighted) return;
        g.setColour(theme::text.withAlpha(0.05f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), static_cast<float>(getHeight()) / 2.0f);
    }
};

// The x that clears a set filter.
class Cross final : public juce::Button {
public:
    Cross() : juce::Button("Clear") { setTitle("Clear"); }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        if (highlighted) {
            g.setColour(theme::amber.withAlpha(0.25f));
            g.fillEllipse(r);
        }
        const auto c = r.getCentre();
        g.setColour(theme::amber);
        g.drawLine(c.x - 3.0f, c.y - 3.0f, c.x + 3.0f, c.y + 3.0f, 1.4f);
        g.drawLine(c.x + 3.0f, c.y - 3.0f, c.x - 3.0f, c.y + 3.0f, 1.4f);
    }
};

const char* nameOf(Facet f)
{
    switch (f) {
    case Facet::Type: return "Type";
    case Facet::Bpm: return "BPM";
    case Facet::Key: return "Key";
    case Facet::Instrument: return "Instrument";
    case Facet::Length: return "Length";
    case Facet::Rating: return "Rating";
    }
    return "";
}

} // namespace

FilterChip::FilterChip() : main_(std::make_unique<Invisible>("Filter")), clear_(std::make_unique<Cross>())
{
    addAndMakeVisible(*main_);
    addChildComponent(*clear_);
}

void FilterChip::setLabel(const juce::String& label, bool active)
{
    if (label == label_ && active == active_) return;
    label_ = label;
    active_ = active;
    main_->setDescription(label);
    clear_->setVisible(active);
    resized();
    repaint();
}

int FilterChip::labelWidth() const
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(chipFont(), label_)));
}

int FilterChip::idealWidth() const
{
    return kPad + labelWidth() + (active_ ? kClearGap + kClear + kClearInset : kPad);
}

juce::Rectangle<int> FilterChip::textArea() const
{
    return getLocalBounds().withTrimmedLeft(kPad).withTrimmedRight(active_ ? kClearGap + kClear + kClearInset : kPad);
}

void FilterChip::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float radius = r.getHeight() / 2.0f;
    if (active_) {
        g.setColour(theme::amber.withAlpha(0.12f));
        g.fillRoundedRectangle(r, radius);
    }
    g.setColour(active_ ? theme::amber : theme::border);
    g.drawRoundedRectangle(r, radius, 1.0f);
    g.setFont(chipFont());
    g.setColour(active_ ? theme::amberLight : theme::muted);
    g.drawFittedText(label_, textArea(), juce::Justification::centredLeft, 1, 1.0f); // shortens with an ellipsis
}

void FilterChip::resized()
{
    main_->setBounds(getLocalBounds());
    clear_->setBounds(getWidth() - kClear - kClearInset, (getHeight() - kClear) / 2, kClear, kClear);
}

ChipRow::ChipRow()
{
    for (const Facet f : kFacets) {
        auto* added = chips_.add(new FilterChip());
        added->mainButton().setTitle(nameOf(f));
        added->mainButton().onClick = [this, f] {
            if (onOpen) onOpen(f, chip(f));
        };
        added->clearButton().setTitle(juce::String("Clear ") + nameOf(f));
        added->clearButton().onClick = [this, f] {
            if (onChange) onChange(cleared(f, model_));
        };
        addAndMakeVisible(added);
    }
    clearAll_.getProperties().set("asma.quiet", true);
    clearAll_.getProperties().set("asma.size", 12.0f);
    clearAll_.getProperties().set("asma.segment", "middle"); // no frame: a link-like button
    clearAll_.onClick = [this] {
        if (onChange) onChange(clearedAll(model_));
    };
    addChildComponent(clearAll_);
    setModel({});
}

void ChipRow::setModel(const SearchModel& model)
{
    model_ = model;
    bool any = false;
    for (const Facet f : kFacets) {
        const bool set = isSet(f, model);
        any |= set;
        chip(f).setLabel(juce::String::fromUTF8(chipLabel(f, model).c_str()), set);
    }
    clearAll_.setVisible(any);
    resized();
}

void ChipRow::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    g.setColour(theme::border);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void ChipRow::resized()
{
    auto area = getLocalBounds().reduced(kMargin, 0);
    clearAll_.setBounds(area.removeFromRight(72).withSizeKeepingCentre(72, kChipHeight));
    area.removeFromRight(kGap);
    // Ideal widths; when they do not fit, every chip gives up its share.
    int wanted = 0;
    for (auto* c : chips_) wanted += c->idealWidth();
    const int room = area.getWidth() - kGap * (chips_.size() - 1);
    const double scale = wanted > room && wanted > 0 ? static_cast<double>(room) / wanted : 1.0;
    int x = area.getX();
    for (auto* c : chips_) {
        const int w = std::max(40, static_cast<int>(c->idealWidth() * scale));
        c->setBounds(x, (getHeight() - kChipHeight) / 2, std::min(w, area.getRight() - x), kChipHeight);
        x += w + kGap;
    }
}

} // namespace asma::app
