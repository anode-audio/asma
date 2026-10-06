// SPDX-License-Identifier: GPL-3.0-only
#include "Sidebar.h"

#include "asma/core/Query.h"

namespace asma::app {

namespace {

SearchModel scopeOnly(const SearchModel& from)
{
    SearchModel m = from;
    m.rootId.reset();
    m.collectionId.reset();
    m.favouritesOnly = false;
    return m;
}

std::string folderName(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return name.empty() ? path : name;
}

} // namespace

std::vector<SidebarEntry> sidebarEntries(LibraryView& library)
{
    std::vector<SidebarEntry> out;
    const SearchModel all;
    out.push_back({EntryKind::All, 0, "All samples", library.matchCount(all), {}});
    SearchModel favourites;
    favourites.favouritesOnly = true;
    out.push_back({EntryKind::Favourites, 0, "Favourites", library.matchCount(favourites), {}});
    for (const auto& root : library.roots()) {
        if (!root.enabled) continue;
        SearchModel in;
        in.rootId = root.id;
        out.push_back({EntryKind::Folder, root.id, folderName(root.path), library.matchCount(in), {}});
    }
    for (const auto& c : library.collections()) {
        SearchModel in;
        in.collectionId = c.id;
        out.push_back({EntryKind::Collection, c.id, c.name, library.matchCount(in), {}});
    }
    for (const auto& s : library.savedSearches()) out.push_back({EntryKind::SavedSearch, s.id, s.name, -1, s.model});
    return out;
}

SearchModel withEntry(const SidebarEntry& entry, const SearchModel& current)
{
    SearchModel m = scopeOnly(current);
    switch (entry.kind) {
    case EntryKind::All: break;
    case EntryKind::Favourites: m.favouritesOnly = true; break;
    case EntryKind::Folder: m.rootId = entry.id; break;
    case EntryKind::Collection: m.collectionId = entry.id; break;
    case EntryKind::SavedSearch: m = entry.saved; break;
    }
    return m;
}

int entryFor(const std::vector<SidebarEntry>& entries, const SearchModel& model)
{
    // The saved search's own form, as it is stored: paging is not part of it.
    const std::string json = searchModelToJson(model);
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].kind == EntryKind::SavedSearch && searchModelToJson(entries[i].saved) == json)
            return static_cast<int>(i);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const bool lit = (model.favouritesOnly && e.kind == EntryKind::Favourites)
                      || (!model.favouritesOnly && model.rootId && e.kind == EntryKind::Folder && e.id == *model.rootId)
                      || (!model.favouritesOnly && model.collectionId && e.kind == EntryKind::Collection
                          && e.id == *model.collectionId)
                      || (!model.favouritesOnly && !model.rootId && !model.collectionId && e.kind == EntryKind::All);
        if (lit) return static_cast<int>(i);
    }
    return -1;
}

} // namespace asma::app
