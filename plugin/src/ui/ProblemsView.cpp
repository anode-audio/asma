// SPDX-License-Identifier: GPL-3.0-only
#include "ui/ProblemsView.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {

namespace {

constexpr int kHeaderHeight = 44;
constexpr int kColumnsHeight = 30;
constexpr int kRowHeight = 46;
constexpr int kButtonWidth = 70;
constexpr int kMargin = 14;

juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

std::string baseName(const std::string& relPath)
{
    const auto slash = relPath.rfind('/');
    return slash == std::string::npos ? relPath : relPath.substr(slash + 1);
}

std::string folderOf(const Problem& p)
{
    const auto slash = p.relPath.rfind('/');
    const std::string root = baseName(p.rootPath);
    if (slash == std::string::npos) return root;
    return root + "/" + p.relPath.substr(0, slash);
}

// The two columns' widths, from the area left of the button.
std::pair<int, int> columns(int width)
{
    const int room = width - 2 * kMargin - kButtonWidth - 20;
    const int file = room * 10 / 23;
    return {file, room - file};
}

} // namespace

class ProblemsView::Rows final : public juce::Component {
public:
    explicit Rows(ProblemsView& owner) : owner_(owner) {}
    void paint(juce::Graphics& g) override
    {
        const auto [file, reason] = columns(getWidth());
        for (int i = 0; i < owner_.rowCount(); ++i) {
            const int y = i * kRowHeight;
            g.setColour(theme::raised);
            g.fillRect(0, y + kRowHeight - 1, getWidth(), 1);
            g.setFont(theme::font(theme::Face::Text, 13.0f));
            g.setColour(theme::text);
            g.drawText(owner_.nameText(i), kMargin, y + 7, file, 18, juce::Justification::centredLeft, true);
            g.setFont(theme::font(theme::Face::Mono, 11.0f));
            g.setColour(theme::muted);
            g.drawText(owner_.folderText(i), kMargin, y + 25, file, 14, juce::Justification::centredLeft, true);
            const bool retrying = owner_.retrying_.count(owner_.problems_[static_cast<std::size_t>(i)].id) > 0;
            g.setFont(theme::font(theme::Face::Text, 12.0f));
            g.setColour(retrying ? theme::muted : theme::text);
            g.drawFittedText(owner_.shownReason(i), kMargin + file + 10, y + 4, reason, kRowHeight - 8,
                             juce::Justification::centredLeft, 2);
        }
    }

private:
    ProblemsView& owner_;
};

ProblemsView::ProblemsView() : rows_(std::make_unique<Rows>(*this))
{
    retryAll_.getProperties().set("asma.accent", true);
    retryAll_.onClick = [this] {
        std::vector<std::int64_t> ids;
        for (const auto& p : problems_)
            if (!retrying_.count(p.id)) ids.push_back(p.id);
        if (!ids.empty() && onRetry) onRetry(ids);
    };
    addAndMakeVisible(retryAll_);
    viewport_.setViewedComponent(rows_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
}

ProblemsView::~ProblemsView() = default;

juce::String ProblemsView::reasonText(const Problem& p)
{
    if (p.kind == Problem::Kind::Analysis) return "Analysis failed: " + utf8(p.reason);
    if (p.reason == "The file is gone") return utf8(p.reason);
    return "Could not read the file: " + utf8(p.reason);
}

juce::String ProblemsView::nameText(int index) const { return utf8(baseName(problems_[static_cast<std::size_t>(index)].relPath)); }

juce::String ProblemsView::folderText(int index) const { return utf8(folderOf(problems_[static_cast<std::size_t>(index)])); }

juce::String ProblemsView::shownReason(int index) const
{
    const auto& p = problems_[static_cast<std::size_t>(index)];
    if (retrying_.count(p.id)) return juce::String::fromUTF8(waiting_ ? "Retrying after the scan…" : "Retrying…");
    return reasonText(p);
}

juce::String ProblemsView::countText() const
{
    return juce::String(problems_.size()) + (problems_.size() == 1 ? " file" : " files");
}

void ProblemsView::setProblems(std::vector<Problem> problems)
{
    problems_ = std::move(problems);
    retryButtons_.clear();
    for (const auto& p : problems_) {
        auto* b = retryButtons_.add(new juce::TextButton("Retry"));
        b->setTitle("Retry " + utf8(baseName(p.relPath)));
        b->getProperties().set("asma.size", 12.0f);
        b->onClick = [this, id = p.id] {
            if (onRetry) onRetry({id});
        };
        rows_->addAndMakeVisible(b);
    }
    setRetrying(retrying_, waiting_);
    resized();
}

void ProblemsView::setRetrying(std::set<std::int64_t> ids, bool waiting)
{
    retrying_ = std::move(ids);
    waiting_ = waiting;
    for (std::size_t i = 0; i < problems_.size(); ++i)
        retryButtons_[static_cast<int>(i)]->setEnabled(!retrying_.count(problems_[i].id));
    bool any = false;
    for (const auto& p : problems_) any |= !retrying_.count(p.id);
    retryAll_.setEnabled(any);
    retryAll_.setVisible(!problems_.empty());
    rows_->repaint();
    repaint();
}

void ProblemsView::paint(juce::Graphics& g)
{
    g.fillAll(theme::surface);
    auto header = getLocalBounds().removeFromTop(kHeaderHeight).reduced(kMargin, 0);
    const auto title = theme::font(theme::Face::Heading, 14.0f);
    g.setFont(title);
    g.setColour(theme::text);
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(title, "Problems")));
    g.drawText("Problems", header.removeFromLeft(w), juce::Justification::centredLeft, false);
    header.removeFromLeft(10);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(theme::muted);
    g.drawText(countText(), header, juce::Justification::centredLeft, false);
    g.setColour(theme::border);
    g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);
    if (problems_.empty()) {
        g.setFont(theme::font(theme::Face::Text, 13.0f));
        g.setColour(theme::muted);
        g.drawText(message(), getLocalBounds().withTrimmedTop(kHeaderHeight), juce::Justification::centred, false);
        return;
    }
    const auto [file, reason] = columns(getWidth());
    g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.06f));
    g.setColour(theme::muted);
    g.drawText("FILE", kMargin, kHeaderHeight, file, kColumnsHeight, juce::Justification::centredLeft, false);
    g.drawText("WHAT WENT WRONG", kMargin + file + 10, kHeaderHeight, reason, kColumnsHeight,
               juce::Justification::centredLeft, false);
    g.setColour(theme::border);
    g.fillRect(0, kHeaderHeight + kColumnsHeight - 1, getWidth(), 1);
}

void ProblemsView::resized()
{
    retryAll_.setBounds(getWidth() - kMargin - 84, (kHeaderHeight - 28) / 2, 84, 28);
    viewport_.setBounds(getLocalBounds().withTrimmedTop(kHeaderHeight + kColumnsHeight));
    const int w = viewport_.getMaximumVisibleWidth();
    rows_->setSize(w, rowCount() * kRowHeight);
    for (int i = 0; i < retryButtons_.size(); ++i)
        retryButtons_[i]->setBounds(w - kMargin - kButtonWidth, i * kRowHeight + (kRowHeight - 26) / 2, kButtonWidth, 26);
}

} // namespace asma::app
