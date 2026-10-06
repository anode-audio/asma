// SPDX-License-Identifier: GPL-3.0-only
#include "Filters.h"

#include "TempoChip.h"

#include <cmath>

namespace asma::app {

namespace {

// The first three, then how many more: a chip stays short.
std::string listed(const std::vector<std::string>& items)
{
    std::string text;
    for (std::size_t i = 0; i < items.size() && i < 3; ++i) text += (i ? ", " : "") + items[i];
    if (items.size() > 3) text += " +" + std::to_string(items.size() - 3);
    return text;
}

// "118\u2013132 BPM", or with one end open "from 118 BPM" / "up to 132 BPM".
std::string range(const std::optional<double>& lo, const std::optional<double>& hi, const char* unit, const char* above,
                  const char* below)
{
    if (lo && hi) return bpmText(*lo) + "\u2013" + bpmText(*hi) + " " + unit;
    if (lo) return std::string(above) + " " + bpmText(*lo) + " " + unit;
    return std::string(below) + " " + bpmText(*hi) + " " + unit;
}

} // namespace

bool isSet(Facet facet, const SearchModel& m)
{
    switch (facet) {
    case Facet::Type: return m.type != SampleType::Any;
    case Facet::Bpm: return m.bpmMin.has_value() || m.bpmMax.has_value();
    case Facet::Key: return !m.keys.empty();
    case Facet::Instrument: return !m.tags.empty();
    case Facet::Length: return m.durationMin.has_value() || m.durationMax.has_value();
    case Facet::Rating: return m.minRating.has_value();
    }
    return false;
}

std::string chipLabel(Facet facet, const SearchModel& m)
{
    if (!isSet(facet, m)) {
        switch (facet) {
        case Facet::Type: return "Type";
        case Facet::Bpm: return "BPM";
        case Facet::Key: return "Key";
        case Facet::Instrument: return "Instrument";
        case Facet::Length: return "Length";
        case Facet::Rating: return "Rating";
        }
    }
    switch (facet) {
    case Facet::Type: return m.type == SampleType::Loop ? "Loops" : "One-shots";
    case Facet::Bpm: return range(m.bpmMin, m.bpmMax, "BPM", "from", "up to");
    case Facet::Key: return listed(m.keys);
    case Facet::Instrument: return listed(m.tags);
    case Facet::Length: return range(m.durationMin, m.durationMax, "s", "over", "under");
    case Facet::Rating: {
        std::string stars;
        for (int i = 0; i < *m.minRating; ++i) stars += "★";
        return *m.minRating >= 5 ? stars : stars + " and up";
    }
    }
    return {};
}

SearchModel cleared(Facet facet, SearchModel m)
{
    switch (facet) {
    case Facet::Type: m.type = SampleType::Any; break;
    case Facet::Bpm: m.bpmMin.reset(); m.bpmMax.reset(); break;
    case Facet::Key: m.keys.clear(); break;
    case Facet::Instrument: m.tags.clear(); break;
    case Facet::Length: m.durationMin.reset(); m.durationMax.reset(); break;
    case Facet::Rating: m.minRating.reset(); break;
    }
    return m;
}

SearchModel clearedAll(SearchModel m)
{
    for (const Facet f : kFacets) m = cleared(f, std::move(m));
    return m;
}

std::pair<double, double> bpmNear(double bpm)
{
    return {std::round(bpm * 0.97), std::round(bpm * 1.03)};
}

} // namespace asma::app
