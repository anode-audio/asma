// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Theme.h"

#include <AsmaFonts.h>
#include <array>

namespace asma::app::theme {

namespace {

// The faces, made on first use and let go when JUCE shuts down: a plugin's
// statics outlive JUCE in the host, and fonts held past shutdown crash some
// hosts as they unload the plugin.
class Faces : private juce::DeletedAtShutdown {
public:
    Faces()
    {
        const auto load = [](const char* data, int size) {
            return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
        };
        faces = {load(AsmaFonts::SpaceGroteskSemiBold_ttf, AsmaFonts::SpaceGroteskSemiBold_ttfSize),
                 load(AsmaFonts::InterRegular_ttf, AsmaFonts::InterRegular_ttfSize),
                 load(AsmaFonts::InterMedium_ttf, AsmaFonts::InterMedium_ttfSize),
                 load(AsmaFonts::InterSemiBold_ttf, AsmaFonts::InterSemiBold_ttfSize),
                 load(AsmaFonts::JetBrainsMonoRegular_ttf, AsmaFonts::JetBrainsMonoRegular_ttfSize)};
    }
    ~Faces() override { clearSingletonInstance(); }

    std::array<juce::Typeface::Ptr, 5> faces;

    JUCE_DECLARE_SINGLETON_INLINE(Faces, false)
};

} // namespace

juce::Typeface::Ptr typeface(Face face) { return Faces::getInstance()->faces[static_cast<std::size_t>(face)]; }

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
