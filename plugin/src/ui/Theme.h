// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "TempoChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The Anode look, copied from the identity rather than shared: anode-common
// is proprietary. Spec section 9, Look.
namespace asma::app::theme {

// Grounds, darkest first.
inline const juce::Colour ground{0xff0b0c0e};  // inputs, waveform, footer
inline const juce::Colour panel{0xff111316};   // top bar, sidebar, bottom panel
inline const juce::Colour surface{0xff15171a}; // the window
inline const juce::Colour raised{0xff1c1f23};  // pressed switches, row lines
inline const juce::Colour border{0xff2a2d33};
inline const juce::Colour faint{0xff3a3e45};   // empty stars, disabled text
inline const juce::Colour text{0xffe8e9eb};
inline const juce::Colour muted{0xff8a8f98};
// Accents: amber only for what is active or selected, cyan for the waveform,
// green only for a synced tempo.
inline const juce::Colour amber{0xffe8a33d};
inline const juce::Colour amberLight{0xfff2bd6b};
inline const juce::Colour cyan{0xff4fd1e6};
inline const juce::Colour cyanDim{0xff3aa3b5};  // the waveform after the playhead
inline const juce::Colour cyanFaint{0xff2b5c66}; // the waveform outside the trim
inline const juce::Colour zeroLine{0xff23262b};  // the waveform's centre line
inline const juce::Colour green{0xff7de38e};

constexpr float kRadius = 4.0f;
constexpr float kCardRadius = 8.0f;

// Sizes from the approved design, in px at 100%.
constexpr int kTopBarHeight = 56;
constexpr int kChipRowHeight = 44;
constexpr int kSidebarWidth = 220;
constexpr int kPreviewHeight = 236;
constexpr int kSimilarWidth = 284;
constexpr int kFooterHeight = 26;
constexpr int kRowHeight = 30;
constexpr int kHeaderRowHeight = 30;

enum class Face {
    Heading,  // Space Grotesk 600: the wordmark, the file name, section labels
    Text,     // Inter 400
    Medium,   // Inter 500
    SemiBold, // Inter 600
    Mono,     // JetBrains Mono 400: numbers
};

// The embedded face at a CSS-style size (the em, as in the design).
juce::Font font(Face face, float size);
juce::Typeface::Ptr typeface(Face face);

juce::Colour colourFor(Tone tone);

} // namespace asma::app::theme
