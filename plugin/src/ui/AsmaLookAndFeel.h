// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace asma::app {

// The Anode theme for JUCE's own widgets. Buttons read properties:
// "asma.segment" ("first", "middle" or "last") draws one as part of a
// segmented switch; "asma.accent" fills it amber when on; "asma.quiet" leaves
// it transparent and muted when off; "asma.size" sets its text size (13).
// A toggling button is quiet when off.
class AsmaLookAndFeel : public juce::LookAndFeel_V4 {
public:
    static constexpr int kTableMargin = 14; // px before a table's first column's text
    AsmaLookAndFeel();

    // Fonts that name no typeface get the embedded Inter, never a system face.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down) override;

    void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override;
    void drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& name,
                               int columnId, int width, int height, bool mouseOver, bool mouseDown,
                               int columnFlags) override;

    int getDefaultScrollbarWidth() override { return 10; }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                       int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override;

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
};

} // namespace asma::app
