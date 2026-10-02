// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "TempoChip.h"
#include "asma/audio/Edits.h"
#include "ui/Controls.h"
#include "ui/WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
#include <string>

namespace asma::app {

// The bottom panel's audition side: the selected file's name, its waveform
// and the controls that change how it plays. It shows what it is told and
// reports what the user does; the editor joins it to the processor.
class PreviewPanel : public juce::Component {
public:
    PreviewPanel();

    // Empty name: nothing selected, and the controls that need a file go dim.
    void setFile(const juce::String& name, const juce::String& line);
    void setEdits(const audio::Edits& edits);
    void setPlaying(bool playing);
    void setTempo(bool on, const ChipText& chip);
    // key: the project key, empty for none; status: what key sync does to
    // the selection ("+2", "?"), may be empty.
    void setKey(bool on, const std::string& key, const std::string& status);
    void setGainMatch(bool on);
    // Beats; 0 starts at once, 1 on the beat, 4 on the bar.
    void setQuantise(double beats);

    std::function<void(const audio::Edits&)> onEditsChanged;
    std::function<void()> onPlayStop;
    std::function<void(bool)> onTempoSync;
    // nullopt: key sync off; else on, with that project key.
    std::function<void(std::optional<std::string>)> onKeySync;
    std::function<void(bool)> onGainMatch;
    std::function<void(double)> onQuantise;

    // What the Key chip's menu does with a pick: 0 is "Off", then the keys
    // in kKeys order. Public for tests; the menu calls it.
    void chooseKey(int item);
    static const juce::StringArray& keys();

    // "Loops / Bass · 44.1 kHz · stereo · 8.00 s" (a long file: "5:01"); parts
    // that are unknown are left out.
    static juce::String fileLine(const std::string& folder, int sampleRate, int channels, double seconds);

    WaveformView& waveform() { return waveform_; }
    SegmentedControl& direction() { return direction_; }
    SegmentedControl& loop() { return loop_; }
    juce::Button& playButton() { return *play_; }
    juce::TextButton& resetButton() { return reset_; }
    ChipButton& tempoChip() { return tempo_; }
    ChipButton& keyChip() { return key_; }
    ChipButton& gainChip() { return gain_; }
    ChipButton& startChip() { return start_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void editsChanged();
    void updateReset(); // enabled when a file is shown and its edits are not the defaults
    void layoutControls(juce::Rectangle<int> area);
    int controlRows(int width) const;

    juce::String name_, line_;
    audio::Edits edits_;
    double quantise_ = 0.0;
    std::string keyName_;
    juce::Rectangle<int> separator_; // between the edits and the settings
    std::unique_ptr<juce::Button> play_;
    WaveformView waveform_;
    SegmentedControl direction_;
    SegmentedControl loop_;
    juce::TextButton reset_{"Reset edits"};
    ChipButton tempo_{"Tempo"};
    ChipButton key_{"Key"};
    ChipButton gain_{"Match loudness"};
    ChipButton start_{"Start"};
};

} // namespace asma::app
