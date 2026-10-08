// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace asma::app {

// After the rows were fetched again. JUCE redraws a row only when its row
// number or selection changes, so a file renamed outside asma (same count,
// new text) would keep its old name on screen: draw them all again.
inline void refreshRows(juce::TableListBox& table)
{
    table.updateContent();
    table.repaint();
}

} // namespace asma::app
