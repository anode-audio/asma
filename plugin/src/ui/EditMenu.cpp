// SPDX-License-Identifier: GPL-3.0-only
#include "EditMenu.h"

namespace asma::app {

EditMenu::EditMenu(std::function<std::string()> label, std::function<void()> undo)
    : label_(std::move(label)), undo_(std::move(undo))
{
}

juce::StringArray EditMenu::getMenuBarNames() { return {"Edit"}; }

juce::PopupMenu EditMenu::getMenuForIndex(int, const juce::String&)
{
    const std::string label = label_ ? label_() : std::string();
    juce::PopupMenu menu;
    juce::PopupMenu::Item undo(label.empty() ? juce::String("Undo") : "Undo " + juce::String::fromUTF8(label.c_str()));
    undo.setID(kUndo).setEnabled(!label.empty());
    undo.shortcutKeyDescription = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0).getTextDescriptionWithIcons();
    menu.addItem(std::move(undo));
    return menu;
}

void EditMenu::menuItemSelected(int itemId, int)
{
    if (itemId == kUndo && undo_) undo_();
}

} // namespace asma::app
