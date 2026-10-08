// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Backup.h"

#include "asma/core/Json.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace asma {

namespace {

std::string jsonString(std::string_view text) { return "\"" + jsonEscape(text) + "\""; }

// The members of an object as a JSON object, in order.
std::string object(const std::vector<std::pair<std::string, std::string>>& members)
{
    std::string out = "{";
    for (std::size_t i = 0; i < members.size(); ++i)
        out += (i ? "," : "") + jsonString(members[i].first) + ":" + members[i].second;
    return out + "}";
}

std::string array(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out + "]";
}

std::vector<std::string> userTags(Db& db, std::int64_t fileId)
{
    auto q = db.prepare("SELECT t.name FROM file_tags x JOIN tags t ON t.id = x.tag_id "
                        "WHERE x.file_id = ? AND x.source = 'user' ORDER BY t.name");
    q.bind(1, fileId);
    std::vector<std::string> out;
    while (q.step()) out.push_back(q.getText(0));
    return out;
}

// Parses a backup, or throws JsonError when it is not one.
JsonValue parseBackup(std::string_view json)
{
    JsonValue doc = parseJson(json);
    const JsonValue* version = doc.get("asma_backup");
    if (!version || !version->asInt()) throw JsonError("not an asma backup");
    return doc;
}

const JsonValue::Array& arrayOf(const JsonValue& doc, std::string_view key)
{
    static const JsonValue::Array kNone;
    const JsonValue* value = doc.get(key);
    const JsonValue::Array* items = value ? value->asArray() : nullptr;
    return items ? *items : kNone;
}

std::string text(const JsonValue& item, std::string_view key)
{
    const JsonValue* value = item.get(key);
    const std::string* s = value ? value->asString() : nullptr;
    return s ? *s : std::string();
}

// Files with this content.
std::vector<std::int64_t> filesWith(Db& db, const JsonValue& item)
{
    const std::string hash = text(item, "hash");
    const JsonValue* size = item.get("size");
    if (hash.empty() || !size || !size->asInt()) return {};
    auto q = db.prepare("SELECT id FROM files WHERE content_hash = ? AND size = ? ORDER BY id");
    q.bind(1, std::string_view(hash)).bind(2, *size->asInt());
    std::vector<std::int64_t> ids;
    while (q.step()) ids.push_back(q.getInt(0));
    return ids;
}

} // namespace

std::string backupJson(Db& db, std::string_view writtenAt)
{
    Library lib(db);
    UserData user(db);
    std::vector<std::string> folders;
    std::map<std::int64_t, std::string> rootPaths;
    for (const auto& r : lib.roots()) {
        folders.push_back(jsonString(r.path));
        rootPaths[r.id] = r.path;
    }

    std::vector<std::string> files;
    auto organised = db.prepare(
        "SELECT f.id, f.content_hash, f.size, f.root_id, f.rel_path, r.rating, x.file_id IS NOT NULL FROM files f "
        "LEFT JOIN ratings r ON r.file_id = f.id LEFT JOIN favourites x ON x.file_id = f.id "
        "WHERE f.content_hash IS NOT NULL AND f.content_hash != '' AND (r.rating IS NOT NULL OR x.file_id IS NOT NULL "
        "OR EXISTS (SELECT 1 FROM file_tags t WHERE t.file_id = f.id AND t.source = 'user')) ORDER BY f.id");
    while (organised.step()) {
        const std::int64_t id = organised.getInt(0);
        std::vector<std::pair<std::string, std::string>> m{
            {"hash", jsonString(organised.getText(1))},
            {"size", std::to_string(organised.getInt(2))},
            {"path", jsonString(rootPaths[organised.getInt(3)] + "/" + organised.getText(4))}, // for a person reading it
        };
        if (!organised.isNull(5)) m.emplace_back("rating", std::to_string(organised.getInt(5)));
        if (organised.getInt(6)) m.emplace_back("favourite", "true");
        std::vector<std::string> tags;
        for (const auto& t : userTags(db, id)) tags.push_back(jsonString(t));
        if (!tags.empty()) m.emplace_back("tags", array(tags));
        files.push_back(object(m));
    }

    std::vector<std::string> collections;
    std::map<std::int64_t, std::string> collectionNames;
    for (const auto& c : user.collections()) {
        collectionNames[c.id] = c.name;
        auto q = db.prepare("SELECT f.content_hash, f.size FROM collection_items i JOIN files f ON f.id = i.file_id "
                            "WHERE i.collection_id = ? AND f.content_hash IS NOT NULL AND f.content_hash != '' "
                            "ORDER BY f.id");
        q.bind(1, c.id);
        std::vector<std::string> members;
        while (q.step())
            members.push_back(object({{"hash", jsonString(q.getText(0))}, {"size", std::to_string(q.getInt(1))}}));
        collections.push_back(object({{"name", jsonString(c.name)}, {"files", array(members)}}));
    }

    std::vector<std::string> searches;
    for (const auto& s : user.savedSearches()) {
        std::vector<std::pair<std::string, std::string>> m{{"name", jsonString(s.name)},
                                                           {"model", jsonString(searchModelToJson(s.model))}};
        if (s.model.rootId && rootPaths.count(*s.model.rootId)) m.emplace_back("folder", jsonString(rootPaths[*s.model.rootId]));
        if (s.model.collectionId && collectionNames.count(*s.model.collectionId))
            m.emplace_back("collection", jsonString(collectionNames[*s.model.collectionId]));
        searches.push_back(object(m));
    }

    return object({{"asma_backup", "1"},
                   {"written", jsonString(writtenAt)},
                   {"folders", array(folders)},
                   {"files", array(files)},
                   {"collections", array(collections)},
                   {"searches", array(searches)}}) +
           "\n";
}

void writeBackup(Db& db, const fs::path& path, const fs::path& previous)
{
    const std::string json = backupJson(db, utcNow());
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << json;
        out.flush();
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    std::error_code ec;
    if (fs::exists(path, ec)) fs::rename(path, previous); // the day before's
    fs::rename(tmp, path);
}

RestoreStats restoreBackup(Db& db, std::string_view json)
{
    const JsonValue doc = parseBackup(json);
    Library lib(db);
    UserData user(db);
    RestoreStats stats;
    Transaction tx(db);
    for (const auto& item : arrayOf(doc, "files")) {
        const auto ids = filesWith(db, item);
        if (ids.empty()) {
            ++stats.unmatched;
            continue;
        }
        ++stats.files;
        const JsonValue* rating = item.get("rating");
        const JsonValue* favourite = item.get("favourite");
        for (const auto id : ids) {
            if (rating && rating->asInt() && *rating->asInt() >= 1 && *rating->asInt() <= 5)
                user.setRating(id, static_cast<int>(*rating->asInt()));
            if (favourite && favourite->asBool()) user.setFavourite(id, *favourite->asBool());
            for (const auto& tag : arrayOf(item, "tags"))
                if (const std::string* t = tag.asString(); t && !t->empty()) lib.addUserTag(id, *t);
        }
    }
    std::map<std::string, std::int64_t> collectionIds;
    for (const auto& item : arrayOf(doc, "collections")) {
        const std::string name = text(item, "name");
        if (name.empty()) continue;
        const auto existing = user.collectionByName(name);
        const std::int64_t id = existing ? existing->id : user.createCollection(name);
        collectionIds[name] = id;
        ++stats.collections;
        for (const auto& member : arrayOf(item, "files"))
            for (const auto file : filesWith(db, member)) user.addToCollection(id, file);
    }
    for (const auto& item : arrayOf(doc, "searches")) {
        const std::string name = text(item, "name");
        auto model = searchModelFromJson(text(item, "model"));
        if (name.empty() || !model) continue;
        // The scope by path and name: the new library has new ids.
        model->rootId.reset();
        model->collectionId.reset();
        if (const std::string folder = text(item, "folder"); !folder.empty())
            for (const auto& r : lib.roots())
                if (r.path == folder) model->rootId = r.id;
        if (const std::string collection = text(item, "collection"); !collection.empty())
            if (const auto c = user.collectionByName(collection)) model->collectionId = c->id;
        user.saveSearch(name, *model);
        ++stats.searches;
    }
    tx.commit();
    return stats;
}

std::vector<std::string> backupFolders(std::string_view json)
{
    std::vector<std::string> out;
    try {
        const JsonValue doc = parseBackup(json);
        for (const auto& f : arrayOf(doc, "folders"))
            if (const std::string* s = f.asString()) out.push_back(*s);
    } catch (const JsonError&) {
    }
    return out;
}

std::string backupWrittenAt(std::string_view json)
{
    try {
        return text(parseBackup(json), "written");
    } catch (const JsonError&) {
        return {};
    }
}

std::string utcNow()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char out[32];
    std::strftime(out, sizeof out, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return out;
}

} // namespace asma
