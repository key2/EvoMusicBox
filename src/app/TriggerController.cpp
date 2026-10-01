#include "app/TriggerController.h"
#include <algorithm>

namespace evobox
{

const char* triggerSourceName(TriggerSource s)
{
    switch (s)
    {
    case TriggerSource::Tile:     return "tile";
    case TriggerSource::Hotkey:   return "hotkey";
    case TriggerSource::Shortcut: return "shortcut";
    case TriggerSource::Live:     return "live";
    case TriggerSource::Simulate: return "simulate";
    case TriggerSource::Test:     return "test";
    default:                      return "other";
    }
}

TriggerController::TriggerController(Project& project, OscService& osc)
    : project_(project), osc_(osc)
{
}

Triggerable* TriggerController::findTriggerable(Uid uid) const
{
    auto it = extra_.find(uid);
    if (it != extra_.end()) return it->second;
    return project_.findTriggerable(uid);
}

void TriggerController::registerExtra(Triggerable* t)
{
    if (t) extra_[t->triggerableUid()] = t;
}

void TriggerController::unregisterExtra(Triggerable* t)
{
    if (!t) return;
    for (auto it = extra_.begin(); it != extra_.end();)
        if (it->second == t) it = extra_.erase(it); else ++it;
}

OscGroup TriggerController::buildGroup(Triggerable& t, OscPhase& phase, int phaseIndex, SessionId sid, bool onlyEnabled)
{
    OscGroup g;
    g.ownerUid = t.triggerableUid();
    g.phaseIndex = phaseIndex;
    g.sessionId = sid;
    g.label = t.displayName() + " / " + phase.niceName;
    for (OscCommand* c : phase.commands.commands())
    {
        if (onlyEnabled && !c->enabled()) continue;
        OscTarget* target = project_.oscTargets.resolve(c->targetUid);
        OscEndpoint ep;
        Uid targetUid = 0;
        if (target)
        {
            targetUid = target->uid;
            ep = osc_.endpoint(target->uid, target->endpointRevision, target->host(), target->port());
        }
        else
        {
            ep.error = "no OSC target";
        }
        g.packets.push_back(osc_.makePacket(c->text(), c->uid, targetUid, ep));
    }
    return g;
}

void TriggerController::scheduleGroup(TriggerSession& s, Triggerable& t, OscPhase& phase, int phaseIndex, TimePoint when)
{
    OscGroup g = buildGroup(t, phase, phaseIndex, s.id);
    // Timer phases are always scheduled (the runtime "active" state follows them); other phases
    // only when they have something to send.
    if (g.packets.empty() && phase.anchor != Anchor::Timer) return;
    SchedToken tok = osc_.scheduler().schedule(when, std::move(g));
    s.pending.push_back({ tok, phaseIndex, phase.anchor, when });
    tokenToSession_[tok] = s.id;
}

void TriggerController::setActiveFlag(Triggerable& t, bool active, SessionId sid)
{
    if (auto* g = dynamic_cast<GiftAction*>(&t)) { g->rt.active = active; g->rt.sessionId = active ? sid : 0; }
    else if (auto* r = dynamic_cast<RoomEventAction*>(&t)) { r->rt.active = active; r->rt.sessionId = active ? sid : 0; }
}

SessionId TriggerController::begin(Triggerable& t, TriggerSource src)
{
    TriggerSession s;
    s.id = nextSession_++;
    s.triggerableUid = t.triggerableUid();
    s.kind = t.triggerableKind();
    s.source = src;
    s.t0 = osc_.scheduler().now();
    auto& phases = t.actions().phases;
    for (size_t i = 0; i < phases.size(); i++)
    {
        OscPhase& p = *phases[i];
        if (p.anchor == Anchor::End) continue;
        int delay = std::max(0, p.delayMs()); // negative delays: later option (UI.md §17)
        scheduleGroup(s, t, p, (int)i, s.t0 + Millis(delay));
    }
    bool hasTimer = s.timerCount() > 0;
    if (hasTimer) setActiveFlag(t, true, s.id);
    // sessions without any pending group and without End phases can be closed right away
    bool hasEnd = t.actions().phase(Anchor::End) != nullptr;
    if (!hasTimer && !hasEnd && s.pending.empty()) { return s.id; }
    if (!hasEnd) s.ended = s.endFired = true; // nothing waits for end()
    sessions_[s.id] = std::move(s);
    return s.id;
}

void TriggerController::end(SessionId id, SessionEndReason reason, bool fireEnd)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    TriggerSession& s = it->second;
    if (s.ended) return;
    s.ended = true;
    Triggerable* t = findTriggerable(s.triggerableUid);
    if (t && fireEnd && reason != SessionEndReason::Cancelled)
    {
        TimePoint tEnd = osc_.scheduler().now();
        auto& phases = t->actions().phases;
        for (size_t i = 0; i < phases.size(); i++)
        {
            OscPhase& p = *phases[i];
            if (p.anchor != Anchor::End) continue;
            scheduleGroup(s, *t, p, (int)i, tEnd + Millis(std::max(0, p.delayMs())));
        }
    }
    s.endFired = true;
    pruneSessions();
}

void TriggerController::cancelTimers(SessionId id)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    TriggerSession& s = it->second;
    for (auto& p : s.pending)
        if (p.anchor == Anchor::Timer) { osc_.scheduler().cancel(p.token); tokenToSession_.erase(p.token); }
    s.pending.erase(std::remove_if(s.pending.begin(), s.pending.end(),
                                   [](const TriggerSession::PendingGroup& p) { return p.anchor == Anchor::Timer; }),
                    s.pending.end());
    if (Triggerable* t = findTriggerable(s.triggerableUid))
    {
        setActiveFlag(*t, false, 0);
        if (onTimersDone) onTimersDone(*t, id);
    }
    pruneSessions();
}

void TriggerController::restartTimers(SessionId id)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    TriggerSession& s = it->second;
    Triggerable* t = findTriggerable(s.triggerableUid);
    if (!t) return;
    TimePoint now = osc_.scheduler().now();
    s.t0 = now;
    for (auto& p : s.pending)
    {
        if (p.anchor != Anchor::Timer) continue;
        OscPhase* phase = t->actions().phaseAt((size_t)p.phaseIndex);
        int delay = phase ? std::max(0, phase->delayMs()) : 0;
        p.due = now + Millis(delay);
        osc_.scheduler().reschedule(p.token, p.due);
    }
}

void TriggerController::retrigger(SessionId id)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    TriggerSession& s = it->second;
    Triggerable* t = findTriggerable(s.triggerableUid);
    if (!t) return;
    restartTimers(id);
    auto& phases = t->actions().phases;
    for (size_t i = 0; i < phases.size(); i++)
    {
        OscPhase& p = *phases[i];
        if (p.anchor != Anchor::Start) continue;
        scheduleGroup(s, *t, p, (int)i, s.t0 + Millis(std::max(0, p.delayMs())));
    }
}

void TriggerController::cancelAll()
{
    for (auto& [id, s] : sessions_)
    {
        for (auto& p : s.pending) { osc_.scheduler().cancel(p.token); }
        s.pending.clear();
        if (Triggerable* t = findTriggerable(s.triggerableUid)) setActiveFlag(*t, false, 0);
    }
    tokenToSession_.clear();
    sessions_.clear();
}

bool TriggerController::isActive(const Triggerable& t) const { return activeSession(t) != 0; }

SessionId TriggerController::activeSession(const Triggerable& t) const
{
    Uid uid = t.triggerableUid();
    for (auto& [id, s] : sessions_)
        if (s.triggerableUid == uid && s.timerCount() > 0) return id;
    return 0;
}

const TriggerSession* TriggerController::session(SessionId id) const
{
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

double TriggerController::timerRemaining(const Triggerable& t) const
{
    SessionId id = activeSession(t);
    if (!id) return -1;
    const TriggerSession& s = sessions_.at(id);
    TimePoint now = osc_.scheduler().now();
    double best = -1;
    for (auto& p : s.pending)
        if (p.anchor == Anchor::Timer)
        {
            double rem = std::chrono::duration<double>(p.due - now).count();
            if (best < 0 || rem < best) best = std::max(0.0, rem);
        }
    return best;
}

void TriggerController::testCommand(Triggerable& owner, OscPhase& phase, OscCommand& c)
{
    OscGroup g;
    g.ownerUid = owner.triggerableUid();
    g.phaseIndex = owner.actions().indexOf(&phase);
    g.label = owner.displayName() + " / test";
    OscTarget* target = project_.oscTargets.resolve(c.targetUid);
    OscEndpoint ep;
    Uid tu = 0;
    if (target) { tu = target->uid; ep = osc_.endpoint(target->uid, target->endpointRevision, target->host(), target->port()); }
    else ep.error = "no OSC target";
    g.packets.push_back(osc_.makePacket(c.text(), c.uid, tu, ep));
    osc_.sendNow(std::move(g));
}

void TriggerController::testPhase(Triggerable& owner, OscPhase& phase)
{
    OscGroup g = buildGroup(owner, phase, owner.actions().indexOf(&phase), 0);
    if (g.packets.empty()) return;
    osc_.sendNow(std::move(g));
}

void TriggerController::applyPulse(Uid ownerUid, Uid commandUid, double now, bool error, const std::string& msg)
{
    Triggerable* t = findTriggerable(ownerUid);
    if (!t) return;
    if (auto* s = dynamic_cast<Sound*>(t)) s->rt.lastOscPulseTime = now;
    else if (auto* g = dynamic_cast<GiftAction*>(t)) g->rt.lastOscPulseTime = now;
    else if (auto* r = dynamic_cast<RoomEventAction*>(t)) r->rt.lastOscPulseTime = now;
    if (commandUid)
    {
        for (auto& phase : t->actions().phases)
            if (OscCommand* c = phase->commands.find(commandUid))
            {
                if (error) { c->rt.lastError = msg; c->rt.lastErrorTime = now; }
                else { c->rt.lastSentTime = now; c->rt.lastError.clear(); }
                break;
            }
    }
}

void TriggerController::handleEvent(const OscEvent& e, double now)
{
    switch (e.kind)
    {
    case OscEvent::Kind::Sent:
        sentCount++;
        applyPulse(e.ownerUid, e.commandUid, now, false, e.message);
        OLOG("OSC", e.message);
        break;
    case OscEvent::Kind::Error:
        errorCount++;
        applyPulse(e.ownerUid, e.commandUid, now, true, e.message);
        OLOGW("OSC", e.message);
        break;
    case OscEvent::Kind::GroupFired:
    case OscEvent::Kind::GroupCancelled:
    {
        auto tit = tokenToSession_.find(e.token);
        if (tit == tokenToSession_.end()) break;
        SessionId sid = tit->second;
        tokenToSession_.erase(tit);
        auto sit = sessions_.find(sid);
        if (sit == sessions_.end()) break;
        TriggerSession& s = sit->second;
        bool wasTimer = false;
        for (auto it = s.pending.begin(); it != s.pending.end(); ++it)
            if (it->token == e.token) { wasTimer = it->anchor == Anchor::Timer; s.pending.erase(it); break; }
        if (wasTimer && s.timerCount() == 0)
        {
            if (Triggerable* t = findTriggerable(s.triggerableUid))
            {
                setActiveFlag(*t, false, 0);
                if (onTimersDone) onTimersDone(*t, sid);
            }
        }
        break;
    }
    }
    if (onOscEvent) onOscEvent(e);
}

void TriggerController::pruneSessions()
{
    for (auto it = sessions_.begin(); it != sessions_.end();)
    {
        const TriggerSession& s = it->second;
        if (s.ended && s.endFired && s.pending.empty()) it = sessions_.erase(it);
        else ++it;
    }
}

void TriggerController::tick(double now)
{
    osc_.drainEvents([&](const OscEvent& e) { handleEvent(e, now); });
    pruneSessions();
}

} // namespace evobox
