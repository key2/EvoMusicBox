// WorkerPool.h — small fixed pool of worker threads executing cancellable jobs.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include "util/ThreadSafeQueue.h"

namespace evobox
{

// Shared cancellation flag handed to a job. Copyable; all copies share state.
class CancelToken
{
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() { flag_->store(true); }
    bool cancelled() const { return flag_->load(); }
    bool operator==(const CancelToken& o) const { return flag_ == o.flag_; }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

class WorkerPool
{
public:
    using Job = std::function<void(const CancelToken&)>;

    WorkerPool() = default;
    explicit WorkerPool(int threads) { start(threads); }
    ~WorkerPool() { stop(); }

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void start(int threads);
    void stop();               // cancels every pending job and joins the threads
    bool running() const { return !threads_.empty(); }

    // Enqueue a job; the returned token cancels it (pending or running, cooperative).
    CancelToken post(Job job);
    void post(Job job, CancelToken token);

    size_t pending() const { return queue_.size(); }

private:
    struct Entry
    {
        Job job;
        CancelToken token;
    };
    void run();

    ThreadSafeQueue<Entry> queue_;
    std::vector<std::thread> threads_;
};

} // namespace evobox
