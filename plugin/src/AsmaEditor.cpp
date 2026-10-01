// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaEditor.h"

#include "AsmaProcessor.h"

namespace asma::app {

AsmaEditor::AsmaEditor(AsmaProcessor& owner) : juce::AudioProcessorEditor(owner)
{
    setSize(900, 600);
}

void AsmaEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

} // namespace asma::app
