// SPDX-License-Identifier: GPL-3.0-only
#include "ScanSchedule.h"

#include <algorithm>

namespace asma::app {

void ScanSchedule::setFolders(const std::vector<std::int64_t>& ids, Clock::time_point now)
{
    for (auto it = pollAt_.begin(); it != pollAt_.end();) {
        if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
            queue_.erase(std::remove(queue_.begin(), queue_.end(), it->first), queue_.end());
            it = pollAt_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto id : ids)
        if (pollAt_.emplace(id, now + kPollEvery).second) changed(id);
}

void ScanSchedule::changed(std::int64_t id)
{
    if (!pollAt_.count(id)) return;
    if (std::find(queue_.begin(), queue_.end(), id) == queue_.end()) queue_.push_back(id);
}

std::optional<std::int64_t> ScanSchedule::next(Clock::time_point now)
{
    if (!queue_.empty()) {
        const auto id = queue_.front();
        queue_.pop_front();
        return id;
    }
    for (const auto& [id, at] : pollAt_)
        if (at <= now) return id;
    return std::nullopt;
}

void ScanSchedule::scanned(std::int64_t id, Clock::time_point now)
{
    if (const auto it = pollAt_.find(id); it != pollAt_.end()) it->second = now + kPollEvery;
}

} // namespace asma::app
