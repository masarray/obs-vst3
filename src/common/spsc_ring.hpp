#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace safevst3 {

// Fixed-capacity single-producer/single-consumer ring for realtime control
// transfer. Capacity is the exact usable item count; one extra storage slot is
// reserved internally to distinguish full from empty. push/pop never allocate,
// never lock, and never wait.
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity > 0, "SPSC ring capacity must be non-zero");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Realtime SPSC payloads must be trivially copyable");

public:
    bool push(const T& value) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        storage_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        value = storage_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept
    {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    std::size_t approximate_size() const noexcept
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return head >= tail ? head - tail : kStorageSize - (tail - head);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t kStorageSize = Capacity + 1;

    static constexpr std::size_t increment(std::size_t index) noexcept
    {
        ++index;
        return index == kStorageSize ? 0 : index;
    }

    alignas(64) std::array<T, kStorageSize> storage_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace safevst3
