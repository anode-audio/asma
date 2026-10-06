// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Query.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace asma::app {

// The filters the chip row offers, in its order.
enum class Facet { Type, Bpm, Key, Instrument, Length, Rating };
inline constexpr std::array<Facet, 6> kFacets{Facet::Type, Facet::Bpm, Facet::Key,
                                              Facet::Instrument, Facet::Length, Facet::Rating};

// The chip's text: its value when the search sets it ("118–132 BPM",
// "Am, C", "★★★ and up"), else what it filters ("BPM"). UTF-8.
std::string chipLabel(Facet facet, const SearchModel& model);
bool isSet(Facet facet, const SearchModel& model);
// The search without that filter; clearedAll drops all six, keeping the
// scope, the text and the sort.
SearchModel cleared(Facet facet, SearchModel model);
SearchModel clearedAll(SearchModel model);

struct LengthPreset {
    const char* label;
    std::optional<double> min, max; // seconds
};
inline const std::array<LengthPreset, 4> kLengthPresets{{{"Under 1 s", std::nullopt, 1.0},
                                                         {"1–10 s", 1.0, 10.0},
                                                         {"10–60 s", 10.0, 60.0},
                                                         {"Over a minute", 60.0, std::nullopt}}};

// A BPM range "near" a tempo: within 3%, to whole BPM.
std::pair<double, double> bpmNear(double bpm);

} // namespace asma::app
