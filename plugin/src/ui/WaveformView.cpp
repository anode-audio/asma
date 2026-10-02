// SPDX-License-Identifier: GPL-3.0-only
#include "ui/WaveformView.h"

#include "TempoChip.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace asma::app {

void WaveformView::setOverview(std::shared_ptr<const audio::Overview> overview)
{
    if (overview == overview_) return;
    overview_ = std::move(overview);
    repaint();
}

void WaveformView::setTrim(double start, double end)
{
    if (dragging_ != Handle::None) return; // the user's hand wins
    start_ = start;
    end_ = end;
    repaint();
}

void WaveformView::setPlayhead(std::optional<double> seconds)
{
    if (seconds == playhead_) return;
    playhead_ = seconds;
    repaint();
}

double WaveformView::secondsAt(float x) const
{
    const auto w = wave();
    if (w.getWidth() <= 0.0f) return 0.0;
    return std::clamp(static_cast<double>((x - w.getX()) / w.getWidth()), 0.0, 1.0) * length();
}

float WaveformView::xFor(double seconds) const
{
    const auto w = wave();
    const double len = length();
    if (len <= 0.0) return w.getX();
    return w.getX() + static_cast<float>(std::clamp(seconds / len, 0.0, 1.0)) * w.getWidth();
}

WaveformView::Handle WaveformView::handleAt(float x) const
{
    if (!overview_) return Handle::None;
    const float s = std::abs(x - xFor(start_));
    const float e = std::abs(x - xFor(endSeconds()));
    if (s > kGrab && e > kGrab) return Handle::None;
    return s <= e ? Handle::Start : Handle::End;
}

void WaveformView::press(float x)
{
    dragging_ = handleAt(x);
    pressStart_ = start_;
    pressEnd_ = end_;
    if (dragging_ == Handle::None && onPlay) onPlay();
}

void WaveformView::drag(float x)
{
    const double s = secondsAt(x);
    if (dragging_ == Handle::Start) start_ = std::clamp(s, 0.0, std::max(0.0, endSeconds() - kMinGap));
    else if (dragging_ == Handle::End) end_ = std::clamp(s, std::min(length(), start_ + kMinGap), length());
    else return;
    repaint();
}

void WaveformView::release()
{
    if (dragging_ == Handle::None) return;
    dragging_ = Handle::None;
    if (end_ >= length()) end_ = -1.0; // dragged to the end: untrimmed there
    // A press that moved nothing restarts nothing.
    const bool moved = start_ < pressStart_ || start_ > pressStart_ || end_ < pressEnd_ || end_ > pressEnd_;
    if (moved && onTrimChanged) onTrimChanged(start_, end_);
}

void WaveformView::doubleClick(float x)
{
    const Handle h = handleAt(x);
    dragging_ = Handle::None;
    if (h == Handle::None) return;
    if (h == Handle::Start) {
        if (start_ <= 0.0) return; // already at the edge
        start_ = 0.0;
    } else {
        if (end_ < 0.0) return;
        end_ = -1.0;
    }
    repaint();
    if (onTrimChanged) onTrimChanged(start_, end_);
}

juce::MouseCursor WaveformView::getMouseCursor()
{
    const auto x = static_cast<float>(getMouseXYRelative().x);
    return handleAt(x) == Handle::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor;
}

void WaveformView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::ground);
    g.fillRoundedRectangle(bounds, theme::kRadius);
    const float mid = std::round(bounds.getCentreY());
    g.setColour(theme::zeroLine);
    g.fillRect(bounds.getX(), mid, bounds.getWidth(), 1.0f);

    if (overview_ && overview_->frames > 0 && overview_->channels() > 0) {
        const float x0 = xFor(start_), x1 = xFor(endSeconds());
        const float head = playhead_ ? xFor(*playhead_) : x0;
        const float half = (bounds.getHeight() - 12.0f) / 2.0f;
        const int width = getWidth();
        const int points = audio::Overview::kPoints;
        for (int px = 0; px < width; ++px) {
            // Every overview point under this column, all channels: one lane.
            const int from = px * points / width;
            const int to = std::max(from + 1, (px + 1) * points / width);
            float lo = 0.0f, hi = 0.0f;
            for (int c = 0; c < overview_->channels(); ++c)
                for (int i = from; i < to; ++i) {
                    lo = std::min(lo, overview_->min[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]);
                    hi = std::max(hi, overview_->max[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]);
                }
            const float x = static_cast<float>(px);
            const float top = mid - std::min(hi, 1.0f) * half;
            const float bottom = mid - std::max(lo, -1.0f) * half;
            const bool inside = x >= x0 && x < x1;
            g.setColour(!inside ? theme::cyanFaint : (x < head ? theme::cyan : theme::cyanDim));
            g.fillRect(x, top, 1.0f, std::max(1.0f, bottom - top));
        }

        // Outside the trim, dimmed; the handles over it.
        g.setColour(theme::ground.withAlpha(0.55f));
        g.fillRect(bounds.withRight(x0));
        g.fillRect(bounds.withLeft(x1));
        // Both handles stand inside what is kept: the start's line right of
        // its point, the end's left of it.
        g.setColour(theme::amber);
        for (const float hx : {std::round(x0), std::round(x1) - 2.0f}) {
            const float lx = std::clamp(hx, bounds.getX(), bounds.getRight() - 2.0f);
            g.fillRect(lx, bounds.getY(), 2.0f, bounds.getHeight());
            juce::Path tab;
            tab.addRoundedRectangle(lx - 5.0f, bounds.getY(), 12.0f, 12.0f, 3.0f, 3.0f, false, false, true, true);
            g.fillPath(tab);
        }
        g.setFont(theme::font(theme::Face::Mono, 10.0f));
        g.drawText(secondsText(start_), juce::Rectangle<float>(x0 + 6.0f, bounds.getBottom() - 16.0f, 80.0f, 14.0f),
                   juce::Justification::centredLeft, false);
        g.drawText(secondsText(endSeconds()), juce::Rectangle<float>(x1 - 86.0f, bounds.getBottom() - 16.0f, 80.0f, 14.0f),
                   juce::Justification::centredRight, false);
        if (playhead_) {
            g.setColour(theme::text);
            g.fillRect(std::round(head), bounds.getY(), 1.0f, bounds.getHeight());
        }
    }

    g.setColour(theme::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme::kRadius, 1.0f);
}

} // namespace asma::app
