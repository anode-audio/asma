// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace asma::app {

class AsmaProcessor;

class AsmaEditor : public juce::AudioProcessorEditor {
public:
    explicit AsmaEditor(AsmaProcessor& owner);
    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaEditor)
};

} // namespace asma::app
