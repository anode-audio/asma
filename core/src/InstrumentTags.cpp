// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/InstrumentTags.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace asma {

namespace detail {
extern const char* const kInstrumentTokensData;
}

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

InstrumentDictionary InstrumentDictionary::parse(std::string_view text)
{
    InstrumentDictionary dict;
    int lineNumber = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto newline = text.find('\n', pos);
        const auto end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = trim(text.substr(pos, end - pos));
        ++lineNumber;
        pos = end + 1;

        if (!line.empty() && line.front() != '#') {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos || trim(line.substr(0, colon)).empty())
                throw std::invalid_argument("instrument dictionary line " + std::to_string(lineNumber)
                                            + ": expected 'tag: token, token'");
            const std::string tag = lower(trim(line.substr(0, colon)));
            std::string_view tokens = line.substr(colon + 1);
            while (!tokens.empty()) {
                const auto comma = tokens.find(',');
                const std::string token = lower(trim(tokens.substr(0, comma)));
                if (!token.empty()) dict.tokenToTag_.emplace(token, tag); // first claim wins
                if (comma == std::string_view::npos) break;
                tokens.remove_prefix(comma + 1);
            }
        }
        if (newline == std::string_view::npos) break;
    }
    return dict;
}

const InstrumentDictionary& InstrumentDictionary::builtin()
{
    static const InstrumentDictionary dict = parse(detail::kInstrumentTokensData);
    return dict;
}

std::vector<std::string> InstrumentDictionary::tagsFor(const std::vector<std::string>& lowercaseTokens) const
{
    std::set<std::string> tags;
    for (const auto& token : lowercaseTokens)
        if (auto it = tokenToTag_.find(token); it != tokenToTag_.end()) tags.insert(it->second);
    return {tags.begin(), tags.end()};
}

} // namespace asma
