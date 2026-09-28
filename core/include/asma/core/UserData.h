// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"
#include "asma/core/Query.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

class UserDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct SavedSearch {
    std::int64_t id = 0;
    std::string name;
    SearchModel model; // the default model when the stored text is unreadable
};

struct Collection {
    std::int64_t id = 0;
    std::string name;
    std::int64_t size = 0; // files in the collection, any status
};

// What the user adds to a library: ratings, favourites, collections and saved
// searches (user tags live in Library). Rows hang off file ids, so they survive a file going
// missing and follow it when the scanner re-links it. Each method is one short
// write; callers group writes in a Transaction when they need several at once.
// Throws UserDataError for an unknown file or collection, or a bad value.
class UserData {
public:
    explicit UserData(Db& db) : db_(db) {}

    // 1 to 5; 0 clears the rating.
    void setRating(std::int64_t fileId, int rating);
    std::optional<int> rating(std::int64_t fileId);

    void setFavourite(std::int64_t fileId, bool favourite);
    bool isFavourite(std::int64_t fileId);

    // Collection and saved-search names are trimmed, non-empty and unique
    // ignoring case.
    std::int64_t createCollection(std::string_view name);
    void renameCollection(std::int64_t id, std::string_view name);
    void deleteCollection(std::int64_t id); // the files stay in the library
    std::vector<Collection> collections();  // by name
    std::optional<Collection> collectionByName(std::string_view name);
    // Adding a file that is already there is not an error.
    void addToCollection(std::int64_t collectionId, std::int64_t fileId);
    void removeFromCollection(std::int64_t collectionId, std::int64_t fileId);

    // Saving under an existing name (ignoring case) replaces that search.
    std::int64_t saveSearch(std::string_view name, const SearchModel& model);
    std::vector<SavedSearch> savedSearches(); // by name
    std::optional<SavedSearch> savedSearchByName(std::string_view name);
    void deleteSavedSearch(std::int64_t id);

private:
    void requireFile(std::int64_t fileId);
    void requireCollection(std::int64_t id);
    static std::string validName(std::string_view name, const char* what);

    Db& db_;
};

} // namespace asma
