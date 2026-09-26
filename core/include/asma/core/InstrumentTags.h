// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace asma {

class InstrumentDictionary {
public:
    // Lines of "tag: token, token". '#' starts a comment line. Throws
    // std::invalid_argument naming the line for malformed input.
    static InstrumentDictionary parse(std::string_view text);

    // Parsed from data/instrument_tokens.txt, embedded at build time.
    static const InstrumentDictionary& builtin();

    // Sorted, unique tags for the given lowercase tokens.
    std::vector<std::string> tagsFor(const std::vector<std::string>& lowercaseTokens) const;

private:
    std::unordered_map<std::string, std::string> tokenToTag_;
};

} // namespace asma
