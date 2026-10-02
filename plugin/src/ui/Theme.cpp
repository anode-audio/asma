// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Theme.h"

#include <AsmaFonts.h>

namespace asma::app::theme {

juce::Typeface::Ptr typeface(Face face)
{
    // Made once, on first use, and kept for the life of the process.
    static const std::array<juce::Typeface::Ptr, 5> faces = [] {
        const auto load = [](const char* data, int size) {
            return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
        };
        return std::array<juce::Typeface::Ptr, 5>{
            load(AsmaFonts::SpaceGroteskSemiBold_ttf, AsmaFonts::SpaceGroteskSemiBold_ttfSize),
            load(AsmaFonts::InterRegular_ttf, AsmaFonts::InterRegular_ttfSize),
            load(AsmaFonts::InterMedium_ttf, AsmaFonts::InterMedium_ttfSize),
            load(AsmaFonts::InterSemiBold_ttf, AsmaFonts::InterSemiBold_ttfSize),
            load(AsmaFonts::JetBrainsMonoRegular_ttf, AsmaFonts::JetBrainsMonoRegular_ttfSize),
        };
    }();
    return faces[static_cast<std::size_t>(face)];
}

juce::Font font(Face face, float size) { return juce::Font(juce::FontOptions(typeface(face)).withPointHeight(size)); }

juce::Colour colourFor(Tone tone)
{
    switch (tone) {
    case Tone::Synced: return green;
    case Tone::Warning: return amber;
    case Tone::Muted: break;
    }
    return muted;
}

} // namespace asma::app::theme
