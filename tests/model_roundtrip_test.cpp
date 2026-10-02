#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "model/Project.h"

using namespace evobox;

static Sound* addSound(Project& p, const char* name, Uid cat)
{
    auto s = std::make_unique<Sound>();
    s->setNiceName(name);
    s->categoryUid = cat;
    s->source.path = std::string("/tmp/") + name + ".wav";
    s->source.durationSec = 4.0;
    s->setTrim(0.5, 2.5, false);
    Sound* raw = p.sounds.addSoundUndoable(std::move(s));
    return raw;
}

TEST_CASE("new project has the fixed defaults")
{
    Project p;
    CHECK(p.categories.items.size() == 6);
    CHECK(p.categories.findByName("Custom") != nullptr);
    CHECK(p.categories.customCategory()->builtin);
    REQUIRE(p.oscTargets.localhost() != nullptr);
    CHECK(p.oscTargets.localhost()->isDefault());
    CHECK(p.oscTargets.localhost()->port() == 8000);
    CHECK(p.oscTargets.defaultTarget() == p.oscTargets.localhost());
    CHECK(p.roomEvents.items.size() == 5);
    CHECK(p.roomEvents.find(RoomEventKind::Join)->cooldownMs() == 2000);
    CHECK(p.roomEvents.find(RoomEventKind::Like)->threshold() == 10);
    CHECK_FALSE(p.dirty());
}

TEST_CASE("sound phases and gift phases are fixed by the owner")
{
    Sound s;
    REQUIRE(s.actions().phases.size() == 2);
    CHECK(s.actions().phases[0]->anchor == Anchor::Start);
    CHECK(s.actions().phases[1]->anchor == Anchor::End);
    GiftAction g;
    REQUIRE(g.actions().phases.size() == 2);
    CHECK(g.actions().phases[0]->anchor == Anchor::Start);
    CHECK(g.actions().phases[1]->anchor == Anchor::Timer);
    CHECK(g.stopTimerMs() == 3000);
}

TEST_CASE("save / load round trip preserves everything")
{
    Project p;
    Category* music = p.categories.findByName("Music");
    Sound* a = addSound(p, "Kick", music->uid);
    Sound* b = addSound(p, "Snare", music->uid);
    b->gainDbP->setValue(-6.f, false);
    b->normalizeP->setValue(true, false);
    b->stickerP->setValue(std::string("ph:rocket"), false);
    OscTarget* pc = p.oscTargets.addTargetUndoable("Lighting PC", "192.168.0.10", 9000);
    OscCommand* c1 = a->actions().phase(Anchor::Start)->commands.addCommandUndoable("/light/flash 1", 0);
    OscCommand* c2 = a->actions().phase(Anchor::End)->commands.addCommandUndoable("/light/flash 0", pc->uid);
    c2->enabledP->setValue(false, false);
    a->actions().phase(Anchor::End)->setDelayMsUndoable(250);

    GiftAction transient;
    transient.seedFromCatalog(5487, "Galaxy", 1000, "https://x/y.webp", 0);
    transient.actions().phase(Anchor::Start)->commands.addCommandUndoable("/fx/galaxy 1", 0);
    transient.actions().phase(Anchor::Timer)->setDelayMsUndoable(4500);
    transient.retriggerP->setValue((int)RetriggerPolicy::Queue, false);
    GiftAction* g = p.giftActions.addFromTransientUndoable(transient);
    REQUIRE(g);
    g->setSoundUidUndoable(b->uid);
    RoomEventAction* like = p.roomEvents.find(RoomEventKind::Like);
    like->thresholdP->setValue(25, false);
    like->actions().phase(Anchor::Start)->commands.addCommandUndoable("/likes 1", 0);
    like->setSoundUidUndoable(a->uid);
    p.settings.playbackPolicyP->setValue((int)PlaybackPolicy::StopOthers, false);
    CHECK(p.dirty());

    json saved = p.save();
    CHECK(saved["formatVersion"] == Project::kFormatVersion);

    Project q;
    q.load(saved);
    CHECK_FALSE(q.dirty());
    REQUIRE(q.sounds.items.size() == 2);
    Sound* qa = q.sounds.find(a->uid);
    Sound* qb = q.sounds.find(b->uid);
    REQUIRE(qa); REQUIRE(qb);
    CHECK(qa->niceName == "Kick");
    CHECK(qa->categoryUid == music->uid);
    CHECK(qa->trimStart() == doctest::Approx(0.5));
    CHECK(qa->trimEnd() == doctest::Approx(2.5));
    CHECK(qb->gainDb() == doctest::Approx(-6.f));
    CHECK(qb->normalize());
    CHECK(qb->sticker() == "ph:rocket");
    CHECK(qa->source.path == "/tmp/Kick.wav");
    // phases + commands
    REQUIRE(qa->actions().phase(Anchor::Start)->commands.items.size() == 1);
    REQUIRE(qa->actions().phase(Anchor::End)->commands.items.size() == 1);
    CHECK(qa->actions().phase(Anchor::Start)->commands.command(0)->text() == "/light/flash 1");
    CHECK(qa->actions().phase(Anchor::Start)->commands.command(0)->uid == c1->uid);
    OscCommand* qc2 = qa->actions().phase(Anchor::End)->commands.command(0);
    CHECK(qc2->targetUid == pc->uid);
    CHECK_FALSE(qc2->enabled());
    CHECK(qa->actions().phase(Anchor::End)->delayMs() == 250);
    // targets
    REQUIRE(q.oscTargets.items.size() == 2);
    CHECK(q.oscTargets.localhost()->isDefault());
    OscTarget* qpc = q.oscTargets.find(pc->uid);
    REQUIRE(qpc);
    CHECK(qpc->host() == "192.168.0.10");
    CHECK(qpc->port() == 9000);
    CHECK_FALSE(qpc->builtin);
    // gift
    REQUIRE(q.giftActions.items.size() == 1);
    GiftAction* qg = q.giftActions.find(5487);
    REQUIRE(qg);
    CHECK(qg->cachedName == "Galaxy");
    CHECK(qg->cachedDiamonds == 1000);
    CHECK(qg->soundUid == b->uid);
    CHECK(qg->retrigger() == RetriggerPolicy::Queue);
    CHECK(qg->stopTimerMs() == 4500);
    CHECK(qg->actions().phase(Anchor::Start)->commands.command(0)->text() == "/fx/galaxy 1");
    // room events
    CHECK(q.roomEvents.items.size() == 5);
    RoomEventAction* ql = q.roomEvents.find(RoomEventKind::Like);
    CHECK(ql->threshold() == 25);
    CHECK(ql->soundUid == a->uid);
    CHECK(ql->actions().commandCount() == 1);
    CHECK(q.settings.playbackPolicy() == PlaybackPolicy::StopOthers);
    // byte-identical second save
    CHECK(q.save() == saved);
}

TEST_CASE("load repairs dangling references and missing defaults")
{
    Project p;
    Sound* s = addSound(p, "Orphan", 999999);
    GiftAction transient;
    transient.seedFromCatalog(1, "Rose", 1, "", 1);
    GiftAction* g = p.giftActions.addFromTransientUndoable(transient);
    g->soundUid = 424242; // dangling
    json j = p.save();
    // remove the targets and the room events entirely, corrupt kind duplicates
    j.erase("oscTargets");
    j.erase("roomEvents");
    Project q;
    q.load(j);
    CHECK(q.oscTargets.localhost() != nullptr);
    CHECK(q.oscTargets.defaultTarget()->isDefault());
    CHECK(q.roomEvents.items.size() == 5);
    CHECK(q.sounds.find(s->uid)->categoryUid == q.categories.customCategory()->uid);
    CHECK(q.giftActions.find(1)->soundUid == 0);
}

TEST_CASE("default target invariant")
{
    Project p;
    OscTarget* a = p.oscTargets.addTargetUndoable("A", "10.0.0.1", 8000);
    OscTarget* b = p.oscTargets.addTargetUndoable("B", "10.0.0.2", 8000);
    p.oscTargets.setDefaultUndoable(b);
    CHECK(b->isDefault());
    CHECK_FALSE(a->isDefault());
    CHECK_FALSE(p.oscTargets.localhost()->isDefault());
    CHECK(p.oscTargets.defaultTarget() == b);
    // un-ticking the only default is refused
    b->isDefaultP->setValue(false);
    CHECK(b->isDefault());
    // ticking another one moves the flag
    a->isDefaultP->setValue(true);
    CHECK(a->isDefault());
    CHECK_FALSE(b->isDefault());
    // deleting the default falls back to Localhost and re-points commands
    Sound* s = addSound(p, "S", 0);
    Uid aUid = a->uid; // keep the uid: deleteTargetUndoable frees the OscTarget, so `a` dangles
    OscCommand* c = s->actions().phase(Anchor::Start)->commands.addCommandUndoable("/x", aUid);
    p.deleteTargetUndoable(a);
    CHECK(p.oscTargets.find(aUid) == nullptr);
    CHECK(c->targetUid == 0);
    CHECK(p.oscTargets.defaultTarget()->isDefault());
    // Localhost cannot be deleted
    p.deleteTargetUndoable(p.oscTargets.localhost());
    CHECK(p.oscTargets.localhost() != nullptr);
    organic::UndoManager::get().undo();
    CHECK(p.oscTargets.find(aUid) != nullptr);
}

TEST_CASE("delete sound clears gift/room references and is undoable")
{
    Project p;
    Sound* s = addSound(p, "Boom", 0);
    GiftAction transient;
    transient.seedFromCatalog(7, "Lion", 29999, "", 0);
    GiftAction* g = p.giftActions.addFromTransientUndoable(transient);
    g->soundUid = s->uid;
    p.roomEvents.find(RoomEventKind::Share)->soundUid = s->uid;
    Uid uid = s->uid;
    p.deleteSoundsUndoable({ s });
    CHECK(p.sounds.find(uid) == nullptr);
    CHECK(g->soundUid == 0);
    CHECK(p.roomEvents.find(RoomEventKind::Share)->soundUid == 0);
    organic::UndoManager::get().undo();
    REQUIRE(p.sounds.find(uid) != nullptr);
    CHECK(p.sounds.find(uid)->niceName == "Boom");
    CHECK(g->soundUid == uid);
    CHECK(p.roomEvents.find(RoomEventKind::Share)->soundUid == uid);
    organic::UndoManager::get().redo();
    CHECK(p.sounds.find(uid) == nullptr);
}

TEST_CASE("delete category moves its sounds to Custom")
{
    Project p;
    Category* music = p.categories.findByName("Music");
    Sound* s = addSound(p, "Track", music->uid);
    Uid musicUid = music->uid;
    p.deleteCategoryUndoable(music);
    CHECK(p.categories.find(musicUid) == nullptr);
    CHECK(s->categoryUid == p.categories.customCategory()->uid);
    organic::UndoManager::get().undo();
    REQUIRE(p.categories.find(musicUid) != nullptr);
    CHECK(s->categoryUid == musicUid);
}

TEST_CASE("command manager undo/redo/reorder")
{
    Project p;
    Sound* s = addSound(p, "S", 0);
    OscPhase* ph = s->actions().phase(Anchor::Start);
    OscCommand* a = ph->commands.addCommandUndoable("/a", 0);
    OscCommand* b = ph->commands.addCommandUndoable("/b", 0);
    CHECK(ph->commands.items.size() == 2);
    ph->commands.undoableMove(1, 0);
    CHECK(ph->commands.command(0)->text() == "/b");
    organic::UndoManager::get().undo();
    CHECK(ph->commands.command(0)->text() == "/a");
    ph->commands.removeCommandUndoable(b);
    CHECK(ph->commands.items.size() == 1);
    organic::UndoManager::get().undo();
    CHECK(ph->commands.items.size() == 2);
    CHECK(ph->commands.command(1)->text() == "/b");
    Uid aUid = a->uid;
    organic::UndoManager::get().undo(); // undo move? no: undo the add of b's removal already undone -> undo move (none) -> undo add b
    (void)aUid;
    CHECK(s->actions().commandCount() >= 1);
    // copy actions between owners
    Sound* t = addSound(p, "T", 0);
    t->actions().copyFrom(s->actions());
    CHECK(t->actions().commandCount() == s->actions().commandCount());
    CHECK(t->actions().phase(Anchor::Start)->commands.command(0)->text() == "/a");
}

TEST_CASE("dirty tracking follows parameter and structure changes")
{
    Project p;
    CHECK_FALSE(p.dirty());
    Sound* s = addSound(p, "S", 0);
    CHECK(p.dirty());
    p.markSaved();
    CHECK_FALSE(p.dirty());
    s->gainDbP->setValue(3.f);
    CHECK(p.dirty());
    p.markSaved();
    s->actions().phase(Anchor::Start)->commands.addCommandUndoable("/x", 0);
    CHECK(p.dirty());
}
