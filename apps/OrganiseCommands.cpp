// SPDX-License-Identifier: GPL-3.0-only
#include "OrganiseCommands.h"

#include "CliCommon.h"
#include "SearchArgs.h"

#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Query.h"
#include "asma/core/UserData.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace asma::cli {

namespace {

// The files named by --id options and the remaining positional paths. Read
// the --id values before any positional, or an id could be taken for one.
// Throws UsageError when no file is named, UserDataError for a path that is
// not in the library.
std::vector<std::int64_t> targetFiles(Args& args, Db& db, const std::vector<std::string>& idOptions)
{
    std::vector<std::int64_t> ids;
    for (const auto& id : idOptions) ids.push_back(static_cast<std::int64_t>(toDouble(id, "--id")));
    Library lib(db);
    while (const auto path = args.positional()) {
        const auto file = lib.fileByAbsolutePath(fromUtf8(*path));
        if (!file) throw UserDataError("not in the library: " + *path);
        ids.push_back(file->id);
    }
    rejectLeftovers(args);
    if (ids.empty()) throw UsageError("name at least one file, by path or --id N");
    return ids;
}

// Positional arguments are read in order, so this is the next one.
std::string required(Args& args, const char* what)
{
    const auto value = args.positional();
    if (!value) throw UsageError(std::string("missing ") + what);
    return *value;
}

Collection collectionNamed(UserData& user, const std::string& name)
{
    const auto collection = user.collectionByName(name);
    if (!collection) throw UserDataError("no collection called '" + name + "'");
    return *collection;
}

} // namespace

int cmdRate(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    const std::string value = required(args, "rating (0 to 5)");
    const double rating = toDouble(value, "rating");
    if (rating != static_cast<int>(rating)) throw UsageError("a rating is a whole number, 0 to 5");
    const auto ids = targetFiles(args, db, idOptions);
    UserData user(db);
    Transaction tx(db);
    for (const auto id : ids) user.setRating(id, static_cast<int>(rating));
    tx.commit();
    return kOk;
}

int cmdFav(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    const std::string state = required(args, "on or off");
    if (state != "on" && state != "off") throw UsageError("fav needs 'on' or 'off'");
    const auto ids = targetFiles(args, db, idOptions);
    UserData user(db);
    Transaction tx(db);
    for (const auto id : ids) user.setFavourite(id, state == "on");
    tx.commit();
    return kOk;
}

int cmdTag(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    const std::string sub = required(args, "add or remove");
    if (sub != "add" && sub != "remove") throw UsageError("tag needs 'add' or 'remove'");
    const std::string tag = required(args, "tag");
    const auto ids = targetFiles(args, db, idOptions);
    Library lib(db);
    UserData user(db);
    Transaction tx(db);
    for (const auto id : ids) {
        if (!lib.fileById(id)) throw UserDataError("no file with id " + std::to_string(id));
        if (sub == "add") lib.addUserTag(id, tag);
        else lib.removeUserTag(id, tag);
    }
    tx.commit();
    return kOk;
}

int cmdCollection(Args& args, Db& db)
{
    const auto idOptions = args.options("id");
    UserData user(db);
    const std::string sub = required(args, "collection command");
    if (sub != "add" && sub != "remove" && !idOptions.empty()) throw UsageError("--id only goes with add or remove");
    if (sub == "list") {
        rejectLeftovers(args);
        for (const auto& c : user.collections()) std::cout << c.id << "\t" << c.name << "\t" << c.size << "\n";
        return kOk;
    }
    if (sub == "create") {
        const std::string name = required(args, "collection name");
        rejectLeftovers(args);
        std::cout << user.createCollection(name) << "\n";
        return kOk;
    }
    if (sub == "rename") {
        const std::string from = required(args, "collection name");
        const std::string to = required(args, "new name");
        rejectLeftovers(args);
        user.renameCollection(collectionNamed(user, from).id, to);
        return kOk;
    }
    if (sub == "delete") {
        const std::string name = required(args, "collection name");
        rejectLeftovers(args);
        user.deleteCollection(collectionNamed(user, name).id);
        return kOk;
    }
    if (sub == "add" || sub == "remove") {
        const std::string name = required(args, "collection name");
        const auto collection = collectionNamed(user, name);
        const auto ids = targetFiles(args, db, idOptions);
        Transaction tx(db);
        for (const auto id : ids) {
            if (sub == "add") user.addToCollection(collection.id, id);
            else user.removeFromCollection(collection.id, id);
        }
        tx.commit();
        return kOk;
    }
    throw UsageError("collection needs list, create, rename, delete, add or remove");
}

int cmdSearch(Args& args, Db& db)
{
    UserData user(db);
    const std::string sub = required(args, "search command");
    if (sub == "list") {
        rejectLeftovers(args);
        for (const auto& s : user.savedSearches())
            std::cout << s.name << "\t" << searchModelToJson(s.model) << "\n";
        return kOk;
    }
    if (sub == "save") {
        // The name first: an option value would otherwise pass for it.
        if (args.rest().empty() || args.rest().front().rfind("--", 0) == 0)
            throw UsageError("search save needs the name before any option");
        const std::string name = required(args, "search name");
        const SearchModel model = modelFromArgs(args, db);
        user.saveSearch(name, model);
        return kOk;
    }
    if (sub == "delete") {
        const std::string name = required(args, "search name");
        rejectLeftovers(args);
        const auto saved = user.savedSearchByName(name);
        if (!saved) throw UserDataError("no saved search called '" + name + "'");
        user.deleteSavedSearch(saved->id);
        return kOk;
    }
    throw UsageError("search needs list, save or delete");
}

} // namespace asma::cli
