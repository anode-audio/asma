// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/NameParse.h"

#include <array>
#include <cctype>

namespace asma {

namespace {

using Group = std::vector<std::string>;

std::string toLower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isNumber(std::string_view s)
{
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::optional<double> numberInRange(std::string_view s, double lo, double hi)
{
    if (!isNumber(s) || s.size() > 3) return std::nullopt;
    const double value = std::stod(std::string(s));
    if (value < lo || value > hi) return std::nullopt;
    return value;
}

enum class Quality { None, Minor, Major };

// Returns the quality named by a whole word, if any.
std::optional<Quality> qualityWord(std::string_view s)
{
    const std::string lower = toLower(s);
    if (lower == "min" || lower == "minor") return Quality::Minor;
    if (lower == "maj" || lower == "major") return Quality::Major;
    return std::nullopt;
}

std::optional<bool> loopDecision(const Group& g)
{
    for (std::size_t i = 0; i < g.size(); ++i) {
        const std::string t = toLower(g[i]);
        if (t == "loop" || t == "loops") return true;
        if (t == "oneshot" || t == "oneshots") return false;
        if (t == "one" && i + 1 < g.size()) {
            const std::string next = toLower(g[i + 1]);
            if (next == "shot" || next == "shots") return false;
        }
    }
    return std::nullopt;
}

std::optional<double> bpmFromBpmToken(const Group& g)
{
    for (std::size_t i = 0; i < g.size(); ++i) {
        if (toLower(g[i]) != "bpm") continue;
        if (i > 0)
            if (auto v = numberInRange(g[i - 1], 40, 300)) return v;
        if (i + 1 < g.size())
            if (auto v = numberInRange(g[i + 1], 40, 300)) return v;
    }
    return std::nullopt;
}

std::optional<double> trailingTempo(const Group& stem)
{
    for (auto it = stem.rbegin(); it != stem.rend(); ++it) {
        if (!isNumber(*it)) continue;
        if (it->size() < 2) return std::nullopt;
        return numberInRange(*it, 60, 200);
    }
    return std::nullopt;
}

} // namespace

std::vector<std::string> splitTokens(std::string_view text)
{
    enum class Kind { None, Alpha, Digit };
    std::vector<std::string> out;
    std::string current;
    Kind kind = Kind::None;
    auto flush = [&] {
        if (!current.empty()) out.push_back(current);
        current.clear();
        kind = Kind::None;
    };
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        Kind k;
        if (std::isdigit(u)) k = Kind::Digit;
        else if (std::isalpha(u) || c == '#' || u >= 0x80) k = Kind::Alpha;
        else {
            flush();
            continue;
        }
        if (kind != Kind::None && k != kind) flush();
        current.push_back(c);
        kind = k;
    }
    flush();
    return out;
}

std::optional<std::string> canonicalKey(std::string_view text)
{
    static constexpr std::array<std::string_view, 12> kNames = {"C", "C#", "D", "D#", "E", "F",
                                                                "F#", "G", "G#", "A", "A#", "B"};
    const std::string_view root = !text.empty() && text.back() == 'm' ? text.substr(0, text.size() - 1) : text;
    for (const auto name : kNames)
        if (name == root) return std::string(text);
    return std::nullopt;
}

std::optional<std::string> parseKeyToken(std::string_view token, std::string_view nextToken)
{
    if (token.empty()) return std::nullopt;
    static constexpr std::string_view kLetters = "CDEFGAB";
    static constexpr std::array<int, 7> kPitch = {0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<const char*, 12> kNames = {"C", "C#", "D", "D#", "E", "F",
                                                           "F#", "G", "G#", "A", "A#", "B"};

    const char letter = token[0];
    const auto index = kLetters.find(static_cast<char>(std::toupper(static_cast<unsigned char>(letter))));
    if (index == std::string_view::npos) return std::nullopt;
    int pitch = kPitch[index];

    std::string_view rest = token.substr(1);
    bool accidental = false;
    if (!rest.empty() && rest[0] == '#') {
        pitch += 1;
        accidental = true;
        rest.remove_prefix(1);
    } else if (!rest.empty() && rest[0] == 'b') {
        pitch += 11;
        accidental = true;
        rest.remove_prefix(1);
    }

    Quality quality = Quality::None;
    bool wholeWord = false;
    if (rest == "m") {
        quality = Quality::Minor; // lowercase only: "AM" is not A minor
    } else if (!rest.empty()) {
        const auto word = qualityWord(rest);
        if (!word) return std::nullopt;
        quality = *word;
        wholeWord = true;
    } else if (!nextToken.empty()) {
        if (const auto word = qualityWord(nextToken)) {
            quality = *word;
            wholeWord = true;
        }
    }

    if (!accidental && quality == Quality::None) return std::nullopt; // "Kick_A"
    if (!std::isupper(static_cast<unsigned char>(letter)) && !wholeWord) return std::nullopt; // "am", "eb"

    std::string key = kNames[static_cast<std::size_t>(pitch % 12)];
    if (quality == Quality::Minor) key += 'm';
    return key;
}

NameInfo parseName(std::string_view relPath)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= relPath.size()) {
        const auto slash = relPath.find('/', start);
        const auto end = slash == std::string_view::npos ? relPath.size() : slash;
        if (end > start) parts.push_back(relPath.substr(start, end - start));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    NameInfo info;
    if (parts.empty()) return info;

    const std::string_view file = parts.back();
    const auto dot = file.rfind('.');
    const std::string_view stem = (dot == std::string_view::npos || dot == 0) ? file : file.substr(0, dot);

    std::vector<Group> groups;
    groups.push_back(splitTokens(stem));
    for (auto it = parts.rbegin() + 1; it != parts.rend(); ++it) groups.push_back(splitTokens(*it));

    for (const auto& group : groups)
        for (const auto& token : group) info.tokens.push_back(toLower(token));

    for (const auto& group : groups) {
        if (!info.key) {
            for (std::size_t i = 0; i < group.size() && !info.key; ++i)
                info.key = parseKeyToken(group[i], i + 1 < group.size() ? std::string_view(group[i + 1])
                                                                        : std::string_view());
        }
        if (!info.isLoop) info.isLoop = loopDecision(group);
        if (!info.bpm) info.bpm = bpmFromBpmToken(group);
    }
    if (!info.bpm && info.isLoop == true) info.bpm = trailingTempo(groups.front());
    return info;
}

} // namespace asma
