#pragma once
#include <atomic>
#include <cstddef>

// Lock-free single-producer / single-consumer ring buffer.
// N must be a power of 2. Effective capacity is N-1 (one slot wasted).
// Safe without mutexes when push() is called from one thread and pop() from another.
template<typename T, size_t N>
class RingBuffer {
    static_assert(N >= 2,           "N must be at least 2");
    static_assert((N & (N-1)) == 0, "N must be a power of 2");

    static constexpr size_t MASK = N - 1;

    T m_data[N];

    // Separate cache lines to avoid false sharing between producer and consumer.
    alignas(64) std::atomic<size_t> m_head{0}; // next write slot (producer)
    alignas(64) std::atomic<size_t> m_tail{0}; // next read  slot (consumer)

public:
    static constexpr size_t CAPACITY = N - 1;

    // Push by const-ref. Returns false if full.
    bool push(const T& item) {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & MASK;
        if (next == m_tail.load(std::memory_order_acquire)) return false; // full
        m_data[head] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    // Push by move. Returns false if full.
    bool push(T&& item) {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & MASK;
        if (next == m_tail.load(std::memory_order_acquire)) return false;
        m_data[head] = std::move(item);
        m_head.store(next, std::memory_order_release);
        return true;
    }

    // Pop into item. Returns false if empty.
    bool pop(T& item) {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false; // empty
        item = std::move(m_data[tail]);
        m_tail.store((tail + 1) & MASK, std::memory_order_release);
        return true;
    }

    // Peek at the next item without removing it. Returns false if empty.
    bool peek(T& item) const {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false;
        item = m_data[tail];
        return true;
    }

    bool empty() const {
        return m_head.load(std::memory_order_acquire) ==
               m_tail.load(std::memory_order_acquire);
    }

    bool full() const {
        const size_t head = m_head.load(std::memory_order_acquire);
        return ((head + 1) & MASK) == m_tail.load(std::memory_order_acquire);
    }

    size_t size() const {
        const size_t h = m_head.load(std::memory_order_acquire);
        const size_t t = m_tail.load(std::memory_order_acquire);
        return (h - t + N) & MASK;
    }

    // Clear all elements. NOT thread-safe; call only when no concurrent push/pop.
    void clear() {
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }
};
