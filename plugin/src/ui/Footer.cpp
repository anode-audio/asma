// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Footer.h"

#include "ui/Theme.h"

#include <cmath>

namespace asma::app {


void Footer::LinkLook::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(highlighted ? theme::amberLight : theme::amber);
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centredRight, false);
}

Footer::Footer()
{
    clear_.setLookAndFeel(&linkLook_);
    clear_.onClick = [this] {
        if (onClearRenders) onClearRenders();
    };
    addChildComponent(clear_);
}

Footer::~Footer() { clear_.setLookAndFeel(nullptr); }

juce::String Footer::sizeText(std::uintmax_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    // Whole numbers from 10 up; one decimal below, so 1.2 GB is not "1 GB".
    const long long tenths = std::llround(value * 10.0);
    const juce::String number = (unit == 0 || tenths >= 100) ? juce::String(std::llround(value))
                                                               : juce::String(tenths / 10) + "." + juce::String(tenths % 10);
    return number + " " + units[unit];
}

void Footer::setDrag(const juce::String& text)
{
    if (text == drag_) return;
    drag_ = text;
    repaint();
}

void Footer::setStatus(const juce::String& text)
{
    if (text == status_) return;
    status_ = text;
    resized();
    repaint();
}

void Footer::setRenderBytes(std::uintmax_t bytes)
{
    if (bytes == bytes_ && clear_.isVisible() == (bytes > 0)) return;
    bytes_ = bytes;
    clear_.setVisible(bytes > 0);
    resized();
    repaint();
}

juce::String Footer::rightText() const
{
    juce::StringArray parts;
    if (status_.isNotEmpty()) parts.add(status_);
    if (bytes_ > 0) parts.add("renders " + sizeText(bytes_));
    juce::String text = parts.joinIntoString(juce::String::fromUTF8(" · "));
    if (bytes_ > 0) text << juce::String::fromUTF8(" · ");
    return text;
}

void Footer::resized()
{
    auto area = getLocalBounds().reduced(16, 0);
    const auto font = theme::font(theme::Face::Mono, 11.0f);
    if (clear_.isVisible()) {
        const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, clear_.getButtonText())));
        clear_.setBounds(area.removeFromRight(w));
    }
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, rightText())));
    rightArea_ = area.removeFromRight(std::min(w, area.getWidth() / 2 + 120));
}

void Footer::paint(juce::Graphics& g)
{
    g.fillAll(theme::ground);
    g.setColour(theme::border);
    g.fillRect(0, 0, getWidth(), 1);
    g.setFont(theme::font(theme::Face::Mono, 11.0f));
    g.setColour(theme::muted);
    g.drawText(rightText(), rightArea_, juce::Justification::centredRight, true);
    g.drawText(drag_, getLocalBounds().reduced(16, 0).withRight(rightArea_.getX() - 16), juce::Justification::centredLeft, true);
}

} // namespace asma::app
