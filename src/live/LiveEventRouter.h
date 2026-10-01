// LiveEventRouter.h — main-thread consumer of live events: gift -> GiftAction (streak / min
// diamonds / cooldown / retrigger policies), like -> threshold, follow/share/subscribe/join ->
// per event, everything -> feed + logger. inject() feeds a synthetic event through the same
// function (that is what Simulate calls).
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include "app/PlaybackController.h"
#include "app/TriggerController.h"
#include "live/GiftCatalog.h"
#include "live/LiveEvent.h"
#include "model/Project.h"

namespace evobox
{

enum class LiveState { Disconnected, Connecting, Connected, Ended, Error };
const char* liveStateName(LiveState s);

class LiveEventRouter
{
public:
    LiveEventRouter(Project& project, TriggerController& trigger, PlaybackController& playback, GiftCatalog& catalog);

    // Routes a real event (main thread, from the drained service queue).
    void route(const LiveEvent& e, double now);
    // Synthetic event through the identical path (Simulate).
    void inject(LiveEvent e);

    // Feed for the Live Monitor (most recent last, capped).
    const std::deque<LiveEvent>& feed() const { return feed_; }
    void clearFeed() { feed_.clear(); }
    size_t maxFeed = 200;

    // connection state mirrored from the service (or set by Simulate when offline)
    LiveState state = LiveState::Disconnected;
    int viewers = 0;
    std::string roomUser;
    std::string errorMessage;

    // session counters
    int giftsReceived = 0;
    int eventsReceived = 0;

    // Retrigger "Queue" support: called by TriggerController::onTimersDone.
    void onTimersDone(Triggerable& t, SessionId sid);

    // Transient gift actions (gallery selection) — fired when a matching gift arrives even before
    // the user configured anything (pulses only; no OSC since there are no commands yet).
    std::function<GiftAction*(int64_t giftId)> transientLookup;

    // Called for every routed event after processing (UI pulses)
    std::function<void(const LiveEvent&)> onEvent;

private:
    Project& project_;
    TriggerController& trigger_;
    PlaybackController& playback_;
    GiftCatalog& catalog_;
    std::deque<LiveEvent> feed_;

    // Streak bookkeeping. TikTok sends a streakable gift as one message per tap (repeatCount
    // 1..n, streaking) and then ONE summary message ~3 s after the last tap (not streaking, same
    // repeatCount). Per gift+sender we remember the highest count seen while streaking so the
    // summary is never mistaken for a new gift ("Every event" must not re-fire) and received
    // counts are not doubled.
    struct StreakState { int count = 0; double lastSeen = 0; };
    std::unordered_map<std::string, StreakState> streaks_;
    // Units this message adds on top of what the combo already delivered (0 for a pure summary).
    int streakNewUnits(const LiveEvent& e, bool streakable, double now);

    void handleGift(const LiveEvent& e, double now);
    void handleLike(const LiveEvent& e, double now);
    void handleRoomEvent(RoomEventKind kind, const LiveEvent& e, double now);
    // Applies the retrigger policy and fires (OSC + optional sound).
    void fireGift(GiftAction& a, double now, TriggerSource src);
    void fireRoomEvent(RoomEventAction& a, double now, TriggerSource src);
    void playAttachedSound(Uid soundUid, TriggerSource src);
    void pushFeed(const LiveEvent& e);
    // True when the cached runtime flags point at a session the controller still runs; otherwise
    // clears them (stale cache) and returns false.
    bool sessionStillActive(bool& active, uint64_t& sessionId) const;
};

} // namespace evobox
