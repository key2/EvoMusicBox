// TriggerController.h — runs OscActions for ANY triggerable (sound, gift, room event).
//   Start -> schedule(t0 + delay)
//   Timer -> schedule(t0 + delay) with the token kept so it can be restarted / cancelled
//   End   -> armed; scheduled at end() + delay
// Lives on the main thread; only scheduling crosses to the scheduler thread. Drains the OSC
// events every frame to drive row/tile pulses, runtime "active" flags and the Logger.
#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include "model/Project.h"
#include "osc/OscService.h"

namespace evobox
{

using SessionId = uint64_t;

enum class TriggerSource { Tile, Hotkey, Shortcut, Live, Simulate, Test, Other };
enum class SessionEndReason { Finished, Stopped, Cancelled };

struct TriggerSession
{
    SessionId id = 0;
    Uid triggerableUid = 0;
    TriggerableKind kind = TriggerableKind::Sound;
    TriggerSource source = TriggerSource::Other;
    TimePoint t0;
    struct PendingGroup
    {
        SchedToken token = 0;
        int phaseIndex = -1;
        Anchor anchor = Anchor::Start;
        TimePoint due;
    };
    std::vector<PendingGroup> pending; // scheduled, not yet fired
    bool ended = false;                // end() was called
    bool endFired = false;             // End phases were scheduled
    int timerCount() const { int n = 0; for (auto& p : pending) if (p.anchor == Anchor::Timer) n++; return n; }
};

class TriggerController
{
public:
    TriggerController(Project& project, OscService& osc);

    // Fires the Start / Timer phases of `t` now. Returns the session id; End phases wait for end().
    SessionId begin(Triggerable& t, TriggerSource src);
    // Schedules the End-anchored phases (fireEnd=false just closes the session).
    void end(SessionId id, SessionEndReason reason, bool fireEnd = true);
    void cancelTimers(SessionId id);                 // drop pending Timer phases
    void restartTimers(SessionId id);                // push Timer phases back to now + delay
    // Retrigger policy "Restart": re-sends the Start phases inside the same session and pushes
    // the Timer phases back to now + timer.
    void retrigger(SessionId id);
    void cancelAll();                                // panic: drop every pending group

    bool isActive(const Triggerable& t) const;       // a Timer phase is still pending
    SessionId activeSession(const Triggerable& t) const; // 0 when none
    const TriggerSession* session(SessionId id) const;
    size_t sessionCount() const { return sessions_.size(); }
    // Remaining time of the first pending Timer phase (seconds), -1 when none.
    double timerRemaining(const Triggerable& t) const;

    // "Test" buttons: immediate send, no session / no runtime state.
    void testCommand(Triggerable& owner, OscPhase& phase, OscCommand& c);
    void testPhase(Triggerable& owner, OscPhase& phase);

    // Transient triggerables (gift shown in the Inspector before it is saved) are not reachable
    // through the project; register them so feedback events can find their owner.
    void registerExtra(Triggerable* t);
    void unregisterExtra(Triggerable* t);

    // Once per frame on the main thread: drains OSC events -> pulses, active flags, logger.
    void tick(double nowSeconds);

    // Hooks for the application / router
    std::function<void(Triggerable&, SessionId)> onTimersDone; // last Timer phase fired or cancelled
    std::function<void(const OscEvent&)> onOscEvent;           // every event (Live Monitor feed)

    // stats
    uint64_t sentCount = 0;
    uint64_t errorCount = 0;

private:
    Project& project_;
    OscService& osc_;
    std::unordered_map<SessionId, TriggerSession> sessions_;
    std::unordered_map<SchedToken, SessionId> tokenToSession_;
    std::unordered_map<Uid, Triggerable*> extra_;
    SessionId nextSession_ = 1;

    Triggerable* findTriggerable(Uid uid) const;
    OscGroup buildGroup(Triggerable& t, OscPhase& phase, int phaseIndex, SessionId sid, bool onlyEnabled = true);
    void scheduleGroup(TriggerSession& s, Triggerable& t, OscPhase& phase, int phaseIndex, TimePoint when);
    void setActiveFlag(Triggerable& t, bool active, SessionId sid);
    void handleEvent(const OscEvent& e, double now);
    void pruneSessions();
    void applyPulse(Uid ownerUid, Uid commandUid, double now, bool error, const std::string& msg);
};

const char* triggerSourceName(TriggerSource s);

} // namespace evobox
