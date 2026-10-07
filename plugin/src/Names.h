// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace asma::app {

// Collection and saved-search names, checked as the library checks them:
// trimmed, not empty, unique within their kind ignoring case. JUCE-free.
enum class NameCheck {
    Ok,
    Empty,
    Taken,     // another collection or saved search has it
    Unchanged, // a rename to the name it already has: no change, not a refusal
};

std::string trimmedName(std::string_view name);

// `current`: the name being renamed, empty for a new one.
NameCheck checkName(std::string_view name, const std::vector<std::string>& existing, std::string_view current = {});

} // namespace asma::app
