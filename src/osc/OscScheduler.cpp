#include "osc/OscScheduler.h"

namespace evobox
{

OscScheduler::OscScheduler() = default;

OscScheduler::~OscScheduler() { stop(); }

void OscScheduler::start()
{
    if (running_.load()) return;
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void OscScheduler::stop()
{
    if (!running_.load()) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_ = false;
    std::lock_guard<std::mutex> lk(m_);
    queue_.clear();
    byToken_.clear();
}

SchedToken OscScheduler::schedule(TimePoint when, OscGroup group)
{
    SchedToken tok = nextToken_++;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = queue_.emplace(when, Entry{ tok, std::move(group) });
        byToken_[tok] = it;
    }
    cv_.notify_all();
    return tok;
}

bool OscScheduler::cancel(SchedToken token)
{
    OscEvent ev;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = byToken_.find(token);
        if (it == byToken_.end()) return false;
        const OscGroup& g = it->second->second.group;
        ev.kind = OscEvent::Kind::GroupCancelled;
        ev.token = token;
        ev.ownerUid = g.ownerUid;
        ev.phaseIndex = g.phaseIndex;
        ev.sessionId = g.sessionId;
        ev.time = nowSeconds();
        queue_.erase(it->second);
        byToken_.erase(it);
    }
    events_.push(std::move(ev));
    cv_.notify_all();
    return true;
}

bool OscScheduler::reschedule(SchedToken token, TimePoint when)
{
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = byToken_.find(token);
        if (it == byToken_.end()) return false;
        Entry e = std::move(it->second->second);
        queue_.erase(it->second);
        auto nit = queue_.emplace(when, std::move(e));
        it->second = nit;
    }
    cv_.notify_all();
    return true;
}

bool OscScheduler::isPending(SchedToken token) const
{
    std::lock_guard<std::mutex> lk(m_);
    return byToken_.count(token) != 0;
}

size_t OscScheduler::pendingCount() const
{
    std::lock_guard<std::mutex> lk(m_);
    return queue_.size();
}

size_t OscScheduler::drainEvents(const std::function<void(const OscEvent&)>& fn)
{
    return events_.drain([&](OscEvent& e) { fn(e); });
}

void OscScheduler::fireNow(const OscGroup& g, SchedToken token)
{
    double t = nowSeconds();
    for (const OscPacket& p : g.packets)
    {
        OscEvent ev;
        ev.token = token;
        ev.ownerUid = g.ownerUid;
        ev.commandUid = p.commandUid;
        ev.targetUid = p.targetUid;
        ev.phaseIndex = g.phaseIndex;
        ev.sessionId = g.sessionId;
        ev.time = t;
        if (p.skip)
        {
            ev.kind = OscEvent::Kind::Error;
            ev.message = p.skipReason.empty() ? "invalid command" : p.skipReason;
        }
        else
        {
            std::string err;
            if (sender_.send(p.endpoint, p.bytes, &err))
            {
                ev.kind = OscEvent::Kind::Sent;
                ev.message = p.text + "  -> " + p.endpoint.label();
            }
            else
            {
                ev.kind = OscEvent::Kind::Error;
                ev.message = p.text + "  -> " + p.endpoint.label() + ": " + err;
            }
        }
        events_.push(std::move(ev));
    }
    OscEvent done;
    done.kind = OscEvent::Kind::GroupFired;
    done.token = token;
    done.ownerUid = g.ownerUid;
    done.phaseIndex = g.phaseIndex;
    done.sessionId = g.sessionId;
    done.message = g.label;
    done.time = t;
    events_.push(std::move(done));
}

void OscScheduler::run()
{
    std::unique_lock<std::mutex> lk(m_);
    while (!stopRequested_)
    {
        if (queue_.empty())
        {
            cv_.wait(lk, [&] { return stopRequested_ || !queue_.empty(); });
            continue;
        }
        auto first = queue_.begin();
        TimePoint due = first->first;
        TimePoint now = Clock::now();
        if (due > now)
        {
            cv_.wait_until(lk, due);
            continue; // re-evaluate: the queue may have changed
        }
        Entry e = std::move(first->second);
        byToken_.erase(e.token);
        queue_.erase(first);
        lk.unlock();
        fireNow(e.group, e.token);
        lk.lock();
    }
}

} // namespace evobox
