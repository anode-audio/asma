// SPDX-License-Identifier: GPL-3.0-only
#include "Browser.h"

#include <climits>

namespace asma::app {

void Browser::setSearch(SearchModel model)
{
    model_ = std::move(model);
    refetch();
}

bool Browser::poll()
{
    library_.refresh();
    if (!library_.changed()) return false;
    refetch();
    return true;
}

void Browser::refetch()
{
    pages_.clear();
    total_ = library_.sampleCount();
    count_ = static_cast<int>(std::min<std::int64_t>(library_.matchCount(model_), INT_MAX));
}

const SearchRow* Browser::row(int index)
{
    if (index < 0 || index >= count_) return nullptr;
    const int page = index / pageSize_;
    auto it = pages_.begin();
    while (it != pages_.end() && it->first != page) ++it;
    if (it == pages_.end()) {
        SearchModel m = model_;
        m.limit = pageSize_;
        m.offset = page * pageSize_;
        pages_.emplace_front(page, library_.search(m));
        if (static_cast<int>(pages_.size()) > kPagesKept) pages_.pop_back();
        it = pages_.begin();
    } else if (it != pages_.begin()) {
        pages_.splice(pages_.begin(), pages_, it); // most recently used first
        it = pages_.begin();
    }
    const auto at = static_cast<std::size_t>(index - page * pageSize_);
    return at < it->second.size() ? &it->second[at] : nullptr; // the library shrank since the count
}

std::filesystem::path Browser::path(int index)
{
    const SearchRow* r = row(index);
    return r ? LibraryView::pathOf(*r) : std::filesystem::path();
}

audio::SampleInfo Browser::info(int index)
{
    const SearchRow* r = row(index);
    return r ? library_.info(r->id) : audio::SampleInfo{};
}

std::string Browser::contentHash(int index)
{
    const SearchRow* r = row(index);
    return r ? library_.contentHash(r->id) : std::string();
}

int Browser::rowOf(const std::filesystem::path& file)
{
    if (file.empty()) return -1; // nothing selected
    const auto id = library_.fileId(file);
    if (!id) return -1;
    const auto position = library_.position(model_, *id);
    return position && *position < count_ ? static_cast<int>(*position) : -1;
}

} // namespace asma::app
