// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Overview.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <optional>

namespace asma::app {

// The selected file's waveform with its trim handles and the playhead.
// Dragging a handle moves that end of the trim, at least kMinGap seconds from
// the other; double-clicking one puts it back at the file's edge; a click
// anywhere else plays. Nothing can be trimmed before the overview arrives.
class WaveformView : public juce::Component {
public:
    static constexpr double kMinGap = 0.01; // seconds between the handles
    static constexpr float kGrab = 6.0f;    // px either side of a handle that take it

    void setOverview(std::shared_ptr<const audio::Overview> overview);
    // Seconds; end < 0 is the end of the file, as audio::Edits has it.
    void setTrim(double start, double end);
    void setPlayhead(std::optional<double> seconds);

    const audio::Overview* overview() const { return overview_.get(); }
    double trimStart() const { return start_; }
    double trimEnd() const { return end_; }

    // A drag's outcome, once, when the mouse lets go; end -1 when at the file's end.
    std::function<void(double start, double end)> onTrimChanged;
    std::function<void()> onPlay;

    // What the mouse handlers do, in component coordinates.
    void press(float x);
    void drag(float x);
    void release();
    void doubleClick(float x);

    double secondsAt(float x) const;
    float xFor(double seconds) const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override { press(e.position.x); }
    void mouseDrag(const juce::MouseEvent& e) override { drag(e.position.x); }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { doubleClick(e.position.x); }
    juce::MouseCursor getMouseCursor() override;

private:
    enum class Handle { None, Start, End };
    double length() const { return overview_ ? overview_->seconds() : 0.0; }
    double endSeconds() const { return end_ < 0.0 ? length() : end_; }
    Handle handleAt(float x) const;
    juce::Rectangle<float> wave() const { return getLocalBounds().toFloat(); }

    std::shared_ptr<const audio::Overview> overview_;
    double start_ = 0.0;
    double end_ = -1.0;
    std::optional<double> playhead_;
    Handle dragging_ = Handle::None;
};

} // namespace asma::app
