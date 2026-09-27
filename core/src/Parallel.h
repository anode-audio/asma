// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace asma::detail {

// 0 means one thread per hardware thread.
inline unsigned threadCount(unsigned requested)
{
    return requested ? requested : std::max(1u, std::thread::hardware_concurrency());
}

// Runs work(i) for every i in [begin, end) on up to `threads` threads, the
// calling thread included, and returns when all are done. work must not throw.
template <typename Work>
void parallelFor(std::size_t begin, std::size_t end, unsigned threads, Work work)
{
    if (begin >= end) return;
    std::atomic<std::size_t> next{begin};
    auto worker = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= end) return;
            work(i);
        }
    };
    const auto poolSize = static_cast<unsigned>(std::min<std::size_t>(std::max(1u, threads), end - begin));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < poolSize; ++t) pool.emplace_back(worker);
    worker();
    for (auto& thread : pool) thread.join();
}

} // namespace asma::detail
