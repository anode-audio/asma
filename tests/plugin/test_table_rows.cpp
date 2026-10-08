// SPDX-License-Identifier: GPL-3.0-only
#include "ui/TableRows.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

// Two rows whose text can change; counts the cells it is asked to paint.
struct Model final : juce::TableListBoxModel {
    juce::String names[2]{"Kick_01.wav", "Snare_02.wav"};
    int cellsPainted = 0;
    int getNumRows() override { return 2; }
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override {}
    void paintCell(juce::Graphics& g, int row, int, int width, int height, bool) override
    {
        ++cellsPainted;
        g.drawText(names[row], 0, 0, width, height, juce::Justification::centredLeft);
    }
};

void settle(juce::Component& c, Model& model)
{
    for (int quiet = 0, last = -1, i = 0; quiet < 5 && i < 100; ++i) {
        if (auto* peer = c.getPeer()) peer->performAnyPendingRepaintsNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        quiet = model.cellsPainted == last ? quiet + 1 : 0;
        last = model.cellsPainted;
    }
}

} // namespace

TEST_CASE("rows whose content changed are drawn again, though there are as many", "[table]")
{
#if JUCE_LINUX
    SKIP("needs a window manager, as the preview panel's repaint test");
#endif
    const juce::ScopedJuceInitialiser_GUI gui;
    Model model;
    juce::TableListBox table("t", &model);
    table.getHeader().addColumn("Name", 1, 200);
    table.setBounds(0, 0, 220, 120);
    table.setVisible(true);
    table.addToDesktop(0);
    settle(table, model);
    REQUIRE(model.cellsPainted > 0); // on screen and painting
    const int before = model.cellsPainted;
    model.names[0] = "Kick_01_renamed.wav"; // a file renamed outside asma: same count, new text
    asma::app::refreshRows(table);
    settle(table, model);
    CHECK(model.cellsPainted > before);
    table.removeFromDesktop();
}
