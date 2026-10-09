// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>

namespace asma::app {

// The standalone's Edit menu on macOS: "Undo Move 5 Samples", greyed when
// there is nothing to undo.
class EditMenu final : public juce::MenuBarModel {
public:
    // `label`: what undo would undo, empty for nothing.
    EditMenu(std::function<std::string()> label, std::function<void()> undo);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int itemId, int index) override;

private:
    enum { kUndo = 1 };
    std::function<std::string()> label_;
    std::function<void()> undo_;
};

} // namespace asma::app
