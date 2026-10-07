// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/UserData.h"

#include <string>

namespace asma {

namespace {

std::string trim(std::string_view s)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && isSpace(s[begin])) ++begin;
    while (end > begin && isSpace(s[end - 1])) --end;
    return std::string(s.substr(begin, end - begin));
}

} // namespace

void UserData::requireFile(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT 1 FROM files WHERE id = ?");
    q.bind(1, fileId);
    if (!q.step()) throw UserDataError("no file with id " + std::to_string(fileId));
}

void UserData::requireCollection(std::int64_t id)
{
    auto q = db_.prepare("SELECT 1 FROM collections WHERE id = ?");
    q.bind(1, id);
    if (!q.step()) throw UserDataError("no collection with id " + std::to_string(id));
}

std::string UserData::validName(std::string_view name, const char* what)
{
    std::string trimmed = trim(name);
    if (trimmed.empty()) throw UserDataError(std::string("a ") + what + " needs a name");
    return trimmed;
}

void UserData::setRating(std::int64_t fileId, int rating)
{
    if (rating < 0 || rating > 5) throw UserDataError("a rating is 1 to 5, or 0 to clear it");
    requireFile(fileId);
    if (rating == 0) {
        auto q = db_.prepare("DELETE FROM ratings WHERE file_id = ?");
        q.bind(1, fileId);
        q.run();
        return;
    }
    auto q = db_.prepare("INSERT INTO ratings(file_id, rating) VALUES (?, ?) "
                         "ON CONFLICT(file_id) DO UPDATE SET rating = excluded.rating");
    q.bind(1, fileId).bind(2, rating);
    q.run();
}

std::optional<int> UserData::rating(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT rating FROM ratings WHERE file_id = ?");
    q.bind(1, fileId);
    if (!q.step()) return std::nullopt;
    return static_cast<int>(q.getInt(0));
}

void UserData::setFavourite(std::int64_t fileId, bool favourite)
{
    requireFile(fileId);
    auto q = db_.prepare(favourite ? "INSERT OR IGNORE INTO favourites(file_id) VALUES (?)"
                                   : "DELETE FROM favourites WHERE file_id = ?");
    q.bind(1, fileId);
    q.run();
}

bool UserData::isFavourite(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT 1 FROM favourites WHERE file_id = ?");
    q.bind(1, fileId);
    return q.step();
}

std::int64_t UserData::createCollection(std::string_view name)
{
    const std::string valid = validName(name, "collection");
    if (collectionByName(valid)) throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("INSERT INTO collections(name) VALUES (?)");
    q.bind(1, std::string_view(valid));
    q.run();
    return db_.lastInsertId();
}

void UserData::renameCollection(std::int64_t id, std::string_view name)
{
    requireCollection(id);
    const std::string valid = validName(name, "collection");
    if (const auto existing = collectionByName(valid); existing && existing->id != id)
        throw UserDataError("a collection called '" + valid + "' already exists");
    auto q = db_.prepare("UPDATE collections SET name = ? WHERE id = ?");
    q.bind(1, std::string_view(valid)).bind(2, id);
    q.run();
}

void UserData::deleteCollection(std::int64_t id)
{
    requireCollection(id);
    auto q = db_.prepare("DELETE FROM collections WHERE id = ?");
    q.bind(1, id);
    q.run();
}

std::vector<Collection> UserData::collections()
{
    std::vector<Collection> out;
    auto q = db_.prepare("SELECT c.id, c.name, (SELECT count(*) FROM collection_items i WHERE i.collection_id = c.id) "
                         "FROM collections c ORDER BY c.name COLLATE NOCASE, c.id");
    while (q.step()) out.push_back({q.getInt(0), q.getText(1), q.getInt(2)});
    return out;
}

std::optional<Collection> UserData::collectionByName(std::string_view name)
{
    const std::string trimmed = trim(name);
    auto q = db_.prepare("SELECT c.id, c.name, (SELECT count(*) FROM collection_items i WHERE i.collection_id = c.id) "
                         "FROM collections c WHERE c.name = ?");
    q.bind(1, std::string_view(trimmed));
    if (!q.step()) return std::nullopt;
    return Collection{q.getInt(0), q.getText(1), q.getInt(2)};
}

void UserData::addToCollection(std::int64_t collectionId, std::int64_t fileId)
{
    requireCollection(collectionId);
    requireFile(fileId);
    auto q = db_.prepare("INSERT OR IGNORE INTO collection_items(collection_id, file_id) VALUES (?, ?)");
    q.bind(1, collectionId).bind(2, fileId);
    q.run();
}

void UserData::removeFromCollection(std::int64_t collectionId, std::int64_t fileId)
{
    requireCollection(collectionId);
    auto q = db_.prepare("DELETE FROM collection_items WHERE collection_id = ? AND file_id = ?");
    q.bind(1, collectionId).bind(2, fileId);
    q.run();
}

std::int64_t UserData::saveSearch(std::string_view name, const SearchModel& model)
{
    const std::string valid = validName(name, "saved search");
    const std::string json = searchModelToJson(model);
    auto q = db_.prepare("INSERT INTO saved_searches(name, model) VALUES (?, ?) "
                         "ON CONFLICT(name) DO UPDATE SET model = excluded.model RETURNING id");
    q.bind(1, std::string_view(valid)).bind(2, std::string_view(json));
    q.step();
    return q.getInt(0);
}

std::vector<SavedSearch> UserData::savedSearches()
{
    std::vector<SavedSearch> out;
    auto q = db_.prepare("SELECT id, name, model FROM saved_searches ORDER BY name COLLATE NOCASE, id");
    while (q.step())
        out.push_back({q.getInt(0), q.getText(1), searchModelFromJson(q.getText(2)).value_or(SearchModel{})});
    return out;
}

std::optional<SavedSearch> UserData::savedSearchByName(std::string_view name)
{
    const std::string trimmed = trim(name);
    auto q = db_.prepare("SELECT id, name, model FROM saved_searches WHERE name = ?");
    q.bind(1, std::string_view(trimmed));
    if (!q.step()) return std::nullopt;
    return SavedSearch{q.getInt(0), q.getText(1), searchModelFromJson(q.getText(2)).value_or(SearchModel{})};
}

void UserData::renameSavedSearch(std::int64_t id, std::string_view name)
{
    auto exists = db_.prepare("SELECT 1 FROM saved_searches WHERE id = ?");
    exists.bind(1, id);
    if (!exists.step()) throw UserDataError("no saved search with id " + std::to_string(id));
    const std::string valid = validName(name, "saved search");
    if (const auto existing = savedSearchByName(valid); existing && existing->id != id)
        throw UserDataError("a saved search called '" + valid + "' already exists");
    auto q = db_.prepare("UPDATE saved_searches SET name = ? WHERE id = ?");
    q.bind(1, std::string_view(valid)).bind(2, id);
    q.run();
}

void UserData::deleteSavedSearch(std::int64_t id)
{
    auto exists = db_.prepare("SELECT 1 FROM saved_searches WHERE id = ?");
    exists.bind(1, id);
    if (!exists.step()) throw UserDataError("no saved search with id " + std::to_string(id));
    auto q = db_.prepare("DELETE FROM saved_searches WHERE id = ?");
    q.bind(1, id);
    q.run();
}

} // namespace asma
