#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "FakeScheduler.h"
#include "app/TriggerController.h"
#include "model/Project.h"

using namespace evobox;

struct Fixture
{
    Project project;
    OscService osc;
    FakeScheduler* sched = nullptr;
    TriggerController trigger{ project, osc };
    Fixture()
    {
        auto s = std::make_unique<FakeScheduler>();
        sched = s.get();
        osc.setScheduler(std::move(s));
    }
    Sound* sound(const char* name)
    {
        auto s = std::make_unique<Sound>();
        s->setNiceName(name);
        return project.sounds.addSoundUndoable(std::move(s));
    }
    void tick() { trigger.tick(nowSeconds()); }
};

TEST_CASE("sound: Start fires at t0+delay, End fires at end+delay, disabled and invalid rows are skipped")
{
    Fixture f;
    Sound* s = f.sound("Kick");
    OscPhase* start = s->actions().phase(Anchor::Start);
    OscPhase* end = s->actions().phase(Anchor::End);
    start->setDelayMsUndoable(100);
    start->commands.addCommandUndoable("/light/flash 1", 0);
    OscCommand* off = start->commands.addCommandUndoable("/bypassed 1", 0);
    off->enabledP->setValue(false, false);
    start->commands.addCommandUndoable("not-an-address", 0); // invalid -> error event, not sent
    end->setDelayMsUndoable(50);
    end->commands.addCommandUndoable("/light/flash 0", 0);

    SessionId sid = f.trigger.begin(*s, TriggerSource::Tile);
    CHECK(sid != 0);
    CHECK(f.sched->pendingCount() == 1);
    f.sched->advance(Millis(99));
    CHECK(f.sched->fired().empty());
    f.sched->advance(Millis(1));
    REQUIRE(f.sched->fired().size() == 1);
    auto texts = f.sched->sentTexts();
    REQUIRE(texts.size() == 1);
    CHECK(texts[0] == "/light/flash 1");
    CHECK(f.sched->fired()[0].group.packets.size() == 2); // valid + invalid(skip)
    CHECK(f.sched->fired()[0].group.packets[1].skip);
    f.tick();
    CHECK(f.trigger.sentCount == 1);
    CHECK(f.trigger.errorCount == 1);
    CHECK(s->rt.lastOscPulseTime >= 0);
    CHECK(start->commands.command(0)->rt.lastSentTime >= 0);
    CHECK_FALSE(start->commands.command(2)->rt.lastError.empty());

    // playback runs 2 s, then ends -> End phase at +50 ms
    f.sched->advance(Millis(2000));
    f.trigger.end(sid, SessionEndReason::Finished);
    CHECK(f.sched->pendingCount() == 1);
    f.sched->advance(Millis(49));
    CHECK(f.sched->fired().size() == 1);
    f.sched->advance(Millis(1));
    REQUIRE(f.sched->fired().size() == 2);
    CHECK(f.sched->sentTexts().back() == "/light/flash 0");
    f.tick();
    CHECK(f.trigger.sessionCount() == 0); // pruned
}

TEST_CASE("sound: end without fireEnd closes the session silently")
{
    Fixture f;
    Sound* s = f.sound("S");
    s->actions().phase(Anchor::End)->commands.addCommandUndoable("/end", 0);
    SessionId sid = f.trigger.begin(*s, TriggerSource::Tile);
    f.trigger.end(sid, SessionEndReason::Stopped, false);
    CHECK(f.sched->pendingCount() == 0);
    f.tick();
    CHECK(f.trigger.sessionCount() == 0);
}

TEST_CASE("gift: Timer phase makes the action active until it fires; restart pushes it back")
{
    Fixture f;
    GiftAction transient;
    transient.seedFromCatalog(5487, "Galaxy", 1000, "", 0);
    GiftAction* g = f.project.giftActions.addFromTransientUndoable(transient);
    g->actions().phase(Anchor::Start)->commands.addCommandUndoable("/fx 1", 0);
    g->actions().phase(Anchor::Timer)->commands.addCommandUndoable("/fx 0", 0);
    g->actions().phase(Anchor::Timer)->setDelayMsUndoable(3000);

    SessionId sid = f.trigger.begin(*g, TriggerSource::Live);
    CHECK(g->rt.active);
    CHECK(g->rt.sessionId == sid);
    CHECK(f.trigger.isActive(*g));
    CHECK(f.trigger.timerRemaining(*g) == doctest::Approx(3.0).epsilon(0.01));
    f.sched->advance(Millis(0));
    REQUIRE(f.sched->sentTexts().size() == 1); // Start at +0
    CHECK(f.sched->sentTexts()[0] == "/fx 1");

    f.sched->advance(Millis(2000));
    CHECK(f.trigger.timerRemaining(*g) == doctest::Approx(1.0).epsilon(0.01));
    // the same gift again -> Restart policy: Start re-sent, timer back to 3 s
    f.trigger.retrigger(sid);
    f.sched->advance(Millis(0));
    CHECK(f.sched->sentTexts().size() == 2);
    CHECK(f.trigger.timerRemaining(*g) == doctest::Approx(3.0).epsilon(0.01));
    f.sched->advance(Millis(2999));
    CHECK(f.sched->sentTexts().size() == 2);
    bool timersDone = false;
    f.trigger.onTimersDone = [&](Triggerable&, SessionId) { timersDone = true; };
    f.sched->advance(Millis(1));
    REQUIRE(f.sched->sentTexts().size() == 3);
    CHECK(f.sched->sentTexts()[2] == "/fx 0");
    f.tick();
    CHECK_FALSE(g->rt.active);
    CHECK(timersDone);
    CHECK_FALSE(f.trigger.isActive(*g));
    CHECK(f.trigger.sessionCount() == 0);
}

TEST_CASE("gift: cancelTimers drops the pending Stop and clears active")
{
    Fixture f;
    GiftAction transient;
    transient.seedFromCatalog(1, "Rose", 1, "", 1);
    GiftAction* g = f.project.giftActions.addFromTransientUndoable(transient);
    g->actions().phase(Anchor::Timer)->commands.addCommandUndoable("/fx 0", 0);
    SessionId sid = f.trigger.begin(*g, TriggerSource::Live);
    CHECK(g->rt.active);
    f.trigger.cancelTimers(sid);
    CHECK_FALSE(g->rt.active);
    f.sched->advance(Millis(5000));
    CHECK(f.sched->sentTexts().empty());
    f.tick();
    CHECK(f.trigger.sessionCount() == 0);
}

TEST_CASE("test buttons send immediately without sessions; targets resolve to the default")
{
    Fixture f;
    Sound* s = f.sound("S");
    OscTarget* pc = f.project.oscTargets.addTargetUndoable("PC", "127.0.0.1", 9001);
    OscPhase* ph = s->actions().phase(Anchor::Start);
    OscCommand* c = ph->commands.addCommandUndoable("/hello 1 2.5 \"x y\"", pc->uid);
    ph->commands.addCommandUndoable("/other", 0);
    f.trigger.testCommand(*s, *ph, *c);
    f.sched->advance(Millis(0));
    REQUIRE(f.sched->fired().size() == 1);
    CHECK(f.sched->fired()[0].group.packets[0].endpoint.port == 9001);
    CHECK(f.sched->fired()[0].group.packets[0].text == "/hello 1 2.5 \"x y\"");
    f.trigger.testPhase(*s, *ph);
    f.sched->advance(Millis(0));
    REQUIRE(f.sched->fired().size() == 2);
    CHECK(f.sched->fired()[1].group.packets.size() == 2);
    CHECK(f.sched->fired()[1].group.packets[1].endpoint.port == 8000); // default target
    CHECK(f.trigger.sessionCount() == 0);
}

TEST_CASE("cancelAll drops everything")
{
    Fixture f;
    Sound* s = f.sound("S");
    s->actions().phase(Anchor::Start)->setDelayMsUndoable(500);
    s->actions().phase(Anchor::Start)->commands.addCommandUndoable("/x", 0);
    f.trigger.begin(*s, TriggerSource::Tile);
    f.trigger.begin(*s, TriggerSource::Tile);
    CHECK(f.sched->pendingCount() == 2);
    f.trigger.cancelAll();
    CHECK(f.sched->pendingCount() == 0);
    CHECK(f.trigger.sessionCount() == 0);
}
