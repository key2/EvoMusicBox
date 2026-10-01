#include "util/WorkerPool.h"

namespace evobox
{

void WorkerPool::start(int threads)
{
    if (running()) return;
    queue_.reset();
    if (threads < 1) threads = 1;
    for (int i = 0; i < threads; i++)
        threads_.emplace_back([this] { run(); });
}

void WorkerPool::stop()
{
    if (!running()) return;
    // cancel pending jobs so they exit quickly when picked up (they are never picked up
    // after stop() because waitPop returns nullopt once stopped and the queue is drained)
    queue_.drain([](Entry& e) { e.token.cancel(); });
    queue_.stop();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
}

CancelToken WorkerPool::post(Job job)
{
    CancelToken tok;
    post(std::move(job), tok);
    return tok;
}

void WorkerPool::post(Job job, CancelToken token)
{
    queue_.push(Entry{ std::move(job), std::move(token) });
}

void WorkerPool::run()
{
    while (true)
    {
        auto e = queue_.waitPop();
        if (!e) return;
        if (e->token.cancelled()) continue;
        try
        {
            e->job(e->token);
        }
        catch (...)
        {
            // jobs report failures through their own result channel; never let an
            // exception escape a worker thread
        }
    }
}

} // namespace evobox
