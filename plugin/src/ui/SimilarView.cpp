// SPDX-License-Identifier: GPL-3.0-only
#include "ui/SimilarView.h"

#include "TempoChip.h"
#include "ui/Theme.h"

namespace asma::app {

namespace {

constexpr int kRowHeight = 24;
constexpr int kTop = 12;
constexpr int kHeading = 20;

class MatchRow final : public juce::Button {
public:
    MatchRow(const juce::String& name, const juce::String& distance) : juce::Button(name), distance_(distance)
    {
        setTitle(name);
        setDescription("distance " + distance);
    }
    const juce::String& distance() const { return distance_; }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down) {
            g.setColour(theme::raised);
            g.fillRect(getLocalBounds());
        }
        auto area = getLocalBounds().reduced(14, 0);
        g.setFont(theme::font(theme::Face::Mono, 11.0f));
        g.setColour(theme::muted);
        g.drawText(distance_, area.removeFromRight(36), juce::Justification::centredRight, false);
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.setColour(theme::text);
        g.drawText(getButtonText(), area.withTrimmedRight(8), juce::Justification::centredLeft, true);
    }

private:
    juce::String distance_;
};

} // namespace

SimilarView::SimilarView() { clear(); }

void SimilarView::clear()
{
    matches_.clear();
    rows_.clear();
    message_ = "Select a sample";
    repaint();
}

void SimilarView::setResult(const SimilarResult& result)
{
    matches_ = result.matches;
    rows_.clear();
    switch (result.state) {
    case SimilarResult::State::NotAnalysed: message_ = "Not analysed yet"; break;
    case SimilarResult::State::Failed: message_ = "No similar samples"; break;
    case SimilarResult::State::Ok: message_ = matches_.empty() ? "No similar samples" : juce::String(); break;
    }
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        const auto& m = matches_[i];
        auto* row = rows_.add(new MatchRow(juce::String::fromUTF8(m.row.name.c_str()),
                                           juce::String::fromUTF8(ratioText(m.distance).c_str())));
        row->onClick = [this, i] {
            if (onPick && i < matches_.size()) onPick(matches_[i].row);
        };
        addAndMakeVisible(row);
    }
    resized();
    repaint();
}

juce::String SimilarView::distanceText(int index) const
{
    return index >= 0 && index < rows_.size() ? static_cast<const MatchRow*>(rows_[index])->distance() : juce::String();
}

void SimilarView::paint(juce::Graphics& g)
{
    g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.08f));
    g.setColour(theme::muted);
    g.drawText("SIMILAR", 14, kTop, getWidth() - 28, kHeading - 4, juce::Justification::centredLeft, false);
    if (message_.isNotEmpty()) {
        g.setFont(theme::font(theme::Face::Text, 12.0f));
        g.drawText(message_, 14, kTop + kHeading, getWidth() - 28, kRowHeight, juce::Justification::centredLeft, true);
    }
}

void SimilarView::resized()
{
    // As many as fit whole; the rest wait for a taller window.
    int y = kTop + kHeading;
    for (auto* r : rows_) {
        r->setBounds(0, y, getWidth(), kRowHeight);
        r->setVisible(y + kRowHeight <= getHeight());
        y += kRowHeight;
    }
}

} // namespace asma::app
