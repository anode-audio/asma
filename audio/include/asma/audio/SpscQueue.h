// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace asma::audio {

// Fixed-capacity queue for one producer thread and one consumer thread.
// push and pop never block or allocate, so either side can be the audio
// thread.
template <typename T, std::size_t Capacity>
class SpscQueue {
public:
    // Producer. False when full.
    bool push(const T& value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) % kSlots;
        if (next == head_.load(std::memory_order_acquire)) return false;
        slots_[tail] = value;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer. False when empty.
    bool pop(T& out)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        out = slots_[head];
        head_.store((head + 1) % kSlots, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kSlots = Capacity + 1; // one slot tells full from empty
    std::array<T, kSlots> slots_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

} // namespace asma::audio
