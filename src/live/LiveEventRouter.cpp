#include "live/LiveEventRouter.h"
#include "util/TimeFormat.h"

namespace evobox
{

const char* liveStateName(LiveState s)
{
    switch (s)
    {
    case LiveState::Disconnected: return "Disconnected";
    case LiveState::Connecting:   return "Connecting";
    case LiveState::Connected:    return "Connected";
    case LiveState::Ended:        return "Ended";
    case LiveState::Error:        return "Error";
    }
    return "?";
}

LiveEventRouter::LiveEventRouter(Project& project, TriggerController& trigger, PlaybackController& playback, GiftCatalog& catalog)
    : project_(project), trigger_(trigger), playback_(playback), catalog_(catalog)
{
}

void LiveEventRouter::pushFeed(const LiveEvent& e)
{
    feed_.push_back(e);
    while (feed_.size() > maxFeed) feed_.pop_front();
}

void LiveEventRouter::inject(LiveEvent e)
{
    e.synthetic = true;
    route(e, nowSeconds());
}

void LiveEventRouter::route(const LiveEvent& ein, double now)
{
    LiveEvent e = ein;
    e.time = now;
    eventsReceived++;
    switch (e.type)
    {
    case LiveEventType::Gift:        handleGift(e, now); break;
    case LiveEventType::Like:        handleLike(e, now); break;
    case LiveEventType::Follow:      handleRoomEvent(RoomEventKind::Follow, e, now); break;
    case LiveEventType::Share:       handleRoomEvent(RoomEventKind::Share, e, now); break;
    case LiveEventType::Subscribe:   handleRoomEvent(RoomEventKind::Subscribe, e, now); break;
    case LiveEventType::Join:        handleRoomEvent(RoomEventKind::Join, e, now); break;
    case LiveEventType::RoomUserSeq: viewers = (int)e.viewerCount; break;
    case LiveEventType::Connect:     if (!e.synthetic) state = LiveState::Connected; break;
    case LiveEventType::LiveEnd:     if (!e.synthetic) state = LiveState::Ended; break; // pending timers keep running
    case LiveEventType::Disconnect:  if (!e.synthetic && state != LiveState::Error && state != LiveState::Ended) state = LiveState::Disconnected; break;
    case LiveEventType::Error:       if (!e.synthetic) { state = LiveState::Error; errorMessage = e.message; } break;
    default: break;
    }
    // feed everything except the viewer counter (it would flood the list)
    if (e.type != LiveEventType::RoomUserSeq) pushFeed(e);
    switch (e.type)
    {
    case LiveEventType::Error: OLOGW("Live", e.summary()); break;
    case LiveEventType::RoomUserSeq: break;
    default: OLOG("Live", e.summary()); break;
    }
    if (onEvent) onEvent(e);
}

// ---------------------------------------------------------------- gifts
int LiveEventRouter::streakNewUnits(const LiveEvent& e, bool streakable, double now)
{
    int count = std::max(1, e.repeatCount);
    if (!streakable) return count; // one message per gift, nothing to reconcile
    // forget combos that never got their summary (disconnects); keeps the map tiny
    for (auto it = streaks_.begin(); it != streaks_.end();)
        it = (now - it->second.lastSeen > 60.0) ? streaks_.erase(it) : std::next(it);
    std::string key = std::to_string(e.giftId) + "|" + e.user.uniqueId;
    auto it = streaks_.find(key);
    int seen = it != streaks_.end() ? it->second.count : 0;
    int fresh = std::max(0, count - seen);
    if (e.giftStreaking) streaks_[key] = StreakState{ std::max(seen, count), now };
    else if (it != streaks_.end()) streaks_.erase(it); // the summary closes the combo
    return fresh;
}

void LiveEventRouter::handleGift(const LiveEvent& e, double now)
{
    giftsReceived++;
    catalog_.mergeFromEvent(e);

    GiftAction* a = project_.giftActions.find(e.giftId);
    if (!a && transientLookup) a = transientLookup(e.giftId);
    const GiftInfo* gi = catalog_.find(e.giftId);
    bool streakable = e.giftType == 1 || (gi && gi->type == 1) || (a && a->isStreakable());
    // what this message adds: every tap of a combo counts once, the end-of-combo summary adds
    // nothing (it repeats the final count) — in every streak mode
    int newUnits = streakNewUnits(e, streakable, now);
    bool summaryOnly = streakable && !e.giftStreaking && newUnits == 0;
    catalog_.noteReceived(e.giftId, now, newUnits);

    if (!a) return; // feed only
    a->rt.lastPulseTime = now;
    a->rt.receivedCount += newUnits;
    if (!a->enabled()) return;

    // streak mode
    int fires = 1;
    switch (a->streakMode())
    {
    case StreakMode::OnceAtStreakEnd:
        if (streakable && e.giftStreaking) return; // wait for the end of the combo
        break;
    case StreakMode::EveryEvent:
        // fire right away for every tap; the summary ~3 s after the last tap is not a new gift
        if (summaryOnly) return;
        break;
    case StreakMode::PerRepeat:
        if (streakable && e.giftStreaking) return;
        fires = std::max(1, e.repeatCount);
        break;
    }
    // min diamonds (value of the whole streak)
    long long value = (long long)std::max(0, e.diamondCount) * std::max(1, e.repeatCount);
    if (value == 0)
        if (const GiftInfo* gi = catalog_.find(e.giftId)) value = (long long)gi->diamondCount * std::max(1, e.repeatCount);
    if (a->minDiamonds() > 0 && value < a->minDiamonds()) return;
    // cooldown
    if (a->cooldownMs() > 0 && (now - a->rt.lastFireTime) * 1000.0 < a->cooldownMs()) return;

    TriggerSource src = e.synthetic ? TriggerSource::Simulate : TriggerSource::Live;
    for (int i = 0; i < fires; i++) fireGift(*a, now, src);
}

bool LiveEventRouter::sessionStillActive(bool& active, uint64_t& sessionId) const
{
    if (!active || !sessionId) return false;
    // The runtime flag is a cache of the controller's state: trust the controller. A session that
    // was pruned (or never existed) must not turn every later event into a no-op retrigger.
    const TriggerSession* s = trigger_.session(sessionId);
    if (s && s->timerCount() > 0) return true;
    active = false;
    sessionId = 0;
    return false;
}

void LiveEventRouter::fireGift(GiftAction& a, double now, TriggerSource src)
{
    a.rt.lastFireTime = now;
    if (sessionStillActive(a.rt.active, a.rt.sessionId))
    {
        switch (a.retrigger())
        {
        case RetriggerPolicy::Restart:
            trigger_.retrigger(a.rt.sessionId);
            playAttachedSound(a.soundUid, src);
            return;
        case RetriggerPolicy::Ignore:
            return;
        case RetriggerPolicy::Queue:
            a.rt.queued++;
            return;
        }
    }
    playAttachedSound(a.soundUid, src);
    trigger_.begin(a, src);
}

void LiveEventRouter::playAttachedSound(Uid soundUid, TriggerSource src)
{
    if (soundUid) playback_.play(soundUid, src);
}

void LiveEventRouter::onTimersDone(Triggerable& t, SessionId)
{
    if (auto* a = dynamic_cast<GiftAction*>(&t))
    {
        if (a->rt.queued > 0)
        {
            a->rt.queued--;
            fireGift(*a, nowSeconds(), TriggerSource::Live);
        }
    }
}

// ---------------------------------------------------------------- room events
void LiveEventRouter::handleLike(const LiveEvent& e, double now)
{
    RoomEventAction* a = project_.roomEvents.find(RoomEventKind::Like);
    if (!a) return;
    a->rt.lastPulseTime = now;
    a->rt.receivedCount += std::max(1, e.likeCount);
    a->rt.likeAccumulator += std::max(1, e.likeCount);
    if (!a->enabled()) return;
    int threshold = std::max(1, a->threshold());
    if (a->rt.likeAccumulator < threshold) return;
    a->rt.likeAccumulator -= threshold;
    if (a->rt.likeAccumulator >= threshold) a->rt.likeAccumulator = threshold - 1; // one fire per event max
    if (a->cooldownMs() > 0 && (now - a->rt.lastFireTime) * 1000.0 < a->cooldownMs()) return;
    fireRoomEvent(*a, now, e.synthetic ? TriggerSource::Simulate : TriggerSource::Live);
}

void LiveEventRouter::handleRoomEvent(RoomEventKind kind, const LiveEvent& e, double now)
{
    RoomEventAction* a = project_.roomEvents.find(kind);
    if (!a) return;
    a->rt.lastPulseTime = now;
    a->rt.receivedCount++;
    if (!a->enabled()) return;
    if (a->cooldownMs() > 0 && (now - a->rt.lastFireTime) * 1000.0 < a->cooldownMs()) return;
    fireRoomEvent(*a, now, e.synthetic ? TriggerSource::Simulate : TriggerSource::Live);
}

void LiveEventRouter::fireRoomEvent(RoomEventAction& a, double now, TriggerSource src)
{
    a.rt.lastFireTime = now;
    playAttachedSound(a.soundUid, src);
    if (sessionStillActive(a.rt.active, a.rt.sessionId)) { trigger_.retrigger(a.rt.sessionId); return; }
    trigger_.begin(a, src);
}

} // namespace evobox
