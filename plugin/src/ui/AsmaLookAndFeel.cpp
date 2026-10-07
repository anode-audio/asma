// SPDX-License-Identifier: GPL-3.0-only
#include "ui/AsmaLookAndFeel.h"

#include "ui/Theme.h"

namespace asma::app {

namespace {

// Quiet (transparent, muted) when off: as "asma.quiet" says, else when it toggles.
bool quiet(const juce::Button& button)
{
    const auto& props = button.getProperties();
    return props.contains("asma.quiet") ? static_cast<bool>(props["asma.quiet"]) : button.getClickingTogglesState();
}

} // namespace

AsmaLookAndFeel::AsmaLookAndFeel()
{
    using namespace theme;
    setColourScheme({surface, panel, ground, border, text, raised, ground, amber, text});
    setColour(juce::ResizableWindow::backgroundColourId, surface);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::buttonOnColourId, raised);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, text);
    setColour(juce::TextEditor::backgroundColourId, ground);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, border);
    setColour(juce::TextEditor::focusedOutlineColourId, border);
    setColour(juce::TextEditor::highlightColourId, amber.withAlpha(0.3f));
    setColour(juce::CaretComponent::caretColourId, amber);
    setColour(juce::ListBox::backgroundColourId, surface);
    setColour(juce::ListBox::textColourId, text);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TableHeaderComponent::backgroundColourId, surface);
    setColour(juce::TableHeaderComponent::textColourId, muted);
    setColour(juce::TableHeaderComponent::outlineColourId, border);
    setColour(juce::ScrollBar::thumbColourId, border);
    setColour(juce::Label::textColourId, text);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, raised);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::Typeface::Ptr AsmaLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return theme::typeface(theme::Face::Text);
    if (font.getTypefaceName() == juce::Font::getDefaultMonospacedFontName()) return theme::typeface(theme::Face::Mono);
    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font AsmaLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return theme::font(theme::Face::Text, 13.0f); }

void AsmaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted,
                                           bool down)
{
    const auto& props = button.getProperties();
    const auto segment = props["asma.segment"].toString();
    const bool on = button.getToggleState();
    juce::Colour fill = theme::raised;
    if (on && props["asma.accent"]) fill = theme::amber;
    else if (!on && quiet(button)) fill = juce::Colours::transparentBlack;
    if (highlighted || down) fill = fill.isTransparent() ? theme::raised.withAlpha(0.6f) : fill.brighter(0.06f);

    auto r = button.getLocalBounds().toFloat();
    if (props["asma.primary"]) { // the one action of a panel: amber, dimmed while it cannot act
        const bool enabled = button.isEnabled();
        g.setColour(enabled ? (highlighted || down ? theme::amber.brighter(0.06f) : theme::amber)
                            : theme::amber.withAlpha(0.3f).overlaidWith(juce::Colours::transparentBlack));
        g.fillRoundedRectangle(r, theme::kRadius);
        return;
    }
    if (segment.isEmpty()) {
        r = r.reduced(0.5f);
        g.setColour(fill);
        g.fillRoundedRectangle(r, theme::kRadius);
        g.setColour(theme::border);
        g.drawRoundedRectangle(r, theme::kRadius, 1.0f);
        return;
    }
    // Inside a segmented switch the group draws the frame; a segment draws
    // its fill, rounded on the switch's outer corners, and the line that
    // parts it from the one before.
    const bool first = segment == "first", last = segment == "last";
    juce::Path shape;
    shape.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), theme::kRadius, theme::kRadius, first, last,
                              first, last);
    g.setColour(fill);
    g.fillPath(shape);
    if (segment == "middle" || segment == "last") {
        g.setColour(theme::border);
        g.fillRect(r.withWidth(1.0f));
    }
}

void AsmaLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto& props = button.getProperties();
    const bool on = button.getToggleState();
    const bool primary = props["asma.primary"];
    const bool accent = (on && props["asma.accent"]) || primary;
    const bool quietText = !on && quiet(button);
    const float size = props.contains("asma.size") ? static_cast<float>(props["asma.size"]) : 13.0f;
    const bool mono = props["asma.mono"];
    g.setFont(theme::font(mono ? theme::Face::Mono : (accent ? theme::Face::SemiBold : theme::Face::Text), size));
    juce::Colour colour = accent ? theme::ground : (quietText ? theme::muted : theme::text);
    if (!button.isEnabled()) colour = primary ? theme::muted : theme::faint;
    g.setColour(colour);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
}

void AsmaLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll(theme::surface);
    g.setColour(theme::border);
    g.fillRect(0, header.getHeight() - 1, header.getWidth(), 1);
}

void AsmaLookAndFeel::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& name,
                                            int columnId, int width, int height, bool, bool, int columnFlags)
{
    // The first column starts at the table's margin, as its cells do; the
    // sorted one is lit, with an arrow for its direction.
    using Header = juce::TableHeaderComponent;
    const int x = header.getIndexOfColumnId(columnId, true) == 0 ? kTableMargin : 0;
    const bool up = (columnFlags & Header::sortedForwards) != 0, down = (columnFlags & Header::sortedBackwards) != 0;
    juce::String text = name.toUpperCase();
    if (up) text << juce::String::fromUTF8(" \u25b4");
    if (down) text << juce::String::fromUTF8(" \u25be");
    g.setFont(theme::font(theme::Face::Heading, 11.0f).withExtraKerningFactor(0.06f));
    g.setColour(up || down ? theme::text : theme::muted);
    g.drawText(text, x, 0, width - x, height - 1, juce::Justification::centredLeft, true);
}

void AsmaLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                    int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int>(x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int>(thumbStart, y, thumbSize, height);
    g.setColour(mouseOver || mouseDown ? theme::faint : theme::border);
    g.fillRoundedRectangle(thumb.toFloat().reduced(3.0f), 2.0f);
}

void AsmaLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    // A popover: a raised panel with a hairline, no shadow.
    g.setColour(theme::panel);
    g.fillPath(path);
    g.setColour(theme::border);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void AsmaLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                           theme::kRadius);
}

void AsmaLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f),
                           theme::kRadius, 1.0f);
}

} // namespace asma::app
