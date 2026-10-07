// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "AsmaProcessor.h"
#include "TestUtil.h"

#include <chrono>
#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::test {

// A host timeline the tests control.
class FakePlayHead final : public juce::AudioPlayHead {
public:
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};

// Runs empty blocks until the engine holds `generation` (the loader runs on
// its own thread), or fails after five seconds.
inline bool waitForPreview(app::AsmaProcessor& p, std::uint64_t generation, int block = 512)
{
    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        p.processBlock(buffer, midi);
        if (p.engine().status().generation == generation) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// A mouse event at `at` in `c`, as a click there would give.
inline juce::MouseEvent mouseAt(juce::Component& c, juce::Point<int> at, juce::ModifierKeys mods = {})
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto p = at.toFloat();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(source, p, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                            now, p, now, 1, false);
}

// Clicks a table cell the way its row would report it, `x` from the cell's
// left (a right click with mods = rightButtonModifier).
inline void clickCell(juce::TableListBox& table, int row, int column, int x, juce::ModifierKeys mods = {})
{
    auto& header = table.getHeader();
    const int left = header.getColumnPosition(header.getIndexOfColumnId(column, true)).getX();
    table.getTableListBoxModel()->cellClicked(row, column, mouseAt(table, {left + x, 5}, mods));
}

} // namespace asma::test
