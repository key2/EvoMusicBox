// FakeScheduler.h — deterministic IOscScheduler for tests: manual clock, groups fire when
// advance() crosses their due time (events are produced exactly like the real scheduler).
#pragma once

#include <algorithm>
#include <map>
#include <vector>
#include "osc/OscScheduler.h"

namespace evobox
{

class FakeScheduler : public IOscScheduler
{
public:
    struct Fired { SchedToken token; OscGroup group; TimePoint at; };

    TimePoint now() const override { return now_; }

    SchedToken schedule(TimePoint when, OscGroup group) override
    {
        SchedToken tok = next_++;
        pending_[tok] = { when, std::move(group) };
        return tok;
    }
    bool cancel(SchedToken token) override
    {
        auto it = pending_.find(token);
        if (it == pending_.end()) return false;
        OscEvent ev;
        ev.kind = OscEvent::Kind::GroupCancelled;
        ev.token = token;
        ev.ownerUid = it->second.group.ownerUid;
        ev.phaseIndex = it->second.group.phaseIndex;
        ev.sessionId = it->second.group.sessionId;
        events_.push_back(ev);
        pending_.erase(it);
        return true;
    }
    bool reschedule(SchedToken token, TimePoint when) override
    {
        auto it = pending_.find(token);
        if (it == pending_.end()) return false;
        it->second.when = when;
        return true;
    }
    bool isPending(SchedToken token) const override { return pending_.count(token) != 0; }
    size_t pendingCount() const override { return pending_.size(); }
    size_t drainEvents(const std::function<void(const OscEvent&)>& fn) override
    {
        auto local = std::move(events_);
        events_.clear();
        for (auto& e : local) fn(e);
        return local.size();
    }

    // ---- test controls
    void advance(Millis dt)
    {
        now_ += dt;
        // fire in due order
        while (true)
        {
            SchedToken best = 0;
            TimePoint bestT;
            for (auto& [tok, e] : pending_)
                if (e.when <= now_ && (best == 0 || e.when < bestT)) { best = tok; bestT = e.when; }
            if (!best) break;
            Entry e = std::move(pending_[best]);
            pending_.erase(best);
            fire(best, e.group);
        }
    }
    TimePoint dueOf(SchedToken tok) const { auto it = pending_.find(tok); return it == pending_.end() ? TimePoint() : it->second.when; }
    const std::vector<Fired>& fired() const { return fired_; }
    std::vector<std::string> sentTexts() const
    {
        std::vector<std::string> out;
        for (auto& f : fired_) for (auto& p : f.group.packets) if (!p.skip) out.push_back(p.text);
        return out;
    }
    void clearFired() { fired_.clear(); }

private:
    struct Entry { TimePoint when; OscGroup group; };
    void fire(SchedToken tok, const OscGroup& g)
    {
        for (auto& p : g.packets)
        {
            OscEvent ev;
            ev.kind = p.skip ? OscEvent::Kind::Error : OscEvent::Kind::Sent;
            ev.token = tok; ev.ownerUid = g.ownerUid; ev.commandUid = p.commandUid; ev.targetUid = p.targetUid;
            ev.phaseIndex = g.phaseIndex; ev.sessionId = g.sessionId; ev.message = p.skip ? p.skipReason : p.text;
            events_.push_back(ev);
        }
        OscEvent done;
        done.kind = OscEvent::Kind::GroupFired;
        done.token = tok; done.ownerUid = g.ownerUid; done.phaseIndex = g.phaseIndex; done.sessionId = g.sessionId;
        events_.push_back(done);
        fired_.push_back({ tok, g, now_ });
    }

    TimePoint now_ = Clock::now();
    SchedToken next_ = 1;
    std::map<SchedToken, Entry> pending_;
    std::vector<OscEvent> events_;
    std::vector<Fired> fired_;
};

} // namespace evobox
