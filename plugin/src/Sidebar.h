// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace asma::app {

enum class EntryKind { All, Favourites, Folder, Collection, SavedSearch };

// One line of the sidebar.
struct SidebarEntry {
    EntryKind kind = EntryKind::All;
    std::int64_t id = 0;     // the folder's, collection's or saved search's
    std::string name;        // UTF-8
    std::int64_t count = -1; // samples it holds in the whole library; -1: none shown
    SearchModel saved;       // a saved search's search
};

// What the sidebar lists: All samples, Favourites, the folders, the
// collections and the saved searches, each kind by name. Counts are of the
// whole library, not of what is being searched.
std::vector<SidebarEntry> sidebarEntries(LibraryView& library);

// The search after picking the entry. A scope (All, Favourites, a folder, a
// collection) replaces the search's scope and keeps its text and chips; a
// saved search replaces the whole search.
SearchModel withEntry(const SidebarEntry& entry, const SearchModel& current);

// The entry the search shows: a saved search it equals, else its scope; -1
// when its scope is not listed (a collection since deleted).
int entryFor(const std::vector<SidebarEntry>& entries, const SearchModel& model);

} // namespace asma::app
