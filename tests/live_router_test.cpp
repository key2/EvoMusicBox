#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "FakeScheduler.h"
#include "app/PlaybackController.h"
#include "app/TriggerController.h"
#include "audio/AudioEngine.h"
#include "live/LiveEventRouter.h"
#include "model/Project.h"

using namespace evobox;

struct Fixture
{
    Project project;
    OscService osc;
    FakeScheduler* sched = nullptr;
    AudioEngine audio; // never initialised: play() returns invalid handles (silent)
    TriggerController trigger{ project, osc };
    PlaybackController playback{ project, audio, trigger };
    GiftCatalog catalog;
    LiveEventRouter router{ project, trigger, playback, catalog };
    double t = 100.0;

    Fixture()
    {
        auto s = std::make_unique<FakeScheduler>();
        sched = s.get();
        osc.setScheduler(std::move(s));
        trigger.onTimersDone = [this](Triggerable& tr, SessionId sid) { router.onTimersDone(tr, sid); };
    }
    GiftAction* gift(int64_t id, const char* name, int diamonds, int type, int timerMs = 3000)
    {
        GiftAction tr;
        tr.seedFromCatalog(id, name, diamonds, "", type);
        GiftAction* g = project.giftActions.addFromTransientUndoable(tr);
        g->actions().phase(Anchor::Start)->commands.addCommandUndoable("/gift/on", 0);
        g->actions().phase(Anchor::Timer)->commands.addCommandUndoable("/gift/off", 0);
        g->actions().phase(Anchor::Timer)->setDelayMsUndoable(timerMs);
        return g;
    }
    LiveEvent giftEvent(int64_t id, int repeat = 1, bool streaking = false, int diamonds = 1, int type = 0)
    {
        return LiveEvent::syntheticGift(id, repeat, streaking, "", diamonds, "", type);
    }
    // route + advance the fake clock by `ms` (fires due groups) + drain events
    void step(Millis ms = Millis(0))
    {
        sched->advance(ms);
        t += ms.count() / 1000.0;
        trigger.tick(t);
    }
    size_t sent() const { return sched->sentTexts().size(); }
    size_t sentOn() const { size_t n = 0; for (auto& s : sched->sentTexts()) if (s == "/gift/on") n++; return n; }
    size_t sentOff() const { size_t n = 0; for (auto& s : sched->sentTexts()) if (s == "/gift/off") n++; return n; }
};

TEST_CASE("unknown gifts only feed the catalog and the feed")
{
    Fixture f;
    f.router.route(f.giftEvent(42, 1, false, 5), f.t);
    f.step();
    CHECK(f.sent() == 0);
    CHECK(f.router.feed().size() == 1);
    CHECK(f.catalog.find(42) != nullptr);
    CHECK(f.catalog.find(42)->receivedCount == 1);
    CHECK(f.router.giftsReceived == 1);
}

TEST_CASE("configured gift fires On then Stop after the timer")
{
    Fixture f;
    GiftAction* g = f.gift(5487, "Galaxy", 1000, 0, 2000);
    f.router.route(f.giftEvent(5487), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    CHECK(g->rt.active);
    CHECK(g->rt.receivedCount == 1);
    f.step(Millis(1999));
    CHECK(f.sentOff() == 0);
    f.step(Millis(1));
    CHECK(f.sentOff() == 1);
    CHECK_FALSE(g->rt.active);
}

TEST_CASE("a gift keeps firing after its Stop timer even when a sound shares its item uid")
{
    // Regression: Sound / GiftAction / RoomEventAction uids are per-manager sequences, so the
    // first sound and the first configured gift both have uid 1. Feedback events (Timer fired)
    // must still reach the *gift*, otherwise rt.active sticks and every later event is treated as
    // a retrigger of a session that no longer exists (no OSC at all).
    Fixture f;
    GiftAction* g = f.gift(5487, "Galaxy", 1000, 0, 1000);
    Sound* sound = nullptr;
    while (!sound || sound->uid < g->uid) // add sounds until one shares the gift's uid
    {
        auto snd = std::make_unique<Sound>();
        snd->setNiceName("Kick");
        sound = f.project.sounds.addSoundUndoable(std::move(snd));
    }
    REQUIRE(sound->uid == g->uid); // the collision this test is about
    for (int i = 0; i < 3; i++)
    {
        f.router.route(f.giftEvent(5487), f.t);
        f.step();
        CHECK(f.sentOn() == (size_t)i + 1);
        CHECK(g->rt.active);
        f.step(Millis(1000));
        CHECK(f.sentOff() == (size_t)i + 1);
        CHECK_FALSE(g->rt.active);
        CHECK(sound->rt.lastOscPulseTime < 0); // the sound never receives the gift's feedback
    }
    // same for a room event whose uid collides with a sound
    RoomEventAction* like = f.project.roomEvents.find(RoomEventKind::Like);
    REQUIRE(f.project.sounds.find(like->uid) != nullptr);
    like->thresholdP->setValue(1, false);
    like->actions().phase(Anchor::Start)->commands.addCommandUndoable("/likes", 0);
    for (int i = 0; i < 2; i++)
    {
        f.router.route(LiveEvent::syntheticRoomEvent(LiveEventType::Like, 1), f.t);
        f.step();
        CHECK(like->rt.active);
        f.step(Millis(3000));
        CHECK_FALSE(like->rt.active);
    }
    size_t likes = 0;
    for (auto& s : f.sched->sentTexts()) if (s == "/likes") likes++;
    CHECK(likes == 2);
}

TEST_CASE("a stale active flag never blocks a gift (defensive)")
{
    Fixture f;
    GiftAction* g = f.gift(5487, "Galaxy", 1000, 0, 1000);
    g->rt.active = true;      // e.g. left over from a session the controller dropped
    g->rt.sessionId = 123456; // unknown session
    f.router.route(f.giftEvent(5487), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    CHECK(g->rt.active);
    CHECK(g->rt.sessionId != 123456);
}

TEST_CASE("disabled gift actions do not fire")
{
    Fixture f;
    GiftAction* g = f.gift(1, "Rose", 1, 1);
    g->enabledP->setValue(false, false);
    f.router.route(f.giftEvent(1), f.t);
    f.step();
    CHECK(f.sent() == 0);
    CHECK(g->rt.receivedCount == 1); // still counted / pulsed
}

TEST_CASE("streak mode OnceAtStreakEnd waits for the end of the combo")
{
    Fixture f;
    f.gift(1, "Rose", 1, 1);
    f.router.route(f.giftEvent(1, 1, true, 1, 1), f.t);
    f.router.route(f.giftEvent(1, 2, true, 1, 1), f.t);
    f.step();
    CHECK(f.sent() == 0);
    f.router.route(f.giftEvent(1, 3, false, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
}

TEST_CASE("streak mode EveryEvent fires on each event; Restart policy re-sends On and restarts the timer")
{
    Fixture f;
    GiftAction* g = f.gift(1, "Rose", 1, 1, 3000);
    g->streakModeP->setValue((int)StreakMode::EveryEvent, false);
    f.router.route(f.giftEvent(1, 1, true, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    f.step(Millis(2000));
    f.router.route(f.giftEvent(1, 2, true, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 2);
    CHECK(f.sentOff() == 0);
    f.step(Millis(2999));
    CHECK(f.sentOff() == 0); // timer restarted at the second event
    f.step(Millis(1));
    CHECK(f.sentOff() == 1);
}

TEST_CASE("EveryEvent: the end-of-combo summary message does not re-fire (regression)")
{
    // TikTok sends one message per tap (streaking, repeatCount 1..n) and ~3 s after the last tap
    // a summary (not streaking, same repeatCount). One tap = two messages; the summary must not
    // fire again in "Every event" mode.
    Fixture f;
    GiftAction* g = f.gift(1, "Rose", 1, 1, 1000);
    g->streakModeP->setValue((int)StreakMode::EveryEvent, false);
    f.router.route(f.giftEvent(1, 1, true, 1, 1), f.t); // the tap
    f.step();
    CHECK(f.sentOn() == 1);
    f.step(Millis(1000));
    CHECK(f.sentOff() == 1);
    f.step(Millis(2000));
    f.router.route(f.giftEvent(1, 1, false, 1, 1), f.t); // the summary, 3 s later
    f.step();
    CHECK(f.sentOn() == 1); // no re-fire
    CHECK(g->rt.receivedCount == 1);
    CHECK(f.catalog.find(1)->receivedCount == 1);

    // a 3-tap combo: three fires, one per tap, nothing at the summary
    f.sched->clearFired();
    for (int n = 1; n <= 3; n++) { f.router.route(f.giftEvent(1, n, true, 1, 1), f.t); f.step(Millis(300)); }
    CHECK(f.sentOn() == 3);
    f.step(Millis(3000));
    f.router.route(f.giftEvent(1, 3, false, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 3);
    CHECK(g->rt.receivedCount == 4); // 1 + 3 taps, not 1 + 1 + 6 + 3

    // the next combo starts from scratch (the summary closed the previous one)
    f.sched->clearFired();
    f.router.route(f.giftEvent(1, 1, true, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    // a summary that carries taps we never saw (joined mid-combo) still fires once
    f.router.route(f.giftEvent(1, 5, false, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 2);
    CHECK(g->rt.receivedCount == 4 + 5);
    // Simulate (one non-streaking event per press) keeps firing every time
    f.sched->clearFired();
    f.router.route(f.giftEvent(1, 1, false, 1, 1), f.t);
    f.step(Millis(1500));
    f.router.route(f.giftEvent(1, 1, false, 1, 1), f.t);
    f.step();
    CHECK(f.sentOn() == 2);
}

TEST_CASE("streak counts are not doubled by the summary in the other modes; combos are per sender")
{
    Fixture f;
    GiftAction* g = f.gift(1, "Rose", 1, 1, 1000);
    auto tap = [&](const char* user, int n, bool streaking)
    {
        LiveEvent e = f.giftEvent(1, n, streaking, 1, 1);
        e.user.uniqueId = user;
        f.router.route(e, f.t);
        f.step();
    };
    // OnceAtStreakEnd: 1 fire, 3 units
    tap("ann", 1, true); tap("ann", 2, true); tap("ann", 3, true);
    CHECK(f.sentOn() == 0);
    tap("ann", 3, false);
    CHECK(f.sentOn() == 1);
    CHECK(g->rt.receivedCount == 3);
    f.step(Millis(1000));

    // PerRepeat: 3 fires at the end (begin + 2 Restart retriggers, each re-sends On), 3 units
    f.sched->clearFired();
    g->streakModeP->setValue((int)StreakMode::PerRepeat, false);
    tap("ann", 1, true); tap("ann", 2, true); tap("ann", 3, true);
    CHECK(f.sentOn() == 0);
    tap("ann", 3, false);
    CHECK(f.sentOn() == 3);
    CHECK(g->rt.receivedCount == 6);
    f.step(Millis(1000));

    // EveryEvent with two senders interleaved: their combos do not interfere
    f.sched->clearFired();
    g->streakModeP->setValue((int)StreakMode::EveryEvent, false);
    tap("ann", 1, true);
    tap("bob", 1, true);
    tap("ann", 2, true);
    CHECK(f.sentOn() == 3);
    tap("bob", 1, false); // bob's summary
    tap("ann", 2, false); // ann's summary
    CHECK(f.sentOn() == 3);
    CHECK(g->rt.receivedCount == 9);
    // non-streakable gifts (type 0) are unaffected: one message, one fire, one unit
    GiftAction* lion = f.gift(7, "Lion", 29999, 0, 1000);
    lion->streakModeP->setValue((int)StreakMode::EveryEvent, false);
    f.router.route(f.giftEvent(7, 1, false, 29999, 0), f.t);
    f.step();
    CHECK(lion->rt.receivedCount == 1);
    size_t lionOn = 0;
    for (auto& s : f.sched->sentTexts()) if (s == "/gift/on") lionOn++;
    CHECK(lionOn == 4);
}

TEST_CASE("retrigger Ignore drops while active; Queue replays after the timer")
{
    Fixture f;
    GiftAction* g = f.gift(7, "Lion", 29999, 0, 1000);
    g->retriggerP->setValue((int)RetriggerPolicy::Ignore, false);
    f.router.route(f.giftEvent(7), f.t);
    f.router.route(f.giftEvent(7), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    f.step(Millis(1000));
    CHECK(f.sentOff() == 1);

    f.sched->clearFired();
    g->retriggerP->setValue((int)RetriggerPolicy::Queue, false);
    f.router.route(f.giftEvent(7), f.t);
    f.router.route(f.giftEvent(7), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    CHECK(g->rt.queued == 1);
    f.step(Millis(1000));           // first Stop fires -> queued replay starts
    CHECK(f.sentOff() == 1);
    f.step();
    CHECK(f.sentOn() == 2);
    CHECK(g->rt.queued == 0);
    f.step(Millis(1000));
    CHECK(f.sentOff() == 2);
}

TEST_CASE("min diamonds and cooldown filter events")
{
    Fixture f;
    GiftAction* g = f.gift(9, "Ring", 300, 0, 500);
    g->minDiamondsP->setValue(1000, false);
    f.router.route(f.giftEvent(9, 1, false, 300), f.t);
    f.step();
    CHECK(f.sent() == 0);
    f.router.route(f.giftEvent(9, 4, false, 300), f.t); // 1200 diamonds total
    f.step();
    CHECK(f.sentOn() == 1);
    f.step(Millis(500));
    CHECK(f.sentOff() == 1);
    // cooldown 10 s: an event 2 s later is dropped, 11 s later passes
    g->cooldownMsP->setValue(10000, false);
    f.step(Millis(2000));
    f.router.route(f.giftEvent(9, 4, false, 300), f.t);
    f.step();
    CHECK(f.sentOn() == 1);
    f.step(Millis(9000));
    f.router.route(f.giftEvent(9, 4, false, 300), f.t);
    f.step();
    CHECK(f.sentOn() == 2);
}

TEST_CASE("likes accumulate to the threshold; other room events fire per event")
{
    Fixture f;
    RoomEventAction* like = f.project.roomEvents.find(RoomEventKind::Like);
    like->thresholdP->setValue(10, false);
    like->actions().phase(Anchor::Start)->commands.addCommandUndoable("/likes", 0);
    RoomEventAction* follow = f.project.roomEvents.find(RoomEventKind::Follow);
    follow->actions().phase(Anchor::Start)->commands.addCommandUndoable("/follow", 0);
    for (int i = 0; i < 3; i++) f.router.route(LiveEvent::syntheticRoomEvent(LiveEventType::Like, 3), f.t);
    f.step();
    CHECK(f.sent() == 0);          // 9 likes
    f.router.route(LiveEvent::syntheticRoomEvent(LiveEventType::Like, 3), f.t);
    f.step();
    CHECK(f.sent() == 1);          // 12 -> fired once, remainder 2
    CHECK(like->rt.likeAccumulator == 2);
    f.router.route(LiveEvent::syntheticRoomEvent(LiveEventType::Follow), f.t);
    f.router.route(LiveEvent::syntheticRoomEvent(LiveEventType::Follow), f.t);
    f.step();
    CHECK(f.sched->sentTexts().back() == "/follow");
    // follow has an (empty) Timer phase: the second event restarts the timer and re-sends On
    CHECK(f.sent() == 3);
    CHECK(follow->rt.active);
    f.step(Millis(3000));
    CHECK_FALSE(follow->rt.active);
    CHECK(follow->rt.receivedCount == 2);
}

TEST_CASE("connection state and viewers follow the events")
{
    Fixture f;
    LiveEvent c; c.type = LiveEventType::Connect;
    f.router.route(c, f.t);
    CHECK(f.router.state == LiveState::Connected);
    LiveEvent v; v.type = LiveEventType::RoomUserSeq; v.viewerCount = 1234;
    f.router.route(v, f.t);
    CHECK(f.router.viewers == 1234);
    CHECK(f.router.feed().size() == 1); // viewer updates are not listed
    LiveEvent e; e.type = LiveEventType::LiveEnd;
    f.router.route(e, f.t);
    CHECK(f.router.state == LiveState::Ended);
    LiveEvent err; err.type = LiveEventType::Error; err.message = "boom";
    f.router.route(err, f.t);
    CHECK(f.router.state == LiveState::Error);
    CHECK(f.router.errorMessage == "boom");
}
