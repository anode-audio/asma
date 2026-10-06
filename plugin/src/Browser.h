// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <cstdint>
#include <filesystem>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace asma::app {

// The search the UI shows and every row it matches, fetched a page at a time
// as rows are asked for, re-run when the library changes. Message thread only.
class Browser {
public:
    static constexpr int kPage = 500;  // rows fetched at once
    static constexpr int kPagesKept = 8; // pages held; older ones are fetched again

    explicit Browser(LibraryView& library, int pageSize = kPage) : library_(library), pageSize_(pageSize) {}

    // The model's limit and offset are ignored: paging is the browser's.
    void setSearch(SearchModel model);
    const SearchModel& searchModel() const { return model_; }
    // Samples the search matches: the table's rows.
    int count() const { return count_; }
    // Samples in the library, whatever the search: "48 of 585".
    std::int64_t total() const { return total_; }

    // Opens the library if it has appeared and re-runs the search when it
    // changed. Returns whether the rows may have changed.
    bool poll();

    // The row, fetching its page if need be; null out of range. Valid until
    // the next call that fetches.
    const SearchRow* row(int index);
    // Row accessors; out of range gives empty values.
    std::filesystem::path path(int index);
    audio::SampleInfo info(int index);
    std::string contentHash(int index);
    // The row showing this file, or -1, found by one query, not by paging.
    int rowOf(const std::filesystem::path& file);

    LibraryView& library() { return library_; }

private:
    void refetch(); // after the search or the library changed

    LibraryView& library_;
    const int pageSize_;
    SearchModel model_;
    int count_ = 0;
    std::int64_t total_ = 0;
    std::list<std::pair<int, std::vector<SearchRow>>> pages_; // most recent first
};

} // namespace asma::app
