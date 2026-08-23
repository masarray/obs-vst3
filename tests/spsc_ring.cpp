#include "common/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

struct Message {
    std::uint32_t id = 0;
    double value = 0.0;
};

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "spsc-ring-test failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using safevst3::SpscRing;

    SpscRing<Message, 4> ring;
    require(ring.empty(), "new ring must be empty");
    require(ring.capacity() == 4, "declared capacity must be exact usable capacity");

    require(ring.push({1, 0.1}), "push 1 failed");
    require(ring.push({2, 0.2}), "push 2 failed");
    require(ring.push({3, 0.3}), "push 3 failed");
    require(ring.push({4, 0.4}), "push 4 failed");
    require(!ring.push({5, 0.5}), "full ring must reject instead of block/overwrite");
    require(ring.approximate_size() == 4, "full ring size mismatch");

    Message message{};
    for (std::uint32_t expected = 1; expected <= 2; ++expected) {
        require(ring.pop(message), "initial pop failed");
        require(message.id == expected, "FIFO order broken before wrap");
    }

    require(ring.push({5, 0.5}), "wrap push 5 failed");
    require(ring.push({6, 0.6}), "wrap push 6 failed");
    for (std::uint32_t expected = 3; expected <= 6; ++expected) {
        require(ring.pop(message), "wrapped pop failed");
        require(message.id == expected, "FIFO order broken after wrap");
    }
    require(!ring.pop(message), "empty ring must reject pop");

    // Exercise the actual SPSC memory-ordering contract with one producer and
    // one consumer running concurrently. The consumer must observe every item
    // exactly once and in sequence even across many wraps.
    SpscRing<Message, 64> concurrent;
    constexpr std::uint32_t kCount = 100000;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        for (std::uint32_t id = 1; id <= kCount; ++id) {
            const Message item{id, static_cast<double>(id) / kCount};
            while (!concurrent.push(item))
                std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        for (std::uint32_t expected = 1; expected <= kCount; ++expected) {
            Message item{};
            while (!concurrent.pop(item))
                std::this_thread::yield();
            if (item.id != expected)
                failed.store(true, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();
    require(!failed.load(std::memory_order_relaxed), "concurrent FIFO sequence corrupted");
    require(concurrent.empty(), "concurrent ring must drain completely");

    std::cout << "bounded SPSC FIFO/wrap/concurrency behavior passed\n";
    return 0;
}
