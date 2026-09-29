// SPDX-License-Identifier: GPL-3.0-only
// Counts heap allocations made on the current thread while one of these is
// alive (see alloc_counter.cpp, which replaces the global operator new).
#pragma once

#include <cstddef>

namespace asma::test {

class CountAllocations {
public:
    CountAllocations();
    ~CountAllocations();
    CountAllocations(const CountAllocations&) = delete;
    CountAllocations& operator=(const CountAllocations&) = delete;
    std::size_t count() const;
};

} // namespace asma::test
