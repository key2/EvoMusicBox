// SpscRing.h — lock-free single-producer / single-consumer ring buffer.
// Used for the audio thread -> main thread VoiceEnded channel: the producer
// (miniaudio end callback) never blocks or allocates.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace evobox
{

template <typename T, size_t N>
class SpscRing
{
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    // producer side (audio thread). Returns false when full (item dropped).
    bool push(const T& v)
    {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        buf_[head] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // consumer side (main thread)
    bool pop(T& out)
    {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = buf_[tail];
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

private:
    std::array<T, N> buf_{};
    std::atomic<size_t> head_{ 0 };
    std::atomic<size_t> tail_{ 0 };
};

} // namespace evobox
