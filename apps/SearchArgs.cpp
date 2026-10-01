// SPDX-License-Identifier: GPL-3.0-only
#include "SearchArgs.h"

#include "asma/core/NameParse.h"
#include "asma/core/UserData.h"

namespace asma::cli {

void rejectLeftovers(const Args& args)
{
    if (!args.rest().empty()) throw UsageError("unexpected argument: " + args.rest().front());
}

double toDouble(const std::string& text, const char* what)
{
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == text.size()) return value;
    } catch (const std::exception&) {
    }
    throw UsageError(std::string("not a number for ") + what + ": " + text);
}

SearchModel modelFromArgs(Args& args, Db& db)
{
    UserData user(db);
    SearchModel m;
    if (const auto saved = args.option("saved")) {
        const auto found = user.savedSearchByName(*saved);
        if (!found) throw UsageError("no saved search called '" + *saved + "'");
        m = found->model;
    }
    if (const auto type = args.option("type")) {
        if (*type == "loop") m.type = SampleType::Loop;
        else if (*type == "oneshot") m.type = SampleType::OneShot;
        else if (*type == "any") m.type = SampleType::Any;
        else throw UsageError("--type must be loop, oneshot or any");
    }
    if (const auto bpm = args.option("bpm")) {
        const auto dash = bpm->find('-');
        if (dash == std::string::npos) {
            const double v = toDouble(*bpm, "--bpm");
            m.bpmMin = v - 0.5;
            m.bpmMax = v + 0.5;
        } else {
            m.bpmMin = toDouble(bpm->substr(0, dash), "--bpm");
            m.bpmMax = toDouble(bpm->substr(dash + 1), "--bpm");
        }
    }
    for (const auto& key : args.options("key")) {
        auto canonical = canonicalKey(key);
        if (!canonical) canonical = parseKeyToken(key);
        if (!canonical) throw UsageError("not a key: " + key + " (try Am, C#m, Eb, F#maj)");
        m.keys.push_back(*canonical);
    }
    for (auto& tag : args.options("tag")) m.tags.push_back(std::move(tag));
    for (auto& format : args.options("format")) m.formats.push_back(std::move(format));
    if (const auto v = args.option("min-duration")) m.durationMin = toDouble(*v, "--min-duration");
    if (const auto v = args.option("max-duration")) m.durationMax = toDouble(*v, "--max-duration");
    if (const auto v = args.option("min-rating")) {
        const double rating = toDouble(*v, "--min-rating");
        if (rating < 1 || rating > 5 || rating != static_cast<int>(rating))
            throw UsageError("--min-rating must be 1 to 5");
        m.minRating = static_cast<int>(rating);
    }
    if (args.flag("favourites")) m.favouritesOnly = true;
    if (const auto name = args.option("collection")) {
        const auto collection = user.collectionByName(*name);
        if (!collection) throw UsageError("no collection called '" + *name + "'");
        m.collectionId = collection->id;
    }
    if (const auto sort = args.option("sort")) {
        if (*sort == "name") m.sort = SortField::Name;
        else if (*sort == "bpm") m.sort = SortField::Bpm;
        else if (*sort == "duration") m.sort = SortField::Duration;
        else if (*sort == "key") m.sort = SortField::Key;
        else if (*sort == "rating") m.sort = SortField::Rating;
        else throw UsageError("--sort must be name, bpm, duration, key or rating");
    }
    if (args.flag("desc")) m.descending = true;
    if (const auto limit = args.option("limit")) m.limit = static_cast<int>(toDouble(*limit, "--limit"));

    std::string words;
    for (const auto& word : args.rest()) {
        if (word.rfind("--", 0) == 0) throw UsageError("unknown option: " + word);
        if (!words.empty()) words += ' ';
        words += word;
    }
    if (!words.empty()) m.text = words;
    return m;
}

} // namespace asma::cli
