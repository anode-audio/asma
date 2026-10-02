// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>

namespace asma::app {

// The line at the foot of the window: on the left what a drag-out carries,
// on the right the scan's progress or outcome, the kept renders' size and
// "Clear renders".
class Footer : public juce::Component {
public:
    Footer();
    ~Footer() override;

    void setDrag(const juce::String& text);
    void setStatus(const juce::String& text);
    void setRenderBytes(std::uintmax_t bytes);

    std::function<void()> onClearRenders;

    // "41 MB", "820 KB", "1.2 GB": the size a person reads.
    static juce::String sizeText(std::uintmax_t bytes);

    juce::String dragText() const { return drag_; }
    juce::String rightText() const;
    juce::TextButton& clearButton() { return clear_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String drag_, status_;
    std::uintmax_t bytes_ = 0;
    // "Clear renders" is a link: amber text, nothing else.
    struct LinkLook final : juce::LookAndFeel_V4 {
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool highlighted, bool) override;
    };
    LinkLook linkLook_;
    juce::TextButton clear_{"Clear renders"};
    juce::Rectangle<int> rightArea_;
};

} // namespace asma::app
