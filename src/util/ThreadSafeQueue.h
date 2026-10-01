// ThreadSafeQueue.h — mutex + deque + condition_variable channel used between
// services (worker threads) and the main thread. Low-rate channels only; the
// audio -> main channel uses SpscRing instead.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace evobox
{

template <typename T>
class ThreadSafeQueue
{
public:
    void push(T v)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(v));
        }
        cv_.notify_one();
    }

    template <typename... Args>
    void emplace(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.emplace_back(std::forward<Args>(args)...);
        }
        cv_.notify_one();
    }

    std::optional<T> tryPop()
    {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop_front();
        return v;
    }

    // Blocks until an item is available or stop() was called. Returns nullopt on stop.
    std::optional<T> waitPop()
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return stopped_ || !q_.empty(); });
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop_front();
        return v;
    }

    // Main-thread helper: pops everything currently queued and hands it to fn.
    template <typename F>
    size_t drain(F&& fn)
    {
        std::deque<T> local;
        {
            std::lock_guard<std::mutex> lk(m_);
            local.swap(q_);
        }
        for (auto& v : local) fn(v);
        return local.size();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    void reset()
    {
        std::lock_guard<std::mutex> lk(m_);
        stopped_ = false;
    }

    bool stopped() const
    {
        std::lock_guard<std::mutex> lk(m_);
        return stopped_;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

    bool empty() const { return size() == 0; }

    void clear()
    {
        std::lock_guard<std::mutex> lk(m_);
        q_.clear();
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
    bool stopped_ = false;
};

} // namespace evobox
