// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

// Splits on anything that isn't an ASCII letter, digit, '#' or a non-ASCII
// byte, and at letter/digit boundaries. Case is preserved.
std::vector<std::string> splitTokens(std::string_view text);

// Canonical key: sharps only, minor keys suffixed with "m" ("C", "F#", "Am",
// "C#m"). nextToken lets "E Minor" be read as one key.
std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken = {});

struct NameInfo {
    std::optional<double> bpm;
    std::optional<std::string> key;
    std::optional<bool> isLoop;
    std::vector<std::string> tokens; // lowercase; stem first, then folders nearest first
};

// relPath: root-relative, '/' separators, extension included.
NameInfo parseName(std::string_view relPath);

} // namespace asma
