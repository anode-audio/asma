// SPDX-License-Identifier: GPL-3.0-only
// Replaces the global operator new and delete for the whole test binary, so
// CountAllocations can see allocations on the audio path. Behaviour is
// otherwise that of malloc and free.
#include "AllocCounter.h"

#include <cstdlib>
#include <new>

namespace {

thread_local bool counting = false;
thread_local std::size_t allocations = 0;

void* allocate(std::size_t size)
{
    if (counting) ++allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace asma::test {

CountAllocations::CountAllocations()
{
    allocations = 0;
    counting = true;
}
CountAllocations::~CountAllocations() { counting = false; }
std::size_t CountAllocations::count() const { return allocations; }

} // namespace asma::test
