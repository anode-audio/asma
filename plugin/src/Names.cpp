// SPDX-License-Identifier: GPL-3.0-only
#include "Names.h"

#include <algorithm>
#include <cctype>

namespace asma::app {

namespace {

// As SQLite's NOCASE compares: ASCII letters only.
bool sameIgnoringCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::string trimmedName(std::string_view name)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!name.empty() && isSpace(name.front())) name.remove_prefix(1);
    while (!name.empty() && isSpace(name.back())) name.remove_suffix(1);
    return std::string(name);
}

NameCheck checkName(std::string_view name, const std::vector<std::string>& existing, std::string_view current)
{
    const std::string wanted = trimmedName(name);
    if (wanted.empty()) return NameCheck::Empty;
    if (!current.empty() && wanted == trimmedName(current)) return NameCheck::Unchanged;
    for (const auto& other : existing) {
        if (!current.empty() && sameIgnoringCase(other, trimmedName(current))) continue; // itself, in other case
        if (sameIgnoringCase(other, wanted)) return NameCheck::Taken;
    }
    return NameCheck::Ok;
}

} // namespace asma::app
