#include <catch2/catch_test_macros.hpp>
#include "util/RingBuffer.h"
#include <thread>
#include <atomic>
#include <cstdint>

// ── Basic single-threaded ops ─────────────────────────────────────────────────

TEST_CASE("RingBuffer: initial state is empty", "[ringbuffer]") {
    RingBuffer<int, 8> rb;
    REQUIRE(rb.empty());
    REQUIRE_FALSE(rb.full());
    REQUIRE(rb.size() == 0);
}

TEST_CASE("RingBuffer: capacity is N-1", "[ringbuffer]") {
    RingBuffer<int, 8> rb;
    REQUIRE(rb.CAPACITY == 7);
}

TEST_CASE("RingBuffer: push and pop single item", "[ringbuffer]") {
    RingBuffer<int, 4> rb;
    REQUIRE(rb.push(42));
    REQUIRE(rb.size() == 1);
    REQUIRE_FALSE(rb.empty());

    int val = 0;
    REQUIRE(rb.pop(val));
    REQUIRE(val == 42);
    REQUIRE(rb.empty());
    REQUIRE(rb.size() == 0);
}

TEST_CASE("RingBuffer: fill to capacity then reject", "[ringbuffer]") {
    RingBuffer<int, 4> rb; // capacity = 3
    REQUIRE(rb.push(1));
    REQUIRE(rb.push(2));
    REQUIRE(rb.push(3));
    REQUIRE(rb.full());
    REQUIRE(rb.size() == 3);

    // One more push should fail
    REQUIRE_FALSE(rb.push(4));
    REQUIRE(rb.size() == 3);
}

TEST_CASE("RingBuffer: pop from empty returns false", "[ringbuffer]") {
    RingBuffer<int, 4> rb;
    int val = 0;
    REQUIRE_FALSE(rb.pop(val));
}

TEST_CASE("RingBuffer: FIFO ordering preserved", "[ringbuffer]") {
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 7; ++i) REQUIRE(rb.push(i));

    for (int i = 0; i < 7; ++i) {
        int val = -1;
        REQUIRE(rb.pop(val));
        REQUIRE(val == i);
    }
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer: peek does not remove item", "[ringbuffer]") {
    RingBuffer<int, 4> rb;
    REQUIRE(rb.push(99));

    int val = 0;
    REQUIRE(rb.peek(val));
    REQUIRE(val == 99);
    REQUIRE(rb.size() == 1); // still there

    REQUIRE(rb.pop(val));
    REQUIRE(val == 99);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer: peek on empty returns false", "[ringbuffer]") {
    RingBuffer<int, 4> rb;
    int val = 0;
    REQUIRE_FALSE(rb.peek(val));
}

TEST_CASE("RingBuffer: clear resets state", "[ringbuffer]") {
    RingBuffer<int, 4> rb;
    rb.push(1); rb.push(2); rb.push(3);
    REQUIRE(rb.full());

    rb.clear();
    REQUIRE(rb.empty());
    REQUIRE(rb.size() == 0);

    // Can push again after clear
    REQUIRE(rb.push(10));
    int val = 0;
    REQUIRE(rb.pop(val));
    REQUIRE(val == 10);
}

TEST_CASE("RingBuffer: wrap-around works correctly", "[ringbuffer]") {
    RingBuffer<int, 4> rb; // capacity = 3

    // Push 3, pop 2, push 2 (exercises wrap)
    REQUIRE(rb.push(1));
    REQUIRE(rb.push(2));
    REQUIRE(rb.push(3));

    int val;
    REQUIRE(rb.pop(val)); REQUIRE(val == 1);
    REQUIRE(rb.pop(val)); REQUIRE(val == 2);

    REQUIRE(rb.push(4));
    REQUIRE(rb.push(5));

    REQUIRE(rb.pop(val)); REQUIRE(val == 3);
    REQUIRE(rb.pop(val)); REQUIRE(val == 4);
    REQUIRE(rb.pop(val)); REQUIRE(val == 5);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer: move push", "[ringbuffer]") {
    RingBuffer<std::vector<int>, 4> rb;
    std::vector<int> v = {1, 2, 3};
    REQUIRE(rb.push(std::move(v)));
    REQUIRE(rb.size() == 1);

    std::vector<int> out;
    REQUIRE(rb.pop(out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == 1);
}

// ── SPSC thread-safety test ───────────────────────────────────────────────────

TEST_CASE("RingBuffer: SPSC producer/consumer", "[ringbuffer][threading]") {
    RingBuffer<uint32_t, 1024> rb;
    std::atomic<bool> done{false};
    constexpr uint32_t COUNT = 50000;
    uint32_t sum_sent = 0;
    uint32_t sum_recv = 0;

    std::thread producer([&]() {
        for (uint32_t i = 0; i < COUNT; ++i) {
            while (!rb.push(i)) { /* spin */ }
            sum_sent += i;
        }
    });

    std::thread consumer([&]() {
        uint32_t received = 0;
        while (received < COUNT) {
            uint32_t val;
            if (rb.pop(val)) {
                sum_recv += val;
                ++received;
            }
        }
        done.store(true);
    });

    producer.join();
    consumer.join();
    REQUIRE(done.load());
    REQUIRE(sum_sent == sum_recv);
}
