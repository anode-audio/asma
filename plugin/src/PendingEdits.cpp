// SPDX-License-Identifier: GPL-3.0-only
#include "PendingEdits.h"

#include <algorithm>

namespace asma::app {

std::uint64_t PendingEdits::add(Edit edit)
{
    edit.ticket = next_++;
    edits_.push_back(std::move(edit));
    return edits_.back().ticket;
}

std::uint64_t PendingEdits::setRating(std::int64_t fileId, int rating)
{
    Edit e;
    e.fileId = fileId;
    e.field = Field::Rating;
    e.rating = rating;
    return add(std::move(e));
}

std::uint64_t PendingEdits::setFavourite(std::int64_t fileId, bool favourite)
{
    Edit e;
    e.fileId = fileId;
    e.field = Field::Favourite;
    e.favourite = favourite;
    return add(std::move(e));
}

std::uint64_t PendingEdits::setTags(std::int64_t fileId, std::vector<std::string> tags)
{
    Edit e;
    e.fileId = fileId;
    e.field = Field::Tags;
    std::sort(tags.begin(), tags.end());
    e.tags = std::move(tags);
    return add(std::move(e));
}

void PendingEdits::finished(std::uint64_t ticket, bool ok)
{
    const auto it = std::find_if(edits_.begin(), edits_.end(), [&](const Edit& e) { return e.ticket == ticket; });
    if (it == edits_.end()) return;
    if (ok) it->written = true;
    else edits_.erase(it);
}

void PendingEdits::libraryChanged()
{
    edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [](const Edit& e) { return e.written; }), edits_.end());
}

SearchRow PendingEdits::apply(SearchRow row) const
{
    for (const auto& e : edits_) {
        if (e.fileId != row.id) continue;
        switch (e.field) {
        case Field::Rating: row.rating = e.rating ? std::optional<int>(e.rating) : std::nullopt; break;
        case Field::Favourite: row.favourite = e.favourite; break;
        case Field::Tags: row.tags = e.tags; break;
        }
    }
    return row;
}

} // namespace asma::app
